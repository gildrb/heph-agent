// Stdin.read: reads all of standard input into one packed Array<U32> (byte
// j in slot j / 4 at bits 8 * (j % 4), little-endian; the pad bytes are 0)
// and answers it beside its byte count. It only copies bytes.
Term stdin_read_run(Env e, Term* f, IoWork* w) {
  u64 cap = 1ull << 16, n = 0;
  u8* buf = io_mem(malloc(cap));
  for (;;) {
    if (n == cap) {
      if (cap >= (1ull << 32)) {
        free(buf);
        return io_fail(e, EFBIG, "heph-core: request over 4 GiB");
      }
      cap *= 2;
      buf = io_mem(realloc(buf, cap));
    }
    ssize_t r = read(0, buf + n, cap - n);
    if (r < 0 && errno == EINTR) {
      continue;
    }
    if (r < 0) {
      u32 code = (u32)errno;
      free(buf);
      return io_fail(e, code, "heph-core: cannot read stdin");
    }
    if (r == 0) {
      break;
    }
    n += (u64)r;
  }
  // at least one pad byte, so the slot count n / 4 + 1 is never 0
  Cls c = cls_fit((u32)(n / 4 + 1));
  Loc l = heap_alloc(e, buf_wcls(c));
  if (err_seen(e.mem)) {
    free(buf);
    return io_fail(e, ENOMEM, "heph-core: out of memory for the request");
  }
  u8* dst = (u8*)(e.mem + l);
  memcpy(dst, buf, n);
  memset(dst + n, 0, (4ull << c) - n);
  free(buf);
  return io_done(e, io_tup(e, term_blk(0, c, l), (Term)n));
}

static void __attribute__((constructor)) stdin_read_use(void) {
  io_eff(CID_STDIN_READ, stdin_read_run, 0);
}
