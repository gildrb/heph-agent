import Base
import ./core.bend as C
import ./score.bend as F

# heph-core protocol v1: frames on stdin, answer lines on stdout, exit 0.
# Numbers are ASCII decimal; payloads are raw, length-prefixed bytes; frames
# follow each other with nothing in between and are answered in order.
#   D <len>\n<bytes>        chunk a document  -> c <d> <start> <end>  (per chunk)
#   E <len>\n<bytes>        add evidence item (id = number of earlier E frames)
#   Q <id> <len>\n<bytes>   verify a quote    -> v <q> ok <offset> | badid | empty | missing
#   R <k> <n> <total> <terms>\n then <terms> blocks of
#     T <count>\n then <count> lines <chunk> <tf> <dl>\n
#                           rank              -> h <r> <rank> <chunk> <score>  (per hit)
# Malformed input exits 2 with a message on stderr, after the answers of the
# frames before it. So that every Nat stays below the runtime's 2^48, a
# well-formed R frame must also keep n < 2^24, tf < 2^16, dl < 2^14,
# terms < 2^15, total < 2^40 and 1024 * dl * n / total < 2^43, else it
# exits 2 ("R numbers out of range"). Any number of 2^40 or more reads as
# 2^40: longer than any payload, beyond every id.

# Input and output: two effects that only move bytes (see stdin.c, stdout.c)
def Stdin.read() -> IO(Result<&1, &1, U32 & String, Array<U32> & Nat>):
  import "./stdin.c"

def Stdout.write(a: Array<U32>, n: Nat) -> IO(Result<&1, &1, U32 & String, Unit>):
  import "./stdout.c"

def HUGE() -> Nat:
  Nat.mul(U32.to_nat(1048576), U32.to_nat(1048576))

# Lexing
# ======

# the byte at p, and whether p < len
type Ch is Type:
  Ch{a: Array<U32>, b: U32, in: Bool}

def ch.of(+p: Nat, +len: Nat, r: Array<U32> & U32) -> Ch:
  (a, b) = r
  Ch{a, b, Nat.is_lt(p, len)}

def ch(a: Array<U32>, +len: Nat, +p: Nat) -> Ch:
  ch.of(p, len, C.byte(a, p))

def at.of(+y: U32, x: Ch) -> Array<U32> & Bool:
  Ch{a, +b, in} = x
  (a, Bool.and(in, U32.is_eq(b, y)))

# the byte at p is y
def at(a: Array<U32>, +len: Nat, +p: Nat, +y: U32) -> Array<U32> & Bool:
  at.of(y, ch(a, len, p))

# a number ending at p (ok: it has a digit)
type Num is Type:
  Num{a: Array<U32>, v: Nat, p: Nat, ok: Bool}

type Dg is Type:
  Dg{a: Array<U32>, b: U32, d: Bool}

def dg.of(+p: Nat, +len: Nat, r: Array<U32> & U32) -> Dg:
  (a, +b) = r
  Dg{a, b, Bool.and(Nat.is_lt(p, len), Bool.and(U32.is_ge(b, 48), U32.is_le(b, 57)))}

def dg(a: Array<U32>, +len: Nat, +p: Nat) -> Dg:
  dg.of(p, len, C.byte(a, p))

# reads digits from p (x: the byte there); v so far, any: a digit seen
def digits(fuel: Nat, +len: Nat, +p: Nat, +v: Nat, any: Bool, x: Dg) -> Num:
  match fuel:
    case 0n:
      Dg{a, b, d} = x
      Num{a, v, p, any}
    case 1n+f:
      Dg{a, +b, d} = x
      match d:
        case False{}:
          Num{a, v, p, any}
        case True{}:
          +v2 = C.min(HUGE(), Nat.add(Nat.mul(v, 10n), U32.to_nat((b - 48 : U32))))
          digits(f, len, 1n+p, v2, True{}, dg(a, len, 1n+p))

# a number at p
def num(a: Array<U32>, +len: Nat, +p: Nat) -> Num:
  digits(1n+Nat.sub(len, p), len, p, 0n, False{}, dg(a, len, p))

def sep.if(ok: Bool, +len: Nat, +p: Nat, a: Array<U32>) -> Num:
  match ok:
    case True{}:
      num(a, len, 1n+p)
    case False{}:
      Num{a, 0n, p, False{}}

def sep.of(+len: Nat, +p: Nat, r: Array<U32> & Bool) -> Num:
  (a, ok) = r
  sep.if(ok, len, p, a)

# " <digits>" at p
def field(a: Array<U32>, +len: Nat, +p: Nat) -> Num:
  sep.of(len, p, at(a, len, p, 32))

# then b at the end of x
def then.of(+v: Nat, +p: Nat, ok: Bool, r: Array<U32> & Bool) -> Num:
  (a, nl) = r
  Num{a, v, 1n+p, Bool.and(ok, nl)}

def then(+len: Nat, +y: U32, x: Num) -> Num:
  Num{a, +v, +p, ok} = x
  then.of(v, p, ok, at(a, len, p, y))

# a header: the numbers (last first), the position after it
type Hdr is Type:
  Hdr{a: Array<U32>, ns: List<&2, Nat>, p: Nat, ok: Bool}

def hdr.end(+ns: List<&2, Nat>, x: Num) -> Hdr:
  Num{a, v, p, ok} = x
  Hdr{a, ns, p, ok}

# c more fields after the one in x, then "\n"
def fields(c: Nat, +len: Nat, ns: List<&2, Nat>, x: Num) -> Hdr:
  match c:
    case 0n:
      Num{a, +v, +p, ok} = x
      hdr.end(v <> ns, then(len, 10, Num{a, v, p, ok}))
    case 1n+c2:
      Num{a, +v, +p, ok} = x
      match ok:
        case False{}:
          Hdr{a, ns, p, False{}}
        case True{}:
          fields(c2, len, v <> ns, field(a, len, p))

def header(a: Array<U32>, +len: Nat, +p: Nat, c: Nat) -> Hdr:
  match c:
    case 0n:
      Hdr{a, Nil{}, p, False{}}
    case 1n+c2:
      fields(c2, len, Nil{}, field(a, len, p))

# Output
# ======
# A writer puts bytes into a zeroed packed buffer at n.

type W is Type:
  W{o: Array<U32>, n: Nat}

def put.of(+n: Nat, +b: U32, r: Array<U32> & U32) -> Array<U32>:
  (o, w) = r
  Array.set(U32, o, U32.from_nat(Nat.div(n, 4n)),
    (w .|. U32.shln(b, Nat.mul(8n, Nat.mod(n, 4n))) : U32))

def put(o: Array<U32>, +n: Nat, +b: U32) -> Array<U32>:
  put.of(n, b, Array.get(U32, o, U32.from_nat(Nat.div(n, 4n))))

def put.b(+b: U32, w: W) -> W:
  W{o, +n} = w
  W{put(o, n, b), 1n+n}

def put.s(+s: String, w: W) -> W:
  match s:
    case SNil{}:
      w
    case SCon{Chr{c}, t}:
      put.s(t, put.b(c, w))

# the digit count of v (at least 1); more: v has another digit past c
def ndig(fuel: Nat, more: Bool, +v: Nat, +c: Nat) -> Nat:
  match fuel more:
    case 1n+f True{}:
      ndig(f, Nat.is_le(10n, Nat.div(v, 10n)), Nat.div(v, 10n), 1n+c)
    case _ _:
      c

# writes v's digits ending just before n, right to left
def put.dig(fuel: Nat, +n: Nat, +v: Nat, o: Array<U32>) -> Array<U32>:
  match fuel:
    case 0n:
      o
    case 1n+f:
      put.dig(f, Nat.sub(n, 1n), Nat.div(v, 10n),
        put(o, Nat.sub(n, 1n), U32.from_nat(Nat.add(48n, Nat.mod(v, 10n)))))

# " <v>"
def put.n(+v: Nat, w: W) -> W:
  W{o, +n} = w
  +c = ndig(20n, Nat.is_le(10n, v), v, 1n)
  +e = Nat.add(1n+n, c)
  W{put.dig(c, e, v, put(o, n, 32)), e}

# a zeroed writer for at most len bytes
def writer(+len: Nat) -> W:
  W{[0 : U32^F.depth(1n+Nat.div(len, 4n))], 0n}

# every line is at most "x" and three or four " <20 digits>" and "\n"
def LINE() -> Nat:
  90n

def out.chunks(cs: List<&2, C.Chunk>, +d: Nat, w: W) -> W:
  match cs:
    case Nil{}:
      w
    case Con{C.Chunk{+s, +e}, t}:
      out.chunks(t, d, put.b(10, put.n(e, put.n(s, put.n(d, put.b(99, w))))))

def out.hits(hs: List<&2, C.Hit>, +r: Nat, +i: Nat, w: W) -> W:
  match hs:
    case Nil{}:
      w
    case Con{C.Hit{+s, +id}, t}:
      out.hits(t, r, 1n+i, put.b(10, put.n(s, put.n(id, put.n(i, put.n(r, put.b(104, w)))))))

def out.verdict(v: C.Verdict, w: W) -> W:
  match v:
    case C.BadId{}:
      put.s(" badid\n", w)
    case C.Empty{}:
      put.s(" empty\n", w)
    case C.Missing{}:
      put.s(" missing\n", w)
    case C.Found{+i}:
      put.b(10, put.n(i, put.s(" ok", w)))

# Frames
# ======

# the request a (len bytes, the next frame at pos), the frame counters, and
# the evidence items: (offset, length) word pairs in ev, c of them
type S is Type:
  S{a: Array<U32>, len: Nat, pos: Nat, d: Nat, q: Nat, r: Nat, ev: Array<U32>, c: Nat}

type Out is Type:
  End{}
  Bad{msg: String}
  Mute{st: S}
  Say{w: W, st: S}

def bad.hdr() -> String:
  "heph-core: bad frame header"

# D
def frame.d.out(+len: Nat, +pos: Nat, +d: Nat, +q: Nat, +r: Nat, ev: Array<U32>, +c: Nat,
  x: Array<U32> & List<&2, C.Chunk>
) -> Out:
  (a, +cs) = x
  w = out.chunks(cs, d, writer(Nat.mul(LINE(), List.length(&2, C.Chunk, cs))))
  Say{w, S{a, len, pos, 1n+d, q, r, ev, c}}

def frame.d(ok: Bool, +len: Nat, +p: Nat, +l: Nat, +d: Nat, +q: Nat, +r: Nat, a: Array<U32>,
  ev: Array<U32>, +c: Nat
) -> Out:
  match ok:
    case False{}:
      Bad{"heph-core: truncated D payload"}
    case True{}:
      frame.d.out(len, Nat.add(p, l), d, q, r, ev, c, C.chunk.doc(a, p, l))

# E: the evidence table grows by doubling
def copy(fuel: Nat, +i: Nat, dst: Array<U32>, r: Array<U32> & U32) -> Array<U32>:
  match fuel:
    case 0n:
      (src, v) = r
      dst
    case 1n+f:
      (src, v) = r
      copy(f, 1n+i, Array.set(U32, dst, U32.from_nat(i), v), Array.get(U32, src, U32.from_nat(1n+i)))

def grow.if(big: Bool, +cap: Nat, ev: Array<U32>) -> Array<U32>:
  match big:
    case True{}:
      ev
    case False{}:
      copy(cap, 0n, [0 : U32^F.depth(Nat.mul(2n, cap))], Array.get(U32, ev, 0))

def grow.of(+need: Nat, r: Array<U32> & U32) -> Array<U32>:
  (ev, +cap) = r
  grow.if(Nat.is_le(need, U32.to_nat(cap)), U32.to_nat(cap), ev)

def store(+c: Nat, +p: Nat, +l: Nat, ev: Array<U32>) -> Array<U32>:
  ev2 = grow.of(Nat.mul(2n, 1n+c), Array.size(U32, ev))
  ev2[U32.from_nat(Nat.mul(2n, c))] <- U32.from_nat(p)
  ev2[U32.from_nat(1n+Nat.mul(2n, c))] <- U32.from_nat(l)
  ev2

def frame.e(ok: Bool, +len: Nat, +p: Nat, +l: Nat, +d: Nat, +q: Nat, +r: Nat, a: Array<U32>,
  ev: Array<U32>, +c: Nat
) -> Out:
  match ok:
    case False{}:
      Bad{"heph-core: truncated E payload"}
    case True{}:
      Mute{S{a, len, Nat.add(p, l), d, q, r, store(c, p, l, ev), 1n+c}}

# Q
def frame.q.out(+len: Nat, +pos: Nat, +d: Nat, +q: Nat, +r: Nat, +c: Nat, x: C.Ev) -> Out:
  C.Ev{a, ev, v} = x
  Say{out.verdict(v, put.n(q, put.b(118, writer(LINE())))), S{a, len, pos, d, 1n+q, r, ev, c}}

def frame.q(ok: Bool, +len: Nat, +p: Nat, +l: Nat, +id: Nat, +d: Nat, +q: Nat, +r: Nat,
  a: Array<U32>, ev: Array<U32>, +c: Nat
) -> Out:
  match ok:
    case False{}:
      Bad{"heph-core: truncated Q payload"}
    case True{}:
      frame.q.out(len, Nat.add(p, l), d, q, r, c, C.verify(a, ev, c, id, p, l))

# R: the postings go packed into P (q words used); mtf and mdl are the
# largest tf and dl so far
type Pr is Type:
  Pr{a: Array<U32>, pos: Nat, pp: Array<U32>, q: Nat, ok: Bool, mtf: Nat, mdl: Nat}

# one posting line
type Pl is Type:
  Pl{a: Array<U32>, id: Nat, tf: Nat, dl: Nat, p: Nat, ok: Bool}

def pl.dl(+id: Nat, +tf: Nat, ok1: Bool, x: Num) -> Pl:
  Num{a, +dl, +p, ok} = x
  Pl{a, id, tf, dl, p, Bool.and(ok1, ok)}

def pl.tf(+len: Nat, +id: Nat, ok1: Bool, x: Num) -> Pl:
  Num{a, +tf, +p, ok} = x
  pl.dl(id, tf, Bool.and(ok1, ok), then(len, 10, field(a, len, p)))

def pl.id(+len: Nat, x: Num) -> Pl:
  Num{a, +id, +p, ok} = x
  pl.tf(len, id, ok, field(a, len, p))

def pline(a: Array<U32>, +len: Nat, +p: Nat) -> Pl:
  pl.id(len, num(a, len, p))

# "T <count>\n"
def tl.if(ok: Bool, +len: Nat, +p: Nat, a: Array<U32>) -> Num:
  match ok:
    case True{}:
      then(len, 10, field(a, len, 1n+p))
    case False{}:
      Num{a, 0n, p, False{}}

def tl.of(+len: Nat, +p: Nat, r: Array<U32> & Bool) -> Num:
  (a, ok) = r
  tl.if(ok, len, p, a)

def tline(a: Array<U32>, +len: Nat, +p: Nat) -> Num:
  tl.of(len, p, at(a, len, p, 84))

# P with room for need words
def room(+need: Nat, pp: Array<U32>) -> Array<U32>:
  grow.of(need, Array.size(U32, pp))

def post.put.if(ok: Bool, +q: Nat, +mtf: Nat, +mdl: Nat, pp: Array<U32>, +id: Nat, +tf: Nat,
  +dl: Nat, +p: Nat, a: Array<U32>
) -> Pr:
  match ok:
    case False{}:
      Pr{a, p, pp, q, False{}, mtf, mdl}
    case True{}:
      pp2 = room(Nat.add(q, 3n), pp)
      pp2[U32.from_nat(q)] <- U32.from_nat(id)
      pp2[U32.from_nat(1n+q)] <- U32.from_nat(tf)
      pp2[U32.from_nat(Nat.add(q, 2n))] <- U32.from_nat(dl)
      Pr{a, p, pp2, Nat.add(q, 3n), True{}, C.max(mtf, tf), C.max(mdl, dl)}

def post.put(+n: Nat, +q: Nat, +mtf: Nat, +mdl: Nat, pp: Array<U32>, x: Pl) -> Pr:
  Pl{a, +id, +tf, +dl, +p, ok} = x
  post.put.if(Bool.and(ok, Bool.and(Nat.is_lt(id, n), Nat.is_lt(0n, tf))), q, mtf, mdl, pp,
    id, tf, dl, p, a)

# cnt more posting lines
def posts(cnt: Nat, +len: Nat, +n: Nat, x: Pr) -> Pr:
  match cnt:
    case 0n:
      x
    case 1n+k:
      Pr{a, +pos, pp, +q, ok, +mtf, +mdl} = x
      match ok:
        case False{}:
          Pr{a, pos, pp, q, False{}, mtf, mdl}
        case True{}:
          posts(k, len, n, post.put(n, q, mtf, mdl, pp, pline(a, len, pos)))

def term.put.if(ok: Bool, +len: Nat, +n: Nat, +q: Nat, +mtf: Nat, +mdl: Nat, pp: Array<U32>,
  +cnt: Nat, +p: Nat, a: Array<U32>
) -> Pr:
  match ok:
    case False{}:
      Pr{a, p, pp, q, False{}, mtf, mdl}
    case True{}:
      pp2 = room(1n+q, pp)
      pp2[U32.from_nat(q)] <- U32.from_nat(cnt)
      posts(cnt, len, n, Pr{a, p, pp2, 1n+q, True{}, mtf, mdl})

def term.put(+len: Nat, +n: Nat, +q: Nat, +mtf: Nat, +mdl: Nat, pp: Array<U32>, x: Num) -> Pr:
  Num{a, +cnt, +p, ok} = x
  term.put.if(ok, len, n, q, mtf, mdl, pp, cnt, p, a)

# t more term blocks
def terms(t: Nat, +len: Nat, +n: Nat, x: Pr) -> Pr:
  match t:
    case 0n:
      x
    case 1n+u:
      Pr{a, +pos, pp, +q, ok, +mtf, +mdl} = x
      match ok:
        case False{}:
          Pr{a, pos, pp, q, False{}, mtf, mdl}
        case True{}:
          terms(u, len, n, term.put(len, n, q, mtf, mdl, pp, tline(a, len, pos)))

def frame.r.out(+len: Nat, +pos: Nat, +d: Nat, +q: Nat, +r: Nat, ev: Array<U32>, +c: Nat,
  a: Array<U32>, x: Array<U32> & List<&2, C.Hit>
) -> Out:
  (pp, +hs) = x
  w = out.hits(hs, r, 0n, writer(Nat.mul(LINE(), List.length(&2, C.Hit, hs))))
  Say{w, S{a, len, pos, d, q, 1n+r, ev, c}}

def frame.r.rank(ok: Bool, +k: Nat, +n: Nat, +tot: Nat, +t: Nat, +len: Nat, +pos: Nat,
  +d: Nat, +q: Nat, +r: Nat, ev: Array<U32>, +c: Nat, a: Array<U32>, pp: Array<U32>
) -> Out:
  match ok:
    case False{}:
      Bad{"heph-core: R numbers out of range"}
    case True{}:
      frame.r.out(len, pos, d, q, r, ev, c, a, C.rank(k, n, tot, pp, t))

# n, tf, dl, terms and total are in range, so 1024 * dl * n fits
def rel.ok(small: Bool, +n: Nat, +tot: Nat, +mdl: Nat) -> Bool:
  match small:
    case False{}:
      False{}
    case True{}:
      Nat.is_lt(Nat.div(Nat.mul(Nat.mul(mdl, n), 1024n), tot), Nat.mul(U32.to_nat(2097152), U32.to_nat(4194304)))

def frame.r.post(+k: Nat, +n: Nat, +tot: Nat, +t: Nat, +len: Nat, +d: Nat, +q: Nat, +r: Nat,
  ev: Array<U32>, +c: Nat, x: Pr
) -> Out:
  Pr{a, +pos, pp, qq, ok, +mtf, +mdl} = x
  match ok:
    case False{}:
      Bad{"heph-core: bad R postings"}
    case True{}:
      +small = Bool.and(Bool.and(Nat.is_lt(n, U32.to_nat(16777216)), Nat.is_lt(mtf, U32.to_nat(65536))),
        Bool.and(Bool.and(Nat.is_lt(mdl, U32.to_nat(16384)), Nat.is_lt(t, U32.to_nat(32768))),
          Nat.is_lt(tot, HUGE())))
      frame.r.rank(rel.ok(small, n, tot, mdl), k, n, tot, t, len, pos, d, q, r, ev, c, a, pp)

# the header numbers come last first
def frame.r.ns(ok: Bool, ns: List<&2, Nat>, +p: Nat, +len: Nat, +d: Nat, +q: Nat, +r: Nat,
  ev: Array<U32>, +c: Nat, a: Array<U32>
) -> Out:
  match ok ns:
    case True{} Con{+t, Con{+tot, Con{+n, Con{+k, Nil{}}}}}:
      frame.r.post(k, n, tot, t, len, d, q, r, ev, c,
        terms(t, len, n, Pr{a, p, [0 : U32^10n], 0n, True{}, 0n, 0n}))
    case _ _:
      Bad{bad.hdr()}

def frame.r(+len: Nat, +d: Nat, +q: Nat, +r: Nat, ev: Array<U32>, +c: Nat, x: Hdr) -> Out:
  Hdr{a, ns, +p, ok} = x
  frame.r.ns(ok, ns, p, len, d, q, r, ev, c, a)

def frame.d.ns(ok: Bool, ns: List<&2, Nat>, +p: Nat, +len: Nat, +d: Nat, +q: Nat, +r: Nat,
  ev: Array<U32>, +c: Nat, a: Array<U32>
) -> Out:
  match ok ns:
    case True{} Con{+l, Nil{}}:
      frame.d(Nat.is_le(Nat.add(p, l), len), len, p, l, d, q, r, a, ev, c)
    case _ _:
      Bad{bad.hdr()}

def frame.d.hdr(+len: Nat, +d: Nat, +q: Nat, +r: Nat, ev: Array<U32>, +c: Nat, x: Hdr) -> Out:
  Hdr{a, ns, +p, ok} = x
  frame.d.ns(ok, ns, p, len, d, q, r, ev, c, a)

def frame.e.ns(ok: Bool, ns: List<&2, Nat>, +p: Nat, +len: Nat, +d: Nat, +q: Nat, +r: Nat,
  ev: Array<U32>, +c: Nat, a: Array<U32>
) -> Out:
  match ok ns:
    case True{} Con{+l, Nil{}}:
      frame.e(Nat.is_le(Nat.add(p, l), len), len, p, l, d, q, r, a, ev, c)
    case _ _:
      Bad{bad.hdr()}

def frame.e.hdr(+len: Nat, +d: Nat, +q: Nat, +r: Nat, ev: Array<U32>, +c: Nat, x: Hdr) -> Out:
  Hdr{a, ns, +p, ok} = x
  frame.e.ns(ok, ns, p, len, d, q, r, ev, c, a)

def frame.q.ns(ok: Bool, ns: List<&2, Nat>, +p: Nat, +len: Nat, +d: Nat, +q: Nat, +r: Nat,
  ev: Array<U32>, +c: Nat, a: Array<U32>
) -> Out:
  match ok ns:
    case True{} Con{+l, Con{+id, Nil{}}}:
      frame.q(Nat.is_le(Nat.add(p, l), len), len, p, l, id, d, q, r, a, ev, c)
    case _ _:
      Bad{bad.hdr()}

def frame.q.hdr(+len: Nat, +d: Nat, +q: Nat, +r: Nat, ev: Array<U32>, +c: Nat, x: Hdr) -> Out:
  Hdr{a, ns, +p, ok} = x
  frame.q.ns(ok, ns, p, len, d, q, r, ev, c, a)

type Tag is Data:
  TD{}
  TE{}
  TQ{}
  TR{}
  TX{}

def tag.pick(d: Bool, e: Bool, q: Bool, r: Bool) -> Tag:
  match d e q r:
    case True{} _ _ _:
      TD{}
    case False{} True{} _ _:
      TE{}
    case False{} False{} True{} _:
      TQ{}
    case False{} False{} False{} True{}:
      TR{}
    case False{} False{} False{} False{}:
      TX{}

def tag(+b: U32) -> Tag:
  tag.pick(U32.is_eq(b, 68), U32.is_eq(b, 69), U32.is_eq(b, 81), U32.is_eq(b, 82))

def frame.kind(g: Tag, +len: Nat, +pos: Nat, +d: Nat, +q: Nat, +r: Nat, ev: Array<U32>,
  +c: Nat, a: Array<U32>
) -> Out:
  match g:
    case TD{}:
      frame.d.hdr(len, d, q, r, ev, c, header(a, len, 1n+pos, 1n))
    case TE{}:
      frame.e.hdr(len, d, q, r, ev, c, header(a, len, 1n+pos, 1n))
    case TQ{}:
      frame.q.hdr(len, d, q, r, ev, c, header(a, len, 1n+pos, 2n))
    case TR{}:
      frame.r(len, d, q, r, ev, c, header(a, len, 1n+pos, 4n))
    case TX{}:
      Bad{"heph-core: bad frame tag"}

def frame.tag(+len: Nat, +pos: Nat, +d: Nat, +q: Nat, +r: Nat, ev: Array<U32>, +c: Nat,
  x: Array<U32> & U32
) -> Out:
  (a, +b) = x
  frame.kind(tag(b), len, pos, d, q, r, ev, c, a)

def frame.at(more: Bool, +len: Nat, +pos: Nat, +d: Nat, +q: Nat, +r: Nat, ev: Array<U32>,
  +c: Nat, a: Array<U32>
) -> Out:
  match more:
    case False{}:
      End{}
    case True{}:
      frame.tag(len, pos, d, q, r, ev, c, C.byte(a, pos))

# the answer to the frame at pos
def frame(x: S) -> Out:
  S{a, +len, +pos, d, q, r, ev, c} = x
  frame.at(Nat.is_lt(pos, len), len, pos, d, q, r, ev, c, a)

# Main loop
# =========

def run(fuel: Nat, x: Out) -> IO(Unit):
  match fuel x:
    case 0n _:
      IO.die(Unit, 2, "heph-core: too many frames")
    case 1n+f End{}:
      IO.pure(Unit, Unit{})
    case 1n+f Bad{msg}:
      IO.die(Unit, 2, msg)
    case 1n+f Mute{st}:
      run(f, frame(st))
    case 1n+f Say{w, st}:
      W{o, n} = w
      IO.bind(Unit, Unit, IO.try(Unit, Stdout.write(o, n)), u => run(f, frame(st)))

def start(r: Array<U32> & Nat) -> IO(Unit):
  (a, +len) = r
  run(1n+len, frame(S{a, len, 0n, 0n, 0n, 0n, [0 : U32^4n], 0n}))

def main() -> IO(Unit):
  do IO<Unit>:
    r : Array<U32> & Nat <- IO.try(Array<U32> & Nat, Stdin.read())
    start(r)
