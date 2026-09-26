import Base
import ../LAWS.bend as Laws
import ./logic.bend as L

# Words
# =====
# What the U32 operations the array index goes through compute, in terms of
# Word.to_nat (N below): comparison is Nat comparison, and the sizes are the
# words P(m, e) = 2^e, so shr halves them and subtracting one gives ones(m, e),
# the mask of the low e bits.

def pow2(e: Nat) -> Nat:
  Laws.Spec.pow2(e)

# the m-bit word 2^e
def P(m: Nat, +e: Nat) -> Word(m):
  match m:
    case 0n:
      WNil{}
    case 1n+p:
      match e:
        case 0n:
          WCon{True{}, Word.zero(p)}
        case 1n+f:
          WCon{False{}, P(p, f)}

# the m-bit word with the low e bits set
def ones(m: Nat, +e: Nat) -> Word(m):
  match m:
    case 0n:
      WNil{}
    case 1n+p:
      match e:
        case 0n:
          WCon{False{}, Word.zero(p)}
        case 1n+f:
          WCon{True{}, ones(p, f)}

# Nat facts
# ---------

def dbl_lt(a: Nat, b: Nat, h: {Nat.is_lt(Nat.double(a), Nat.double(b)) == True{} : Bool}) ->
  {Nat.is_lt(a, b) == True{} : Bool}:
  match a b:
    case _ 0n:
      L.absurd({Nat.is_lt(a, 0n) == True{} : Bool}, L.not_lt0(Nat.double(a), h))
    case 0n 1n+q:
      {==}
    case 1n+p 1n+q:
      dbl_lt(p, q, h)

# 1 + 2a < 2b gives a < b
def dbl1_lt(a: Nat, b: Nat, h: {Nat.is_lt(1n+Nat.double(a), Nat.double(b)) == True{} : Bool}) ->
  {Nat.is_lt(a, b) == True{} : Bool}:
  match a b:
    case _ 0n:
      L.absurd({Nat.is_lt(a, 0n) == True{} : Bool}, L.not_lt0(1n+Nat.double(a), h))
    case 0n 1n+q:
      {==}
    case 1n+p 1n+q:
      dbl1_lt(p, q, h)

def dbl_le(a: Nat, b: Nat, h: {Nat.is_le(Nat.double(a), Nat.double(b)) == True{} : Bool}) ->
  {Nat.is_le(a, b) == True{} : Bool}:
  match a b:
    case 0n _:
      L.le0(b)
    case 1n+p 0n:
      L.absurd({Nat.is_le(1n+p, 0n) == True{} : Bool}, L.ft(h))
    case 1n+p 1n+q:
      dbl_le(p, q, h)

# 2a <= 1 + 2b gives a <= b
def dbl_le1(a: Nat, b: Nat, h: {Nat.is_le(Nat.double(a), 1n+Nat.double(b)) == True{} : Bool}) ->
  {Nat.is_le(a, b) == True{} : Bool}:
  match a b:
    case 0n _:
      L.le0(b)
    case 1n+p 0n:
      L.absurd({Nat.is_le(1n+p, 0n) == True{} : Bool}, L.ft(h))
    case 1n+p 1n+q:
      dbl_le1(p, q, h)

def sub0(a: Nat) -> {Nat.sub(a, 0n) == a : Nat}:
  match a:
    case 0n:
      {==}
    case 1n+p:
      {==}

# 2(a - b) = 2a - 2b
def dsub(a: Nat, b: Nat) -> {Nat.double(Nat.sub(a, b)) == Nat.sub(Nat.double(a), Nat.double(b)) : Nat}:
  match a b:
    case 0n 0n:
      {==}
    case 0n 1n+q:
      {==}
    case 1n+p 0n:
      {==}
    case 1n+p 1n+q:
      dsub(p, q)

# 1 + 2(a - b) = (1 + 2a) - 2b, for b <= a
def dsub1(a: Nat, b: Nat, h: {Nat.is_le(b, a) == True{} : Bool}) ->
  {1n+Nat.double(Nat.sub(a, b)) == Nat.sub(1n+Nat.double(a), Nat.double(b)) : Nat}:
  match a b:
    case _ 0n:
      %Equal.sym(Nat, Nat.sub(a, 0n), a, sub0(a)) : {1n+Nat.double(_) == 1n+Nat.double(a) : Nat}
      {==}
    case 0n 1n+q:
      L.absurd({1n == Nat.sub(1n, Nat.double(1n+q)) : Nat}, L.ft(h))
    case 1n+p 1n+q:
      dsub1(p, q, h)

# 1 + 2(a - 1) = 2a - 1, for a >= 1
def dsub_one(a: Nat, h: {Nat.is_le(1n, a) == True{} : Bool}) ->
  {1n+Nat.double(Nat.sub(a, 1n)) == Nat.sub(Nat.double(a), 1n) : Nat}:
  match a:
    case 0n:
      L.absurd({1n+Nat.double(Nat.sub(0n, 1n)) == Nat.sub(0n, 1n) : Nat}, L.ft(h))
    case 1n+p:
      %Equal.sym(Nat, Nat.sub(p, 0n), p, sub0(p)) : {1n+Nat.double(_) == 1n+Nat.double(p) : Nat}
      {==}

# Words and Word.to_nat
# ---------------------

def zN(m: Nat) -> {Word.to_nat(m, Word.zero(m)) == 0n : Nat}:
  match m:
    case 0n:
      {==}
    case 1n+p:
      %Equal.sym(Nat, Word.to_nat(p, Word.zero(p)), 0n, zN(p)) : {Nat.double(_) == 0n : Nat}
      {==}

# N(P(m, e)) = 2^e for e < m
def pN(m: Nat, +e: Nat, h: {Nat.is_lt(e, m) == True{} : Bool}) -> {Word.to_nat(m, P(m, e)) == pow2(e) : Nat}:
  match m e:
    case 0n _:
      L.absurd({Word.to_nat(0n, P(0n, e)) == pow2(e) : Nat}, L.not_lt0(e, h))
    case 1n+p 0n:
      %Equal.sym(Nat, Word.to_nat(p, Word.zero(p)), 0n, zN(p)) : {1n+Nat.double(_) == 1n : Nat}
      {==}
    case 1n+p 1n+f:
      %Equal.sym(Nat, Word.to_nat(p, P(p, f)), pow2(f), pN(p, f, h)) : {Nat.double(_) == pow2(1n+f) : Nat}
      {==}

# a low bit b over the number u: b + 2u
def bn(b: Bool, u: Nat) -> Nat:
  match b:
    case False{}:
      Nat.double(u)
    case True{}:
      1n+Nat.double(u)

# Word.cmp.fin on two low bits is Nat.cmp on the numbers they end
def dcmp(u: Nat, v: Nat, x: Bool, y: Bool) ->
  {Word.cmp.fin(x, y, Nat.cmp(u, v)) == Nat.cmp(bn(x, u), bn(y, v)) : Cmp}:
  match u v x y:
    case 0n 0n False{} False{}:
      {==}
    case 0n 0n False{} True{}:
      {==}
    case 0n 0n True{} False{}:
      {==}
    case 0n 0n True{} True{}:
      {==}
    case 0n 1n+q False{} False{}:
      {==}
    case 0n 1n+q False{} True{}:
      {==}
    case 0n 1n+q True{} False{}:
      {==}
    case 0n 1n+q True{} True{}:
      {==}
    case 1n+p 0n False{} False{}:
      {==}
    case 1n+p 0n False{} True{}:
      {==}
    case 1n+p 0n True{} False{}:
      {==}
    case 1n+p 0n True{} True{}:
      {==}
    case 1n+p 1n+q False{} False{}:
      dcmp(p, q, False{}, False{})
    case 1n+p 1n+q False{} True{}:
      dcmp(p, q, False{}, True{})
    case 1n+p 1n+q True{} False{}:
      dcmp(p, q, True{}, False{})
    case 1n+p 1n+q True{} True{}:
      dcmp(p, q, True{}, True{})

def wcmp.bits(x: Bool, y: Bool, +p: Nat, +at: Word(p), +bt: Word(p)) ->
  {Word.cmp.fin(x, y, Nat.cmp(Word.to_nat(p, at), Word.to_nat(p, bt))) ==
    Nat.cmp(Word.to_nat(1n+p, WCon{x, at}), Word.to_nat(1n+p, WCon{y, bt})) : Cmp}:
  match x y:
    case False{} False{}:
      dcmp(Word.to_nat(p, at), Word.to_nat(p, bt), False{}, False{})
    case False{} True{}:
      dcmp(Word.to_nat(p, at), Word.to_nat(p, bt), False{}, True{})
    case True{} False{}:
      dcmp(Word.to_nat(p, at), Word.to_nat(p, bt), True{}, False{})
    case True{} True{}:
      dcmp(Word.to_nat(p, at), Word.to_nat(p, bt), True{}, True{})

# Word.cmp is Nat.cmp on Word.to_nat
def wcmp(+m: Nat, a: Word(m), b: Word(m)) ->
  {Word.cmp(m, a, b) == Nat.cmp(Word.to_nat(m, a), Word.to_nat(m, b)) : Cmp}:
  match m:
    case 0n:
      match a b:
        case WNil{} WNil{}:
          {==}
    case 1n+p:
      match a b:
        case WCon{x, +at} WCon{y, +bt}:
          %Equal.sym(Cmp, Word.cmp(p, at, bt), Nat.cmp(Word.to_nat(p, at), Word.to_nat(p, bt)), wcmp(p, at, bt)) :
            {Word.cmp.fin(x, y, _) == Nat.cmp(Word.to_nat(1n+p, WCon{x, at}), Word.to_nat(1n+p, WCon{y, bt})) : Cmp}
          wcmp.bits(x, y, p, at, bt)

# Increment
# ---------

# d <= 0 and 1 < 2^d clash
def ninc.z(d: Nat, hd: {Nat.is_le(d, 0n) == True{} : Bool}, h: {Nat.is_lt(1n, pow2(d)) == True{} : Bool}) -> Empty:
  match d:
    case 0n:
      L.ft(h)
    case 1n+q:
      L.ft(hd)

def NIncT(+p: Nat, +t: Word(p)) -> Type:
  @+d: Nat -> @hd: {Nat.is_le(d, p) == True{} : Bool} ->
    @h: {Nat.is_lt(1n+Word.to_nat(p, t), pow2(d)) == True{} : Bool} ->
    {Word.to_nat(p, Word.inc(p, t)) == 1n+Word.to_nat(p, t) : Nat}

# the carry case: 2 + 2 N(t) < 2^d, so d = 1 + d' with 1 + N(t) < 2^d'
def ninc.t(+p: Nat, +t: Word(p), +d: Nat, hd: {Nat.is_le(d, 1n+p) == True{} : Bool},
  h: {Nat.is_lt(2n+Nat.double(Word.to_nat(p, t)), pow2(d)) == True{} : Bool}, ih: NIncT(p, t)
) -> {Nat.double(Word.to_nat(p, Word.inc(p, t))) == 2n+Nat.double(Word.to_nat(p, t)) : Nat}:
  match d:
    case 0n:
      L.absurd({Nat.double(Word.to_nat(p, Word.inc(p, t))) == 2n+Nat.double(Word.to_nat(p, t)) : Nat}, L.ft(h))
    case 1n+q:
      +u = Word.to_nat(p, t)
      %Equal.sym(Nat, Word.to_nat(p, Word.inc(p, t)), 1n+u, ih(q, hd, dbl_lt(1n+u, pow2(q), h))) :
        {Nat.double(_) == 2n+Nat.double(u) : Nat}
      {==}

# inc adds one when it does not wrap: 1 + N(w) < 2^d, d <= m
def ninc(+m: Nat, w: Word(m), +d: Nat, hd: {Nat.is_le(d, m) == True{} : Bool},
  h: {Nat.is_lt(1n+Word.to_nat(m, w), pow2(d)) == True{} : Bool}
) -> {Word.to_nat(m, Word.inc(m, w)) == 1n+Word.to_nat(m, w) : Nat}:
  match m:
    case 0n:
      match w:
        case WNil{}:
          L.absurd({Word.to_nat(0n, Word.inc(0n, WNil{})) == 1n : Nat}, ninc.z(d, hd, h))
    case 1n+p:
      match w:
        case WCon{False{}, +t}:
          {==}
        case WCon{True{}, +t}:
          ninc.t(p, t, d, hd, h, dd => hh => h2 => ninc(p, t, dd, hh, h2))

def uinc(u: U32, +d: Nat, hd: {Nat.is_le(d, 32n) == True{} : Bool},
  h: {Nat.is_lt(1n+U32.to_nat(u), pow2(d)) == True{} : Bool}
) -> {U32.to_nat(U32.inc(u)) == 1n+U32.to_nat(u) : Nat}:
  match u:
    case U32{x}:
      ninc(32n, x, d, hd, h)

def lt1_conv(-x: Nat, -y: Nat, -b: Nat, e: {x == y : Nat}, h: {Nat.is_lt(1n+y, b) == True{} : Bool}) ->
  {Nat.is_lt(1n+x, b) == True{} : Bool}:
  %Equal.sym(Nat, x, y, e) : {Nat.is_lt(1n+_, b) == True{} : Bool}
  h

# U32.from_nat(i) is i, for i < 2^d, d <= 32
def nfrom(+i: Nat, +d: Nat, +hd: {Nat.is_le(d, 32n) == True{} : Bool},
  +h: {Nat.is_lt(i, pow2(d)) == True{} : Bool}
) -> {U32.to_nat(U32.from_nat(i)) == i : Nat}:
  match i:
    case 0n:
      {==}
    case 1n+p:
      +e1 = nfrom(p, d, hd, L.lt_pred(p, pow2(d), h))
      Equal.trans(Nat, U32.to_nat(U32.inc(U32.from_nat(p))), 1n+U32.to_nat(U32.from_nat(p)), 1n+p,
        uinc(U32.from_nat(p), d, hd, lt1_conv(U32.to_nat(U32.from_nat(p)), p, pow2(d), e1, h)),
        Equal.cong(Nat, Nat, x => 1n+x, U32.to_nat(U32.from_nat(p)), p, e1))

# Subtraction
# -----------

def sub_id(+m: Nat, a: Word(m)) -> {Word.adc(m, a, Word.zero(m), True{}, True{}) == a : Word(m)}:
  match m:
    case 0n:
      match a:
        case WNil{}:
          {==}
    case 1n+p:
      match a:
        case WCon{False{}, +t}:
          %Equal.sym(Word(p), Word.adc(p, t, Word.zero(p), True{}, True{}), t, sub_id(p, t)) :
            {WCon{False{}, _} == WCon{False{}, t} : Word(1n+p)}
          {==}
        case WCon{True{}, +t}:
          %Equal.sym(Word(p), Word.adc(p, t, Word.zero(p), True{}, True{}), t, sub_id(p, t)) :
            {WCon{True{}, _} == WCon{True{}, t} : Word(1n+p)}
          {==}

def le1_dbl(u: Nat, h: {Nat.is_le(1n, Nat.double(u)) == True{} : Bool}) -> {Nat.is_le(1n, u) == True{} : Bool}:
  match u:
    case 0n:
      L.absurd({Nat.is_le(1n, 0n) == True{} : Bool}, L.ft(h))
    case 1n+q:
      L.le0(q)

# a - 1, for a >= 1
def dec(+m: Nat, a: Word(m), h: {Nat.is_le(1n, Word.to_nat(m, a)) == True{} : Bool}) ->
  {Word.to_nat(m, Word.adc(m, a, Word.zero(m), True{}, False{})) == Nat.sub(Word.to_nat(m, a), 1n) : Nat}:
  match m:
    case 0n:
      match a:
        case WNil{}:
          L.absurd({0n == 0n : Nat}, L.ft(h))
    case 1n+p:
      match a:
        case WCon{True{}, +t}:
          %Equal.sym(Word(p), Word.adc(p, t, Word.zero(p), True{}, True{}), t, sub_id(p, t)) :
            {Nat.double(Word.to_nat(p, _)) == Nat.sub(Nat.double(Word.to_nat(p, t)), 0n) : Nat}
          +d = Nat.double(Word.to_nat(p, t))
          %Equal.sym(Nat, Nat.sub(d, 0n), d, sub0(d)) : {d == _ : Nat}
          {==}
        case WCon{False{}, +t}:
          +u = Word.to_nat(p, t)
          +hu = le1_dbl(u, h)
          %Equal.sym(Nat, Word.to_nat(p, Word.adc(p, t, Word.zero(p), True{}, False{})), Nat.sub(u, 1n),
            dec(p, t, hu)) :
            {1n+Nat.double(_) == Nat.sub(Nat.double(u), 1n) : Nat}
          dsub_one(u, hu)

# a - 2^e, for 2^e <= a
def subP(+m: Nat, +e: Nat, a: Word(m), h1: {Nat.is_lt(e, m) == True{} : Bool},
  h2: {Nat.is_le(pow2(e), Word.to_nat(m, a)) == True{} : Bool}
) -> {Word.to_nat(m, Word.sub(m, a, P(m, e))) == Nat.sub(Word.to_nat(m, a), pow2(e)) : Nat}:
  match m:
    case 0n:
      L.absurd({Word.to_nat(0n, Word.sub(0n, a, P(0n, e))) == Nat.sub(Word.to_nat(0n, a), pow2(e)) : Nat},
        L.not_lt0(e, h1))
    case 1n+p:
      match e a:
        case 0n WCon{False{}, +t}:
          +u = Word.to_nat(p, t)
          +hu = le1_dbl(u, h2)
          %Equal.sym(Nat, Word.to_nat(p, Word.adc(p, t, Word.zero(p), True{}, False{})), Nat.sub(u, 1n),
            dec(p, t, hu)) :
            {1n+Nat.double(_) == Nat.sub(Nat.double(u), 1n) : Nat}
          dsub_one(u, hu)
        case 0n WCon{True{}, +t}:
          %Equal.sym(Word(p), Word.adc(p, t, Word.zero(p), True{}, True{}), t, sub_id(p, t)) :
            {Nat.double(Word.to_nat(p, _)) == Nat.sub(Nat.double(Word.to_nat(p, t)), 0n) : Nat}
          +d = Nat.double(Word.to_nat(p, t))
          %Equal.sym(Nat, Nat.sub(d, 0n), d, sub0(d)) : {d == _ : Nat}
          {==}
        case 1n+f WCon{False{}, +t}:
          +u = Word.to_nat(p, t)
          %Equal.sym(Nat, Word.to_nat(p, Word.sub(p, t, P(p, f))), Nat.sub(u, pow2(f)),
            subP(p, f, t, h1, dbl_le(pow2(f), u, h2))) :
            {Nat.double(_) == Nat.sub(Nat.double(u), Nat.double(pow2(f))) : Nat}
          dsub(u, pow2(f))
        case 1n+f WCon{True{}, +t}:
          +u = Word.to_nat(p, t)
          +hu = dbl_le1(pow2(f), u, h2)
          %Equal.sym(Nat, Word.to_nat(p, Word.sub(p, t, P(p, f))), Nat.sub(u, pow2(f)),
            subP(p, f, t, h1, hu)) :
            {1n+Nat.double(_) == Nat.sub(1n+Nat.double(u), Nat.double(pow2(f))) : Nat}
          dsub1(u, pow2(f), hu)

# Masks
# -----

def andz(+m: Nat, w: Word(m)) -> {Word.and(m, w, Word.zero(m)) == Word.zero(m) : Word(m)}:
  match m:
    case 0n:
      match w:
        case WNil{}:
          {==}
    case 1n+p:
      match w:
        case WCon{False{}, +t}:
          %Equal.sym(Word(p), Word.and(p, t, Word.zero(p)), Word.zero(p), andz(p, t)) :
            {WCon{False{}, _} == Word.zero(1n+p) : Word(1n+p)}
          {==}
        case WCon{True{}, +t}:
          %Equal.sym(Word(p), Word.and(p, t, Word.zero(p)), Word.zero(p), andz(p, t)) :
            {WCon{False{}, _} == Word.zero(1n+p) : Word(1n+p)}
          {==}

def dbl0(u: Nat, h: {Nat.is_lt(Nat.double(u), 1n) == True{} : Bool}) -> {0n == u : Nat}:
  match u:
    case 0n:
      {==}
    case 1n+q:
      L.absurd({0n == 1n+q : Nat}, L.ft(h))

# masking with the low d bits keeps a number below 2^d
def nand(+m: Nat, +d: Nat, w: Word(m), h: {Nat.is_lt(Word.to_nat(m, w), pow2(d)) == True{} : Bool}) ->
  {Word.to_nat(m, Word.and(m, w, ones(m, d))) == Word.to_nat(m, w) : Nat}:
  match m:
    case 0n:
      match w:
        case WNil{}:
          {==}
    case 1n+p:
      match d w:
        case 0n WCon{True{}, +t}:
          L.absurd({Word.to_nat(1n+p, Word.and(1n+p, WCon{True{}, t}, ones(1n+p, 0n))) ==
            1n+Nat.double(Word.to_nat(p, t)) : Nat}, L.not_lt0(Nat.double(Word.to_nat(p, t)), h))
        case 0n WCon{False{}, +t}:
          +u = Word.to_nat(p, t)
          %dbl0(u, h) : {Nat.double(Word.to_nat(p, Word.and(p, t, Word.zero(p)))) == Nat.double(_) : Nat}
          %Equal.sym(Word(p), Word.and(p, t, Word.zero(p)), Word.zero(p), andz(p, t)) :
            {Nat.double(Word.to_nat(p, _)) == 0n : Nat}
          %Equal.sym(Nat, Word.to_nat(p, Word.zero(p)), 0n, zN(p)) : {Nat.double(_) == 0n : Nat}
          {==}
        case 1n+f WCon{False{}, +t}:
          +u = Word.to_nat(p, t)
          %Equal.sym(Nat, Word.to_nat(p, Word.and(p, t, ones(p, f))), u, nand(p, f, t, dbl_lt(u, pow2(f), h))) :
            {Nat.double(_) == Nat.double(u) : Nat}
          {==}
        case 1n+f WCon{True{}, +t}:
          +u = Word.to_nat(p, t)
          %Equal.sym(Nat, Word.to_nat(p, Word.and(p, t, ones(p, f))), u, nand(p, f, t, dbl1_lt(u, pow2(f), h))) :
            {1n+Nat.double(_) == 1n+Nat.double(u) : Nat}
          {==}

# 2^f - 1 is the mask of the low f bits
def decP(+m: Nat, +f: Nat, h: {Nat.is_lt(f, m) == True{} : Bool}) ->
  {Word.adc(m, P(m, f), Word.zero(m), True{}, False{}) == ones(m, f) : Word(m)}:
  match m f:
    case 0n _:
      L.absurd({Word.adc(0n, P(0n, f), Word.zero(0n), True{}, False{}) == ones(0n, f) : Word(0n)}, L.not_lt0(f, h))
    case 1n+q 0n:
      %Equal.sym(Word(q), Word.adc(q, Word.zero(q), Word.zero(q), True{}, True{}), Word.zero(q),
        sub_id(q, Word.zero(q))) : {WCon{False{}, _} == WCon{False{}, Word.zero(q)} : Word(1n+q)}
      {==}
    case 1n+q 1n+g:
      %Equal.sym(Word(q), Word.adc(q, P(q, g), Word.zero(q), True{}, False{}), ones(q, g), decP(q, g, h)) :
        {WCon{True{}, _} == WCon{True{}, ones(q, g)} : Word(1n+q)}
      {==}

# the size 2^d minus one is the mask of the low d bits
def msk(+m: Nat, +d: Nat, h: {Nat.is_lt(d, m) == True{} : Bool}) ->
  {Word.sub(m, P(m, d), P(m, 0n)) == ones(m, d) : Word(m)}:
  match m d:
    case 0n _:
      L.absurd({Word.sub(0n, P(0n, d), P(0n, 0n)) == ones(0n, d) : Word(0n)}, L.not_lt0(d, h))
    case 1n+p 0n:
      %Equal.sym(Word(p), Word.adc(p, Word.zero(p), Word.zero(p), True{}, True{}), Word.zero(p),
        sub_id(p, Word.zero(p))) : {WCon{False{}, _} == WCon{False{}, Word.zero(p)} : Word(1n+p)}
      {==}
    case 1n+p 1n+f:
      %Equal.sym(Word(p), Word.adc(p, P(p, f), Word.zero(p), True{}, False{}), ones(p, f), decP(p, f, h)) :
        {WCon{True{}, _} == WCon{True{}, ones(p, f)} : Word(1n+p)}
      {==}

# Shifts
# ------

def spZ(+p: Nat) -> {Word.shl.put(p, False{}, Word.zero(p)) == Word.zero(p) : Word(p)}:
  match p:
    case 0n:
      {==}
    case 1n+q:
      %Equal.sym(Word(q), Word.shl.put(q, False{}, Word.zero(q)), Word.zero(q), spZ(q)) :
        {WCon{False{}, _} == Word.zero(1n+q) : Word(1n+q)}
      {==}

def spT(+q: Nat) -> {Word.shl.put(1n+q, True{}, Word.zero(1n+q)) == P(1n+q, 0n) : Word(1n+q)}:
  %Equal.sym(Word(q), Word.shl.put(q, False{}, Word.zero(q)), Word.zero(q), spZ(q)) :
    {WCon{True{}, _} == P(1n+q, 0n) : Word(1n+q)}
  {==}

# shl.put shifts c in below 2^f
def spP(+q: Nat, +f: Nat, +c: Bool, h: {Nat.is_lt(f, q) == True{} : Bool}) ->
  {Word.shl.put(1n+q, c, P(1n+q, f)) == WCon{c, P(q, f)} : Word(1n+q)}:
  match q f:
    case 0n _:
      L.absurd({Word.shl.put(1n, c, P(1n, f)) == WCon{c, P(0n, f)} : Word(1n)}, L.not_lt0(f, h))
    case 1n+r 0n:
      %Equal.sym(Word(1n+r), Word.shl.put(1n+r, True{}, Word.zero(1n+r)), P(1n+r, 0n), spT(r)) :
        {WCon{c, _} == WCon{c, P(1n+r, 0n)} : Word(2n+r)}
      {==}
    case 1n+r 1n+g:
      %Equal.sym(Word(1n+r), Word.shl.put(1n+r, False{}, P(1n+r, g)), WCon{False{}, P(r, g)},
        spP(r, g, False{}, h)) :
        {WCon{c, _} == WCon{c, P(1n+r, 1n+g)} : Word(2n+r)}
      {==}

# shl doubles 2^e below the top bit
def shlP(+p: Nat, +e: Nat, h: {Nat.is_lt(e, p) == True{} : Bool}) ->
  {Word.shl(1n+p, P(1n+p, e)) == P(1n+p, 1n+e) : Word(1n+p)}:
  match p e:
    case 0n _:
      L.absurd({Word.shl(1n, P(1n, e)) == P(1n, 1n+e) : Word(1n)}, L.not_lt0(e, h))
    case 1n+r 0n:
      %Equal.sym(Word(1n+r), Word.shl.put(1n+r, True{}, Word.zero(1n+r)), P(1n+r, 0n), spT(r)) :
        {WCon{False{}, _} == P(2n+r, 1n) : Word(2n+r)}
      {==}
    case 1n+r 1n+f:
      %Equal.sym(Word(1n+r), Word.shl.put(1n+r, False{}, P(1n+r, f)), WCon{False{}, P(r, f)},
        spP(r, f, False{}, h)) :
        {WCon{False{}, _} == P(2n+r, 2n+f) : Word(2n+r)}
      {==}

def padZ(+p: Nat) -> {Word.shr.pad(p, Word.zero(p)) == Word.zero(1n+p) : Word(1n+p)}:
  match p:
    case 0n:
      {==}
    case 1n+q:
      %Equal.sym(Word(1n+q), Word.shr.pad(q, Word.zero(q)), Word.zero(1n+q), padZ(q)) :
        {WCon{False{}, _} == Word.zero(2n+q) : Word(2n+q)}
      {==}

def padP(+p: Nat, +f: Nat, h: {Nat.is_lt(f, p) == True{} : Bool}) ->
  {Word.shr.pad(p, P(p, f)) == P(1n+p, f) : Word(1n+p)}:
  match p f:
    case 0n _:
      L.absurd({Word.shr.pad(0n, P(0n, f)) == P(1n, f) : Word(1n)}, L.not_lt0(f, h))
    case 1n+q 0n:
      %Equal.sym(Word(1n+q), Word.shr.pad(q, Word.zero(q)), Word.zero(1n+q), padZ(q)) :
        {WCon{True{}, _} == P(2n+q, 0n) : Word(2n+q)}
      {==}
    case 1n+q 1n+g:
      %Equal.sym(Word(1n+q), Word.shr.pad(q, P(q, g)), P(1n+q, g), padP(q, g, h)) :
        {WCon{False{}, _} == P(2n+q, 1n+g) : Word(2n+q)}
      {==}

# U32
# ---

# the U32 2^e
def PU(+e: Nat) -> U32:
  U32{P(32n, e)}

def ult(a: U32, b: U32) -> {U32.is_lt(a, b) == Nat.is_lt(U32.to_nat(a), U32.to_nat(b)) : Bool}:
  match a b:
    case U32{x} U32{y}:
      %Equal.sym(Cmp, Word.cmp(32n, x, y), Nat.cmp(Word.to_nat(32n, x), Word.to_nat(32n, y)), wcmp(32n, x, y)) :
        {Cmp.is_lt(_) == Nat.is_lt(Word.to_nat(32n, x), Word.to_nat(32n, y)) : Bool}
      {==}

def uP(+e: Nat, h: {Nat.is_lt(e, 32n) == True{} : Bool}) -> {U32.to_nat(PU(e)) == pow2(e) : Nat}:
  pN(32n, e, h)

def usub(a: U32, +f: Nat, h1: {Nat.is_lt(f, 32n) == True{} : Bool},
  h2: {Nat.is_le(pow2(f), U32.to_nat(a)) == True{} : Bool}
) -> {U32.to_nat(U32.sub(a, PU(f))) == Nat.sub(U32.to_nat(a), pow2(f)) : Nat}:
  match a:
    case U32{x}:
      subP(32n, f, x, h1, h2)

def uand(a: U32, +d: Nat, h: {Nat.is_lt(U32.to_nat(a), pow2(d)) == True{} : Bool}) ->
  {U32.to_nat(U32.and(a, U32{ones(32n, d)})) == U32.to_nat(a) : Nat}:
  match a:
    case U32{x}:
      nand(32n, d, x, h)

# shr halves 2^(1 + f)
def ushr(+f: Nat, h: {Nat.is_lt(f, 31n) == True{} : Bool}) -> {U32.shr(PU(1n+f)) == PU(f) : U32}:
  Equal.cong(Word(32n), U32, w => U32{w}, Word.shr.pad(31n, P(31n, f)), P(32n, f), padP(31n, f, h))

# shl doubles 2^e
def ushl(+e: Nat, h: {Nat.is_lt(e, 31n) == True{} : Bool}) -> {U32.shl(PU(e)) == PU(1n+e) : U32}:
  Equal.cong(Word(32n), U32, w => U32{w}, Word.shl(32n, P(32n, e)), P(32n, 1n+e), shlP(31n, e, h))

# 2^d - 1 is the mask of the low d bits
def umsk(+d: Nat, h: {Nat.is_lt(d, 32n) == True{} : Bool}) -> {U32.sub(PU(d), 1) == U32{ones(32n, d)} : U32}:
  Equal.cong(Word(32n), U32, w => U32{w}, Word.sub(32n, P(32n, d), P(32n, 0n)), ones(32n, d), msk(32n, d, h))
