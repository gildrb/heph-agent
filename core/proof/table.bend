import Base
import ../core.bend as C
import ../LAWS.bend as Laws
import ../score.bend as F
import ./logic.bend as L
import ./array.bend as A
import ./word.bend as W

# The score table
# ===============
# The score table is a perfect tree of depth d. Read and written at U32
# index from_nat(i), it is the tree read and written at Nat index i, when
# i < 2^d and d <= 31.

def perf(t: A.Tr<Nat>, +d: Nat) -> Bool:
  match t d:
    case A.TL{v} 0n:
      True{}
    case A.TL{v} 1n+e:
      False{}
    case A.TN{l, r} 0n:
      False{}
    case A.TN{l, r} 1n+e:
      Bool.and(perf(l, e), perf(r, e))

# slot i of a tree of depth e
def pget(t: A.Tr<Nat>, +e: Nat, +i: Nat) -> Nat:
  match t e:
    case A.TL{v} _:
      v
    case A.TN{+l, +r} 0n:
      pget(l, 0n, i)
    case A.TN{+l, +r} 1n+f:
      Bool.pick(Nat, Nat.is_lt(i, W.pow2(f)), pget(l, f, i), pget(r, f, Nat.sub(i, W.pow2(f))))

# the tree after writing v to slot i
def pset(t: A.Tr<Nat>, +e: Nat, +i: Nat, +v: Nat) -> A.Tr<Nat>:
  match t e:
    case A.TL{x} _:
      A.TL{v}
    case A.TN{+l, +r} 0n:
      A.TN{pset(l, 0n, i, v), r}
    case A.TN{+l, +r} 1n+f:
      Bool.pick(A.Tr<Nat>, Nat.is_lt(i, W.pow2(f)), A.TN{pset(l, f, i, v), r},
        A.TN{l, pset(r, f, Nat.sub(i, W.pow2(f)), v)})

# Nat facts
# ---------

def dbl_add(+b: Nat) -> {Nat.double(b) == Nat.add(b, b) : Nat}:
  match b:
    case 0n:
      {==}
    case 1n+q:
      %Equal.sym(Nat, Nat.add(q, 1n+q), 1n+Nat.add(q, q), L.add_s(q, q)) :
        {2n+Nat.double(q) == 1n+_ : Nat}
      %Equal.sym(Nat, Nat.double(q), Nat.add(q, q), dbl_add(q)) : {2n+_ == 2n+Nat.add(q, q) : Nat}
      {==}

def lt_conv(-a: Nat, -x: Nat, -y: Nat, e: {x == y : Nat}, h: {Nat.is_lt(a, x) == True{} : Bool}) ->
  {Nat.is_lt(a, y) == True{} : Bool}:
  %e : {Nat.is_lt(a, _) == True{} : Bool}
  h

def lt_eqv(-a: Nat, -b: Nat, -c: Nat, e: {a == b : Nat}, h: {Nat.is_lt(b, c) == True{} : Bool}) ->
  {Nat.is_lt(a, c) == True{} : Bool}:
  %Equal.sym(Nat, a, b, e) : {Nat.is_lt(_, c) == True{} : Bool}
  h

# a < b + c and b <= a give a - b < c
def sub_lt(a: Nat, b: Nat, +c: Nat, h1: {Nat.is_lt(a, Nat.add(b, c)) == True{} : Bool},
  h2: {Nat.is_le(b, a) == True{} : Bool}
) -> {Nat.is_lt(Nat.sub(a, b), c) == True{} : Bool}:
  match a b:
    case _ 0n:
      %Equal.sym(Nat, Nat.sub(a, 0n), a, W.sub0(a)) : {Nat.is_lt(_, c) == True{} : Bool}
      h1
    case 0n 1n+q:
      L.absurd({Nat.is_lt(0n, c) == True{} : Bool}, L.ft(h2))
    case 1n+p 1n+q:
      sub_lt(p, q, c, h1, h2)

# a < 2b and b <= a give a - b < b
def sub_half(+a: Nat, +b: Nat, h1: {Nat.is_lt(a, Nat.double(b)) == True{} : Bool},
  h2: {Nat.is_le(b, a) == True{} : Bool}
) -> {Nat.is_lt(Nat.sub(a, b), b) == True{} : Bool}:
  sub_lt(a, b, b, lt_conv(a, Nat.double(b), Nat.add(b, b), dbl_add(b), h1), h2)

# Navigation
# ----------

def perf_l(+l: A.Tr<Nat>, +r: A.Tr<Nat>, +e: Nat, h: {perf(A.TN{l, r}, 1n+e) == True{} : Bool}) ->
  {perf(l, e) == True{} : Bool}:
  L.and_l(perf(l, e), perf(r, e), h)

def perf_r(+l: A.Tr<Nat>, +r: A.Tr<Nat>, +e: Nat, h: {perf(A.TN{l, r}, 1n+e) == True{} : Bool}) ->
  {perf(r, e) == True{} : Bool}:
  L.and_r(perf(l, e), perf(r, e), h)

# a perfect tree of depth d <= 31 has 2^d slots
def tsize(+t: A.Tr<Nat>, +d: Nat, +hp: {perf(t, d) == True{} : Bool}, +hd: {Nat.is_lt(d, 32n) == True{} : Bool}) ->
  {A.tsize(Nat, t) == W.PU(d) : U32}:
  match t d:
    case A.TL{+v} 0n:
      {==}
    case A.TL{+v} 1n+e:
      L.absurd({A.tsize(Nat, A.TL{v}) == W.PU(1n+e) : U32}, L.ft(hp))
    case A.TN{+l, +r} 0n:
      L.absurd({A.tsize(Nat, A.TN{l, r}) == W.PU(0n) : U32}, L.ft(hp))
    case A.TN{+l, +r} 1n+e:
      %Equal.sym(U32, A.tsize(Nat, l), W.PU(e), tsize(l, e, perf_l(l, r, e, hp), L.lt_pred(e, 32n, hd))) :
        {U32.shl(_) == W.PU(1n+e) : U32}
      W.ushl(e, hd)

def IhG(+t: A.Tr<Nat>, +f: Nat) -> Type:
  @+J: U32 -> @h: {Nat.is_lt(U32.to_nat(J), W.pow2(f)) == True{} : Bool} ->
    {A.tget.go(Nat, t, W.PU(f), J) == pget(t, f, U32.to_nat(J)) : Nat}

def gon.pick(+l: A.Tr<Nat>, +r: A.Tr<Nat>, +f: Nat, +I: U32, z: Bool,
  ez: {Nat.is_lt(U32.to_nat(I), W.pow2(f)) == z : Bool},
  hI: {Nat.is_lt(U32.to_nat(I), Nat.double(W.pow2(f))) == True{} : Bool},
  +hf: {Nat.is_lt(f, 32n) == True{} : Bool}, ihl: IhG(l, f), ihr: IhG(r, f)
) -> {Bool.pick(Nat, z, A.tget.go(Nat, l, W.PU(f), I), A.tget.go(Nat, r, W.PU(f), U32.sub(I, W.PU(f)))) ==
  Bool.pick(Nat, z, pget(l, f, U32.to_nat(I)), pget(r, f, Nat.sub(U32.to_nat(I), W.pow2(f)))) : Nat}:
  match z:
    case True{}:
      ihl(I, ez)
    case False{}:
      +i = U32.to_nat(I)
      +le = L.nlt_le(i, W.pow2(f), ez)
      +us = W.usub(I, f, hf, le)
      +J = U32.sub(I, W.PU(f))
      %us : {A.tget.go(Nat, r, W.PU(f), J) == pget(r, f, _) : Nat}
      ihr(J, lt_eqv(U32.to_nat(J), Nat.sub(i, W.pow2(f)), W.pow2(f), us, sub_half(i, W.pow2(f), hI, le)))

# reading a perfect tree at U32 index I is reading it at Nat index N(I)
def gon(+t: A.Tr<Nat>, +e: Nat, +I: U32, +hp: {perf(t, e) == True{} : Bool},
  +hd: {Nat.is_lt(e, 32n) == True{} : Bool}, hi: {Nat.is_lt(U32.to_nat(I), W.pow2(e)) == True{} : Bool}
) -> {A.tget.go(Nat, t, W.PU(e), I) == pget(t, e, U32.to_nat(I)) : Nat}:
  match t e:
    case A.TL{+v} _:
      {==}
    case A.TN{+l, +r} 0n:
      L.absurd({A.tget.go(Nat, A.TN{l, r}, W.PU(0n), I) == pget(A.TN{l, r}, 0n, U32.to_nat(I)) : Nat}, L.ft(hp))
    case A.TN{+l, +r} 1n+f:
      +i = U32.to_nat(I)
      +hf = L.lt_pred(f, 32n, hd)
      %Equal.sym(U32, U32.shr(W.PU(1n+f)), W.PU(f), W.ushr(f, hd)) :
        {Bool.pick(Nat, U32.is_lt(I, _), A.tget.go(Nat, l, _, I), A.tget.go(Nat, r, _, U32.sub(I, _))) ==
          Bool.pick(Nat, Nat.is_lt(i, W.pow2(f)), pget(l, f, i), pget(r, f, Nat.sub(i, W.pow2(f)))) : Nat}
      %Equal.sym(Bool, U32.is_lt(I, W.PU(f)), Nat.is_lt(i, U32.to_nat(W.PU(f))), W.ult(I, W.PU(f))) :
        {Bool.pick(Nat, _, A.tget.go(Nat, l, W.PU(f), I), A.tget.go(Nat, r, W.PU(f), U32.sub(I, W.PU(f)))) ==
          Bool.pick(Nat, Nat.is_lt(i, W.pow2(f)), pget(l, f, i), pget(r, f, Nat.sub(i, W.pow2(f)))) : Nat}
      %Equal.sym(Nat, U32.to_nat(W.PU(f)), W.pow2(f), W.uP(f, hf)) :
        {Bool.pick(Nat, Nat.is_lt(i, _), A.tget.go(Nat, l, W.PU(f), I),
          A.tget.go(Nat, r, W.PU(f), U32.sub(I, W.PU(f)))) ==
          Bool.pick(Nat, Nat.is_lt(i, W.pow2(f)), pget(l, f, i), pget(r, f, Nat.sub(i, W.pow2(f)))) : Nat}
      gon.pick(l, r, f, I, Nat.is_lt(i, W.pow2(f)), {==}, hi, hf,
        J => h => gon(l, f, J, perf_l(l, r, f, hp), hf, h),
        J => h => gon(r, f, J, perf_r(l, r, f, hp), hf, h))

def IhS(+t: A.Tr<Nat>, +f: Nat, +v: Nat) -> Type:
  @+J: U32 -> @h: {Nat.is_lt(U32.to_nat(J), W.pow2(f)) == True{} : Bool} ->
    {A.tset.go(Nat, t, W.PU(f), J, v) == pset(t, f, U32.to_nat(J), v) : A.Tr<Nat>}

def gset.pick(+l: A.Tr<Nat>, +r: A.Tr<Nat>, +f: Nat, +I: U32, +v: Nat, z: Bool,
  ez: {Nat.is_lt(U32.to_nat(I), W.pow2(f)) == z : Bool},
  hI: {Nat.is_lt(U32.to_nat(I), Nat.double(W.pow2(f))) == True{} : Bool},
  +hf: {Nat.is_lt(f, 32n) == True{} : Bool}, ihl: IhS(l, f, v), ihr: IhS(r, f, v)
) -> {Bool.pick(A.Tr<Nat>, z, A.TN{A.tset.go(Nat, l, W.PU(f), I, v), r},
    A.TN{l, A.tset.go(Nat, r, W.PU(f), U32.sub(I, W.PU(f)), v)}) ==
  Bool.pick(A.Tr<Nat>, z, A.TN{pset(l, f, U32.to_nat(I), v), r},
    A.TN{l, pset(r, f, Nat.sub(U32.to_nat(I), W.pow2(f)), v)}) : A.Tr<Nat>}:
  match z:
    case True{}:
      %Equal.sym(A.Tr<Nat>, A.tset.go(Nat, l, W.PU(f), I, v), pset(l, f, U32.to_nat(I), v), ihl(I, ez)) :
        {A.TN{_, r} == A.TN{pset(l, f, U32.to_nat(I), v), r} : A.Tr<Nat>}
      {==}
    case False{}:
      +i = U32.to_nat(I)
      +le = L.nlt_le(i, W.pow2(f), ez)
      +us = W.usub(I, f, hf, le)
      +J = U32.sub(I, W.PU(f))
      %us : {A.TN{l, A.tset.go(Nat, r, W.PU(f), J, v)} == A.TN{l, pset(r, f, _, v)} : A.Tr<Nat>}
      %Equal.sym(A.Tr<Nat>, A.tset.go(Nat, r, W.PU(f), J, v), pset(r, f, U32.to_nat(J), v),
        ihr(J, lt_eqv(U32.to_nat(J), Nat.sub(i, W.pow2(f)), W.pow2(f), us, sub_half(i, W.pow2(f), hI, le)))) :
        {A.TN{l, _} == A.TN{l, pset(r, f, U32.to_nat(J), v)} : A.Tr<Nat>}
      {==}

# writing a perfect tree at U32 index I is writing it at Nat index N(I)
def gset(+t: A.Tr<Nat>, +e: Nat, +I: U32, +v: Nat, +hp: {perf(t, e) == True{} : Bool},
  +hd: {Nat.is_lt(e, 32n) == True{} : Bool}, hi: {Nat.is_lt(U32.to_nat(I), W.pow2(e)) == True{} : Bool}
) -> {A.tset.go(Nat, t, W.PU(e), I, v) == pset(t, e, U32.to_nat(I), v) : A.Tr<Nat>}:
  match t e:
    case A.TL{+x} _:
      {==}
    case A.TN{+l, +r} 0n:
      L.absurd({A.tset.go(Nat, A.TN{l, r}, W.PU(0n), I, v) == pset(A.TN{l, r}, 0n, U32.to_nat(I), v) : A.Tr<Nat>},
        L.ft(hp))
    case A.TN{+l, +r} 1n+f:
      +i = U32.to_nat(I)
      +hf = L.lt_pred(f, 32n, hd)
      %Equal.sym(U32, U32.shr(W.PU(1n+f)), W.PU(f), W.ushr(f, hd)) :
        {Bool.pick(A.Tr<Nat>, U32.is_lt(I, _), A.TN{A.tset.go(Nat, l, _, I, v), r},
          A.TN{l, A.tset.go(Nat, r, _, U32.sub(I, _), v)}) ==
          Bool.pick(A.Tr<Nat>, Nat.is_lt(i, W.pow2(f)), A.TN{pset(l, f, i, v), r},
            A.TN{l, pset(r, f, Nat.sub(i, W.pow2(f)), v)}) : A.Tr<Nat>}
      %Equal.sym(Bool, U32.is_lt(I, W.PU(f)), Nat.is_lt(i, U32.to_nat(W.PU(f))), W.ult(I, W.PU(f))) :
        {Bool.pick(A.Tr<Nat>, _, A.TN{A.tset.go(Nat, l, W.PU(f), I, v), r},
          A.TN{l, A.tset.go(Nat, r, W.PU(f), U32.sub(I, W.PU(f)), v)}) ==
          Bool.pick(A.Tr<Nat>, Nat.is_lt(i, W.pow2(f)), A.TN{pset(l, f, i, v), r},
            A.TN{l, pset(r, f, Nat.sub(i, W.pow2(f)), v)}) : A.Tr<Nat>}
      %Equal.sym(Nat, U32.to_nat(W.PU(f)), W.pow2(f), W.uP(f, hf)) :
        {Bool.pick(A.Tr<Nat>, Nat.is_lt(i, _), A.TN{A.tset.go(Nat, l, W.PU(f), I, v), r},
          A.TN{l, A.tset.go(Nat, r, W.PU(f), U32.sub(I, W.PU(f)), v)}) ==
          Bool.pick(A.Tr<Nat>, Nat.is_lt(i, W.pow2(f)), A.TN{pset(l, f, i, v), r},
            A.TN{l, pset(r, f, Nat.sub(i, W.pow2(f)), v)}) : A.Tr<Nat>}
      gset.pick(l, r, f, I, v, Nat.is_lt(i, W.pow2(f)), {==}, hi, hf,
        J => h => gset(l, f, J, v, perf_l(l, r, f, hp), hf, h),
        J => h => gset(r, f, J, v, perf_r(l, r, f, hp), hf, h))

# the index from_nat(i), masked to d bits, is i
def uidx(+i: Nat, +d: Nat, +hd: {Nat.is_lt(d, 32n) == True{} : Bool}, +hi: {Nat.is_lt(i, W.pow2(d)) == True{} : Bool}) ->
  {U32.to_nat(U32.and(U32.from_nat(i), U32{W.ones(32n, d)})) == i : Nat}:
  +e1 = W.nfrom(i, d, L.lt_le(d, 32n, hd), hi)
  Equal.trans(Nat, U32.to_nat(U32.and(U32.from_nat(i), U32{W.ones(32n, d)})), U32.to_nat(U32.from_nat(i)), i,
    W.uand(U32.from_nat(i), d, lt_eqv(U32.to_nat(U32.from_nat(i)), i, W.pow2(d), e1, hi)), e1)

# Array.get's slot from_nat(i) of a perfect tree is pget's slot i
def tget_at(+t: A.Tr<Nat>, +d: Nat, +i: Nat, +hp: {perf(t, d) == True{} : Bool},
  +hd: {Nat.is_lt(d, 32n) == True{} : Bool}, +hi: {Nat.is_lt(i, W.pow2(d)) == True{} : Bool}
) -> {A.tget(Nat, t, U32.from_nat(i)) == pget(t, d, i) : Nat}:
  %Equal.sym(U32, A.tsize(Nat, t), W.PU(d), tsize(t, d, hp, hd)) :
    {A.tget.go(Nat, t, _, U32.and(U32.from_nat(i), U32.sub(_, 1))) == pget(t, d, i) : Nat}
  %Equal.sym(U32, U32.sub(W.PU(d), 1), U32{W.ones(32n, d)}, W.umsk(d, hd)) :
    {A.tget.go(Nat, t, W.PU(d), U32.and(U32.from_nat(i), _)) == pget(t, d, i) : Nat}
  +I = U32.and(U32.from_nat(i), U32{W.ones(32n, d)})
  +ei = uidx(i, d, hd, hi)
  Equal.trans(Nat, A.tget.go(Nat, t, W.PU(d), I), pget(t, d, U32.to_nat(I)), pget(t, d, i),
    gon(t, d, I, hp, hd, lt_eqv(U32.to_nat(I), i, W.pow2(d), ei, hi)),
    Equal.cong(Nat, Nat, x => pget(t, d, x), U32.to_nat(I), i, ei))

# Array.set's slot from_nat(i) of a perfect tree is pset's slot i
def tset_at(+t: A.Tr<Nat>, +d: Nat, +i: Nat, +v: Nat, +hp: {perf(t, d) == True{} : Bool},
  +hd: {Nat.is_lt(d, 32n) == True{} : Bool}, +hi: {Nat.is_lt(i, W.pow2(d)) == True{} : Bool}
) -> {A.tset(Nat, t, U32.from_nat(i), v) == pset(t, d, i, v) : A.Tr<Nat>}:
  %Equal.sym(U32, A.tsize(Nat, t), W.PU(d), tsize(t, d, hp, hd)) :
    {A.tset.go(Nat, t, _, U32.and(U32.from_nat(i), U32.sub(_, 1)), v) == pset(t, d, i, v) : A.Tr<Nat>}
  %Equal.sym(U32, U32.sub(W.PU(d), 1), U32{W.ones(32n, d)}, W.umsk(d, hd)) :
    {A.tset.go(Nat, t, W.PU(d), U32.and(U32.from_nat(i), _), v) == pset(t, d, i, v) : A.Tr<Nat>}
  +I = U32.and(U32.from_nat(i), U32{W.ones(32n, d)})
  +ei = uidx(i, d, hd, hi)
  Equal.trans(A.Tr<Nat>, A.tset.go(Nat, t, W.PU(d), I, v), pset(t, d, U32.to_nat(I), v), pset(t, d, i, v),
    gset(t, d, I, v, hp, hd, lt_eqv(U32.to_nat(I), i, W.pow2(d), ei, hi)),
    Equal.cong(Nat, A.Tr<Nat>, x => pset(t, d, x, v), U32.to_nat(I), i, ei))

# Trees, by Nat index
# -------------------

def pick_eq(-A: Data, z: Bool, -x: A, -y: A, -w: A, ex: {x == w : A}, ey: {y == w : A}) ->
  {Bool.pick(A, z, x, y) == w : A}:
  match z:
    case True{}:
      ex
    case False{}:
      ey

def perf_set.pick(+l: A.Tr<Nat>, +r: A.Tr<Nat>, +f: Nat, +i: Nat, +v: Nat, z: Bool,
  hl: {perf(l, f) == True{} : Bool}, hr: {perf(r, f) == True{} : Bool},
  il: {perf(pset(l, f, i, v), f) == True{} : Bool},
  ir: {perf(pset(r, f, Nat.sub(i, W.pow2(f)), v), f) == True{} : Bool}
) -> {perf(Bool.pick(A.Tr<Nat>, z, A.TN{pset(l, f, i, v), r}, A.TN{l, pset(r, f, Nat.sub(i, W.pow2(f)), v)}),
  1n+f) == True{} : Bool}:
  match z:
    case True{}:
      L.and_i(perf(pset(l, f, i, v), f), perf(r, f), il, hr)
    case False{}:
      L.and_i(perf(l, f), perf(pset(r, f, Nat.sub(i, W.pow2(f)), v), f), hl, ir)

# writing keeps a tree perfect
def perf_set(+t: A.Tr<Nat>, +e: Nat, +i: Nat, +v: Nat, +hp: {perf(t, e) == True{} : Bool}) ->
  {perf(pset(t, e, i, v), e) == True{} : Bool}:
  match t e:
    case A.TL{+x} 0n:
      {==}
    case A.TL{+x} 1n+f:
      L.absurd({perf(pset(A.TL{x}, 1n+f, i, v), 1n+f) == True{} : Bool}, L.ft(hp))
    case A.TN{+l, +r} 0n:
      L.absurd({perf(pset(A.TN{l, r}, 0n, i, v), 0n) == True{} : Bool}, L.ft(hp))
    case A.TN{+l, +r} 1n+f:
      +hl = perf_l(l, r, f, hp)
      +hr = perf_r(l, r, f, hp)
      perf_set.pick(l, r, f, i, v, Nat.is_lt(i, W.pow2(f)), hl, hr, perf_set(l, f, i, v, hl),
        perf_set(r, f, Nat.sub(i, W.pow2(f)), v, hr))

def same.pick(+l: A.Tr<Nat>, +r: A.Tr<Nat>, +f: Nat, +i: Nat, +v: Nat, z: Bool,
  ez: {Nat.is_lt(i, W.pow2(f)) == z : Bool},
  il: {pget(pset(l, f, i, v), f, i) == v : Nat},
  ir: {pget(pset(r, f, Nat.sub(i, W.pow2(f)), v), f, Nat.sub(i, W.pow2(f))) == v : Nat}
) -> {pget(Bool.pick(A.Tr<Nat>, z, A.TN{pset(l, f, i, v), r}, A.TN{l, pset(r, f, Nat.sub(i, W.pow2(f)), v)}),
  1n+f, i) == v : Nat}:
  match z:
    case True{}:
      %Equal.sym(Bool, Nat.is_lt(i, W.pow2(f)), True{}, ez) :
        {Bool.pick(Nat, _, pget(pset(l, f, i, v), f, i), pget(r, f, Nat.sub(i, W.pow2(f)))) == v : Nat}
      il
    case False{}:
      %Equal.sym(Bool, Nat.is_lt(i, W.pow2(f)), False{}, ez) :
        {Bool.pick(Nat, _, pget(l, f, i), pget(pset(r, f, Nat.sub(i, W.pow2(f)), v), f, Nat.sub(i, W.pow2(f)))) ==
          v : Nat}
      ir

# a written slot reads back
def get_same(+t: A.Tr<Nat>, +e: Nat, +i: Nat, +v: Nat) -> {pget(pset(t, e, i, v), e, i) == v : Nat}:
  match t e:
    case A.TL{+x} _:
      {==}
    case A.TN{+l, +r} 0n:
      get_same(l, 0n, i, v)
    case A.TN{+l, +r} 1n+f:
      same.pick(l, r, f, i, v, Nat.is_lt(i, W.pow2(f)), {==}, get_same(l, f, i, v),
        get_same(r, f, Nat.sub(i, W.pow2(f)), v))

# i != j, both at least h, give i - h != j - h
def sub_neq(i: Nat, j: Nat, h: Nat, +e: {Nat.is_eq(i, j) == False{} : Bool},
  hi: {Nat.is_le(h, i) == True{} : Bool}, hj: {Nat.is_le(h, j) == True{} : Bool}
) -> {Nat.is_eq(Nat.sub(i, h), Nat.sub(j, h)) == False{} : Bool}:
  match i j h:
    case _ _ 0n:
      %Equal.sym(Nat, Nat.sub(i, 0n), i, W.sub0(i)) : {Nat.is_eq(_, Nat.sub(j, 0n)) == False{} : Bool}
      %Equal.sym(Nat, Nat.sub(j, 0n), j, W.sub0(j)) : {Nat.is_eq(i, _) == False{} : Bool}
      e
    case 0n _ 1n+g:
      L.absurd({Nat.is_eq(Nat.sub(0n, 1n+g), Nat.sub(j, 1n+g)) == False{} : Bool}, L.ft(hi))
    case 1n+a 0n 1n+g:
      L.absurd({Nat.is_eq(Nat.sub(1n+a, 1n+g), Nat.sub(0n, 1n+g)) == False{} : Bool}, L.ft(hj))
    case 1n+a 1n+b 1n+g:
      sub_neq(a, b, g, e, hi, hj)

def dbl_pos(u: Nat, h: {Nat.is_le(1n, u) == True{} : Bool}) -> {Nat.is_le(1n, Nat.double(u)) == True{} : Bool}:
  match u:
    case 0n:
      L.absurd({Nat.is_le(1n, 0n) == True{} : Bool}, L.ft(h))
    case 1n+q:
      {==}

def pow_pos(+e: Nat) -> {Nat.is_le(1n, W.pow2(e)) == True{} : Bool}:
  match e:
    case 0n:
      {==}
    case 1n+f:
      dbl_pos(W.pow2(f), pow_pos(f))

def lt1_eq(i: Nat, j: Nat, hi: {Nat.is_lt(i, 1n) == True{} : Bool}, hj: {Nat.is_lt(j, 1n) == True{} : Bool}) ->
  {Nat.is_eq(i, j) == True{} : Bool}:
  match i j:
    case 0n 0n:
      {==}
    case 0n 1n+b:
      L.absurd({Nat.is_eq(0n, 1n+b) == True{} : Bool}, L.not_lt0(b, hj))
    case 1n+a _:
      L.absurd({Nat.is_eq(1n+a, j) == True{} : Bool}, L.not_lt0(a, hi))

def other.pick(+l: A.Tr<Nat>, +r: A.Tr<Nat>, +f: Nat, +i: Nat, +j: Nat, +v: Nat, zi: Bool, zj: Bool,
  +ezi: {Nat.is_lt(i, W.pow2(f)) == zi : Bool}, +ezj: {Nat.is_lt(j, W.pow2(f)) == zj : Bool},
  +ne: {Nat.is_eq(i, j) == False{} : Bool},
  hi: {Nat.is_lt(i, Nat.double(W.pow2(f))) == True{} : Bool},
  hj: {Nat.is_lt(j, Nat.double(W.pow2(f))) == True{} : Bool},
  ihl: @a: {Nat.is_lt(i, W.pow2(f)) == True{} : Bool} -> @b: {Nat.is_lt(j, W.pow2(f)) == True{} : Bool} ->
    {pget(pset(l, f, i, v), f, j) == pget(l, f, j) : Nat},
  ihr: @n2: {Nat.is_eq(Nat.sub(i, W.pow2(f)), Nat.sub(j, W.pow2(f))) == False{} : Bool} ->
    @a: {Nat.is_lt(Nat.sub(i, W.pow2(f)), W.pow2(f)) == True{} : Bool} ->
    @b: {Nat.is_lt(Nat.sub(j, W.pow2(f)), W.pow2(f)) == True{} : Bool} ->
    {pget(pset(r, f, Nat.sub(i, W.pow2(f)), v), f, Nat.sub(j, W.pow2(f))) == pget(r, f, Nat.sub(j, W.pow2(f))) : Nat}
) -> {pget(Bool.pick(A.Tr<Nat>, zi, A.TN{pset(l, f, i, v), r}, A.TN{l, pset(r, f, Nat.sub(i, W.pow2(f)), v)}),
  1n+f, j) == Bool.pick(Nat, zj, pget(l, f, j), pget(r, f, Nat.sub(j, W.pow2(f)))) : Nat}:
  match zi zj:
    case True{} True{}:
      %Equal.sym(Bool, Nat.is_lt(j, W.pow2(f)), True{}, ezj) :
        {Bool.pick(Nat, _, pget(pset(l, f, i, v), f, j), pget(r, f, Nat.sub(j, W.pow2(f)))) == pget(l, f, j) : Nat}
      ihl(ezi, ezj)
    case True{} False{}:
      %Equal.sym(Bool, Nat.is_lt(j, W.pow2(f)), False{}, ezj) :
        {Bool.pick(Nat, _, pget(pset(l, f, i, v), f, j), pget(r, f, Nat.sub(j, W.pow2(f)))) ==
          pget(r, f, Nat.sub(j, W.pow2(f))) : Nat}
      {==}
    case False{} True{}:
      %Equal.sym(Bool, Nat.is_lt(j, W.pow2(f)), True{}, ezj) :
        {Bool.pick(Nat, _, pget(l, f, j), pget(pset(r, f, Nat.sub(i, W.pow2(f)), v), f, Nat.sub(j, W.pow2(f)))) ==
          pget(l, f, j) : Nat}
      {==}
    case False{} False{}:
      %Equal.sym(Bool, Nat.is_lt(j, W.pow2(f)), False{}, ezj) :
        {Bool.pick(Nat, _, pget(l, f, j), pget(pset(r, f, Nat.sub(i, W.pow2(f)), v), f, Nat.sub(j, W.pow2(f)))) ==
          pget(r, f, Nat.sub(j, W.pow2(f))) : Nat}
      +li = L.nlt_le(i, W.pow2(f), ezi)
      +lj = L.nlt_le(j, W.pow2(f), ezj)
      ihr(sub_neq(i, j, W.pow2(f), ne, li, lj), sub_half(i, W.pow2(f), hi, li), sub_half(j, W.pow2(f), hj, lj))

# writing slot i leaves every other slot j < 2^e alone
def get_other(+t: A.Tr<Nat>, +e: Nat, +i: Nat, +j: Nat, +v: Nat, +hp: {perf(t, e) == True{} : Bool},
  +ne: {Nat.is_eq(i, j) == False{} : Bool}, +hi: {Nat.is_lt(i, W.pow2(e)) == True{} : Bool},
  +hj: {Nat.is_lt(j, W.pow2(e)) == True{} : Bool}
) -> {pget(pset(t, e, i, v), e, j) == pget(t, e, j) : Nat}:
  match t e:
    case A.TL{+x} 0n:
      L.absurd({v == x : Nat}, L.clash(Nat.is_eq(i, j), lt1_eq(i, j, hi, hj), ne))
    case A.TL{+x} 1n+f:
      L.absurd({v == x : Nat}, L.ft(hp))
    case A.TN{+l, +r} 0n:
      L.absurd({pget(pset(A.TN{l, r}, 0n, i, v), 0n, j) == pget(A.TN{l, r}, 0n, j) : Nat}, L.ft(hp))
    case A.TN{+l, +r} 1n+f:
      +h = W.pow2(f)
      other.pick(l, r, f, i, j, v, Nat.is_lt(i, h), Nat.is_lt(j, h), {==}, {==}, ne, hi, hj,
        a => b => get_other(l, f, i, j, v, perf_l(l, r, f, hp), ne, a, b),
        n2 => a => b => get_other(r, f, Nat.sub(i, h), Nat.sub(j, h), v, perf_r(l, r, f, hp), n2, a, b))

# a new tree is perfect and holds v everywhere
def perf_new(+d: Nat, +v: Nat) -> {perf(A.tnew(Nat, d, v), d) == True{} : Bool}:
  match d:
    case 0n:
      {==}
    case 1n+e:
      L.and_i(perf(A.tnew(Nat, e, v), e), perf(A.tnew(Nat, e, v), e), perf_new(e, v), perf_new(e, v))

def get_new(+d: Nat, +v: Nat, +i: Nat) -> {pget(A.tnew(Nat, d, v), d, i) == v : Nat}:
  match d:
    case 0n:
      {==}
    case 1n+e:
      pick_eq(Nat, Nat.is_lt(i, W.pow2(e)), pget(A.tnew(Nat, e, v), e, i),
        pget(A.tnew(Nat, e, v), e, Nat.sub(i, W.pow2(e))), v, get_new(e, v, i),
        get_new(e, v, Nat.sub(i, W.pow2(e))))

# The table as a list
# -------------------

# the m slots from i
def ML(+t: A.Tr<Nat>, +e: Nat, m: Nat, +i: Nat) -> List<&2, Nat>:
  match m:
    case 0n:
      Nil{}
    case 1n+k:
      pget(t, e, i) <> ML(t, e, k, 1n+i)

def ml_len(+t: A.Tr<Nat>, +e: Nat, m: Nat, +i: Nat) -> {List.length(&2, Nat, ML(t, e, m, i)) == m : Nat}:
  match m:
    case 0n:
      {==}
    case 1n+k:
      %Equal.sym(Nat, List.length(&2, Nat, ML(t, e, k, 1n+i)), k, ml_len(t, e, k, 1n+i)) : {1n+_ == 1n+k : Nat}
      {==}

# the new table is all zeros
def ml_new(+d: Nat, m: Nat, +i: Nat) -> {ML(A.tnew(Nat, d, 0n), d, m, i) == Laws.Spec.zeros(m) : List<&2, Nat>}:
  match m:
    case 0n:
      {==}
    case 1n+k:
      %Equal.sym(Nat, pget(A.tnew(Nat, d, 0n), d, i), 0n, get_new(d, 0n, i)) :
        {_ <> ML(A.tnew(Nat, d, 0n), d, k, 1n+i) == 0n <> Laws.Spec.zeros(k) : List<&2, Nat>}
      %Equal.sym(List<&2, Nat>, ML(A.tnew(Nat, d, 0n), d, k, 1n+i), Laws.Spec.zeros(k), ml_new(d, k, 1n+i)) :
        {0n <> _ == 0n <> Laws.Spec.zeros(k) : List<&2, Nat>}
      {==}

# a slot past the end adds nothing
def add_out(xs: List<&2, Nat>, j: Nat, +x: Nat, h: {Nat.is_le(List.length(&2, Nat, xs), j) == True{} : Bool}) ->
  {Laws.Spec.add(xs, j, x) == xs : List<&2, Nat>}:
  match xs j:
    case Nil{} _:
      {==}
    case Con{+v, +t} 0n:
      L.absurd({Laws.Spec.add(v <> t, 0n, x) == v <> t : List<&2, Nat>}, L.ft(h))
    case Con{+v, +t} 1n+p:
      %Equal.sym(List<&2, Nat>, Laws.Spec.add(t, p, x), t, add_out(t, p, x, h)) :
        {v <> _ == v <> t : List<&2, Nat>}
      {==}

# i + 1 + k <= P gives i < P
def hd_lt(i: Nat, +k: Nat, P: Nat, h: {Nat.is_le(Nat.add(i, 1n+k), P) == True{} : Bool}) ->
  {Nat.is_lt(i, P) == True{} : Bool}:
  match i P:
    case 0n 0n:
      L.absurd({Nat.is_lt(0n, 0n) == True{} : Bool}, L.ft(h))
    case 0n 1n+q:
      {==}
    case 1n+a 0n:
      L.absurd({Nat.is_lt(1n+a, 0n) == True{} : Bool}, L.ft(h))
    case 1n+a 1n+q:
      hd_lt(a, k, q, h)

def tl_le(+i: Nat, +k: Nat, -P: Nat, h: {Nat.is_le(Nat.add(i, 1n+k), P) == True{} : Bool}) ->
  {Nat.is_le(Nat.add(1n+i, k), P) == True{} : Bool}:
  %L.add_s(i, k) : {Nat.is_le(_, P) == True{} : Bool}
  h

# writing slot id < i leaves the slots from i alone
def ml_other(+t: A.Tr<Nat>, +e: Nat, +m: Nat, +i: Nat, +id: Nat, +w: Nat, +hp: {perf(t, e) == True{} : Bool},
  +hm: {Nat.is_le(Nat.add(i, m), W.pow2(e)) == True{} : Bool}, +hid: {Nat.is_lt(id, W.pow2(e)) == True{} : Bool},
  +hlt: {Nat.is_lt(id, i) == True{} : Bool}
) -> {ML(pset(t, e, id, w), e, m, i) == ML(t, e, m, i) : List<&2, Nat>}:
  match m:
    case 0n:
      {==}
    case 1n+k:
      %Equal.sym(Nat, pget(pset(t, e, id, w), e, i), pget(t, e, i),
        get_other(t, e, id, i, w, hp, L.lt_neq(id, i, hlt), hid, hd_lt(i, k, W.pow2(e), hm))) :
        {_ <> ML(pset(t, e, id, w), e, k, 1n+i) == pget(t, e, i) <> ML(t, e, k, 1n+i) : List<&2, Nat>}
      %Equal.sym(List<&2, Nat>, ML(pset(t, e, id, w), e, k, 1n+i), ML(t, e, k, 1n+i),
        ml_other(t, e, k, 1n+i, id, w, hp, tl_le(i, k, W.pow2(e), hm), hid, L.le_lt(id, i, 1n+i, L.lt_le(id, i, hlt), L.lt_succ_self(i)))) :
        {pget(t, e, i) <> _ == pget(t, e, i) <> ML(t, e, k, 1n+i) : List<&2, Nat>}
      {==}

def sub_self(i: Nat) -> {Nat.sub(i, i) == 0n : Nat}:
  match i:
    case 0n:
      {==}
    case 1n+p:
      sub_self(p)

def le_ne_lt(a: Nat, b: Nat, h: {Nat.is_le(a, b) == True{} : Bool}, e: {Nat.is_eq(a, b) == False{} : Bool}) ->
  {Nat.is_lt(a, b) == True{} : Bool}:
  match a b:
    case 0n 0n:
      L.absurd({Nat.is_lt(0n, 0n) == True{} : Bool}, L.tf(e))
    case 0n 1n+q:
      {==}
    case 1n+p 0n:
      L.absurd({Nat.is_lt(1n+p, 0n) == True{} : Bool}, L.ft(h))
    case 1n+p 1n+q:
      le_ne_lt(p, q, h, e)

# b - a = 1 + (b - (a + 1)) for a < b
def sub_succ(b: Nat, a: Nat, h: {Nat.is_lt(a, b) == True{} : Bool}) -> {Nat.sub(b, a) == 1n+Nat.sub(b, 1n+a) : Nat}:
  match b a:
    case 0n _:
      L.absurd({Nat.sub(0n, a) == 1n+Nat.sub(0n, 1n+a) : Nat}, L.not_lt0(a, h))
    case 1n+c 0n:
      %Equal.sym(Nat, Nat.sub(c, 0n), c, W.sub0(c)) : {1n+c == 1n+_ : Nat}
      {==}
    case 1n+c 1n+d:
      sub_succ(c, d, h)

def BumpIH(+t: A.Tr<Nat>, +e: Nat, +k: Nat, +i: Nat, +id: Nat, +x: Nat) -> Type:
  @h: {Nat.is_le(1n+i, id) == True{} : Bool} ->
    {ML(pset(t, e, id, Nat.add(pget(t, e, id), x)), e, k, 1n+i) ==
      Laws.Spec.add(ML(t, e, k, 1n+i), Nat.sub(id, 1n+i), x) : List<&2, Nat>}

def bump.at(+t: A.Tr<Nat>, +e: Nat, +k: Nat, +i: Nat, +id: Nat, +x: Nat, +hp: {perf(t, e) == True{} : Bool},
  +hm: {Nat.is_le(Nat.add(i, 1n+k), W.pow2(e)) == True{} : Bool}, +hid: {Nat.is_lt(id, W.pow2(e)) == True{} : Bool},
  +hle: {Nat.is_le(i, id) == True{} : Bool}, b: Bool, +eb: {Nat.is_eq(i, id) == b : Bool}, ih: BumpIH(t, e, k, i, id, x)
) -> {ML(pset(t, e, id, Nat.add(pget(t, e, id), x)), e, 1n+k, i) ==
  Laws.Spec.add(ML(t, e, 1n+k, i), Nat.sub(id, i), x) : List<&2, Nat>}:
  match b:
    case True{}:
      %L.eq_prop(i, id, eb) :
        {ML(pset(t, e, _, Nat.add(pget(t, e, _), x)), e, 1n+k, i) ==
          Laws.Spec.add(ML(t, e, 1n+k, i), Nat.sub(_, i), x) : List<&2, Nat>}
      %Equal.sym(Nat, Nat.sub(i, i), 0n, sub_self(i)) :
        {ML(pset(t, e, i, Nat.add(pget(t, e, i), x)), e, 1n+k, i) ==
          Laws.Spec.add(ML(t, e, 1n+k, i), _, x) : List<&2, Nat>}
      +V = Nat.add(pget(t, e, i), x)
      %Equal.sym(Nat, pget(pset(t, e, i, V), e, i), V, get_same(t, e, i, V)) :
        {_ <> ML(pset(t, e, i, V), e, k, 1n+i) == V <> ML(t, e, k, 1n+i) : List<&2, Nat>}
      %Equal.sym(List<&2, Nat>, ML(pset(t, e, i, V), e, k, 1n+i), ML(t, e, k, 1n+i),
        ml_other(t, e, k, 1n+i, i, V, hp, tl_le(i, k, W.pow2(e), hm), hd_lt(i, k, W.pow2(e), hm),
          L.lt_succ_self(i))) :
        {V <> _ == V <> ML(t, e, k, 1n+i) : List<&2, Nat>}
      {==}
    case False{}:
      +lt = le_ne_lt(i, id, hle, eb)
      +V = Nat.add(pget(t, e, id), x)
      %Equal.sym(Nat, Nat.sub(id, i), 1n+Nat.sub(id, 1n+i), sub_succ(id, i, lt)) :
        {ML(pset(t, e, id, V), e, 1n+k, i) == Laws.Spec.add(ML(t, e, 1n+k, i), _, x) : List<&2, Nat>}
      %Equal.sym(Nat, pget(pset(t, e, id, V), e, i), pget(t, e, i),
        get_other(t, e, id, i, V, hp, L.neq_flip(i, id, eb), hid, hd_lt(i, k, W.pow2(e), hm))) :
        {_ <> ML(pset(t, e, id, V), e, k, 1n+i) ==
          pget(t, e, i) <> Laws.Spec.add(ML(t, e, k, 1n+i), Nat.sub(id, 1n+i), x) : List<&2, Nat>}
      %Equal.sym(List<&2, Nat>, ML(pset(t, e, id, V), e, k, 1n+i),
        Laws.Spec.add(ML(t, e, k, 1n+i), Nat.sub(id, 1n+i), x), ih(L.lt_sle(i, id, lt))) :
        {pget(t, e, i) <> _ == pget(t, e, i) <> Laws.Spec.add(ML(t, e, k, 1n+i), Nat.sub(id, 1n+i), x) :
          List<&2, Nat>}
      {==}

# adding x to slot id of the tree adds x to slot id - i of the list from i
def bump_ml(+t: A.Tr<Nat>, +e: Nat, +m: Nat, +i: Nat, +id: Nat, +x: Nat, +hp: {perf(t, e) == True{} : Bool},
  +hm: {Nat.is_le(Nat.add(i, m), W.pow2(e)) == True{} : Bool}, +hid: {Nat.is_lt(id, W.pow2(e)) == True{} : Bool},
  +hle: {Nat.is_le(i, id) == True{} : Bool}
) -> {ML(pset(t, e, id, Nat.add(pget(t, e, id), x)), e, m, i) ==
  Laws.Spec.add(ML(t, e, m, i), Nat.sub(id, i), x) : List<&2, Nat>}:
  match m:
    case 0n:
      {==}
    case 1n+k:
      bump.at(t, e, k, i, id, x, hp, hm, hid, hle, Nat.is_eq(i, id), {==},
        h => bump_ml(t, e, k, 1n+i, id, x, hp, tl_le(i, k, W.pow2(e), hm), hid, h))

# Scattering the postings
# -----------------------
# The postings buffer is emb(tp) and the table emb(ts), a perfect tree of
# depth e with n <= 2^e. What the core computes, as functions of the trees.

def lt_le_t(a: Nat, b: Nat, c: Nat, h1: {Nat.is_lt(a, b) == True{} : Bool}, h2: {Nat.is_le(b, c) == True{} : Bool}) ->
  {Nat.is_lt(a, c) == True{} : Bool}:
  match a b c:
    case _ 0n _:
      L.absurd({Nat.is_lt(a, c) == True{} : Bool}, L.not_lt0(a, h1))
    case _ 1n+q 0n:
      L.absurd({Nat.is_lt(a, 0n) == True{} : Bool}, L.ft(h2))
    case 0n 1n+q 1n+r:
      {==}
    case 1n+p 1n+q 1n+r:
      lt_le_t(p, q, r, h1, h2)

# word q of the postings
def pw(+tp: A.Tr<U32>, +q: Nat) -> Nat:
  U32.to_nat(A.tget(U32, tp, U32.from_nat(q)))

# adds x to slot id when ok
def padd(ok: Bool, +ts: A.Tr<Nat>, +e: Nat, +id: Nat, +x: Nat) -> A.Tr<Nat>:
  match ok:
    case False{}:
      ts
    case True{}:
      pset(ts, e, id, Nat.add(pget(ts, e, id), x))

# the posting in words q, q + 1, q + 2, of a term with weight w
def ppost(+tp: A.Tr<U32>, +ts: A.Tr<Nat>, +e: Nat, +q: Nat, +w: Nat, +tot: Nat, +n: Nat) -> A.Tr<Nat>:
  padd(Nat.is_lt(pw(tp, q), n), ts, e, pw(tp, q), Nat.mul(w, F.tfn(pw(tp, 1n+q), pw(tp, Nat.add(q, 2n)), tot, n)))

def pposts(c: Nat, +tp: A.Tr<U32>, +e: Nat, +q: Nat, +w: Nat, +tot: Nat, +n: Nat, +ts: A.Tr<Nat>) -> A.Tr<Nat>:
  match c:
    case 0n:
      ts
    case 1n+m:
      pposts(m, tp, e, Nat.add(q, 3n), w, tot, n, ppost(tp, ts, e, q, w, tot, n))

# the word after the term at q
def qnext(+tp: A.Tr<U32>, +q: Nat) -> Nat:
  Nat.add(1n+q, Nat.mul(3n, pw(tp, q)))

def pterms(t: Nat, +tp: A.Tr<U32>, +e: Nat, +q: Nat, +tot: Nat, +n: Nat, +ts: A.Tr<Nat>) -> A.Tr<Nat>:
  match t:
    case 0n:
      ts
    case 1n+u:
      pterms(u, tp, e, qnext(tp, q), tot, n, pposts(pw(tp, q), tp, e, 1n+q, F.idf(n, pw(tp, q)), tot, n, ts))

def qterms(t: Nat, +tp: A.Tr<U32>, +q: Nat) -> Nat:
  match t:
    case 0n:
      q
    case 1n+u:
      qterms(u, tp, qnext(tp, q))

def perf_padd(ok: Bool, +ts: A.Tr<Nat>, +e: Nat, +id: Nat, +x: Nat, +hp: {perf(ts, e) == True{} : Bool}) ->
  {perf(padd(ok, ts, e, id, x), e) == True{} : Bool}:
  match ok:
    case False{}:
      hp
    case True{}:
      perf_set(ts, e, id, Nat.add(pget(ts, e, id), x), hp)

def perf_posts(c: Nat, +tp: A.Tr<U32>, +e: Nat, +q: Nat, +w: Nat, +tot: Nat, +n: Nat, +ts: A.Tr<Nat>,
  +hp: {perf(ts, e) == True{} : Bool}
) -> {perf(pposts(c, tp, e, q, w, tot, n, ts), e) == True{} : Bool}:
  match c:
    case 0n:
      hp
    case 1n+m:
      perf_posts(m, tp, e, Nat.add(q, 3n), w, tot, n, ppost(tp, ts, e, q, w, tot, n),
        perf_padd(Nat.is_lt(pw(tp, q), n), ts, e, pw(tp, q),
          Nat.mul(w, F.tfn(pw(tp, 1n+q), pw(tp, Nat.add(q, 2n)), tot, n)), hp))

def perf_terms(t: Nat, +tp: A.Tr<U32>, +e: Nat, +q: Nat, +tot: Nat, +n: Nat, +ts: A.Tr<Nat>,
  +hp: {perf(ts, e) == True{} : Bool}
) -> {perf(pterms(t, tp, e, q, tot, n, ts), e) == True{} : Bool}:
  match t:
    case 0n:
      hp
    case 1n+u:
      perf_terms(u, tp, e, qnext(tp, q), tot, n, pposts(pw(tp, q), tp, e, 1n+q, F.idf(n, pw(tp, q)), tot, n, ts),
        perf_posts(pw(tp, q), tp, e, 1n+q, F.idf(n, pw(tp, q)), tot, n, ts, hp))

# The core's scatter
# ------------------

# C.add.at on emb(ts)
def addat(ok: Bool, +id: Nat, +x: Nat, +ts: A.Tr<Nat>, +e: Nat, +n: Nat, +eok: {Nat.is_lt(id, n) == ok : Bool},
  +hp: {perf(ts, e) == True{} : Bool}, +hd: {Nat.is_lt(e, 32n) == True{} : Bool},
  +hn: {Nat.is_le(n, W.pow2(e)) == True{} : Bool}
) -> {C.add.at(ok, id, x, A.emb(Nat, ts)) == A.emb(Nat, padd(ok, ts, e, id, x)) : Array<Nat>}:
  match ok:
    case False{}:
      {==}
    case True{}:
      +i = U32.from_nat(id)
      +hi = lt_le_t(id, n, W.pow2(e), eok, hn)
      %Equal.sym(Array<Nat> & Nat, Array.get(Nat, A.emb(Nat, ts), i), (A.emb(Nat, ts), A.tget(Nat, ts, i)),
        A.get_emb(Nat, ts, i)) :
        {F.bump.put(id, x, _) == A.emb(Nat, pset(ts, e, id, Nat.add(pget(ts, e, id), x))) : Array<Nat>}
      +V = Nat.add(A.tget(Nat, ts, i), x)
      %Equal.sym(Array<Nat>, Array.set(Nat, A.emb(Nat, ts), i, V), A.emb(Nat, A.tset(Nat, ts, i, V)),
        A.set_emb(Nat, ts, i, V)) :
        {_ == A.emb(Nat, pset(ts, e, id, Nat.add(pget(ts, e, id), x))) : Array<Nat>}
      %Equal.sym(Nat, A.tget(Nat, ts, i), pget(ts, e, id), tget_at(ts, e, id, hp, hd, hi)) :
        {A.emb(Nat, A.tset(Nat, ts, i, Nat.add(_, x))) ==
          A.emb(Nat, pset(ts, e, id, Nat.add(pget(ts, e, id), x))) : Array<Nat>}
      +V2 = Nat.add(pget(ts, e, id), x)
      %Equal.sym(A.Tr<Nat>, A.tset(Nat, ts, i, V2), pset(ts, e, id, V2), tset_at(ts, e, id, V2, hp, hd, hi)) :
        {A.emb(Nat, _) == A.emb(Nat, pset(ts, e, id, V2)) : Array<Nat>}
      {==}

def post_dl(+q: Nat, +id: Nat, +tf: Nat, +w: Nat, +tot: Nat, +n: Nat, +tp: A.Tr<U32>, +ts: A.Tr<Nat>, +e: Nat,
  +hp: {perf(ts, e) == True{} : Bool}, +hd: {Nat.is_lt(e, 32n) == True{} : Bool},
  +hn: {Nat.is_le(n, W.pow2(e)) == True{} : Bool}
) -> {C.post.dl(id, tf, w, tot, n, A.emb(Nat, ts), Array.get(U32, A.emb(U32, tp), U32.from_nat(Nat.add(q, 2n)))) ==
  C.Acc{A.emb(U32, tp), A.emb(Nat, padd(Nat.is_lt(id, n), ts, e, id,
    Nat.mul(w, F.tfn(tf, pw(tp, Nat.add(q, 2n)), tot, n))))} : C.Acc}:
  +i = U32.from_nat(Nat.add(q, 2n))
  -R = {C.Acc{A.emb(U32, tp), A.emb(Nat, padd(Nat.is_lt(id, n), ts, e, id,
    Nat.mul(w, F.tfn(tf, pw(tp, Nat.add(q, 2n)), tot, n))))} : C.Acc}
  %Equal.sym(Array<U32> & U32, Array.get(U32, A.emb(U32, tp), i), (A.emb(U32, tp), A.tget(U32, tp, i)),
    A.get_emb(U32, tp, i)) :
    {C.post.dl(id, tf, w, tot, n, A.emb(Nat, ts), _) == R : C.Acc}
  +x = Nat.mul(w, F.tfn(tf, pw(tp, Nat.add(q, 2n)), tot, n))
  %Equal.sym(Array<Nat>, C.add.at(Nat.is_lt(id, n), id, x, A.emb(Nat, ts)),
    A.emb(Nat, padd(Nat.is_lt(id, n), ts, e, id, x)), addat(Nat.is_lt(id, n), id, x, ts, e, n, {==}, hp, hd, hn)) :
    {C.Acc{A.emb(U32, tp), _} == R : C.Acc}
  {==}

def post_tf(+q: Nat, +id: Nat, +w: Nat, +tot: Nat, +n: Nat, +tp: A.Tr<U32>, +ts: A.Tr<Nat>, +e: Nat,
  +hp: {perf(ts, e) == True{} : Bool}, +hd: {Nat.is_lt(e, 32n) == True{} : Bool},
  +hn: {Nat.is_le(n, W.pow2(e)) == True{} : Bool}
) -> {C.post.tf(q, id, w, tot, n, A.emb(Nat, ts), Array.get(U32, A.emb(U32, tp), U32.from_nat(1n+q))) ==
  C.Acc{A.emb(U32, tp), A.emb(Nat, padd(Nat.is_lt(id, n), ts, e, id,
    Nat.mul(w, F.tfn(pw(tp, 1n+q), pw(tp, Nat.add(q, 2n)), tot, n))))} : C.Acc}:
  +i = U32.from_nat(1n+q)
  %Equal.sym(Array<U32> & U32, Array.get(U32, A.emb(U32, tp), i), (A.emb(U32, tp), A.tget(U32, tp, i)),
    A.get_emb(U32, tp, i)) :
    {C.post.tf(q, id, w, tot, n, A.emb(Nat, ts), _) ==
      C.Acc{A.emb(U32, tp), A.emb(Nat, padd(Nat.is_lt(id, n), ts, e, id,
        Nat.mul(w, F.tfn(pw(tp, 1n+q), pw(tp, Nat.add(q, 2n)), tot, n))))} : C.Acc}
  post_dl(q, id, pw(tp, 1n+q), w, tot, n, tp, ts, e, hp, hd, hn)

# one posting of the core is ppost
def post_eq(+q: Nat, +w: Nat, +tot: Nat, +n: Nat, +tp: A.Tr<U32>, +ts: A.Tr<Nat>, +e: Nat,
  +hp: {perf(ts, e) == True{} : Bool}, +hd: {Nat.is_lt(e, 32n) == True{} : Bool},
  +hn: {Nat.is_le(n, W.pow2(e)) == True{} : Bool}
) -> {C.post(q, w, tot, n, C.Acc{A.emb(U32, tp), A.emb(Nat, ts)}) ==
  C.Acc{A.emb(U32, tp), A.emb(Nat, ppost(tp, ts, e, q, w, tot, n))} : C.Acc}:
  +i = U32.from_nat(q)
  %Equal.sym(Array<U32> & U32, Array.get(U32, A.emb(U32, tp), i), (A.emb(U32, tp), A.tget(U32, tp, i)),
    A.get_emb(U32, tp, i)) :
    {C.post.id(q, w, tot, n, A.emb(Nat, ts), _) == C.Acc{A.emb(U32, tp), A.emb(Nat, ppost(tp, ts, e, q, w, tot, n))} :
      C.Acc}
  post_tf(q, pw(tp, q), w, tot, n, tp, ts, e, hp, hd, hn)

def posts_eq(c: Nat, +q: Nat, +w: Nat, +tot: Nat, +n: Nat, +tp: A.Tr<U32>, +ts: A.Tr<Nat>, +e: Nat,
  +hp: {perf(ts, e) == True{} : Bool}, +hd: {Nat.is_lt(e, 32n) == True{} : Bool},
  +hn: {Nat.is_le(n, W.pow2(e)) == True{} : Bool}
) -> {C.add.posts(c, q, w, tot, n, C.Acc{A.emb(U32, tp), A.emb(Nat, ts)}) ==
  C.Acc{A.emb(U32, tp), A.emb(Nat, pposts(c, tp, e, q, w, tot, n, ts))} : C.Acc}:
  match c:
    case 0n:
      {==}
    case 1n+m:
      +ts2 = ppost(tp, ts, e, q, w, tot, n)
      %Equal.sym(C.Acc, C.post(q, w, tot, n, C.Acc{A.emb(U32, tp), A.emb(Nat, ts)}),
        C.Acc{A.emb(U32, tp), A.emb(Nat, ts2)}, post_eq(q, w, tot, n, tp, ts, e, hp, hd, hn)) :
        {C.add.posts(m, Nat.add(q, 3n), w, tot, n, _) ==
          C.Acc{A.emb(U32, tp), A.emb(Nat, pposts(m, tp, e, Nat.add(q, 3n), w, tot, n, ts2))} : C.Acc}
      posts_eq(m, Nat.add(q, 3n), w, tot, n, tp, ts2, e,
        perf_padd(Nat.is_lt(pw(tp, q), n), ts, e, pw(tp, q),
          Nat.mul(w, F.tfn(pw(tp, 1n+q), pw(tp, Nat.add(q, 2n)), tot, n)), hp), hd, hn)

# one term of the core
def term_eq(+q: Nat, +tot: Nat, +n: Nat, +tp: A.Tr<U32>, +ts: A.Tr<Nat>, +e: Nat,
  +hp: {perf(ts, e) == True{} : Bool}, +hd: {Nat.is_lt(e, 32n) == True{} : Bool},
  +hn: {Nat.is_le(n, W.pow2(e)) == True{} : Bool}
) -> {C.term(tot, n, C.Tq{A.emb(U32, tp), A.emb(Nat, ts), q}) ==
  C.Tq{A.emb(U32, tp), A.emb(Nat, pposts(pw(tp, q), tp, e, 1n+q, F.idf(n, pw(tp, q)), tot, n, ts)), qnext(tp, q)} :
  C.Tq}:
  +i = U32.from_nat(q)
  +c = pw(tp, q)
  -R = {C.Tq{A.emb(U32, tp), A.emb(Nat, pposts(c, tp, e, 1n+q, F.idf(n, c), tot, n, ts)), qnext(tp, q)} : C.Tq}
  %Equal.sym(Array<U32> & U32, Array.get(U32, A.emb(U32, tp), i), (A.emb(U32, tp), A.tget(U32, tp, i)),
    A.get_emb(U32, tp, i)) :
    {C.term.df(q, tot, n, A.emb(Nat, ts), _) == R : C.Tq}
  %Equal.sym(C.Acc, C.add.posts(c, 1n+q, F.idf(n, c), tot, n, C.Acc{A.emb(U32, tp), A.emb(Nat, ts)}),
    C.Acc{A.emb(U32, tp), A.emb(Nat, pposts(c, tp, e, 1n+q, F.idf(n, c), tot, n, ts))},
    posts_eq(c, 1n+q, F.idf(n, c), tot, n, tp, ts, e, hp, hd, hn)) :
    {C.term.fin(qnext(tp, q), _) == R : C.Tq}
  {==}

# the core's scatter of t terms is pterms
def terms_eq(t: Nat, +q: Nat, +tot: Nat, +n: Nat, +tp: A.Tr<U32>, +ts: A.Tr<Nat>, +e: Nat,
  +hp: {perf(ts, e) == True{} : Bool}, +hd: {Nat.is_lt(e, 32n) == True{} : Bool},
  +hn: {Nat.is_le(n, W.pow2(e)) == True{} : Bool}
) -> {C.add.terms(t, tot, n, C.Tq{A.emb(U32, tp), A.emb(Nat, ts), q}) ==
  C.Tq{A.emb(U32, tp), A.emb(Nat, pterms(t, tp, e, q, tot, n, ts)), qterms(t, tp, q)} : C.Tq}:
  match t:
    case 0n:
      {==}
    case 1n+u:
      +c = pw(tp, q)
      +ts2 = pposts(c, tp, e, 1n+q, F.idf(n, c), tot, n, ts)
      %Equal.sym(C.Tq, C.term(tot, n, C.Tq{A.emb(U32, tp), A.emb(Nat, ts), q}),
        C.Tq{A.emb(U32, tp), A.emb(Nat, ts2), qnext(tp, q)}, term_eq(q, tot, n, tp, ts, e, hp, hd, hn)) :
        {C.add.terms(u, tot, n, _) ==
          C.Tq{A.emb(U32, tp), A.emb(Nat, pterms(u, tp, e, qnext(tp, q), tot, n, ts2)), qterms(u, tp, qnext(tp, q))} :
          C.Tq}
      terms_eq(u, qnext(tp, q), tot, n, tp, ts2, e,
        perf_posts(c, tp, e, 1n+q, F.idf(n, c), tot, n, ts, hp), hd, hn)

def app_nil(-X: Data, +xs: List<&2, X>) -> {xs == List.append(&2, X, xs, Nil{}) : List<&2, X>}:
  match xs:
    case Nil{}:
      {==}
    case Con{+h, +t}:
      %app_nil(X, t) : {h <> t == h <> _ : List<&2, X>}
      {==}

# The spec's reading
# ------------------

def dpost(+tp: A.Tr<U32>, +q: Nat) -> Laws.Spec.Post:
  Laws.Spec.Post{pw(tp, q), pw(tp, 1n+q), pw(tp, Nat.add(q, 2n))}

def dposts(c: Nat, +tp: A.Tr<U32>, +q: Nat) -> List<&2, Laws.Spec.Post>:
  match c:
    case 0n:
      Nil{}
    case 1n+m:
      dpost(tp, q) <> dposts(m, tp, Nat.add(q, 3n))

def dterms(t: Nat, +tp: A.Tr<U32>, +q: Nat) -> List<&2, List<&2, Laws.Spec.Post>>:
  match t:
    case 0n:
      Nil{}
    case 1n+u:
      dposts(pw(tp, q), tp, 1n+q) <> dterms(u, tp, qnext(tp, q))

def qposts(c: Nat, +q: Nat) -> Nat:
  match c:
    case 0n:
      q
    case 1n+m:
      qposts(m, Nat.add(q, 3n))

def app_one(-X: Data, +xs: List<&2, X>, +x: X, +ys: List<&2, X>) ->
  {List.append(&2, X, List.append(&2, X, xs, [x]), ys) == List.append(&2, X, xs, x <> ys) : List<&2, X>}:
  match xs:
    case Nil{}:
      {==}
    case Con{+h, +t}:
      %Equal.sym(List<&2, X>, List.append(&2, X, List.append(&2, X, t, [x]), ys), List.append(&2, X, t, x <> ys),
        app_one(X, t, x, ys)) :
        {h <> _ == h <> List.append(&2, X, t, x <> ys) : List<&2, X>}
      {==}

def sp_post(+tp: A.Tr<U32>, +q: Nat, +ps: List<&2, Laws.Spec.Post>) ->
  {Laws.Spec.post(Laws.Spec.Ps{A.emb(U32, tp), q, ps}) ==
    Laws.Spec.Ps{A.emb(U32, tp), Nat.add(q, 3n), List.append(&2, Laws.Spec.Post, ps, [dpost(tp, q)])} : Laws.Spec.Ps}:
  -R = {Laws.Spec.Ps{A.emb(U32, tp), Nat.add(q, 3n), List.append(&2, Laws.Spec.Post, ps, [dpost(tp, q)])} :
    Laws.Spec.Ps}
  +i0 = U32.from_nat(q)
  +i1 = U32.from_nat(1n+q)
  +i2 = U32.from_nat(Nat.add(q, 2n))
  %Equal.sym(Array<U32> & U32, Array.get(U32, A.emb(U32, tp), i0), (A.emb(U32, tp), A.tget(U32, tp, i0)),
    A.get_emb(U32, tp, i0)) :
    {Laws.Spec.post.id(q, ps, _) == R : Laws.Spec.Ps}
  %Equal.sym(Array<U32> & U32, Array.get(U32, A.emb(U32, tp), i1), (A.emb(U32, tp), A.tget(U32, tp, i1)),
    A.get_emb(U32, tp, i1)) :
    {Laws.Spec.post.tf(q, pw(tp, q), ps, _) == R : Laws.Spec.Ps}
  %Equal.sym(Array<U32> & U32, Array.get(U32, A.emb(U32, tp), i2), (A.emb(U32, tp), A.tget(U32, tp, i2)),
    A.get_emb(U32, tp, i2)) :
    {Laws.Spec.post.dl(q, pw(tp, q), pw(tp, 1n+q), ps, _) == R : Laws.Spec.Ps}
  {==}

def sp_posts(+c: Nat, +tp: A.Tr<U32>, +q: Nat, +ps: List<&2, Laws.Spec.Post>) ->
  {Laws.Spec.posts(c, Laws.Spec.Ps{A.emb(U32, tp), q, ps}) ==
    Laws.Spec.Ps{A.emb(U32, tp), qposts(c, q), List.append(&2, Laws.Spec.Post, ps, dposts(c, tp, q))} :
    Laws.Spec.Ps}:
  match c:
    case 0n:
      %app_nil(Laws.Spec.Post, ps) : {Laws.Spec.Ps{A.emb(U32, tp), q, ps} == Laws.Spec.Ps{A.emb(U32, tp), q, _} :
        Laws.Spec.Ps}
      {==}
    case 1n+m:
      +ps2 = List.append(&2, Laws.Spec.Post, ps, [dpost(tp, q)])
      %Equal.sym(Laws.Spec.Ps, Laws.Spec.post(Laws.Spec.Ps{A.emb(U32, tp), q, ps}),
        Laws.Spec.Ps{A.emb(U32, tp), Nat.add(q, 3n), ps2}, sp_post(tp, q, ps)) :
        {Laws.Spec.posts(m, _) == Laws.Spec.Ps{A.emb(U32, tp), qposts(m, Nat.add(q, 3n)),
          List.append(&2, Laws.Spec.Post, ps, dpost(tp, q) <> dposts(m, tp, Nat.add(q, 3n)))} : Laws.Spec.Ps}
      %app_one(Laws.Spec.Post, ps, dpost(tp, q), dposts(m, tp, Nat.add(q, 3n))) :
        {Laws.Spec.posts(m, Laws.Spec.Ps{A.emb(U32, tp), Nat.add(q, 3n), ps2}) ==
          Laws.Spec.Ps{A.emb(U32, tp), qposts(m, Nat.add(q, 3n)), _} : Laws.Spec.Ps}
      sp_posts(m, tp, Nat.add(q, 3n), ps2)

def sp_term(+tp: A.Tr<U32>, +q: Nat, +ts: List<&2, List<&2, Laws.Spec.Post>>) ->
  {Laws.Spec.term(Laws.Spec.Ts{A.emb(U32, tp), q, ts}) ==
    Laws.Spec.Ts{A.emb(U32, tp), qnext(tp, q),
      List.append(&2, List<&2, Laws.Spec.Post>, ts, [dposts(pw(tp, q), tp, 1n+q)])} : Laws.Spec.Ts}:
  -R = {Laws.Spec.Ts{A.emb(U32, tp), qnext(tp, q),
    List.append(&2, List<&2, Laws.Spec.Post>, ts, [dposts(pw(tp, q), tp, 1n+q)])} : Laws.Spec.Ts}
  +i = U32.from_nat(q)
  %Equal.sym(Array<U32> & U32, Array.get(U32, A.emb(U32, tp), i), (A.emb(U32, tp), A.tget(U32, tp, i)),
    A.get_emb(U32, tp, i)) :
    {Laws.Spec.term.df(q, ts, _) == R : Laws.Spec.Ts}
  +c = pw(tp, q)
  %Equal.sym(Laws.Spec.Ps, Laws.Spec.posts(c, Laws.Spec.Ps{A.emb(U32, tp), 1n+q, Nil{}}),
    Laws.Spec.Ps{A.emb(U32, tp), qposts(c, 1n+q), List.append(&2, Laws.Spec.Post, Nil{}, dposts(c, tp, 1n+q))},
    sp_posts(c, tp, 1n+q, Nil{})) :
    {Laws.Spec.term.fin(qnext(tp, q), ts, _) == R : Laws.Spec.Ts}
  {==}

def sp_terms(+t: Nat, +tp: A.Tr<U32>, +q: Nat, +ts: List<&2, List<&2, Laws.Spec.Post>>) ->
  {Laws.Spec.terms(t, Laws.Spec.Ts{A.emb(U32, tp), q, ts}) ==
    Laws.Spec.Ts{A.emb(U32, tp), qterms(t, tp, q), List.append(&2, List<&2, Laws.Spec.Post>, ts, dterms(t, tp, q))} :
    Laws.Spec.Ts}:
  match t:
    case 0n:
      %app_nil(List<&2, Laws.Spec.Post>, ts) :
        {Laws.Spec.Ts{A.emb(U32, tp), q, ts} == Laws.Spec.Ts{A.emb(U32, tp), q, _} : Laws.Spec.Ts}
      {==}
    case 1n+u:
      +D = dposts(pw(tp, q), tp, 1n+q)
      +ts2 = List.append(&2, List<&2, Laws.Spec.Post>, ts, [D])
      %Equal.sym(Laws.Spec.Ts, Laws.Spec.term(Laws.Spec.Ts{A.emb(U32, tp), q, ts}),
        Laws.Spec.Ts{A.emb(U32, tp), qnext(tp, q), ts2}, sp_term(tp, q, ts)) :
        {Laws.Spec.terms(u, _) == Laws.Spec.Ts{A.emb(U32, tp), qterms(u, tp, qnext(tp, q)),
          List.append(&2, List<&2, Laws.Spec.Post>, ts, D <> dterms(u, tp, qnext(tp, q)))} : Laws.Spec.Ts}
      %app_one(List<&2, Laws.Spec.Post>, ts, D, dterms(u, tp, qnext(tp, q))) :
        {Laws.Spec.terms(u, Laws.Spec.Ts{A.emb(U32, tp), qnext(tp, q), ts2}) ==
          Laws.Spec.Ts{A.emb(U32, tp), qterms(u, tp, qnext(tp, q)), _} : Laws.Spec.Ts}
      sp_terms(u, tp, qnext(tp, q), ts2)

# the spec reads emb(tp) into dterms
def postings_eq(+tp: A.Tr<U32>, +t: Nat) -> {Laws.Spec.postings(A.emb(U32, tp), t) == dterms(t, tp, 0n) :
  List<&2, List<&2, Laws.Spec.Post>>}:
  %Equal.sym(Laws.Spec.Ts, Laws.Spec.terms(t, Laws.Spec.Ts{A.emb(U32, tp), 0n, Nil{}}),
    Laws.Spec.Ts{A.emb(U32, tp), qterms(t, tp, 0n),
      List.append(&2, List<&2, Laws.Spec.Post>, Nil{}, dterms(t, tp, 0n))}, sp_terms(t, tp, 0n, Nil{})) :
    {Laws.Spec.ts(_) == dterms(t, tp, 0n) : List<&2, List<&2, Laws.Spec.Post>>}
  {==}

# The table's list is the spec's fold
# -----------------------------------

def le_conv(-a: Nat, -b: Nat, -c: Nat, e: {a == b : Nat}, h: {Nat.is_le(b, c) == True{} : Bool}) ->
  {Nat.is_le(a, c) == True{} : Bool}:
  %Equal.sym(Nat, a, b, e) : {Nat.is_le(_, c) == True{} : Bool}
  h

def ml_padd(ok: Bool, +id: Nat, +x: Nat, +ts: A.Tr<Nat>, +e: Nat, +n: Nat, +eok: {Nat.is_lt(id, n) == ok : Bool},
  +hp: {perf(ts, e) == True{} : Bool}, +hn: {Nat.is_le(n, W.pow2(e)) == True{} : Bool}
) -> {ML(padd(ok, ts, e, id, x), e, n, 0n) == Laws.Spec.add(ML(ts, e, n, 0n), id, x) : List<&2, Nat>}:
  match ok:
    case True{}:
      %W.sub0(id) :
        {ML(pset(ts, e, id, Nat.add(pget(ts, e, id), x)), e, n, 0n) == Laws.Spec.add(ML(ts, e, n, 0n), _, x) :
          List<&2, Nat>}
      bump_ml(ts, e, n, 0n, id, x, hp, hn, lt_le_t(id, n, W.pow2(e), eok, hn), L.le0(id))
    case False{}:
      Equal.sym(List<&2, Nat>, Laws.Spec.add(ML(ts, e, n, 0n), id, x), ML(ts, e, n, 0n),
        add_out(ML(ts, e, n, 0n), id, x,
          le_conv(List.length(&2, Nat, ML(ts, e, n, 0n)), n, id, ml_len(ts, e, n, 0n), L.nlt_le(id, n, eok))))

def ml_posts(c: Nat, +tp: A.Tr<U32>, +e: Nat, +q: Nat, +w: Nat, +tot: Nat, +n: Nat, +ts: A.Tr<Nat>,
  +hp: {perf(ts, e) == True{} : Bool}, +hn: {Nat.is_le(n, W.pow2(e)) == True{} : Bool}
) -> {ML(pposts(c, tp, e, q, w, tot, n, ts), e, n, 0n) ==
  Laws.Spec.add_posts(dposts(c, tp, q), w, tot, n, ML(ts, e, n, 0n)) : List<&2, Nat>}:
  match c:
    case 0n:
      {==}
    case 1n+m:
      +id = pw(tp, q)
      +x = Nat.mul(w, F.tfn(pw(tp, 1n+q), pw(tp, Nat.add(q, 2n)), tot, n))
      +ts2 = ppost(tp, ts, e, q, w, tot, n)
      %ml_padd(Nat.is_lt(id, n), id, x, ts, e, n, {==}, hp, hn) :
        {ML(pposts(m, tp, e, Nat.add(q, 3n), w, tot, n, ts2), e, n, 0n) ==
          Laws.Spec.add_posts(dposts(m, tp, Nat.add(q, 3n)), w, tot, n, _) : List<&2, Nat>}
      ml_posts(m, tp, e, Nat.add(q, 3n), w, tot, n, ts2, perf_padd(Nat.is_lt(id, n), ts, e, id, x, hp), hn)

def dposts_len(c: Nat, +tp: A.Tr<U32>, +q: Nat) -> {List.length(&2, Laws.Spec.Post, dposts(c, tp, q)) == c : Nat}:
  match c:
    case 0n:
      {==}
    case 1n+m:
      %Equal.sym(Nat, List.length(&2, Laws.Spec.Post, dposts(m, tp, Nat.add(q, 3n))), m,
        dposts_len(m, tp, Nat.add(q, 3n))) : {1n+_ == 1n+m : Nat}
      {==}

def ml_terms(t: Nat, +tp: A.Tr<U32>, +e: Nat, +q: Nat, +tot: Nat, +n: Nat, +ts: A.Tr<Nat>,
  +hp: {perf(ts, e) == True{} : Bool}, +hn: {Nat.is_le(n, W.pow2(e)) == True{} : Bool}
) -> {ML(pterms(t, tp, e, q, tot, n, ts), e, n, 0n) ==
  Laws.Spec.add_terms(dterms(t, tp, q), tot, n, ML(ts, e, n, 0n)) : List<&2, Nat>}:
  match t:
    case 0n:
      {==}
    case 1n+u:
      +c = pw(tp, q)
      +D = dposts(c, tp, 1n+q)
      +ts2 = pposts(c, tp, e, 1n+q, F.idf(n, c), tot, n, ts)
      %Equal.sym(Nat, List.length(&2, Laws.Spec.Post, D), c, dposts_len(c, tp, 1n+q)) :
        {ML(pterms(u, tp, e, qnext(tp, q), tot, n, ts2), e, n, 0n) == Laws.Spec.add_terms(dterms(u, tp, qnext(tp, q)),
          tot, n, Laws.Spec.add_posts(D, F.idf(n, _), tot, n, ML(ts, e, n, 0n))) : List<&2, Nat>}
      %ml_posts(c, tp, e, 1n+q, F.idf(n, c), tot, n, ts, hp, hn) :
        {ML(pterms(u, tp, e, qnext(tp, q), tot, n, ts2), e, n, 0n) ==
          Laws.Spec.add_terms(dterms(u, tp, qnext(tp, q)), tot, n, _) : List<&2, Nat>}
      ml_terms(u, tp, e, qnext(tp, q), tot, n, ts2, perf_posts(c, tp, e, 1n+q, F.idf(n, c), tot, n, ts, hp), hn)

def le_eqr(-a: Nat, -b: Nat, -c: Nat, e: {b == c : Nat}, h: {Nat.is_le(a, b) == True{} : Bool}) ->
  {Nat.is_le(a, c) == True{} : Bool}:
  %e : {Nat.is_le(a, _) == True{} : Bool}
  h

# The depth of the table
# ----------------------

def le_trans(a: Nat, b: Nat, c: Nat, h1: {Nat.is_le(a, b) == True{} : Bool}, h2: {Nat.is_le(b, c) == True{} : Bool}) ->
  {Nat.is_le(a, c) == True{} : Bool}:
  match a b c:
    case 0n _ _:
      L.le0(c)
    case 1n+p 0n _:
      L.absurd({Nat.is_le(1n+p, c) == True{} : Bool}, L.ft(h1))
    case 1n+p 1n+q 0n:
      L.absurd({Nat.is_le(1n+p, 0n) == True{} : Bool}, L.ft(h2))
    case 1n+p 1n+q 1n+r:
      le_trans(p, q, r, h1, h2)

def pow2_lt(+d: Nat, +D: Nat, h: {Nat.is_lt(W.pow2(d), W.pow2(D)) == True{} : Bool}) -> {Nat.is_lt(d, D) == True{} : Bool}:
  match d D:
    case _ 0n:
      L.absurd({Nat.is_lt(d, 0n) == True{} : Bool}, L.ft(L.le_lt(1n, W.pow2(d), 1n, pow_pos(d), h)))
    case 0n 1n+E:
      {==}
    case 1n+a 1n+E:
      pow2_lt(a, E, W.dbl_lt(W.pow2(a), W.pow2(E), h))

def mul2(+w: Nat) -> {Nat.mul(2n, w) == Nat.double(w) : Nat}:
  %Equal.sym(Nat, Nat.add(w, 0n), w, L.add0(w)) : {Nat.add(w, _) == Nat.double(w) : Nat}
  Equal.sym(Nat, Nat.double(w), Nat.add(w, w), dbl_add(w))

def DepthOk(-r: Nat, -n: Nat, -D: Nat) -> Type:
  Pair({Nat.is_le(r, D) == True{} : Bool}, {Nat.is_le(n, W.pow2(r)) == True{} : Bool})

def lt_irr_e(a: Nat, h: {Nat.is_lt(a, a) == True{} : Bool}) -> Empty:
  L.clash(Nat.is_lt(a, a), h, L.lt_irr(a))

# depth.go ends at some r <= D with n <= 2^r, while w = 2^d, n <= 2^D
def dep(+fuel: Nat, more: Bool, +n: Nat, +d: Nat, +w: Nat, +D: Nat, +em: {Nat.is_lt(w, n) == more : Bool},
  +ew: {w == W.pow2(d) : Nat}, +hdD: {Nat.is_le(d, D) == True{} : Bool},
  +hn: {Nat.is_le(n, W.pow2(D)) == True{} : Bool}, +hf: {Nat.is_le(D, Nat.add(d, fuel)) == True{} : Bool}
) -> DepthOk(F.depth.go(fuel, more, n, d, w), n, D):
  match fuel more:
    case 0n True{}:
      +ltD = pow2_lt(d, D, lt_le_t(W.pow2(d), n, W.pow2(D), lt_eqv(W.pow2(d), w, n, Equal.sym(Nat, w, W.pow2(d), ew), em), hn))
      +le = le_eqr(D, Nat.add(d, 0n), d, L.add0(d), hf)
      L.absurd(DepthOk(d, n, D), lt_irr_e(d, lt_le_t(d, D, d, ltD, le)))
    case 0n False{}:
      (hdD, le_eqr(n, w, W.pow2(d), ew, L.nlt_le(w, n, em)))
    case 1n+f False{}:
      (hdD, le_eqr(n, w, W.pow2(d), ew, L.nlt_le(w, n, em)))
    case 1n+f True{}:
      +ltD = pow2_lt(d, D, lt_le_t(W.pow2(d), n, W.pow2(D), lt_eqv(W.pow2(d), w, n, Equal.sym(Nat, w, W.pow2(d), ew), em), hn))
      +e2 = Equal.trans(Nat, Nat.mul(2n, w), Nat.double(w), W.pow2(1n+d), mul2(w),
        Equal.cong(Nat, Nat, x => Nat.double(x), w, W.pow2(d), ew))
      dep(f, Nat.is_lt(Nat.mul(2n, w), n), n, 1n+d, Nat.mul(2n, w), D, {==}, e2, L.lt_sle(d, D, ltD), hn,
        le_eqr(D, Nat.add(d, 1n+f), 1n+Nat.add(d, f), L.add_s(d, f), hf))

def depth_ok.of(+n: Nat, +D: Nat, +hD: {Nat.is_le(D, 31n) == True{} : Bool}, pr: DepthOk(F.depth(n), n, D)) ->
  Pair({Nat.is_lt(F.depth(n), 32n) == True{} : Bool}, {Nat.is_le(n, W.pow2(F.depth(n))) == True{} : Bool}):
  (h1, h2) = pr
  (L.le_lt(F.depth(n), D, 32n, h1, L.le_lt(D, 31n, 32n, hD, {==})), h2)

# the table for n <= 2^D, D <= 31 chunks has depth below 32 and room for n
def depth_ok(+n: Nat, +D: Nat, +hD: {Nat.is_le(D, 31n) == True{} : Bool}, +hn: {Nat.is_le(n, W.pow2(D)) == True{} : Bool}) ->
  Pair({Nat.is_lt(F.depth(n), 32n) == True{} : Bool}, {Nat.is_le(n, W.pow2(F.depth(n))) == True{} : Bool}):
  depth_ok.of(n, D, hD, dep(48n, Nat.is_lt(1n, n), n, 0n, 1n, D, {==}, {==}, L.le0(D), hn,
    le_trans(D, 31n, 48n, hD, {==})))
