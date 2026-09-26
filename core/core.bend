import Base
import ./score.bend as F

# Heph core: chunking, BM25 ranking over parsed postings, citation checks.
# Bytes live packed in an Array<U32>: byte p sits at bits 8 * (p % 4) of
# slot p / 4 (little-endian). Every reader threads its array and hands it
# back. All arithmetic is integer, so a reference implementation matches bit
# for bit; main.bend keeps every Nat below the runtime's 2^48 limit.

type Chunk is Data:
  Chunk{start: Nat, end: Nat}

type Hit is Data:
  Hit{score: Nat, id: Nat}

type Verdict is Data:
  BadId{}
  Empty{}
  Missing{}
  Found{off: Nat}

def max(+a: Nat, +b: Nat) -> Nat:
  F.pick(Nat.is_lt(a, b), b, a)

def min(+a: Nat, +b: Nat) -> Nat:
  F.pick(Nat.is_lt(a, b), a, b)

def is_ws(+b: U32) -> Bool:
  Bool.or(U32.is_eq(b, 32), Bool.and(U32.is_ge(b, 9), U32.is_le(b, 13)))

def is_cont(+b: U32) -> Bool:
  U32.is_eq((b .&. 192 : U32), 128)

# Bytes
# =====

def byte.of(+p: Nat, r: Array<U32> & U32) -> Array<U32> & U32:
  (a, w) = r
  (a, (U32.shrn(w, Nat.mul(8n, Nat.mod(p, 4n))) .&. 255 : U32))

# byte p of the packed buffer a
def byte(a: Array<U32>, +p: Nat) -> Array<U32> & U32:
  byte.of(p, Array.get(U32, a, U32.from_nat(Nat.div(p, 4n))))

# a byte with its classes, for loops that branch on them
type Rd is Type:
  Rd{a: Array<U32>, b: U32, ws: Bool, cont: Bool, nl: Bool}

def rd.of(r: Array<U32> & U32) -> Rd:
  (a, +b) = r
  Rd{a, b, is_ws(b), is_cont(b), U32.is_eq(b, 10)}

def rd(a: Array<U32>, +p: Nat) -> Rd:
  rd.of(byte(a, p))

# Chunking
# ========
# A chunk ends at the last paragraph break ("\n\n") past MIN bytes, else the
# last whitespace byte past MIN, else the last UTF-8 character boundary, all
# within TARGET bytes. The next chunk starts OVERLAP bytes before the end,
# moved forward to a character boundary and then just past the next
# whitespace byte before the end, if any, so it starts on a word. The
# document is the n bytes of a from offset o; positions are relative to o.

def TARGET() -> Nat:
  U32.to_nat(1200)

def OVERLAP() -> Nat:
  U32.to_nat(200)

def MIN() -> Nat:
  U32.to_nat(600)

# the largest i' <= i with a character boundary at s + i', scanning down
# from x = the byte at s + i; TARGET when there is none in (0, i]
def cut.cb(fuel: Nat, +o: Nat, +s: Nat, +i: Nat, x: Rd) -> Array<U32> & Nat:
  match fuel:
    case 0n:
      Rd{a, b, w, c, l} = x
      (a, TARGET())
    case 1n+f:
      Rd{a, b, w, c, l} = x
      match c:
        case False{}:
          (a, i)
        case True{}:
          cut.cb(f, o, s, Nat.sub(i, 1n), rd(a, Nat.add(o, Nat.add(s, Nat.sub(i, 1n)))))

# no paragraph break: the last whitespace end, else the last boundary
def cut.none(+o: Nat, +s: Nat, ws: Nat, a: Array<U32>) -> Array<U32> & Nat:
  match ws:
    case 0n:
      cut.cb(TARGET(), o, s, TARGET(), rd(a, Nat.add(o, Nat.add(s, TARGET()))))
    case 1n+w:
      (a, 1n+w)

# scans ends j = TARGET down to MIN + 1 for the last "\n\n" end: b1 is the
# byte at s + j - 1 (w1: whitespace, l1: newline), x the byte at s + j - 2;
# ws is the last whitespace end seen (0: none yet)
def cut.back(
  fuel: Nat, l1: Bool, +o: Nat, +s: Nat, +j: Nat, w1: Bool, +ws: Nat, x: Rd
) -> Array<U32> & Nat:
  match fuel l1:
    case 0n _:
      Rd{a, b, w, c, l} = x
      cut.none(o, s, ws, a)
    case 1n+f True{}:
      Rd{a, b, +w, c, +l} = x
      match l:
        case True{}:
          (a, j)
        case False{}:
          cut.back(f, l, o, s, Nat.sub(j, 1n), w, F.pick(Bool.and(Nat.is_eq(ws, 0n), w1), j, ws),
            rd(a, Nat.add(o, Nat.sub(Nat.add(s, j), 3n))))
    case 1n+f False{}:
      Rd{a, b, +w, c, +l} = x
      cut.back(f, l, o, s, Nat.sub(j, 1n), w, F.pick(Bool.and(Nat.is_eq(ws, 0n), w1), j, ws),
        rd(a, Nat.add(o, Nat.sub(Nat.add(s, j), 3n))))

def cut.start(+o: Nat, +s: Nat, x: Rd) -> Array<U32> & Nat:
  Rd{a, b, w, c, l} = x
  cut.back(Nat.sub(TARGET(), MIN()), l, o, s, TARGET(), w, 0n,
    rd(a, Nat.add(o, Nat.add(s, Nat.sub(TARGET(), 2n)))))

def end.fin(+s: Nat, r: Array<U32> & Nat) -> Array<U32> & Nat:
  (a, c) = r
  (a, max(1n+s, Nat.add(s, c)))

def end.if(short: Bool, +o: Nat, +s: Nat, +n: Nat, a: Array<U32>) -> Array<U32> & Nat:
  match short:
    case True{}:
      (a, max(1n+s, n))
    case False{}:
      end.fin(s, cut.start(o, s, rd(a, Nat.add(o, Nat.add(s, Nat.sub(TARGET(), 1n))))))

# the end of the chunk starting at s
def chunk_end(a: Array<U32>, +o: Nat, +s: Nat, +n: Nat) -> Array<U32> & Nat:
  end.if(Nat.is_le(Nat.sub(n, s), TARGET()), o, s, n, a)

# skips continuation bytes while p < e (inr); x is the byte at p
def adv(fuel: Nat, inr: Bool, +o: Nat, +p: Nat, +e: Nat, x: Rd) -> Array<U32> & Nat:
  match fuel inr:
    case 1n+f True{}:
      Rd{a, b, w, c, l} = x
      match c:
        case True{}:
          adv(f, Nat.is_lt(1n+p, e), o, 1n+p, e, rd(a, Nat.add(o, 1n+p)))
        case False{}:
          (a, p)
    case _ _:
      Rd{a, b, w, c, l} = x
      (a, p)

# just past the first whitespace byte in [p, e) (inr: p < e), else raw
def skip(fuel: Nat, inr: Bool, +o: Nat, +p: Nat, +e: Nat, +raw: Nat, x: Rd) ->
  Array<U32> & Nat:
  match fuel inr:
    case 1n+f True{}:
      Rd{a, b, w, c, l} = x
      match w:
        case True{}:
          (a, 1n+p)
        case False{}:
          skip(f, Nat.is_lt(1n+p, e), o, 1n+p, e, raw, rd(a, Nat.add(o, 1n+p)))
    case _ _:
      Rd{a, b, w, c, l} = x
      (a, raw)

def skip.start(+o: Nat, +e: Nat, r: Array<U32> & Nat) -> Array<U32> & Nat:
  (a, +p) = r
  skip(OVERLAP(), Nat.is_lt(p, e), o, p, e, p, rd(a, Nat.add(o, p)))

# keeps a next start inside (s, e]
def clamp(+s: Nat, +e: Nat, x: Nat) -> Nat:
  min(e, max(1n+s, x))

def next.fin(+s: Nat, +e: Nat, r: Array<U32> & Nat) -> Array<U32> & Nat:
  (a, x) = r
  (a, clamp(s, e, x))

# the start of the chunk after [s, e)
def next_start(a: Array<U32>, +o: Nat, +s: Nat, +e: Nat) -> Array<U32> & Nat:
  +p = max(1n+s, Nat.sub(e, OVERLAP()))
  next.fin(s, e, skip.start(o, e, adv(OVERLAP(), Nat.is_lt(p, e), o, p, e, rd(a, Nat.add(o, p)))))

# a chunk start s and its end e; go = e < n
type Span is Type:
  Span{a: Array<U32>, s: Nat, e: Nat, go: Bool}

def span.of(+s: Nat, +n: Nat, r: Array<U32> & Nat) -> Span:
  (a, +e) = r
  Span{a, s, e, Nat.is_lt(e, n)}

def span.at(+o: Nat, +n: Nat, r: Array<U32> & Nat) -> Span:
  (a, +s) = r
  span.of(s, n, chunk_end(a, o, s, n))

def chunk.put(c: Chunk, r: Array<U32> & List<&2, Chunk>) -> Array<U32> & List<&2, Chunk>:
  (a, cs) = r
  (a, c <> cs)

def chunk.go(fuel: Nat, +o: Nat, +n: Nat, x: Span) -> Array<U32> & List<&2, Chunk>:
  match fuel:
    case 0n:
      Span{a, s, e, go} = x
      (a, [Chunk{s, n}])
    case 1n+f:
      Span{a, +s, +e, go} = x
      match go:
        case False{}:
          (a, [Chunk{s, n}])
        case True{}:
          chunk.put(Chunk{s, e}, chunk.go(f, o, n, span.at(o, n, next_start(a, o, s, e))))

# the chunks of the n-byte document at offset o of a
def chunk.doc(a: Array<U32>, +o: Nat, n: Nat) -> Array<U32> & List<&2, Chunk>:
  match n:
    case 0n:
      (a, Nil{})
    case 1n++m:
      chunk.go(1n+m, o, 1n+m, span.of(0n, 1n+m, chunk_end(a, o, 0n, 1n+m)))

# Ranking
# =======
# BM25 (k1 = 6/5, b = 3/4) in fixed point: idf in log2 units with 16
# fraction bits, the term weight scaled by 1024. Postings come packed from
# word 0: per term its df, then df (id, tf, dl) triples, one U32 each.

# the postings buffer p and the score slots s
type Acc is Type:
  Acc{p: Array<U32>, s: Array<Nat>}

# adds x to slot id of s when id < n (go)
def add.at(go: Bool, +id: Nat, +x: Nat, s: Array<Nat>) -> Array<Nat>:
  match go:
    case False{}:
      s
    case True{}:
      F.bump(s, id, x)

def post.dl(+id: Nat, +tf: Nat, +w: Nat, +tot: Nat, +n: Nat, s: Array<Nat>, r: Array<U32> & U32) ->
  Acc:
  (p, +dl) = r
  Acc{p, add.at(Nat.is_lt(id, n), id, Nat.mul(w, F.tfn(tf, U32.to_nat(dl), tot, n)), s)}

def post.tf(+q: Nat, +id: Nat, +w: Nat, +tot: Nat, +n: Nat, s: Array<Nat>, r: Array<U32> & U32) ->
  Acc:
  (p, +tf) = r
  post.dl(id, U32.to_nat(tf), w, tot, n, s, Array.get(U32, p, U32.from_nat(Nat.add(q, 2n))))

def post.id(+q: Nat, +w: Nat, +tot: Nat, +n: Nat, s: Array<Nat>, r: Array<U32> & U32) -> Acc:
  (p, +id) = r
  post.tf(q, U32.to_nat(id), w, tot, n, s, Array.get(U32, p, U32.from_nat(1n+q)))

# the posting (id, tf, dl) in words q .. q + 2 adds w * tfn(tf, dl) to slot id
def post(+q: Nat, +w: Nat, +tot: Nat, +n: Nat, x: Acc) -> Acc:
  Acc{p, s} = x
  post.id(q, w, tot, n, s, Array.get(U32, p, U32.from_nat(q)))

# scatters the c postings from word q of one term with weight w
def add.posts(c: Nat, +q: Nat, +w: Nat, +tot: Nat, +n: Nat, x: Acc) -> Acc:
  match c:
    case 0n:
      x
    case 1n+m:
      add.posts(m, Nat.add(q, 3n), w, tot, n, post(q, w, tot, n, x))

# the scores so far and the word where the next term starts
type Tq is Type:
  Tq{p: Array<U32>, s: Array<Nat>, q: Nat}

def term.fin(+q: Nat, x: Acc) -> Tq:
  Acc{p, s} = x
  Tq{p, s, q}

def term.df(+q: Nat, +tot: Nat, +n: Nat, s: Array<Nat>, r: Array<U32> & U32) -> Tq:
  (p, +df) = r
  +c = U32.to_nat(df)
  term.fin(Nat.add(1n+q, Nat.mul(3n, c)), add.posts(c, 1n+q, F.idf(n, c), tot, n, Acc{p, s}))

# the term at word q: its df, then df (id, tf, dl) triples
def term(+tot: Nat, +n: Nat, x: Tq) -> Tq:
  Tq{p, s, +q} = x
  term.df(q, tot, n, s, Array.get(U32, p, U32.from_nat(q)))

# scatters t more terms
def add.terms(t: Nat, +tot: Nat, +n: Nat, x: Tq) -> Tq:
  match t:
    case 0n:
      x
    case 1n+u:
      add.terms(u, tot, n, term(tot, n, x))

# a strictly before b: higher score, or equal score and lower id
def before(a: Hit, b: Hit) -> Bool:
  match a b:
    case Hit{+sa, +ia} Hit{+sb, +ib}:
      Bool.or(Nat.is_lt(sb, sa), Bool.and(Nat.is_eq(sa, sb), Nat.is_lt(ia, ib)))

# The best hits so far sit worst first. Slots are visited in id order, so a
# new hit comes before a kept one exactly when its score is higher.

# h comes before the first hit of hs
def lead(+h: Hit, hs: List<&2, Hit>) -> Bool:
  match hs:
    case Nil{}:
      False{}
    case Con{x, t}:
      before(h, x)

# puts h after the hits it comes before (go = lead(h, hs)); acc holds the
# hits passed so far, reversed
def ins(hs: List<&2, Hit>, +h: Hit, go: Bool, acc: List<&2, Hit>) -> List<&2, Hit>:
  match hs go:
    case Nil{} _:
      List.reverse.go(&2, Hit, acc, [h])
    case Con{+x, t} False{}:
      List.reverse.go(&2, Hit, acc, h <> Con{x, t})
    case Con{+x, +t} True{}:
      ins(t, h, lead(h, t), x <> acc)

def keep.if(go: Bool, +h: Hit, +x: Hit, +t: List<&2, Hit>) -> List<&2, Hit>:
  match go:
    case True{}:
      ins(t, h, lead(h, t), Nil{})
    case False{}:
      Con{x, t}

# room: fewer than k kept; else h replaces the worst when it beats it
def keep(room: Bool, +h: Hit, +hs: List<&2, Hit>) -> List<&2, Hit>:
  match room hs:
    case True{} _:
      ins(hs, h, lead(h, hs), Nil{})
    case False{} Nil{}:
      Nil{}
    case False{} Con{+x, +t}:
      keep.if(before(h, x), h, x, t)

# the selection state at slot i: its score v, c = |hs| kept hits
type Sel is Type:
  Sel{s: Array<Nat>, v: Nat, c: Nat, hs: List<&2, Hit>}

def sel.of(+c: Nat, hs: List<&2, Hit>, r: Array<Nat> & Nat) -> Sel:
  (s, v) = r
  Sel{s, v, c, hs}

def sel.step(+k: Nat, +i: Nat, x: Sel) -> Sel:
  Sel{s, v, +c, hs} = x
  match v:
    case 0n:
      sel.of(c, hs, Array.get(Nat, s, U32.from_nat(1n+i)))
    case 1n+w:
      +room = Nat.is_lt(c, k)
      sel.of(F.pick(room, 1n+c, c), keep(room, Hit{1n+w, i}, hs),
        Array.get(Nat, s, U32.from_nat(1n+i)))

# visits slots i .. i + fuel - 1
def sel(fuel: Nat, +k: Nat, +i: Nat, x: Sel) -> Sel:
  match fuel:
    case 0n:
      x
    case 1n+f:
      sel(f, k, 1n+i, sel.step(k, i, x))

def rank.fin(p: Array<U32>, x: Sel) -> Array<U32> & List<&2, Hit>:
  Sel{s, v, c, hs} = x
  (p, List.reverse(&2, Hit, hs))

def rank.sel(+k: Nat, +n: Nat, x: Tq) -> Array<U32> & List<&2, Hit>:
  Tq{p, s, q} = x
  rank.fin(p, sel(n, k, 0n, sel.of(0n, Nil{}, Array.get(Nat, s, 0))))

# the k best of chunks 0 .. n - 1 with a positive score, best first; tot =
# sum of chunk lengths; p holds t terms
def rank(+k: Nat, +n: Nat, +tot: Nat, p: Array<U32>, t: Nat) -> Array<U32> & List<&2, Hit>:
  rank.sel(k, n, add.terms(t, tot, n, Tq{p, [0n : Nat^F.depth(n)], 0n}))

# Citations
# =========
# A quote is accepted when its whitespace-normalized bytes occur in the
# whitespace-normalized bytes of the cited evidence. Normalizing collapses
# every run of ASCII whitespace to one space and drops leading and trailing
# runs. A cursor walks raw bytes [p, e) of a in one of three states: Lead
# (nothing emitted yet, or just after an emitted space), Word (after a
# byte), Gap (in a whitespace run after a byte). A Gap followed by a byte
# emits the space and moves to Lead without consuming the byte.

type St is Data:
  SLead{}
  SWord{}
  SGap{}

type Cur is Data:
  Cur{p: Nat, st: St}

type Emit is Data:
  EStop{}
  EByte{b: U32, cur: Cur}

# a byte that is not whitespace, at p in state st
def emit.st(st: St, +p: Nat, +b: U32) -> Emit:
  match st:
    case SLead{}:
      EByte{b, Cur{1n+p, SWord{}}}
    case SWord{}:
      EByte{b, Cur{1n+p, SWord{}}}
    case SGap{}:
      EByte{32, Cur{p, SLead{}}}

# the state after a whitespace byte in st
def ws.st(st: St) -> St:
  match st:
    case SLead{}:
      SLead{}
    case SWord{}:
      SGap{}
    case SGap{}:
      SGap{}

# the next emitted byte from raw position p (inr: p < e) in state st; x is
# the byte at p
def next(fuel: Nat, inr: Bool, +p: Nat, +e: Nat, st: St, x: Rd) -> Array<U32> & Emit:
  match fuel inr:
    case 1n+f True{}:
      Rd{a, +b, w, c, l} = x
      match w:
        case True{}:
          next(f, Nat.is_lt(1n+p, e), 1n+p, e, ws.st(st), rd(a, 1n+p))
        case False{}:
          (a, emit.st(st, p, b))
    case _ _:
      Rd{a, b, w, c, l} = x
      (a, EStop{})

# the next emitted byte after cursor c, before raw end e
def step(+e: Nat, c: Cur, a: Array<U32>) -> Array<U32> & Emit:
  match c:
    case Cur{+p, st}:
      next(1n+Nat.sub(e, p), Nat.is_lt(p, e), p, e, st, rd(a, p))

# i more emitted bytes after the one in x (Stop: there are fewer)
def skip.n(i: Nat, +e: Nat, x: Array<U32> & Emit) -> Array<U32> & Emit:
  match i:
    case 0n:
      x
    case 1n+j:
      (a, y) = x
      match y:
        case EStop{}:
          (a, EStop{})
        case EByte{b, c}:
          skip.n(j, e, step(e, c, a))

# comparing the evidence (cursor ce, end ee) with the quote (cq, eq)
type Cmp2 is Type:
  CEnd{a: Array<U32>, ok: Bool}
  CMore{a: Array<U32>, ce: Cur, cq: Cur}

def cmp.if(eq: Bool, ce: Cur, cq: Cur, a: Array<U32>) -> Cmp2:
  match eq:
    case True{}:
      CMore{a, ce, cq}
    case False{}:
      CEnd{a, False{}}

def cmp.e(+y: U32, cq: Cur, r: Array<U32> & Emit) -> Cmp2:
  (a, x) = r
  match x:
    case EStop{}:
      CEnd{a, False{}}
    case EByte{+z, ce}:
      cmp.if(U32.is_eq(y, z), ce, cq, a)

def cmp.q(+ee: Nat, ce: Cur, r: Array<U32> & Emit) -> Cmp2:
  (a, x) = r
  match x:
    case EStop{}:
      CEnd{a, True{}}
    case EByte{+y, cq}:
      cmp.e(y, cq, step(ee, ce, a))

# the quote's remaining bytes come next in the evidence
def same(fuel: Nat, +ee: Nat, +eq: Nat, x: Cmp2) -> Array<U32> & Bool:
  match fuel:
    case 0n:
      match x:
        case CEnd{a, ok}:
          (a, ok)
        case CMore{a, ce, cq}:
          (a, False{})
    case 1n+f:
      match x:
        case CEnd{a, ok}:
          (a, ok)
        case CMore{a, ce, cq}:
          same(f, ee, eq, cmp.q(ee, ce, step(eq, cq, a)))

def check.at(+ee: Nat, +qo: Nat, +ql: Nat, r: Array<U32> & Emit) -> Array<U32> & Bool:
  (a, x) = r
  match x:
    case EStop{}:
      (a, False{})
    case EByte{b, ce}:
      same(1n+ql, ee, Nat.add(qo, ql), CMore{a, ce, Cur{qo, SLead{}}})

# the normalized quote at [qo, qo + ql) sits at offset i of the normalized
# evidence at [eo, eo + el)
def check(a: Array<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat, +i: Nat) -> Array<U32> & Bool:
  +ee = Nat.add(eo, el)
  check.at(ee, qo, ql, skip.n(i, ee, (a, EByte{0, Cur{eo, SLead{}}})))

# Search
# ------
# KMP over the normalized evidence proposes offsets; check confirms each
# one, so an accepted offset never rests on the table logic alone.

type Pat is Type:
  Pat{a: Array<U32>, pat: Array<U32>, m: Nat}

# writes the normalized quote into pat, one byte per slot; m so far
def pat.fill(fuel: Nat, +e: Nat, +m: Nat, pat: Array<U32>, x: Array<U32> & Emit) -> Pat:
  match fuel:
    case 0n:
      (a, y) = x
      Pat{a, pat, m}
    case 1n+f:
      (a, y) = x
      match y:
        case EStop{}:
          Pat{a, pat, m}
        case EByte{+b, c}:
          pat.fill(f, e, 1n+m, Array.set(U32, pat, U32.from_nat(m), b), step(e, c, a))

# the pattern and its failure table: f[j] = the longest proper border of
# pat[0 .. j]
type Tab is Type:
  Tab{pat: Array<U32>, f: Array<U32>}

type Fall is Type:
  FHold{t: Tab, k: Nat}
  FBack{t: Tab, k: Nat}

def fall.back(pat: Array<U32>, r: Array<U32> & U32) -> Fall:
  (f, k) = r
  FBack{Tab{pat, f}, U32.to_nat(k)}

def fall.miss(k: Nat, pat: Array<U32>, f: Array<U32>) -> Fall:
  match k:
    case 0n:
      FHold{Tab{pat, f}, 0n}
    case 1n+j:
      fall.back(pat, Array.get(U32, f, U32.from_nat(j)))

def fall.if(eq: Bool, +k: Nat, pat: Array<U32>, f: Array<U32>) -> Fall:
  match eq:
    case True{}:
      FHold{Tab{pat, f}, 1n+k}
    case False{}:
      fall.miss(k, pat, f)

def fall.at(+k: Nat, +y: U32, f: Array<U32>, r: Array<U32> & U32) -> Fall:
  (pat, +x) = r
  fall.if(U32.is_eq(x, y), k, pat, f)

# one step from border k for the next byte y
def fall.look(+k: Nat, +y: U32, t: Tab) -> Fall:
  Tab{pat, f} = t
  fall.at(k, y, f, Array.get(U32, pat, U32.from_nat(k)))

# the border after byte y
def fall(fuel: Nat, +y: U32, x: Fall) -> Tab & Nat:
  match fuel:
    case 0n:
      match x:
        case FHold{t, k}:
          (t, k)
        case FBack{t, k}:
          (t, 0n)
    case 1n+g:
      match x:
        case FHold{t, k}:
          (t, k)
        case FBack{t, +k}:
          fall(g, y, fall.look(k, y, t))

def tab.put(+j: Nat, r: Tab & Nat) -> Tab & Nat:
  (t, +k) = r
  Tab{pat, f} = t
  (Tab{pat, Array.set(U32, f, U32.from_nat(j), U32.from_nat(k))}, k)

def tab.at(+j: Nat, +k: Nat, f: Array<U32>, r: Array<U32> & U32) -> Tab & Nat:
  (pat, +y) = r
  tab.put(j, fall(1n+k, y, fall.look(k, y, Tab{pat, f})))

def tab.step(+j: Nat, +k: Nat, t: Tab) -> Tab & Nat:
  Tab{pat, f} = t
  tab.at(j, k, f, Array.get(U32, pat, U32.from_nat(j)))

# fills f[j ..] (k = f[j - 1])
def tab.fill(fuel: Nat, +j: Nat, x: Tab & Nat) -> Tab:
  match fuel:
    case 0n:
      (t, k) = x
      t
    case 1n+g:
      (t, +k) = x
      tab.fill(g, 1n+j, tab.step(j, k, t))

# the search after i emitted evidence bytes, k of the pattern matched
type Find is Type:
  FGot{a: Array<U32>, t: Tab, v: Verdict}
  FLook{a: Array<U32>, t: Tab, c: Cur, i: Nat, k: Nat}

# the evidence [eo, eo + el) and quote [qo, qo + ql); m pattern bytes
type Job is Data:
  Job{eo: Nat, el: Nat, qo: Nat, ql: Nat, m: Nat}

def job.m(j: Job) -> Nat:
  match j:
    case Job{eo, el, qo, ql, m}:
      m

def find.again(c: Cur, +i: Nat, a: Array<U32>, pat: Array<U32>, r: Array<U32> & U32) -> Find:
  (f, k) = r
  FLook{a, Tab{pat, f}, c, i, U32.to_nat(k)}

def find.miss(c: Cur, +i: Nat, +k: Nat, a: Array<U32>, t: Tab) -> Find:
  Tab{pat, f} = t
  find.again(c, i, a, pat, Array.get(U32, f, U32.from_nat(Nat.sub(k, 1n))))

def find.ok(+at: Nat, c: Cur, +i: Nat, +k: Nat, t: Tab, r: Array<U32> & Bool) -> Find:
  (a, ok) = r
  match ok:
    case True{}:
      FGot{a, t, Found{at}}
    case False{}:
      find.miss(c, i, k, a, t)

def find.full(full: Bool, +j: Job, c: Cur, +i: Nat, +k: Nat, t: Tab, a: Array<U32>) -> Find:
  match full:
    case False{}:
      FLook{a, t, c, i, k}
    case True{}:
      Job{+eo, +el, +qo, +ql, +m} = j
      +at = Nat.sub(i, m)
      find.ok(at, c, i, k, t, check(a, eo, el, qo, ql, at))

def find.k(+j: Job, c: Cur, +i: Nat, a: Array<U32>, r: Tab & Nat) -> Find:
  (t, +k) = r
  find.full(Nat.is_eq(k, job.m(j)), j, c, i, k, t, a)

def find.byte(+j: Job, +i: Nat, +k: Nat, t: Tab, r: Array<U32> & Emit) -> Find:
  (a, x) = r
  match x:
    case EStop{}:
      FGot{a, t, Missing{}}
    case EByte{+y, c}:
      find.k(j, c, 1n+i, a, fall(1n+k, y, fall.look(k, y, t)))

def find.step(+j: Job, +e: Nat, a: Array<U32>, t: Tab, c: Cur, +i: Nat, +k: Nat) -> Find:
  find.byte(j, i, k, t, step(e, c, a))

def find(fuel: Nat, +j: Job, +e: Nat, x: Find) -> Array<U32> & Verdict:
  match fuel:
    case 0n:
      match x:
        case FGot{a, t, v}:
          (a, v)
        case FLook{a, t, c, i, k}:
          (a, Missing{})
    case 1n+g:
      match x:
        case FGot{a, t, v}:
          (a, v)
        case FLook{a, t, c, +i, +k}:
          find(g, j, e, find.step(j, e, a, t, c, i, k))

def search.pat(+eo: Nat, +el: Nat, +qo: Nat, +ql: Nat, +d: Nat, x: Pat) -> Array<U32> & Verdict:
  Pat{a, pat, m} = x
  match m:
    case 0n:
      (a, Empty{})
    case 1n++mm:
      t = tab.fill(mm, 1n, (Tab{pat, [0 : U32^d]}, 0n))
      +e = Nat.add(eo, el)
      find(1n+Nat.mul(2n, el), Job{eo, el, qo, ql, 1n+mm}, e, FLook{a, t, Cur{eo, SLead{}}, 0n, 0n})

# the verdict on the quote at [qo, qo + ql) against the evidence at
# [eo, eo + el)
def search(a: Array<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat) -> Array<U32> & Verdict:
  +d = F.depth(1n+ql)
  +eq = Nat.add(qo, ql)
  search.pat(eo, el, qo, ql, d,
    pat.fill(1n+Nat.mul(2n, ql), eq, 0n, [0 : U32^d], step(eq, Cur{qo, SLead{}}, a)))

# The evidence items sit in ev as (offset, length) word pairs, c of them.

type Ev is Type:
  Ev{a: Array<U32>, ev: Array<U32>, v: Verdict}

def verify.fin(ev: Array<U32>, r: Array<U32> & Verdict) -> Ev:
  (a, v) = r
  Ev{a, ev, v}

def verify.len(+qo: Nat, +ql: Nat, a: Array<U32>, +eo: Nat, r: Array<U32> & U32) -> Ev:
  (ev, el) = r
  verify.fin(ev, search(a, eo, U32.to_nat(el), qo, ql))

def verify.off(+id: Nat, +qo: Nat, +ql: Nat, a: Array<U32>, r: Array<U32> & U32) -> Ev:
  (ev, eo) = r
  verify.len(qo, ql, a, U32.to_nat(eo), Array.get(U32, ev, U32.from_nat(1n+Nat.mul(2n, id))))

def verify.if(ok: Bool, +id: Nat, +qo: Nat, +ql: Nat, a: Array<U32>, ev: Array<U32>) -> Ev:
  match ok:
    case False{}:
      Ev{a, ev, BadId{}}
    case True{}:
      verify.off(id, qo, ql, a, Array.get(U32, ev, U32.from_nat(Nat.mul(2n, id))))

# the verdict on quote [qo, qo + ql) of a against evidence item id of the
# c items in ev
def verify(a: Array<U32>, ev: Array<U32>, +c: Nat, +id: Nat, +qo: Nat, +ql: Nat) -> Ev:
  verify.if(Nat.is_lt(id, c), id, qo, ql, a, ev)
