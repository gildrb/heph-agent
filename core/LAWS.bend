import Base
import ./core.bend as C
import ./score.bend as F

# Heph core laws. Human-owned claims about core.bend; proofs live in PROOF.bend.
# Spec.* defs are part of the claims: they say what the words mean.
#
# The core reads bytes packed four to a slot of an Array<U32>: byte p sits
# at bits 8 * (p % 4) of slot p / 4. A document, an evidence item or a quote
# is a range of the request buffer, and the ranking postings come packed
# into another buffer. Spec.read and Spec.terms turn these into lists, so
# the claims speak about lists and numbers as before. A read hands its
# buffer back, and a claim may read the same buffer more than once.

# Specs: buffers
# ==============

def Spec.byte.of(+p: Nat, r: Array<U32> & U32) -> Array<U32> & U32:
  (a, w) = r
  (a, (U32.shrn(w, Nat.mul(8n, Nat.mod(p, 4n))) .&. 255 : U32))

# byte p of a buffer
def Spec.byte(a: Array<U32>, +p: Nat) -> Array<U32> & U32:
  Spec.byte.of(p, Array.get(U32, a, U32.from_nat(Nat.div(p, 4n))))

def Spec.cons(-A: Data, +x: A, r: Array<U32> & List<&2, A>) -> Array<U32> & List<&2, A>:
  (a, xs) = r
  (a, x <> xs)

# n bytes from p; r holds the byte at p
def Spec.bytes(n: Nat, +p: Nat, r: Array<U32> & U32) -> Array<U32> & List<&2, U32>:
  match n:
    case 0n:
      (a, b) = r
      (a, Nil{})
    case 1n+m:
      (a, +b) = r
      Spec.cons(U32, b, Spec.bytes(m, 1n+p, Spec.byte(a, 1n+p)))

# the n bytes of a from p
def Spec.read(a: Array<U32>, +p: Nat, n: Nat) -> Array<U32> & List<&2, U32>:
  Spec.bytes(n, p, Spec.byte(a, p))

def Spec.list(-A: Data, r: Array<U32> & List<&2, A>) -> List<&2, A>:
  (a, xs) = r
  xs

# Specs: citations
# ================

def Spec.is_ws(+b: U32) -> Bool:
  Bool.or(U32.is_eq(b, 32), Bool.and(U32.is_ge(b, 9), U32.is_le(b, 13)))

# what one byte adds to a normalized text: nothing for whitespace, else the
# byte, after one space when it closes a gap between words
def Spec.norm.put(w: Bool, +b: U32, gap: Bool) -> List<&2, U32>:
  match w gap:
    case True{} _:
      Nil{}
    case False{} True{}:
      [32, b]
    case False{} False{}:
      [b]

# gap: whitespace since the last byte put; any: a byte put so far
def Spec.norm.go(xs: List<&2, U32>, gap: Bool, +any: Bool) -> List<&2, U32>:
  match xs:
    case Nil{}:
      Nil{}
    case Con{+b, t}:
      +w = Spec.is_ws(b)
      List.append(&2, U32, Spec.norm.put(w, b, gap),
        Spec.norm.go(t, Bool.pick(Bool, w, any, False{}), Bool.pick(Bool, w, any, True{})))

# every run of ASCII whitespace becomes one space; leading and trailing runs go
def Spec.norm(xs: List<&2, U32>) -> List<&2, U32>:
  Spec.norm.go(xs, False{}, False{})

# same bytes, same length
def Spec.same(a: List<&2, U32>, b: List<&2, U32>) -> Bool:
  match a b:
    case Nil{} Nil{}:
      True{}
    case Nil{} Con{y, yt}:
      False{}
    case Con{x, xt} Nil{}:
      False{}
    case Con{x, xt} Con{y, yt}:
      Bool.and(U32.is_eq(x, y), Spec.same(xt, yt))

# the len bytes of h at offset i
def Spec.slice(+h: List<&2, U32>, +i: Nat, +len: Nat) -> List<&2, U32>:
  List.take(&2, U32, List.drop(&2, U32, h, i), len)

# the quote q, normalized, is the slice of the normalized evidence c at i
def Spec.quoted(+c: List<&2, U32>, +q: List<&2, U32>, +i: Nat) -> Bool:
  +nq = Spec.norm(q)
  Spec.same(Spec.slice(Spec.norm(c), i, List.length(&2, U32, nq)), nq)

# an evidence item: where its bytes sit in the request
type Spec.Item is Data:
  Spec.Item{off: Nat, len: Nat}

def Spec.item.len(+off: Nat, r: Array<U32> & U32) -> Maybe<&2, Spec.Item>:
  (ev, len) = r
  Some{Spec.Item{off, U32.to_nat(len)}}

def Spec.item.off(+id: Nat, r: Array<U32> & U32) -> Maybe<&2, Spec.Item>:
  (ev, off) = r
  Spec.item.len(U32.to_nat(off), Array.get(U32, ev, U32.from_nat(1n+Nat.mul(2n, id))))

def Spec.item.if(ok: Bool, +id: Nat, ev: Array<U32>) -> Maybe<&2, Spec.Item>:
  match ok:
    case False{}:
      None{}
    case True{}:
      Spec.item.off(id, Array.get(U32, ev, U32.from_nat(Nat.mul(2n, id))))

# evidence item id of the c items in ev, which holds an (offset, length)
# word pair per item
def Spec.item(ev: Array<U32>, +c: Nat, +id: Nat) -> Maybe<&2, Spec.Item>:
  Spec.item.if(Nat.is_lt(id, c), id, ev)

def Spec.verdict(x: C.Ev) -> C.Verdict:
  match x:
    case C.Ev{a, ev, v}:
      v

def Spec.is_bad(v: C.Verdict) -> Bool:
  match v:
    case C.BadId{}:
      True{}
    case C.Empty{}:
      False{}
    case C.Missing{}:
      False{}
    case C.Found{i}:
      False{}

def Spec.quoted.at(-a: Array<U32>, +qo: Nat, +ql: Nat, +i: Nat, m: Maybe<&2, Spec.Item>) -> Type:
  match m:
    case None{}:
      Empty
    case Some{Spec.Item{+off, +len}}:
      {Spec.quoted(Spec.list(U32, Spec.read(a, off, len)), Spec.list(U32, Spec.read(a, qo, ql)), i)
        == True{} : Bool}

# an accepted citation names a real evidence item, and its quote (the ql
# bytes at qo) normalized is the slice of that item normalized at the offset
def Spec.cited(-a: Array<U32>, +m: Maybe<&2, Spec.Item>, +qo: Nat, +ql: Nat, v: C.Verdict) -> Type:
  match v:
    case C.BadId{}:
      Unit
    case C.Empty{}:
      Unit
    case C.Missing{}:
      Unit
    case C.Found{+i}:
      Spec.quoted.at(a, qo, ql, i, m)

# Citations
# =========

# soundness: an accepted citation's id is valid and its normalized quote is
# the slice of the normalized evidence at the reported offset
law cite_sound:
  for a: Array<U32>
  for ev: Array<U32>
  for +c: Nat
  for +id: Nat
  for +qo: Nat
  for +ql: Nat
  Spec.cited(a, Spec.item(ev, c, id), qo, ql, Spec.verdict(C.verify(a, ev, c, id, qo, ql)))

# a citation is rejected as BadId exactly when its id names no evidence item
law cite_badid:
  for a: Array<U32>
  for ev: Array<U32>
  for +c: Nat
  for +id: Nat
  for +qo: Nat
  for +ql: Nat
  {Spec.is_bad(Spec.verdict(C.verify(a, ev, c, id, qo, ql))) ==
    Maybe.is_none(&2, Spec.Item, Spec.item(ev, c, id)) : Bool}

# Specs: ranking
# ==============
# The postings come packed into p from word 0: per query term its df (the
# term's posting count), then df (id, tf, dl) triples, one U32 each.
# Spec.postings reads them into one list per term; Spec.scores folds the
# lists, as in the list-based core.

type Spec.Post is Data:
  Spec.Post{id: Nat, tf: Nat, dl: Nat}

# a term's postings read so far, and the word q where the next one starts
type Spec.Ps is Type:
  Spec.Ps{p: Array<U32>, q: Nat, ps: List<&2, Spec.Post>}

def Spec.post.dl(+q: Nat, +id: Nat, +tf: Nat, ps: List<&2, Spec.Post>, r: Array<U32> & U32) ->
  Spec.Ps:
  (p, dl) = r
  Spec.Ps{p, Nat.add(q, 3n), List.append(&2, Spec.Post, ps, [Spec.Post{id, tf, U32.to_nat(dl)}])}

def Spec.post.tf(+q: Nat, +id: Nat, ps: List<&2, Spec.Post>, r: Array<U32> & U32) -> Spec.Ps:
  (p, tf) = r
  Spec.post.dl(q, id, U32.to_nat(tf), ps, Array.get(U32, p, U32.from_nat(Nat.add(q, 2n))))

def Spec.post.id(+q: Nat, ps: List<&2, Spec.Post>, r: Array<U32> & U32) -> Spec.Ps:
  (p, id) = r
  Spec.post.tf(q, U32.to_nat(id), ps, Array.get(U32, p, U32.from_nat(1n+q)))

# the posting (id, tf, dl) in words q, q + 1, q + 2
def Spec.post(x: Spec.Ps) -> Spec.Ps:
  Spec.Ps{p, +q, ps} = x
  Spec.post.id(q, ps, Array.get(U32, p, U32.from_nat(q)))

# c more postings
def Spec.posts(c: Nat, x: Spec.Ps) -> Spec.Ps:
  match c:
    case 0n:
      x
    case 1n+m:
      Spec.posts(m, Spec.post(x))

# the terms read so far, and the word q where the next one starts
type Spec.Ts is Type:
  Spec.Ts{p: Array<U32>, q: Nat, ts: List<&2, List<&2, Spec.Post>>}

def Spec.term.fin(+q: Nat, ts: List<&2, List<&2, Spec.Post>>, x: Spec.Ps) -> Spec.Ts:
  Spec.Ps{p, e, ps} = x
  Spec.Ts{p, q, List.append(&2, List<&2, Spec.Post>, ts, [ps])}

def Spec.term.df(+q: Nat, ts: List<&2, List<&2, Spec.Post>>, r: Array<U32> & U32) -> Spec.Ts:
  (p, df) = r
  +c = U32.to_nat(df)
  Spec.term.fin(Nat.add(1n+q, Nat.mul(3n, c)), ts, Spec.posts(c, Spec.Ps{p, 1n+q, Nil{}}))

# the term at word q: its df, then df postings
def Spec.term(x: Spec.Ts) -> Spec.Ts:
  Spec.Ts{p, +q, ts} = x
  Spec.term.df(q, ts, Array.get(U32, p, U32.from_nat(q)))

# t more terms
def Spec.terms(t: Nat, x: Spec.Ts) -> Spec.Ts:
  match t:
    case 0n:
      x
    case 1n+u:
      Spec.terms(u, Spec.term(x))

def Spec.ts(x: Spec.Ts) -> List<&2, List<&2, Spec.Post>>:
  Spec.Ts{p, q, ts} = x
  ts

# the postings of the t terms packed in p, one list per term
def Spec.postings(p: Array<U32>, t: Nat) -> List<&2, List<&2, Spec.Post>>:
  Spec.ts(Spec.terms(t, Spec.Ts{p, 0n, Nil{}}))

# The BM25 score of every chunk: n slots start at 0, and each posting of a
# term adds idf(n, df) * tfn(tf, dl, tot, n) to its chunk's slot, where df is
# the term's posting count (a posting that names no slot adds nothing). The
# weights idf and tfn are score.bend's fixed-point definitions.
def Spec.zeros(n: Nat) -> List<&2, Nat>:
  match n:
    case 0n:
      Nil{}
    case 1n+m:
      0n <> Spec.zeros(m)

def Spec.add(xs: List<&2, Nat>, j: Nat, +x: Nat) -> List<&2, Nat>:
  match xs j:
    case Nil{} _:
      Nil{}
    case Con{v, t} 0n:
      Con{Nat.add(v, x), t}
    case Con{v, t} 1n+p:
      Con{v, Spec.add(t, p, x)}

def Spec.add_posts(ps: List<&2, Spec.Post>, +w: Nat, +tot: Nat, +n: Nat, xs: List<&2, Nat>) ->
  List<&2, Nat>:
  match ps:
    case Nil{}:
      xs
    case Con{Spec.Post{id, tf, dl}, t}:
      Spec.add_posts(t, w, tot, n, Spec.add(xs, id, Nat.mul(w, F.tfn(tf, dl, tot, n))))

def Spec.add_terms(ts: List<&2, List<&2, Spec.Post>>, +tot: Nat, +n: Nat, xs: List<&2, Nat>) ->
  List<&2, Nat>:
  match ts:
    case Nil{}:
      xs
    case Con{+ps, rest}:
      Spec.add_terms(rest, tot, n,
        Spec.add_posts(ps, F.idf(n, List.length(&2, Spec.Post, ps)), tot, n, xs))

# the n chunk scores of the t terms packed in p
def Spec.scores(+n: Nat, +tot: Nat, p: Array<U32>, t: Nat) -> List<&2, Nat>:
  Spec.add_terms(Spec.postings(p, t), tot, n, Spec.zeros(n))

# 2^d
def Spec.pow2(d: Nat) -> Nat:
  match d:
    case 0n:
      1n
    case 1n+e:
      Nat.double(Spec.pow2(e))

def Spec.hits(r: Array<U32> & List<&2, C.Hit>) -> List<&2, C.Hit>:
  (p, hs) = r
  hs

def Spec.all_lt(hs: List<&2, C.Hit>, +n: Nat) -> Bool:
  match hs:
    case Nil{}:
      True{}
    case Con{C.Hit{s, id}, t}:
      Bool.and(Nat.is_lt(id, n), Spec.all_lt(t, n))

# a strictly before b: higher score, or equal score and lower id
def Spec.before(a: C.Hit, b: C.Hit) -> Bool:
  match a b:
    case C.Hit{+sa, +ia} C.Hit{+sb, +ib}:
      Bool.or(Nat.is_lt(sb, sa), Bool.and(Nat.is_eq(sa, sb), Nat.is_lt(ia, ib)))

# no hit is strictly before the one ahead of it
def Spec.sorted(hs: List<&2, C.Hit>) -> Bool:
  match hs:
    case Nil{}:
      True{}
    case Con{+h, t}:
      match t:
        case Nil{}:
          True{}
        case Con{+h2, t2}:
          Bool.and(Bool.not(Spec.before(h2, h)), Spec.sorted(Con{h2, t2}))

# m is the score of the hit's chunk, and it equals s and is positive
def Spec.scored.at(m: Maybe<&2, Nat>, +s: Nat) -> Bool:
  match m:
    case None{}:
      False{}
    case Some{v}:
      Bool.and(Nat.is_eq(v, s), Nat.is_lt(0n, s))

def Spec.all_scored(+xs: List<&2, Nat>, hs: List<&2, C.Hit>) -> Bool:
  match hs:
    case Nil{}:
      True{}
    case Con{C.Hit{s, id}, t}:
      Bool.and(Spec.scored.at(List.get(&2, Nat, xs, id), s), Spec.all_scored(xs, t))

def Spec.has_id(hs: List<&2, C.Hit>, +c: Nat) -> Bool:
  match hs:
    case Nil{}:
      False{}
    case Con{C.Hit{s, id}, t}:
      Bool.or(Nat.is_eq(id, c), Spec.has_id(t, c))

def Spec.all_before(hs: List<&2, C.Hit>, +y: C.Hit) -> Bool:
  match hs:
    case Nil{}:
      True{}
    case Con{h, t}:
      Bool.and(Spec.before(h, y), Spec.all_before(t, y))

# chunk c has score m; if it is positive and c is not among the hits, then
# the hits are full (k of them) and every one comes strictly before c
def Spec.complete.at(m: Maybe<&2, Nat>, +hs: List<&2, C.Hit>, +k: Nat, +c: Nat) -> Bool:
  match m:
    case None{}:
      True{}
    case Some{v}:
      match v:
        case 0n:
          True{}
        case 1n+p:
          Bool.or(Spec.has_id(hs, c), Bool.and(Nat.is_eq(List.length(&2, C.Hit, hs), k),
            Spec.all_before(hs, C.Hit{1n+p, c})))

# Ranking
# =======
# For any postings packed in p (t terms), top-k over n chunks. The score
# table is indexed by U32, so the laws about scores ask n <= 2^d for some
# d <= 31 (main admits n < 2^24).

# every hit carries its chunk's BM25 score, which is positive
law rank_scores:
  for +k: Nat
  for +d: Nat
  for +n: Nat
  for hd: {Nat.is_le(d, 31n) == True{} : Bool}
  for hn: {Nat.is_le(n, Spec.pow2(d)) == True{} : Bool}
  for +tot: Nat
  for p: Array<U32>
  for +t: Nat
  {Spec.all_scored(Spec.scores(n, tot, p, t), Spec.hits(C.rank(k, n, tot, p, t))) == True{} : Bool}

# top-k is complete: a chunk with a positive score is returned, or k hits
# that all come strictly before it are
law rank_complete:
  for +k: Nat
  for +d: Nat
  for +n: Nat
  for hd: {Nat.is_le(d, 31n) == True{} : Bool}
  for hn: {Nat.is_le(n, Spec.pow2(d)) == True{} : Bool}
  for +tot: Nat
  for p: Array<U32>
  for +t: Nat
  for +c: Nat
  {Spec.complete.at(List.get(&2, Nat, Spec.scores(n, tot, p, t), c),
    Spec.hits(C.rank(k, n, tot, p, t)), k, c) == True{} : Bool}

# returns at most k hits
law rank_bounded:
  for +k: Nat
  for +n: Nat
  for +tot: Nat
  for p: Array<U32>
  for +t: Nat
  {Nat.is_le(List.length(&2, C.Hit, Spec.hits(C.rank(k, n, tot, p, t))), k) == True{} : Bool}

# every returned id names one of the n chunks
law rank_ids_valid:
  for +k: Nat
  for +n: Nat
  for +tot: Nat
  for p: Array<U32>
  for +t: Nat
  {Spec.all_lt(Spec.hits(C.rank(k, n, tot, p, t)), n) == True{} : Bool}

# no hit comes strictly before the one ahead of it: scores never rise, and
# ids never fall between equal scores
law rank_sorted:
  for +k: Nat
  for +n: Nat
  for +tot: Nat
  for p: Array<U32>
  for +t: Nat
  {Spec.sorted(Spec.hits(C.rank(k, n, tot, p, t))) == True{} : Bool}

# Specs: chunking
# ===============

def Spec.chunks(r: Array<U32> & List<&2, C.Chunk>) -> List<&2, C.Chunk>:
  (a, cs) = r
  cs

# the chunks after one spanning [ps, pe): each starts after the previous start
# and no later than the previous end (ordered, no gap), is non-empty, ends
# inside the document, and the last one ends at the document's end n
def Spec.chain(cs: List<&2, C.Chunk>, +ps: Nat, +pe: Nat, +n: Nat) -> Bool:
  match cs:
    case Nil{}:
      Nat.is_eq(pe, n)
    case Con{C.Chunk{+a, +e}, t}:
      Bool.and(Nat.is_lt(ps, a), Bool.and(Nat.is_le(a, pe),
        Bool.and(Nat.is_lt(a, e), Bool.and(Nat.is_le(e, n), Spec.chain(t, a, e, n)))))

# the chunks of an n-byte document: none when it is empty, else the first
# starts at 0 and the rest chain on
def Spec.covers(cs: List<&2, C.Chunk>, +n: Nat) -> Bool:
  match cs:
    case Nil{}:
      Nat.is_eq(n, 0n)
    case Con{C.Chunk{+a, +e}, t}:
      Bool.and(Nat.is_eq(a, 0n),
        Bool.and(Nat.is_lt(a, e), Bool.and(Nat.is_le(e, n), Spec.chain(t, a, e, n))))

# every chunk's text, the request bytes [o + start, o + end), is exactly
# the document's bytes [start, end)
def Spec.texts(-a: Array<U32>, +o: Nat, +doc: List<&2, U32>, cs: List<&2, C.Chunk>) -> Type:
  match cs:
    case Nil{}:
      Unit
    case Con{C.Chunk{+s, +e}, t}:
      Pair({Spec.list(U32, Spec.read(a, Nat.add(o, s), Nat.sub(e, s))) ==
        Spec.slice(doc, s, Nat.sub(e, s)) : List<&2, U32>}, Spec.texts(a, o, doc, t))

# Chunking
# ========
# The document is the n bytes of the request a at offset o.

# chunks are ordered, overlap-or-touch (never leave a gap) and cover the document
law chunk_cover:
  for a: Array<U32>
  for +o: Nat
  for +n: Nat
  {Spec.covers(Spec.chunks(C.chunk.doc(a, o, n)), n) == True{} : Bool}

# every chunk's text is exactly the document's bytes [start, end)
law chunk_text:
  for a: Array<U32>
  for +o: Nat
  for +n: Nat
  Spec.texts(a, o, Spec.list(U32, Spec.read(a, o, n)), Spec.chunks(C.chunk.doc(a, o, n)))
