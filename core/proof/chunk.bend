import Base
import ../core.bend as C
import ../LAWS.bend as Laws
import ../score.bend as F
import ./logic.bend as L
import ./array.bend as A

# Chunking: cover
# ===============
# Chunk bounds come from max/min/clamp alone, so the bytes read (and the
# array threaded through the reads) never matter here. A run's facts come
# with its shape (a pair), since the run holds the array and can be walked
# only once.

def cg.a(r: Array<U32> & List<&2, C.Chunk>) -> Array<U32>:
  (a, cs) = r
  a

def CEta(-r: Array<U32> & List<&2, C.Chunk>) -> Type:
  {r == (cg.a(r), Laws.Spec.chunks(r)) : Array<U32> & List<&2, C.Chunk>}

# the run from x chains on after [s, e)
def Tail(-f: Nat, -o: Nat, -n: Nat, -s: Nat, -e: Nat, -x: C.Span) -> Type:
  Pair(CEta(C.chunk.go(f, o, n, x)),
    {Laws.Spec.chain(Laws.Spec.chunks(C.chunk.go(f, o, n, x)), s, e, n) == True{} : Bool})

# the last chunk [s, n) closes a chain after [ps, pe)
def last_ok(+s: Nat, +n: Nat, +ps: Nat, +pe: Nat,
  +h1: {Nat.is_lt(ps, s) == True{} : Bool}, +h2: {Nat.is_le(s, pe) == True{} : Bool},
  +h3: {Nat.is_lt(s, n) == True{} : Bool}
) -> {Laws.Spec.chain([C.Chunk{s, n}], ps, pe, n) == True{} : Bool}:
  L.and_i(Nat.is_lt(ps, s), Bool.and(Nat.is_le(s, pe), Bool.and(Nat.is_lt(s, n),
      Bool.and(Nat.is_le(n, n), Nat.is_eq(n, n)))), h1,
    L.and_i(Nat.is_le(s, pe), Bool.and(Nat.is_lt(s, n), Bool.and(Nat.is_le(n, n), Nat.is_eq(n, n))), h2,
      L.and_i(Nat.is_lt(s, n), Bool.and(Nat.is_le(n, n), Nat.is_eq(n, n)), h3,
        L.and_i(Nat.is_le(n, n), Nat.is_eq(n, n), L.le_refl(n), L.eq_refl(n)))))

# the run from Span{a, s2, e2, e2 < n} chains on after [s, e), for every a,
# s2 in (s, e] with s2 < n, and e2 > s2
def Next(-f: Nat, -o: Nat, -n: Nat, -s: Nat, -e: Nat) -> Type:
  @a: Array<U32> -> @+s2: Nat -> @+e2: Nat ->
    @h1: {Nat.is_lt(s, s2) == True{} : Bool} -> @h2: {Nat.is_le(s2, e) == True{} : Bool} ->
    @h3: {Nat.is_lt(s2, n) == True{} : Bool} -> @h4: {Nat.is_lt(s2, e2) == True{} : Bool} ->
    Tail(f, o, n, s, e, C.Span{a, s2, e2, Nat.is_lt(e2, n)})

def cv_fin(-f: Nat, -o: Nat, +n: Nat, -s: Nat, -e: Nat, +s2: Nat, r: Array<U32> & Nat,
  +h1: {Nat.is_lt(s, s2) == True{} : Bool}, +h2: {Nat.is_le(s2, e) == True{} : Bool},
  +h3: {Nat.is_lt(s2, n) == True{} : Bool}, k: Next(f, o, n, s, e)
) -> Tail(f, o, n, s, e, C.span.of(s2, n, C.end.fin(s2, r))):
  match r:
    case (a, +c):
      k(a, s2, C.max(1n+s2, Nat.add(s2, c)), h1, h2, h3, L.lt_s(s2, Nat.add(s2, c)))

def cv_end(-f: Nat, +o: Nat, +n: Nat, -s: Nat, -e: Nat, +s2: Nat, short: Bool, a: Array<U32>,
  +h1: {Nat.is_lt(s, s2) == True{} : Bool}, +h2: {Nat.is_le(s2, e) == True{} : Bool},
  +h3: {Nat.is_lt(s2, n) == True{} : Bool}, k: Next(f, o, n, s, e)
) -> Tail(f, o, n, s, e, C.span.of(s2, n, C.end.if(short, o, s2, n, a))):
  match short:
    case True{}:
      k(a, s2, C.max(1n+s2, n), h1, h2, h3, L.lt_s(s2, n))
    case False{}:
      cv_fin(f, o, n, s, e, s2,
        C.cut.start(o, s2, C.rd(a, Nat.add(o, Nat.add(s2, Nat.sub(C.TARGET(), 1n))))), h1, h2, h3, k)

def cv_next(-f: Nat, +o: Nat, +n: Nat, +s: Nat, +e: Nat, r: Array<U32> & Nat,
  +h4: {Nat.is_lt(s, e) == True{} : Bool}, +h5: {Nat.is_lt(e, n) == True{} : Bool},
  k: Next(f, o, n, s, e)
) -> Tail(f, o, n, s, e, C.span.at(o, n, C.next.fin(s, e, r))):
  match r:
    case (a, +x):
      +s2 = C.clamp(s, e, x)
      cv_end(f, o, n, s, e, s2, Nat.is_le(Nat.sub(n, s2), C.TARGET()), a,
        L.clamp_gt(s, e, x, h4), L.clamp_le(s, e, x), L.le_lt(s2, e, n, L.clamp_le(s, e, x), h5), k)

def ns_step(+o: Nat, +s: Nat, +e: Nat, a: Array<U32>) -> Array<U32> & Nat:
  +p = C.max(1n+s, Nat.sub(e, C.OVERLAP()))
  C.skip.start(o, e, C.adv(C.OVERLAP(), Nat.is_lt(p, e), o, p, e, C.rd(a, Nat.add(o, p))))

def PutTail(-f: Nat, -o: Nat, -n: Nat, -s: Nat, -e: Nat, -ps: Nat, -pe: Nat, -x: C.Span) -> Type:
  Pair(CEta(C.chunk.put(C.Chunk{s, e}, C.chunk.go(f, o, n, x))),
    {Laws.Spec.chain(Laws.Spec.chunks(C.chunk.put(C.Chunk{s, e}, C.chunk.go(f, o, n, x))), ps, pe,
      n) == True{} : Bool})

# [s, e) then a run that chains on after it
def go_put(-f: Nat, -o: Nat, +n: Nat, +s: Nat, +e: Nat, +ps: Nat, +pe: Nat, -x: C.Span,
  +h1: {Nat.is_lt(ps, s) == True{} : Bool}, +h2: {Nat.is_le(s, pe) == True{} : Bool},
  +h4: {Nat.is_lt(s, e) == True{} : Bool}, +h5: {Nat.is_lt(e, n) == True{} : Bool},
  t: Tail(f, o, n, s, e, x)
) -> PutTail(f, o, n, s, e, ps, pe, x):
  (eta, r2) = t
  -g = C.chunk.go(f, o, n, x)
  %Equal.sym(Array<U32> & List<&2, C.Chunk>, g, (cg.a(g), Laws.Spec.chunks(g)), eta) :
    Pair(CEta(C.chunk.put(C.Chunk{s, e}, _)),
      {Laws.Spec.chain(Laws.Spec.chunks(C.chunk.put(C.Chunk{s, e}, _)), ps, pe, n) == True{} : Bool})
  -rest = Laws.Spec.chain(Laws.Spec.chunks(g), s, e, n)
  ({==}, L.and_i(Nat.is_lt(ps, s), Bool.and(Nat.is_le(s, pe), Bool.and(Nat.is_lt(s, e),
      Bool.and(Nat.is_le(e, n), rest))), h1,
    L.and_i(Nat.is_le(s, pe), Bool.and(Nat.is_lt(s, e), Bool.and(Nat.is_le(e, n), rest)), h2,
      L.and_i(Nat.is_lt(s, e), Bool.and(Nat.is_le(e, n), rest), h4,
        L.and_i(Nat.is_le(e, n), rest, L.lt_le(e, n, h5), r2)))))

# every chunk.go run chains on after any [ps, pe) with ps < s <= pe
def go_chain(fuel: Nat, +o: Nat, +n: Nat, a: Array<U32>, +s: Nat, +e: Nat, go: Bool,
  +ps: Nat, +pe: Nat,
  +h1: {Nat.is_lt(ps, s) == True{} : Bool}, +h2: {Nat.is_le(s, pe) == True{} : Bool},
  +h3: {Nat.is_lt(s, n) == True{} : Bool}, +h4: {Nat.is_lt(s, e) == True{} : Bool},
  +h5: {Nat.is_lt(e, n) == go : Bool}
) -> Tail(fuel, o, n, ps, pe, C.Span{a, s, e, go}):
  match fuel go:
    case 0n _:
      ({==}, last_ok(s, n, ps, pe, h1, h2, h3))
    case 1n+f False{}:
      ({==}, last_ok(s, n, ps, pe, h1, h2, h3))
    case 1n++f True{}:
      go_put(f, o, n, s, e, ps, pe, C.span.at(o, n, C.next_start(a, o, s, e)), h1, h2, h4, h5,
        cv_next(f, o, n, s, e, ns_step(o, s, e, a), h4, h5,
          a2 => s2 => e2 => g1 => g2 => g3 => g4 =>
            go_chain(f, o, n, a2, s2, e2, Nat.is_lt(e2, n), s, e, g1, g2, g3, g4, {==})))

def first_ok(+n: Nat, +h3: {Nat.is_lt(0n, n) == True{} : Bool}) ->
  {Laws.Spec.covers([C.Chunk{0n, n}], n) == True{} : Bool}:
  L.and_i(Nat.is_eq(0n, 0n), Bool.and(Nat.is_lt(0n, n), Bool.and(Nat.is_le(n, n), Nat.is_eq(n, n))), {==},
    L.and_i(Nat.is_lt(0n, n), Bool.and(Nat.is_le(n, n), Nat.is_eq(n, n)), h3,
      L.and_i(Nat.is_le(n, n), Nat.is_eq(n, n), L.le_refl(n), L.eq_refl(n))))

# [0, e) then a run that chains on after it covers the document
def cover_put(-f: Nat, -o: Nat, +n: Nat, +e: Nat, -x: C.Span,
  +h4: {Nat.is_lt(0n, e) == True{} : Bool}, +h5: {Nat.is_lt(e, n) == True{} : Bool},
  t: Tail(f, o, n, 0n, e, x)
) -> {Laws.Spec.covers(Laws.Spec.chunks(C.chunk.put(C.Chunk{0n, e}, C.chunk.go(f, o, n, x))), n) ==
  True{} : Bool}:
  (eta, r2) = t
  -g = C.chunk.go(f, o, n, x)
  %Equal.sym(Array<U32> & List<&2, C.Chunk>, g, (cg.a(g), Laws.Spec.chunks(g)), eta) :
    {Laws.Spec.covers(Laws.Spec.chunks(C.chunk.put(C.Chunk{0n, e}, _)), n) == True{} : Bool}
  -rest = Laws.Spec.chain(Laws.Spec.chunks(g), 0n, e, n)
  L.and_i(Nat.is_eq(0n, 0n), Bool.and(Nat.is_lt(0n, e), Bool.and(Nat.is_le(e, n), rest)), {==},
    L.and_i(Nat.is_lt(0n, e), Bool.and(Nat.is_le(e, n), rest), h4,
      L.and_i(Nat.is_le(e, n), rest, L.lt_le(e, n, h5), r2)))

# the run from the first chunk [0, e) covers a non-empty document
def go_cover(fuel: Nat, +o: Nat, +n: Nat, a: Array<U32>, +e: Nat, go: Bool,
  +h3: {Nat.is_lt(0n, n) == True{} : Bool}, +h4: {Nat.is_lt(0n, e) == True{} : Bool},
  +h5: {Nat.is_lt(e, n) == go : Bool}
) -> {Laws.Spec.covers(Laws.Spec.chunks(C.chunk.go(fuel, o, n, C.Span{a, 0n, e, go})), n) ==
  True{} : Bool}:
  match fuel go:
    case 0n _:
      first_ok(n, h3)
    case 1n+f False{}:
      first_ok(n, h3)
    case 1n++f True{}:
      cover_put(f, o, n, e, C.span.at(o, n, C.next_start(a, o, 0n, e)), h4, h5,
        cv_next(f, o, n, 0n, e, ns_step(o, 0n, e, a), h4, h5,
          a2 => s2 => e2 => g1 => g2 => g3 => g4 =>
            go_chain(f, o, n, a2, s2, e2, Nat.is_lt(e2, n), 0n, e, g1, g2, g3, g4, {==})))

def First(-f: Nat, -o: Nat, -n: Nat) -> Type:
  @a: Array<U32> -> @+e: Nat -> @h4: {Nat.is_lt(0n, e) == True{} : Bool} ->
    {Laws.Spec.covers(Laws.Spec.chunks(C.chunk.go(f, o, n, C.Span{a, 0n, e, Nat.is_lt(e, n)})), n) ==
      True{} : Bool}

def fc_fin(-f: Nat, -o: Nat, +n: Nat, r: Array<U32> & Nat, k: First(f, o, n)) ->
  {Laws.Spec.covers(Laws.Spec.chunks(C.chunk.go(f, o, n, C.span.of(0n, n, C.end.fin(0n, r)))), n) ==
    True{} : Bool}:
  match r:
    case (a, +c):
      k(a, C.max(1n, Nat.add(0n, c)), L.lt_s(0n, Nat.add(0n, c)))

def fc_end(-f: Nat, +o: Nat, +n: Nat, short: Bool, a: Array<U32>, k: First(f, o, n)) ->
  {Laws.Spec.covers(Laws.Spec.chunks(C.chunk.go(f, o, n,
    C.span.of(0n, n, C.end.if(short, o, 0n, n, a)))), n) == True{} : Bool}:
  match short:
    case True{}:
      k(a, C.max(1n, n), L.lt_s(0n, n))
    case False{}:
      fc_fin(f, o, n, C.cut.start(o, 0n, C.rd(a, Nat.add(o, Nat.add(0n, Nat.sub(C.TARGET(), 1n))))), k)

def Laws.chunk_cover(a, o, n):
  match n:
    case 0n:
      {==}
    case 1n++m:
      fc_end(1n+m, o, 1n+m, Nat.is_le(Nat.sub(1n+m, 0n), C.TARGET()), a,
        a2 => e2 => h4 => go_cover(1n+m, o, 1n+m, a2, e2, Nat.is_lt(e2, 1n+m), {==}, h4, {==}))

# Chunking: text
# ==============
# The array is rewritten to emb(t) once, so the document is bytes(t, n, o)
# and a chunk's text is bytes(t, e - s, o + s). Chunk bounds come from
# chunk_cover (e <= n for every chunk), so the chunker is not walked again.

# dropping s bytes starts the read s bytes later
def bytes_drop(+t: A.Tr<U32>, n: Nat, +p: Nat, s: Nat) ->
  {List.drop(&2, U32, A.bytes(t, n, p), s) == A.bytes(t, Nat.sub(n, s), Nat.add(p, s)) : List<&2, U32>}:
  match n s:
    case 0n 0n:
      {==}
    case 0n 1n+q:
      {==}
    case 1n+m 0n:
      %Equal.sym(Nat, Nat.add(p, 0n), p, L.add0(p)) :
        {A.bytes(t, 1n+m, p) == A.bytes(t, 1n+m, _) : List<&2, U32>}
      {==}
    case 1n+m 1n++q:
      %Equal.sym(Nat, Nat.add(p, 1n+q), 1n+Nat.add(p, q), L.add_s(p, q)) :
        {List.drop(&2, U32, A.bytes(t, m, 1n+p), q) == A.bytes(t, Nat.sub(m, q), _) : List<&2, U32>}
      bytes_drop(t, m, 1n+p, q)

# taking l <= m of m bytes reads l bytes
def bytes_take(+t: A.Tr<U32>, m: Nat, +p: Nat, l: Nat, h: {Nat.is_le(l, m) == True{} : Bool}) ->
  {List.take(&2, U32, A.bytes(t, m, p), l) == A.bytes(t, l, p) : List<&2, U32>}:
  match m l:
    case 0n 0n:
      {==}
    case 0n 1n+k:
      L.absurd({Nil{} == A.bytes(t, 1n+k, p) : List<&2, U32>}, L.ft(h))
    case 1n+j 0n:
      {==}
    case 1n+j 1n+k:
      %Equal.sym(List<&2, U32>, List.take(&2, U32, A.bytes(t, j, 1n+p), k), A.bytes(t, k, 1n+p),
        bytes_take(t, j, 1n+p, k, h)) :
        {A.bv(t, p) <> _ == A.bv(t, p) <> A.bytes(t, k, 1n+p) : List<&2, U32>}
      {==}

# e <= n gives e - s <= n - s
def le_sub(s: Nat, e: Nat, n: Nat, h: {Nat.is_le(e, n) == True{} : Bool}) ->
  {Nat.is_le(Nat.sub(e, s), Nat.sub(n, s)) == True{} : Bool}:
  match s e n:
    case 0n 0n _:
      L.le0(Nat.sub(n, 0n))
    case 0n 1n+f 0n:
      L.absurd({Nat.is_le(1n+f, 0n) == True{} : Bool}, L.ft(h))
    case 0n 1n+f 1n+g:
      h
    case 1n+q 0n _:
      L.le0(Nat.sub(n, 1n+q))
    case 1n+q 1n+f 0n:
      L.absurd({Nat.is_le(Nat.sub(f, q), 0n) == True{} : Bool}, L.ft(h))
    case 1n+q 1n+f 1n+g:
      le_sub(q, f, g, h)

# the text of a chunk [s, e) with e <= n is the document's bytes [s, e)
def text_one(+t: A.Tr<U32>, +o: Nat, +n: Nat, +s: Nat, +e: Nat, h: {Nat.is_le(e, n) == True{} : Bool}) ->
  {Laws.Spec.list(U32, Laws.Spec.read(A.emb(U32, t), Nat.add(o, s), Nat.sub(e, s))) ==
    Laws.Spec.slice(A.bytes(t, n, o), s, Nat.sub(e, s)) : List<&2, U32>}:
  +p = Nat.add(o, s)
  +l = Nat.sub(e, s)
  -b = A.emb(U32, t)
  %Equal.sym(Array<U32> & List<&2, U32>, Laws.Spec.read(b, p, l), (b, A.bytes(t, l, p)),
    A.read_emb(t, p, l)) :
    {Laws.Spec.list(U32, _) == Laws.Spec.slice(A.bytes(t, n, o), s, l) : List<&2, U32>}
  %Equal.sym(List<&2, U32>, List.drop(&2, U32, A.bytes(t, n, o), s), A.bytes(t, Nat.sub(n, s), p),
    bytes_drop(t, n, o, s)) :
    {A.bytes(t, l, p) == List.take(&2, U32, _, l) : List<&2, U32>}
  Equal.sym(List<&2, U32>, List.take(&2, U32, A.bytes(t, Nat.sub(n, s), p), l), A.bytes(t, l, p),
    bytes_take(t, Nat.sub(n, s), p, l, le_sub(s, e, n, h)))

# every chunk of a list that chains on after [ps, pe) reads the document's bytes
def texts_chain(+t: A.Tr<U32>, +o: Nat, +n: Nat, cs: List<&2, C.Chunk>, +ps: Nat, +pe: Nat,
  h: {Laws.Spec.chain(cs, ps, pe, n) == True{} : Bool}
) -> Laws.Spec.texts(A.emb(U32, t), o, A.bytes(t, n, o), cs):
  match cs:
    case Nil{}:
      Unit{}
    case Con{C.Chunk{+s, +e}, rest}:
      -r = Laws.Spec.chain(rest, s, e, n)
      +h3 = L.and_r(Nat.is_lt(s, e), Bool.and(Nat.is_le(e, n), r),
        L.and_r(Nat.is_le(s, pe), Bool.and(Nat.is_lt(s, e), Bool.and(Nat.is_le(e, n), r)),
          L.and_r(Nat.is_lt(ps, s), Bool.and(Nat.is_le(s, pe), Bool.and(Nat.is_lt(s, e),
            Bool.and(Nat.is_le(e, n), r))), h)))
      (text_one(t, o, n, s, e, L.and_l(Nat.is_le(e, n), r, h3)),
        texts_chain(t, o, n, rest, s, e, L.and_r(Nat.is_le(e, n), r, h3)))

# every chunk of a list that covers the document reads the document's bytes
def texts_cover(+t: A.Tr<U32>, +o: Nat, +n: Nat, cs: List<&2, C.Chunk>,
  h: {Laws.Spec.covers(cs, n) == True{} : Bool}
) -> Laws.Spec.texts(A.emb(U32, t), o, A.bytes(t, n, o), cs):
  match cs:
    case Nil{}:
      Unit{}
    case Con{C.Chunk{+s, +e}, rest}:
      -r = Laws.Spec.chain(rest, s, e, n)
      +h3 = L.and_r(Nat.is_lt(s, e), Bool.and(Nat.is_le(e, n), r),
        L.and_r(Nat.is_eq(s, 0n), Bool.and(Nat.is_lt(s, e), Bool.and(Nat.is_le(e, n), r)), h))
      (text_one(t, o, n, s, e, L.and_l(Nat.is_le(e, n), r, h3)),
        texts_chain(t, o, n, rest, s, e, L.and_r(Nat.is_le(e, n), r, h3)))

# the law on the tree behind the array
def text_view(-a: Array<U32>, +o: Nat, +n: Nat, v: &t: A.Tr<U32> -> {A.emb(U32, t) == a : Array<U32>}) ->
  Laws.Spec.texts(a, o, Laws.Spec.list(U32, Laws.Spec.read(a, o, n)), Laws.Spec.chunks(C.chunk.doc(a, o, n))):
  (+t, eq) = v
  %eq : Laws.Spec.texts(_, o, Laws.Spec.list(U32, Laws.Spec.read(_, o, n)),
    Laws.Spec.chunks(C.chunk.doc(_, o, n)))
  -b = A.emb(U32, t)
  %Equal.sym(Array<U32> & List<&2, U32>, Laws.Spec.read(b, o, n), (b, A.bytes(t, n, o)),
    A.read_emb(t, o, n)) :
    Laws.Spec.texts(b, o, Laws.Spec.list(U32, _), Laws.Spec.chunks(C.chunk.doc(b, o, n)))
  texts_cover(t, o, n, Laws.Spec.chunks(C.chunk.doc(A.emb(U32, t), o, n)),
    Laws.chunk_cover(A.emb(U32, t), o, n))

def Laws.chunk_text(a, o, n):
  text_view(a, o, n, A.view(U32, a))
