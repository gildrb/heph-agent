import Base
import ../LAWS.bend as Laws
import ../core.bend as C

# Arrays as trees
# ===============
# Base's Array is a tree of ALeaf and ANode. Every array is emb(t) for a
# tree t (view), and get, set and new on emb(t) compute to emb of a pure
# function of t. Proofs then reason about t, which is Data: unlike the
# array, it may be inspected any number of times.

type Tr<-T: Data> is Data:
  TL{v: T}
  TN{l: Tr<T>, r: Tr<T>}

def emb(-T: Data, t: Tr<T>) -> Array<T>:
  match t:
    case TL{v}:
      ALeaf{v}
    case TN{l, r}:
      ANode{emb(T, l), emb(T, r)}

def view.eq(-T: Data, -xs: Array<T>, -ys: Array<T>, -tx: Tr<T>, -ty: Tr<T>,
  ex: {emb(T, tx) == xs : Array<T>}, ey: {emb(T, ty) == ys : Array<T>}
) -> {emb(T, TN{tx, ty}) == ANode{xs, ys} : Array<T>}:
  %ex : {emb(T, TN{tx, ty}) == ANode{_, ys} : Array<T>}
  %ey : {emb(T, TN{tx, ty}) == ANode{emb(T, tx), _} : Array<T>}
  {==}

def view.node(-T: Data, -xs: Array<T>, -ys: Array<T>,
  vx: &tx: Tr<T> -> {emb(T, tx) == xs : Array<T>},
  vy: &ty: Tr<T> -> {emb(T, ty) == ys : Array<T>}
) -> &t: Tr<T> -> {emb(T, t) == ANode{xs, ys} : Array<T>}:
  (+tx, ex) = vx
  (+ty, ey) = vy
  (TN{tx, ty}, view.eq(T, xs, ys, tx, ty, ex, ey))

# every array is the embedding of a tree
def view(-T: Data, a: Array<T>) -> &t: Tr<T> -> {emb(T, t) == a : Array<T>}:
  match a:
    case ALeaf{+x}:
      (TL{x}, {==})
    case ANode{xs, ys}:
      view.node(T, xs, ys, view(T, xs), view(T, ys))

# Size
# ----

def tsize(-T: Data, t: Tr<T>) -> U32:
  match t:
    case TL{v}:
      1
    case TN{l, r}:
      U32.shl(tsize(T, l))

def size_emb(-T: Data, +t: Tr<T>) ->
  {Array.size(T, emb(T, t)) == (emb(T, t), tsize(T, t)) : Array<T> & U32}:
  match t:
    case TL{+v}:
      {==}
    case TN{+l, +r}:
      %Equal.sym(Array<T> & U32, Array.size(T, emb(T, l)), (emb(T, l), tsize(T, l)), size_emb(T, l)) :
        {Array.size.node(T, emb(T, r), _) == (ANode{emb(T, l), emb(T, r)}, U32.shl(tsize(T, l))) :
          Array<T> & U32}
      {==}

# Get
# ---

def tget.go(-T: Data, t: Tr<T>, +n: U32, +i: U32) -> T:
  match t:
    case TL{v}:
      v
    case TN{+l, +r}:
      +h = U32.shr(n)
      Bool.pick(T, U32.is_lt(i, h), tget.go(T, l, h, i), tget.go(T, r, h, U32.sub(i, h)))

# slot i of the tree, where Array.get finds it
def tget(-T: Data, +t: Tr<T>, +i: U32) -> T:
  +n = tsize(T, t)
  tget.go(T, t, n, U32.and(i, U32.sub(n, 1)))

# Base's Array.get.go takes z, which half of a node the index is in: its
# caller computes it as U32.is_lt(i, U32.shr(n)), the test tget.go makes
def get_node(-T: Data, +l: Tr<T>, +r: Tr<T>, +n: U32, +i: U32, z: Bool,
  gl: {Array.get.go(T, emb(T, l), U32.shr(n), i, U32.is_lt(i, U32.shr(U32.shr(n)))) ==
    (emb(T, l), tget.go(T, l, U32.shr(n), i)) : Array<T> & T},
  gr: {Array.get.go(T, emb(T, r), U32.shr(n), U32.sub(i, U32.shr(n)),
    U32.is_lt(U32.sub(i, U32.shr(n)), U32.shr(U32.shr(n)))) ==
    (emb(T, r), tget.go(T, r, U32.shr(n), U32.sub(i, U32.shr(n)))) : Array<T> & T}
) -> {Array.get.go(T, ANode{emb(T, l), emb(T, r)}, n, i, z) ==
  (ANode{emb(T, l), emb(T, r)},
    Bool.pick(T, z, tget.go(T, l, U32.shr(n), i), tget.go(T, r, U32.shr(n), U32.sub(i, U32.shr(n))))) :
  Array<T> & T}:
  match z:
    case True{}:
      +h = U32.shr(n)
      %Equal.sym(Array<T> & T, Array.get.go(T, emb(T, l), h, i, U32.is_lt(i, U32.shr(h))),
        (emb(T, l), tget.go(T, l, h, i)), gl) :
        {Array.swap.lo(T, emb(T, r), _) == (ANode{emb(T, l), emb(T, r)}, tget.go(T, l, h, i)) : Array<T> & T}
      {==}
    case False{}:
      +h = U32.shr(n)
      +j = U32.sub(i, h)
      %Equal.sym(Array<T> & T, Array.get.go(T, emb(T, r), h, j, U32.is_lt(j, U32.shr(h))),
        (emb(T, r), tget.go(T, r, h, j)), gr) :
        {Array.swap.hi(T, emb(T, l), _) == (ANode{emb(T, l), emb(T, r)}, tget.go(T, r, h, j)) : Array<T> & T}
      {==}

def get_go_emb(-T: Data, +t: Tr<T>, +n: U32, +i: U32) ->
  {Array.get.go(T, emb(T, t), n, i, U32.is_lt(i, U32.shr(n))) == (emb(T, t), tget.go(T, t, n, i)) :
    Array<T> & T}:
  match t:
    case TL{+v}:
      {==}
    case TN{+l, +r}:
      +h = U32.shr(n)
      get_node(T, l, r, n, i, U32.is_lt(i, h), get_go_emb(T, l, h, i), get_go_emb(T, r, h, U32.sub(i, h)))

# Array.get on emb(t) reads tget and hands emb(t) back
def get_emb(-T: Data, +t: Tr<T>, +i: U32) ->
  {Array.get(T, emb(T, t), i) == (emb(T, t), tget(T, t, i)) : Array<T> & T}:
  +n = tsize(T, t)
  %Equal.sym(Array<T> & U32, Array.size(T, emb(T, t)), (emb(T, t), n), size_emb(T, t)) :
    {Array.get.at(T, i, _) == (emb(T, t), tget(T, t, i)) : Array<T> & T}
  get_go_emb(T, t, n, U32.and(i, U32.sub(n, 1)))

# Set
# ---

def tset.go(-T: Data, t: Tr<T>, +n: U32, +i: U32, +v: T) -> Tr<T>:
  match t:
    case TL{x}:
      TL{v}
    case TN{+l, +r}:
      +h = U32.shr(n)
      Bool.pick(Tr<T>, U32.is_lt(i, h), TN{tset.go(T, l, h, i, v), r}, TN{l, tset.go(T, r, h, U32.sub(i, h), v)})

# the tree after Array.set writes v to slot i
def tset(-T: Data, +t: Tr<T>, +i: U32, +v: T) -> Tr<T>:
  +n = tsize(T, t)
  tset.go(T, t, n, U32.and(i, U32.sub(n, 1)), v)

# Array.swap.go takes z like Array.get.go
def swap_node(-T: Data, +l: Tr<T>, +r: Tr<T>, +n: U32, +i: U32, +v: T, z: Bool,
  gl: {Array.swap.go(T, emb(T, l), U32.shr(n), i, v, U32.is_lt(i, U32.shr(U32.shr(n)))) ==
    (emb(T, tset.go(T, l, U32.shr(n), i, v)), tget.go(T, l, U32.shr(n), i)) : Array<T> & T},
  gr: {Array.swap.go(T, emb(T, r), U32.shr(n), U32.sub(i, U32.shr(n)), v,
    U32.is_lt(U32.sub(i, U32.shr(n)), U32.shr(U32.shr(n)))) ==
    (emb(T, tset.go(T, r, U32.shr(n), U32.sub(i, U32.shr(n)), v)),
      tget.go(T, r, U32.shr(n), U32.sub(i, U32.shr(n)))) : Array<T> & T}
) -> {Array.swap.go(T, ANode{emb(T, l), emb(T, r)}, n, i, v, z) ==
  (emb(T, Bool.pick(Tr<T>, z, TN{tset.go(T, l, U32.shr(n), i, v), r},
    TN{l, tset.go(T, r, U32.shr(n), U32.sub(i, U32.shr(n)), v)})),
    Bool.pick(T, z, tget.go(T, l, U32.shr(n), i), tget.go(T, r, U32.shr(n), U32.sub(i, U32.shr(n))))) :
  Array<T> & T}:
  match z:
    case True{}:
      +h = U32.shr(n)
      %Equal.sym(Array<T> & T, Array.swap.go(T, emb(T, l), h, i, v, U32.is_lt(i, U32.shr(h))),
        (emb(T, tset.go(T, l, h, i, v)), tget.go(T, l, h, i)), gl) :
        {Array.swap.lo(T, emb(T, r), _) ==
          (ANode{emb(T, tset.go(T, l, h, i, v)), emb(T, r)}, tget.go(T, l, h, i)) : Array<T> & T}
      {==}
    case False{}:
      +h = U32.shr(n)
      +j = U32.sub(i, h)
      %Equal.sym(Array<T> & T, Array.swap.go(T, emb(T, r), h, j, v, U32.is_lt(j, U32.shr(h))),
        (emb(T, tset.go(T, r, h, j, v)), tget.go(T, r, h, j)), gr) :
        {Array.swap.hi(T, emb(T, l), _) ==
          (ANode{emb(T, l), emb(T, tset.go(T, r, h, j, v))}, tget.go(T, r, h, j)) : Array<T> & T}
      {==}

def swap_go_emb(-T: Data, +t: Tr<T>, +n: U32, +i: U32, +v: T) ->
  {Array.swap.go(T, emb(T, t), n, i, v, U32.is_lt(i, U32.shr(n))) ==
    (emb(T, tset.go(T, t, n, i, v)), tget.go(T, t, n, i)) : Array<T> & T}:
  match t:
    case TL{+x}:
      {==}
    case TN{+l, +r}:
      +h = U32.shr(n)
      swap_node(T, l, r, n, i, v, U32.is_lt(i, h), swap_go_emb(T, l, h, i, v),
        swap_go_emb(T, r, h, U32.sub(i, h), v))

# Array.set on emb(t) is emb of tset
def set_emb(-T: Data, +t: Tr<T>, +i: U32, +v: T) ->
  {Array.set(T, emb(T, t), i, v) == emb(T, tset(T, t, i, v)) : Array<T>}:
  +n = tsize(T, t)
  %Equal.sym(Array<T> & U32, Array.size(T, emb(T, t)), (emb(T, t), n), size_emb(T, t)) :
    {Array.set.fin(T, Array.swap.at(T, i, v, _)) == emb(T, tset(T, t, i, v)) : Array<T>}
  +m = U32.and(i, U32.sub(n, 1))
  %Equal.sym(Array<T> & T, Array.swap.go(T, emb(T, t), n, m, v, U32.is_lt(m, U32.shr(n))),
    (emb(T, tset.go(T, t, n, m, v)), tget.go(T, t, n, m)), swap_go_emb(T, t, n, m, v)) :
    {Array.set.fin(T, _) == emb(T, tset(T, t, i, v)) : Array<T>}
  {==}

# New
# ---

def tnew(-T: Data, +d: Nat, +v: T) -> Tr<T>:
  match d:
    case 0n:
      TL{v}
    case 1n+p:
      TN{tnew(T, p, v), tnew(T, p, v)}

def new_emb(-T: Data, +d: Nat, +v: T) -> {Array.new(T, d, v) == emb(T, tnew(T, d, v)) : Array<T>}:
  match d:
    case 0n:
      {==}
    case 1n+p:
      +e = Equal.sym(Array<T>, Array.new(T, p, v), emb(T, tnew(T, p, v)), new_emb(T, p, v))
      %e : {ANode{_, Array.new(T, p, v)} == emb(T, tnew(T, 1n+p, v)) : Array<T>}
      %e : {ANode{emb(T, tnew(T, p, v)), _} == emb(T, tnew(T, 1n+p, v)) : Array<T>}
      {==}

# Bytes
# -----
# byte p sits at bits 8 * (p % 4) of slot p / 4

def bv(+t: Tr<U32>, +p: Nat) -> U32:
  (U32.shrn(tget(U32, t, U32.from_nat(Nat.div(p, 4n))), Nat.mul(8n, Nat.mod(p, 4n))) .&. 255 : U32)

def byte_emb(+t: Tr<U32>, +p: Nat) -> {C.byte(emb(U32, t), p) == (emb(U32, t), bv(t, p)) : Array<U32> & U32}:
  %Equal.sym(Array<U32> & U32, Array.get(U32, emb(U32, t), U32.from_nat(Nat.div(p, 4n))),
    (emb(U32, t), tget(U32, t, U32.from_nat(Nat.div(p, 4n)))), get_emb(U32, t, U32.from_nat(Nat.div(p, 4n)))) :
    {C.byte.of(p, _) == (emb(U32, t), bv(t, p)) : Array<U32> & U32}
  {==}

def sbyte_emb(+t: Tr<U32>, +p: Nat) ->
  {Laws.Spec.byte(emb(U32, t), p) == (emb(U32, t), bv(t, p)) : Array<U32> & U32}:
  %Equal.sym(Array<U32> & U32, Array.get(U32, emb(U32, t), U32.from_nat(Nat.div(p, 4n))),
    (emb(U32, t), tget(U32, t, U32.from_nat(Nat.div(p, 4n)))), get_emb(U32, t, U32.from_nat(Nat.div(p, 4n)))) :
    {Laws.Spec.byte.of(p, _) == (emb(U32, t), bv(t, p)) : Array<U32> & U32}
  {==}

# the n bytes from p
def bytes(+t: Tr<U32>, n: Nat, +p: Nat) -> List<&2, U32>:
  match n:
    case 0n:
      Nil{}
    case 1n+m:
      bv(t, p) <> bytes(t, m, 1n+p)

def sbytes_emb(+t: Tr<U32>, n: Nat, +p: Nat) ->
  {Laws.Spec.bytes(n, p, (emb(U32, t), bv(t, p))) == (emb(U32, t), bytes(t, n, p)) : Array<U32> & List<&2, U32>}:
  match n:
    case 0n:
      {==}
    case 1n+m:
      %Equal.sym(Array<U32> & U32, Laws.Spec.byte(emb(U32, t), 1n+p), (emb(U32, t), bv(t, 1n+p)),
        sbyte_emb(t, 1n+p)) :
        {Laws.Spec.cons(U32, bv(t, p), Laws.Spec.bytes(m, 1n+p, _)) ==
          (emb(U32, t), bytes(t, 1n+m, p)) : Array<U32> & List<&2, U32>}
      %Equal.sym(Array<U32> & List<&2, U32>, Laws.Spec.bytes(m, 1n+p, (emb(U32, t), bv(t, 1n+p))),
        (emb(U32, t), bytes(t, m, 1n+p)), sbytes_emb(t, m, 1n+p)) :
        {Laws.Spec.cons(U32, bv(t, p), _) == (emb(U32, t), bytes(t, 1n+m, p)) : Array<U32> & List<&2, U32>}
      {==}

# Spec.read on emb(t) is bytes
def read_emb(+t: Tr<U32>, +p: Nat, +n: Nat) ->
  {Laws.Spec.read(emb(U32, t), p, n) == (emb(U32, t), bytes(t, n, p)) : Array<U32> & List<&2, U32>}:
  %Equal.sym(Array<U32> & U32, Laws.Spec.byte(emb(U32, t), p), (emb(U32, t), bv(t, p)), sbyte_emb(t, p)) :
    {Laws.Spec.bytes(n, p, _) == (emb(U32, t), bytes(t, n, p)) : Array<U32> & List<&2, U32>}
  sbytes_emb(t, n, p)
