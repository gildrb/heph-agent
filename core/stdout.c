// Stdout.write: writes the first n bytes of a packed Array<U32> (byte j in
// slot j / 4 at bits 8 * (j % 4), little-endian) to standard output and
// frees the array. It only copies bytes.
Term stdout_write_run(Env e, Term* f, IoWork* w) {
  Term a = f[0];
  u64  n = (u64)f[1];
  if (term_tag(a) != TAG_BUF || n > (4ull << blk_cls(a))) {
    term_sink(e, a);
    return io_fail(e, EINVAL, "heph-core: output past its buffer");
  }
  const u8* p = (const u8*)(e.mem + blk_loc(e.mem, a));
  size_t put = fwrite(p, 1, (size_t)n, stdout);
  term_sink(e, a);
  if (put != (size_t)n) {
    return io_fail(e, EIO, "heph-core: cannot write stdout");
  }
  return io_done(e, term_pak(CID_UNIT, 0));
}

static void __attribute__((constructor)) stdout_write_use(void) {
  io_eff(CID_STDOUT_WRITE, stdout_write_run, 0);
}
