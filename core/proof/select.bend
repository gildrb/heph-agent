import Base
import ../core.bend as C
import ../LAWS.bend as Laws
import ../score.bend as F
import ./logic.bend as L
import ./array.bend as A

# Ranking: the top-k selection
# ============================
# The core visits the score slots in id order and keeps the best k hits.

# n slots from i; r holds slot i
def slots(n: Nat, +i: Nat, r: Array<Nat> & Nat) -> List<&2, Nat>:
  match n:
    case 0n:
      Nil{}
    case 1n+m:
      (s, +v) = r
      v <> slots(m, 1n+i, Array.get(Nat, s, U32.from_nat(1n+i)))

# Ranking: selection
# ==================
# msel is the selection over the list of slot values; the core's loop reads
# the same values from the score slots. One walk of the slots yields them as
# a list (a copy the proofs may use freely).

def msel(xs: List<&2, Nat>, +k: Nat, +i: Nat, +c: Nat, hs: List<&2, C.Hit>) -> List<&2, C.Hit>:
  match xs:
    case Nil{}:
      hs
    case Con{v, t}:
      match v:
        case 0n:
          msel(t, k, 1n+i, c, hs)
        case 1n+w:
          +room = Nat.is_lt(c, k)
          msel(t, k, 1n+i, F.pick(room, 1n+c, c), C.keep(room, C.Hit{1n+w, i}, hs))

def BT(-fuel: Nat, -k: Nat, -i: Nat, -c: Nat, -hs: List<&2, C.Hit>, -r: Array<Nat> & Nat,
  -p2: Array<U32>, -lv: List<&2, Nat>
) -> Type:
  Pair(Pair({slots(fuel, i, r) == lv : List<&2, Nat>},
    {List.length(&2, Nat, lv) == fuel : Nat}),
    {Laws.Spec.hits(C.rank.fin(p2, C.sel(fuel, k, i, C.sel.of(c, hs, r)))) ==
      List.reverse(&2, C.Hit, msel(lv, k, i, c, hs)) : List<&2, C.Hit>})

def BS(-fuel: Nat, -k: Nat, -i: Nat, -c: Nat, -hs: List<&2, C.Hit>, -r: Array<Nat> & Nat,
  -p2: Array<U32>
) -> Type:
  &lv: List<&2, Nat> -> BT(fuel, k, i, c, hs, r, p2, lv)

def cons_eq(-A: Data, -v: A, -x: List<&2, A>, -y: List<&2, A>, e: {x == y : List<&2, A>}) ->
  {v <> x == v <> y : List<&2, A>}:
  %e : {v <> x == v <> _ : List<&2, A>}
  {==}

def len_cons(-v: Nat, -lt: List<&2, Nat>, -f: Nat, e: {List.length(&2, Nat, lt) == f : Nat}) ->
  {List.length(&2, Nat, v <> lt) == 1n+f : Nat}:
  %e : {1n+List.length(&2, Nat, lt) == 1n+_ : Nat}
  {==}

def hits_step(-x: List<&2, C.Hit>, -m1: List<&2, C.Hit>, -m2: List<&2, C.Hit>,
  e2: {x == List.reverse(&2, C.Hit, m2) : List<&2, C.Hit>}, em: {m1 == m2 : List<&2, C.Hit>}
) -> {x == List.reverse(&2, C.Hit, m1) : List<&2, C.Hit>}:
  %Equal.sym(List<&2, C.Hit>, m1, m2, em) : {x == List.reverse(&2, C.Hit, _) : List<&2, C.Hit>}
  e2

# msel over v then lt is msel over lt from the state after v
def MStep(-v: Nat, -k: Nat, -i: Nat, -c: Nat, -hs: List<&2, C.Hit>, -c2: Nat,
  -hs2: List<&2, C.Hit>
) -> Type:
  @lt: List<&2, Nat> -> {msel(v <> lt, k, i, c, hs) == msel(lt, k, 1n+i, c2, hs2) : List<&2, C.Hit>}

# one more slot of value v in front
def b_cons(-f: Nat, -k: Nat, -i: Nat, -c: Nat, -hs: List<&2, C.Hit>, -c2: Nat,
  -hs2: List<&2, C.Hit>, -s: Array<Nat>, +v: Nat, -p2: Array<U32>,
  ih: BS(f, k, 1n+i, c2, hs2, Array.get(Nat, s, U32.from_nat(1n+i)), p2),
  em: MStep(v, k, i, c, hs, c2, hs2)
) -> &lv: List<&2, Nat> ->
  Pair(Pair({v <> slots(f, 1n+i, Array.get(Nat, s, U32.from_nat(1n+i))) == lv :
    List<&2, Nat>}, {List.length(&2, Nat, lv) == 1n+f : Nat}),
    {Laws.Spec.hits(C.rank.fin(p2, C.sel(f, k, 1n+i,
      C.sel.of(c2, hs2, Array.get(Nat, s, U32.from_nat(1n+i)))))) ==
      List.reverse(&2, C.Hit, msel(lv, k, i, c, hs)) : List<&2, C.Hit>}):
  (+lt, pr) = ih
  (pa, e2) = pr
  (e1, e3) = pa
  (v <> lt, ((cons_eq(Nat, v, slots(f, 1n+i, Array.get(Nat, s, U32.from_nat(1n+i))), lt, e1),
    len_cons(v, lt, f, e3)),
    hits_step(Laws.Spec.hits(C.rank.fin(p2, C.sel(f, k, 1n+i,
      C.sel.of(c2, hs2, Array.get(Nat, s, U32.from_nat(1n+i)))))),
      msel(v <> lt, k, i, c, hs), msel(lt, k, 1n+i, c2, hs2), e2, em(lt))))

# the core's selection over the slots from i is msel over their values
def bsel(fuel: Nat, +k: Nat, +i: Nat, +c: Nat, +hs: List<&2, C.Hit>, r: Array<Nat> & Nat,
  -p2: Array<U32>
) -> BS(fuel, k, i, c, hs, r, p2):
  match fuel:
    case 0n:
      match r:
        case (s, v):
          (Nil{}, (({==}, {==}), {==}))
    case 1n+f:
      match r:
        case (s, +v):
          match v:
            case 0n:
              b_cons(f, k, i, c, hs, c, hs, s, 0n, p2,
                bsel(f, k, 1n+i, c, hs, Array.get(Nat, s, U32.from_nat(1n+i)), p2),
                lt => {==})
            case 1n+w:
              +room = Nat.is_lt(c, k)
              +c2 = F.pick(room, 1n+c, c)
              +hs2 = C.keep(room, C.Hit{1n+w, i}, hs)
              b_cons(f, k, i, c, hs, c2, hs2, s, 1n+w, p2,
                bsel(f, k, 1n+i, c2, hs2, Array.get(Nat, s, U32.from_nat(1n+i)), p2),
                lt => {==})

# Ranking: the kept hits
# ======================
# The kept hits sit worst first: every later hit comes before every earlier
# one (sw). A new hit h goes in after the hits it comes before.

def or_l(a: Bool, -b: Bool, +e: {a == True{} : Bool}) -> {Bool.or(a, b) == True{} : Bool}:
  %Equal.sym(Bool, a, True{}, e) : {Bool.or(_, b) == True{} : Bool}
  {==}

def bef_eq(x: C.Hit, y: C.Hit) -> {C.before(x, y) == Laws.Spec.before(x, y) : Bool}:
  match x y:
    case C.Hit{+sa, +ia} C.Hit{+sb, +ib}:
      {==}

# every hit of hs comes before x
def ab(hs: List<&2, C.Hit>, +x: C.Hit) -> Bool:
  match hs:
    case Nil{}:
      True{}
    case Con{h, t}:
      Bool.and(C.before(h, x), ab(t, x))

def sw(hs: List<&2, C.Hit>) -> Bool:
  match hs:
    case Nil{}:
      True{}
    case Con{+x, +t}:
      Bool.and(ab(t, x), sw(t))

def mins(+hs: List<&2, C.Hit>, +h: C.Hit) -> List<&2, C.Hit>:
  C.ins(hs, h, C.lead(h, hs), Nil{})

def ins_acc(+hs: List<&2, C.Hit>, +h: C.Hit, g: Bool, +acc: List<&2, C.Hit>) ->
  {C.ins(hs, h, g, acc) == List.reverse.go(&2, C.Hit, acc, C.ins(hs, h, g, Nil{})) :
    List<&2, C.Hit>}:
  match hs g:
    case Nil{} _:
      {==}
    case Con{+x, +t} False{}:
      {==}
    case Con{+x, +t} True{}:
      +g2 = C.lead(h, t)
      %Equal.sym(List<&2, C.Hit>, C.ins(t, h, g2, [x]),
        List.reverse.go(&2, C.Hit, [x], C.ins(t, h, g2, Nil{})), ins_acc(t, h, g2, [x])) :
        {C.ins(t, h, g2, x <> acc) == List.reverse.go(&2, C.Hit, acc, _) : List<&2, C.Hit>}
      ins_acc(t, h, g2, x <> acc)

def mins_go(+x: C.Hit, +t: List<&2, C.Hit>, +h: C.Hit, g: Bool,
  +e: {C.before(h, x) == g : Bool}
) -> {mins(x <> t, h) == C.ins(x <> t, h, g, Nil{}) : List<&2, C.Hit>}:
  %e : {mins(x <> t, h) == C.ins(x <> t, h, _, Nil{}) : List<&2, C.Hit>}
  {==}

# h comes before x: x stays in front
def mins_pass(+x: C.Hit, +t: List<&2, C.Hit>, +h: C.Hit, +e: {C.before(h, x) == True{} : Bool}) ->
  {mins(x <> t, h) == x <> mins(t, h) : List<&2, C.Hit>}:
  %Equal.sym(List<&2, C.Hit>, mins(x <> t, h), C.ins(x <> t, h, True{}, Nil{}),
    mins_go(x, t, h, True{}, e)) : {_ == x <> mins(t, h) : List<&2, C.Hit>}
  ins_acc(t, h, C.lead(h, t), [x])

# h does not come before x: h goes in front
def mins_stop(+x: C.Hit, +t: List<&2, C.Hit>, +h: C.Hit, +e: {C.before(h, x) == False{} : Bool}) ->
  {mins(x <> t, h) == h <> x <> t : List<&2, C.Hit>}:
  %Equal.sym(List<&2, C.Hit>, mins(x <> t, h), C.ins(x <> t, h, False{}, Nil{}),
    mins_go(x, t, h, False{}, e)) : {_ == h <> x <> t : List<&2, C.Hit>}
  {==}

def or_false(b: Bool, -c: Bool, +eq: {b == False{} : Bool}, +e: {Bool.or(b, c) == True{} : Bool}) ->
  {c == True{} : Bool}:
  match b:
    case False{}:
      e
    case True{}:
      L.absurd({c == True{} : Bool}, L.tf(eq))

def hid(h: C.Hit) -> Nat:
  match h:
    case C.Hit{s, i}:
      i

# rewrites mins(x <> t, h) by the case on before(h, x)
def MCase(-x: C.Hit, -t: List<&2, C.Hit>, -h: C.Hit, -P: List<&2, C.Hit> -> Type) -> Type:
  Pair(@e: {C.before(h, x) == True{} : Bool} -> P(x <> mins(t, h)),
    @e: {C.before(h, x) == False{} : Bool} -> P(h <> x <> t))

def mcase.go(+x: C.Hit, +t: List<&2, C.Hit>, +h: C.Hit, g: Bool, +e: {C.before(h, x) == g : Bool},
  -P: List<&2, C.Hit> -> Type, k: MCase(x, t, h, P)
) -> P(mins(x <> t, h)):
  match g:
    case True{}:
      (kt, kf) = k
      %Equal.sym(List<&2, C.Hit>, mins(x <> t, h), x <> mins(t, h), mins_pass(x, t, h, e)) : P(_)
      kt(e)
    case False{}:
      (kt, kf) = k
      %Equal.sym(List<&2, C.Hit>, mins(x <> t, h), h <> x <> t, mins_stop(x, t, h, e)) : P(_)
      kf(e)

def mcase(+x: C.Hit, +t: List<&2, C.Hit>, +h: C.Hit, -P: List<&2, C.Hit> -> Type,
  k: MCase(x, t, h, P)
) -> P(mins(x <> t, h)):
  mcase.go(x, t, h, C.before(h, x), {==}, P, k)

def succ_eq(-a: Nat, -b: Nat, +e: {a == b : Nat}) -> {1n+a == 1n+b : Nat}:
  %e : {1n+a == 1n+_ : Nat}
  {==}

def mins_len(+hs: List<&2, C.Hit>, +h: C.Hit) ->
  {List.length(&2, C.Hit, mins(hs, h)) == 1n+List.length(&2, C.Hit, hs) : Nat}:
  match hs:
    case Nil{}:
      {==}
    case Con{+x, +t}:
      mcase(x, t, h, l => {List.length(&2, C.Hit, l) == 2n+List.length(&2, C.Hit, t) : Nat},
        (e => succ_eq(List.length(&2, C.Hit, mins(t, h)), 1n+List.length(&2, C.Hit, t), mins_len(t, h)),
          e => {==}))

def has_cons.go(+ix: Nat, -sx: Nat, -t: List<&2, C.Hit>, -t2: List<&2, C.Hit>, +j: Nat, b: Bool,
  +eb: {Nat.is_eq(ix, j) == b : Bool},
  +e: {Laws.Spec.has_id(C.Hit{sx, ix} <> t, j) == True{} : Bool},
  f: @h: {Laws.Spec.has_id(t, j) == True{} : Bool} -> {Laws.Spec.has_id(t2, j) == True{} : Bool}
) -> {Laws.Spec.has_id(C.Hit{sx, ix} <> t2, j) == True{} : Bool}:
  match b:
    case True{}:
      or_l(Nat.is_eq(ix, j), Laws.Spec.has_id(t2, j), eb)
    case False{}:
      L.or_r(Nat.is_eq(ix, j), Laws.Spec.has_id(t2, j),
        f(or_false(Nat.is_eq(ix, j), Laws.Spec.has_id(t, j), eb, e)))

def has_cons(+ix: Nat, -sx: Nat, -t: List<&2, C.Hit>, -t2: List<&2, C.Hit>, +j: Nat,
  +e: {Laws.Spec.has_id(C.Hit{sx, ix} <> t, j) == True{} : Bool},
  f: @h: {Laws.Spec.has_id(t, j) == True{} : Bool} -> {Laws.Spec.has_id(t2, j) == True{} : Bool}
) -> {Laws.Spec.has_id(C.Hit{sx, ix} <> t2, j) == True{} : Bool}:
  has_cons.go(ix, sx, t, t2, j, Nat.is_eq(ix, j), {==}, e, f)

def has_self(-sh: Nat, +ih: Nat, -t: List<&2, C.Hit>) ->
  {Laws.Spec.has_id(C.Hit{sh, ih} <> t, ih) == True{} : Bool}:
  or_l(Nat.is_eq(ih, ih), Laws.Spec.has_id(t, ih), L.eq_refl(ih))

# the new hit is kept
def mins_new(+hs: List<&2, C.Hit>, +sh: Nat, +ih: Nat) ->
  {Laws.Spec.has_id(mins(hs, C.Hit{sh, ih}), ih) == True{} : Bool}:
  match hs:
    case Nil{}:
      has_self(sh, ih, Nil{})
    case Con{C.Hit{+sx, +ix}, +t}:
      mcase(C.Hit{sx, ix}, t, C.Hit{sh, ih}, l => {Laws.Spec.has_id(l, ih) == True{} : Bool},
        (e => L.or_r(Nat.is_eq(ix, ih), Laws.Spec.has_id(mins(t, C.Hit{sh, ih}), ih), mins_new(t, sh, ih)),
          e => has_self(sh, ih, C.Hit{sx, ix} <> t)))

# every kept hit stays
def mins_old(+hs: List<&2, C.Hit>, +sh: Nat, +ih: Nat, +j: Nat,
  +e: {Laws.Spec.has_id(hs, j) == True{} : Bool}
) -> {Laws.Spec.has_id(mins(hs, C.Hit{sh, ih}), j) == True{} : Bool}:
  match hs:
    case Nil{}:
      L.absurd({Laws.Spec.has_id(mins(Nil{}, C.Hit{sh, ih}), j) == True{} : Bool}, L.ft(e))
    case Con{C.Hit{+sx, +ix}, +t}:
      mcase(C.Hit{sx, ix}, t, C.Hit{sh, ih}, l => {Laws.Spec.has_id(l, j) == True{} : Bool},
        (g => has_cons(ix, sx, t, mins(t, C.Hit{sh, ih}), j, e, h => mins_old(t, sh, ih, j, h)),
          g => L.or_r(Nat.is_eq(ih, j), Laws.Spec.has_id(C.Hit{sx, ix} <> t, j), e)))

def mins_lt(+hs: List<&2, C.Hit>, +sh: Nat, +ih: Nat, +n: Nat,
  +e1: {Laws.Spec.all_lt(hs, n) == True{} : Bool}, +e2: {Nat.is_lt(ih, n) == True{} : Bool}
) -> {Laws.Spec.all_lt(mins(hs, C.Hit{sh, ih}), n) == True{} : Bool}:
  match hs:
    case Nil{}:
      L.and_i(Nat.is_lt(ih, n), True{}, e2, {==})
    case Con{C.Hit{+sx, +ix}, +t}:
      mcase(C.Hit{sx, ix}, t, C.Hit{sh, ih}, l => {Laws.Spec.all_lt(l, n) == True{} : Bool},
        (g => L.and_i(Nat.is_lt(ix, n), Laws.Spec.all_lt(mins(t, C.Hit{sh, ih}), n),
            L.and_l(Nat.is_lt(ix, n), Laws.Spec.all_lt(t, n), e1),
            mins_lt(t, sh, ih, n, L.and_r(Nat.is_lt(ix, n), Laws.Spec.all_lt(t, n), e1), e2)),
          g => L.and_i(Nat.is_lt(ih, n), Laws.Spec.all_lt(C.Hit{sx, ix} <> t, n), e2, e1)))

def mins_sc(+xs: List<&2, Nat>, +hs: List<&2, C.Hit>, +sh: Nat, +ih: Nat,
  +e1: {Laws.Spec.all_scored(xs, hs) == True{} : Bool},
  +e2: {Laws.Spec.scored.at(List.get(&2, Nat, xs, ih), sh) == True{} : Bool}
) -> {Laws.Spec.all_scored(xs, mins(hs, C.Hit{sh, ih})) == True{} : Bool}:
  match hs:
    case Nil{}:
      L.and_i(Laws.Spec.scored.at(List.get(&2, Nat, xs, ih), sh), True{}, e2, {==})
    case Con{C.Hit{+sx, +ix}, +t}:
      +hx = Laws.Spec.scored.at(List.get(&2, Nat, xs, ix), sx)
      mcase(C.Hit{sx, ix}, t, C.Hit{sh, ih}, l => {Laws.Spec.all_scored(xs, l) == True{} : Bool},
        (g => L.and_i(hx, Laws.Spec.all_scored(xs, mins(t, C.Hit{sh, ih})),
            L.and_l(hx, Laws.Spec.all_scored(xs, t), e1),
            mins_sc(xs, t, sh, ih, L.and_r(hx, Laws.Spec.all_scored(xs, t), e1), e2)),
          g => L.and_i(Laws.Spec.scored.at(List.get(&2, Nat, xs, ih), sh),
            Laws.Spec.all_scored(xs, C.Hit{sx, ix} <> t), e2, e1)))

def mins_ab(+hs: List<&2, C.Hit>, +sh: Nat, +ih: Nat, +y: C.Hit,
  +e1: {ab(hs, y) == True{} : Bool}, +e2: {C.before(C.Hit{sh, ih}, y) == True{} : Bool}
) -> {ab(mins(hs, C.Hit{sh, ih}), y) == True{} : Bool}:
  match hs:
    case Nil{}:
      L.and_i(C.before(C.Hit{sh, ih}, y), True{}, e2, {==})
    case Con{+x, +t}:
      mcase(x, t, C.Hit{sh, ih}, l => {ab(l, y) == True{} : Bool},
        (g => L.and_i(C.before(x, y), ab(mins(t, C.Hit{sh, ih}), y),
            L.and_l(C.before(x, y), ab(t, y), e1),
            mins_ab(t, sh, ih, y, L.and_r(C.before(x, y), ab(t, y), e1), e2)),
          g => L.and_i(C.before(C.Hit{sh, ih}, y), ab(x <> t, y), e2, e1)))

def ab_trans(+t: List<&2, C.Hit>, +x: C.Hit, +y: C.Hit, +e1: {ab(t, x) == True{} : Bool},
  +e2: {C.before(x, y) == True{} : Bool}
) -> {ab(t, y) == True{} : Bool}:
  match t:
    case Nil{}:
      {==}
    case Con{+h, +r}:
      L.and_i(C.before(h, y), ab(r, y),
        L.trans(h, x, y, L.and_l(C.before(h, x), ab(r, x), e1), e2),
        ab_trans(r, x, y, L.and_r(C.before(h, x), ab(r, x), e1), e2))

# a kept list stays worst first
def mins_sw(+hs: List<&2, C.Hit>, +sh: Nat, +ih: Nat, +e1: {sw(hs) == True{} : Bool},
  +e2: {Laws.Spec.all_lt(hs, ih) == True{} : Bool}
) -> {sw(mins(hs, C.Hit{sh, ih})) == True{} : Bool}:
  match hs:
    case Nil{}:
      {==}
    case Con{C.Hit{+sx, +ix}, +t}:
      +x = {C.Hit{sx, ix} : C.Hit}
      +h = {C.Hit{sh, ih} : C.Hit}
      +ltx = L.and_l(Nat.is_lt(ix, ih), Laws.Spec.all_lt(t, ih), e2)
      mcase(x, t, h, l => {sw(l) == True{} : Bool},
        (g => L.and_i(ab(mins(t, h), x), sw(mins(t, h)),
            mins_ab(t, sh, ih, x, L.and_l(ab(t, x), sw(t), e1), g),
            mins_sw(t, sh, ih, L.and_r(ab(t, x), sw(t), e1),
              L.and_r(Nat.is_lt(ix, ih), Laws.Spec.all_lt(t, ih), e2))),
          +g => L.and_i(ab(x <> t, h), sw(x <> t),
            L.and_i(C.before(x, h), ab(t, h),
              L.total(sx, ix, sh, ih, g, L.lt_neq(ix, ih, ltx)),
              ab_trans(t, x, h, L.and_l(ab(t, x), sw(t), e1),
                L.total(sx, ix, sh, ih, g, L.lt_neq(ix, ih, ltx)))),
            e1)))

# Ranking: lists
# ==============

def ldisc(l: List<&2, Nat>) -> Type:
  match l:
    case Nil{}:
      Empty
    case Con{h, t}:
      Unit

def nil_con(-v: Nat, -t: List<&2, Nat>, +e: {Nil{} == v <> t : List<&2, Nat>}) -> Empty:
  %Equal.sym(List<&2, Nat>, Nil{}, v <> t, e) : ldisc(_)
  Unit{}

def ltail(l: List<&2, Nat>) -> List<&2, Nat>:
  match l:
    case Nil{}:
      Nil{}
    case Con{h, t}:
      t

def drop0(+r: List<&2, Nat>) -> {List.drop(&2, Nat, r, 0n) == r : List<&2, Nat>}:
  match r:
    case Nil{}:
      {==}
    case Con{h, t}:
      {==}

# the list from i starts with v
def drop_get(+full: List<&2, Nat>, i: Nat, -v: Nat, -t: List<&2, Nat>,
  +e: {List.drop(&2, Nat, full, i) == v <> t : List<&2, Nat>}
) -> {List.get(&2, Nat, full, i) == Some{v} : Maybe<&2, Nat>}:
  match full i:
    case Nil{} _:
      L.absurd({List.get(&2, Nat, Nil{}, i) == Some{v} : Maybe<&2, Nat>}, nil_con(v, t, e))
    case Con{+x, +r} 0n:
      %Equal.sym(List<&2, Nat>, x <> r, v <> t, e) : {List.get(&2, Nat, _, 0n) == Some{v} : Maybe<&2, Nat>}
      {==}
    case Con{+x, +r} 1n+p:
      drop_get(r, p, v, t, e)

# and goes on with t from i + 1
def drop_next(+full: List<&2, Nat>, i: Nat, -v: Nat, -t: List<&2, Nat>,
  +e: {List.drop(&2, Nat, full, i) == v <> t : List<&2, Nat>}
) -> {List.drop(&2, Nat, full, 1n+i) == t : List<&2, Nat>}:
  match full i:
    case Nil{} _:
      L.absurd({List.drop(&2, Nat, Nil{}, 1n+i) == t : List<&2, Nat>}, nil_con(v, t, e))
    case Con{+x, +r} 0n:
      %Equal.sym(List<&2, Nat>, List.drop(&2, Nat, r, 0n), r, drop0(r)) : {_ == t : List<&2, Nat>}
      Equal.cong(List<&2, Nat>, List<&2, Nat>, l => ltail(l), x <> r, v <> t, e)
    case Con{+x, +r} 1n+p:
      drop_next(r, p, v, t, e)

# nothing from i on: no slot at j >= i
def drop_none(+full: List<&2, Nat>, i: Nat, j: Nat,
  +e: {List.drop(&2, Nat, full, i) == Nil{} : List<&2, Nat>}, +h: {Nat.is_le(i, j) == True{} : Bool}
) -> {List.get(&2, Nat, full, j) == None{} : Maybe<&2, Nat>}:
  match full i j:
    case Nil{} _ _:
      {==}
    case Con{+x, +r} 0n _:
      L.absurd({List.get(&2, Nat, x <> r, j) == None{} : Maybe<&2, Nat>},
        nil_con(x, r, Equal.sym(List<&2, Nat>, x <> r, Nil{}, e)))
    case Con{+x, +r} 1n+p 0n:
      L.absurd({List.get(&2, Nat, x <> r, 0n) == None{} : Maybe<&2, Nat>}, L.ft(h))
    case Con{+x, +r} 1n+p 1n+q:
      drop_none(r, p, q, e, h)

# j < i + 1: j < i, or j is i
def lt_split(j: Nat, i: Nat, +h: {Nat.is_lt(j, 1n+i) == True{} : Bool}, -P: Nat -> Type,
  klt: @e: {Nat.is_lt(j, i) == True{} : Bool} -> P(j), keq: P(i)
) -> P(j):
  match j i:
    case 0n 0n:
      keq
    case 0n 1n+q:
      klt({==})
    case 1n+p 0n:
      L.absurd(P(1n+p), L.not_lt0(p, h))
    case 1n+p 1n+q:
      lt_split(p, q, h, x => P(1n+x), klt, keq)

def or_true_r(b: Bool) -> {Bool.or(b, True{}) == True{} : Bool}:
  match b:
    case True{}:
      {==}
    case False{}:
      {==}

def rev_len(+xs: List<&2, C.Hit>, +acc: List<&2, C.Hit>) ->
  {List.length(&2, C.Hit, List.reverse.go(&2, C.Hit, xs, acc)) ==
    Nat.add(List.length(&2, C.Hit, xs), List.length(&2, C.Hit, acc)) : Nat}:
  match xs:
    case Nil{}:
      {==}
    case Con{+x, +t}:
      +lt = List.length(&2, C.Hit, t)
      +la = List.length(&2, C.Hit, acc)
      Equal.trans(Nat, List.length(&2, C.Hit, List.reverse.go(&2, C.Hit, t, x <> acc)),
        Nat.add(lt, 1n+la), 1n+Nat.add(lt, la), rev_len(t, x <> acc), L.add_s(lt, la))

def rev_lt(+xs: List<&2, C.Hit>, +acc: List<&2, C.Hit>, +n: Nat,
  +e1: {Laws.Spec.all_lt(xs, n) == True{} : Bool}, +e2: {Laws.Spec.all_lt(acc, n) == True{} : Bool}
) -> {Laws.Spec.all_lt(List.reverse.go(&2, C.Hit, xs, acc), n) == True{} : Bool}:
  match xs:
    case Nil{}:
      e2
    case Con{C.Hit{+s, +i}, +t}:
      rev_lt(t, C.Hit{s, i} <> acc, n, L.and_r(Nat.is_lt(i, n), Laws.Spec.all_lt(t, n), e1),
        L.and_i(Nat.is_lt(i, n), Laws.Spec.all_lt(acc, n),
          L.and_l(Nat.is_lt(i, n), Laws.Spec.all_lt(t, n), e1), e2))

def rev_sc(+xs: List<&2, C.Hit>, +acc: List<&2, C.Hit>, +full: List<&2, Nat>,
  +e1: {Laws.Spec.all_scored(full, xs) == True{} : Bool},
  +e2: {Laws.Spec.all_scored(full, acc) == True{} : Bool}
) -> {Laws.Spec.all_scored(full, List.reverse.go(&2, C.Hit, xs, acc)) == True{} : Bool}:
  match xs:
    case Nil{}:
      e2
    case Con{C.Hit{+s, +i}, +t}:
      +hx = Laws.Spec.scored.at(List.get(&2, Nat, full, i), s)
      rev_sc(t, C.Hit{s, i} <> acc, full, L.and_r(hx, Laws.Spec.all_scored(full, t), e1),
        L.and_i(hx, Laws.Spec.all_scored(full, acc), L.and_l(hx, Laws.Spec.all_scored(full, t), e1), e2))

def or3_t(-a: Bool, b: Bool, -c: Bool, +e: {a == True{} : Bool}) ->
  {Bool.or(b, Bool.or(a, c)) == True{} : Bool}:
  %Equal.sym(Bool, a, True{}, e) : {Bool.or(b, Bool.or(_, c)) == True{} : Bool}
  or_true_r(b)

def rev_has.at(+t: List<&2, C.Hit>, -s: Nat, +i: Nat, -acc: List<&2, C.Hit>, +j: Nat, b: Bool,
  +eb: {Nat.is_eq(i, j) == b : Bool},
  +e: {Bool.or(Bool.or(b, Laws.Spec.has_id(t, j)), Laws.Spec.has_id(acc, j)) == True{} : Bool}
) -> {Bool.or(Laws.Spec.has_id(t, j), Laws.Spec.has_id(C.Hit{s, i} <> acc, j)) == True{} : Bool}:
  match b:
    case True{}:
      or3_t(Nat.is_eq(i, j), Laws.Spec.has_id(t, j), Laws.Spec.has_id(acc, j), eb)
    case False{}:
      %Equal.sym(Bool, Nat.is_eq(i, j), False{}, eb) :
        {Bool.or(Laws.Spec.has_id(t, j), Bool.or(_, Laws.Spec.has_id(acc, j))) == True{} : Bool}
      e

def rev_has(+xs: List<&2, C.Hit>, +acc: List<&2, C.Hit>, +j: Nat,
  +e: {Bool.or(Laws.Spec.has_id(xs, j), Laws.Spec.has_id(acc, j)) == True{} : Bool}
) -> {Laws.Spec.has_id(List.reverse.go(&2, C.Hit, xs, acc), j) == True{} : Bool}:
  match xs:
    case Nil{}:
      e
    case Con{C.Hit{+s, +i}, +t}:
      rev_has(t, C.Hit{s, i} <> acc, j, rev_has.at(t, s, i, acc, j, Nat.is_eq(i, j), {==}, e))

def bef_spec(+x: C.Hit, +y: C.Hit, +e: {C.before(x, y) == True{} : Bool}) ->
  {Laws.Spec.before(x, y) == True{} : Bool}:
  %bef_eq(x, y) : {_ == True{} : Bool}
  e

# ab is the spec's all_before
def ab_spec(+l: List<&2, C.Hit>, +y: C.Hit) -> {ab(l, y) == Laws.Spec.all_before(l, y) : Bool}:
  match l:
    case Nil{}:
      {==}
    case Con{+x, +t}:
      %bef_eq(x, y) : {Bool.and(C.before(x, y), ab(t, y)) == Bool.and(_, Laws.Spec.all_before(t, y)) : Bool}
      %ab_spec(t, y) : {Bool.and(C.before(x, y), ab(t, y)) == Bool.and(C.before(x, y), _) : Bool}
      {==}

def ab_to(+l: List<&2, C.Hit>, +y: C.Hit, +e: {ab(l, y) == True{} : Bool}) ->
  {Laws.Spec.all_before(l, y) == True{} : Bool}:
  %ab_spec(l, y) : {_ == True{} : Bool}
  e

def ab_of(+l: List<&2, C.Hit>, +y: C.Hit, +e: {Laws.Spec.all_before(l, y) == True{} : Bool}) ->
  {ab(l, y) == True{} : Bool}:
  %Equal.sym(Bool, ab(l, y), Laws.Spec.all_before(l, y), ab_spec(l, y)) : {_ == True{} : Bool}
  e

def rev_ab(+xs: List<&2, C.Hit>, +acc: List<&2, C.Hit>, +y: C.Hit, +e1: {ab(xs, y) == True{} : Bool},
  +e2: {Laws.Spec.all_before(acc, y) == True{} : Bool}
) -> {Laws.Spec.all_before(List.reverse.go(&2, C.Hit, xs, acc), y) == True{} : Bool}:
  match xs:
    case Nil{}:
      e2
    case Con{+x, +t}:
      rev_ab(t, x <> acc, y, L.and_r(C.before(x, y), ab(t, y), e1),
        L.and_i(Laws.Spec.before(x, y), Laws.Spec.all_before(acc, y),
          bef_spec(x, y, L.and_l(C.before(x, y), ab(t, y), e1)), e2))

# every hit of xs comes before every hit of acc
def ball(+xs: List<&2, C.Hit>, acc: List<&2, C.Hit>) -> Bool:
  match acc:
    case Nil{}:
      True{}
    case Con{+a, r}:
      Bool.and(ab(xs, a), ball(xs, r))

def ball_tail(+x: C.Hit, +t: List<&2, C.Hit>, +acc: List<&2, C.Hit>,
  +e: {ball(x <> t, acc) == True{} : Bool}
) -> {ball(t, acc) == True{} : Bool}:
  match acc:
    case Nil{}:
      {==}
    case Con{+a, +r}:
      +h = ab(x <> t, a)
      L.and_i(ab(t, a), ball(t, r),
        L.and_r(C.before(x, a), ab(t, a), L.and_l(h, ball(x <> t, r), e)),
        ball_tail(x, t, r, L.and_r(h, ball(x <> t, r), e)))

def sorted_push(+x: C.Hit, +t: List<&2, C.Hit>, +acc: List<&2, C.Hit>,
  +e2: {Laws.Spec.sorted(acc) == True{} : Bool}, +e3: {ball(x <> t, acc) == True{} : Bool}
) -> {Laws.Spec.sorted(x <> acc) == True{} : Bool}:
  match acc:
    case Nil{}:
      {==}
    case Con{+a, +r}:
      +h = ab(x <> t, a)
      +xa = L.and_l(C.before(x, a), ab(t, a), L.and_l(h, ball(x <> t, r), e3))
      L.and_i(Bool.not(Laws.Spec.before(a, x)), Laws.Spec.sorted(a <> r), L.nb(a, x, L.asym(x, a, xa)), e2)

def rev_sorted(+xs: List<&2, C.Hit>, +acc: List<&2, C.Hit>, +e1: {sw(xs) == True{} : Bool},
  +e2: {Laws.Spec.sorted(acc) == True{} : Bool}, +e3: {ball(xs, acc) == True{} : Bool}
) -> {Laws.Spec.sorted(List.reverse.go(&2, C.Hit, xs, acc)) == True{} : Bool}:
  match xs:
    case Nil{}:
      e2
    case Con{+x, +t}:
      rev_sorted(t, x <> acc, L.and_r(ab(t, x), sw(t), e1), sorted_push(x, t, acc, e2, e3),
        L.and_i(ab(t, x), ball(t, acc), L.and_l(ab(t, x), sw(t), e1), ball_tail(x, t, acc, e3)))

def lt_up1(+a: Nat, +b: Nat, +h: {Nat.is_lt(a, b) == True{} : Bool}) ->
  {Nat.is_lt(a, 1n+b) == True{} : Bool}:
  L.le_lt(a, b, 1n+b, L.lt_le(a, b, h), L.lt_succ_self(b))

def lt_up(+hs: List<&2, C.Hit>, +i: Nat, +e: {Laws.Spec.all_lt(hs, i) == True{} : Bool}) ->
  {Laws.Spec.all_lt(hs, 1n+i) == True{} : Bool}:
  match hs:
    case Nil{}:
      {==}
    case Con{C.Hit{+s, +id}, +t}:
      L.and_i(Nat.is_lt(id, 1n+i), Laws.Spec.all_lt(t, 1n+i),
        lt_up1(id, i, L.and_l(Nat.is_lt(id, i), Laws.Spec.all_lt(t, i), e)),
        lt_up(t, i, L.and_r(Nat.is_lt(id, i), Laws.Spec.all_lt(t, i), e)))

# Ranking: the selection invariant
# ================================
# msel walks the slots of full in id order. Before slot i, with the hits hs
# kept: c = |hs| <= k, every id is below i, hs sits worst first, every hit
# carries its slot's value, and every slot before i is complete (kept, or k
# hits all come before it).

# or, by cases
def or_elim(a: Bool, -b: Bool, e: {Bool.or(a, b) == True{} : Bool}, -P: Type,
  ka: @h: {a == True{} : Bool} -> P, kb: @h: {b == True{} : Bool} -> P
) -> P:
  match a:
    case True{}:
      ka({==})
    case False{}:
      kb(e)

def andF_l(a: Bool, -b: Bool, e: {a == False{} : Bool}) -> {Bool.and(a, b) == False{} : Bool}:
  match a:
    case False{}:
      {==}
    case True{}:
      L.absurd({Bool.and(True{}, b) == False{} : Bool}, L.tf(e))

# the right side is false: the left one holds
def or_false_r(a: Bool, -b: Bool, eb: {b == False{} : Bool}, e: {Bool.or(a, b) == True{} : Bool}) ->
  {a == True{} : Bool}:
  match a:
    case True{}:
      {==}
    case False{}:
      L.absurd({False{} == True{} : Bool}, L.clash(b, e, eb))

def le_eq(-c: Nat, -l: Nat, -k: Nat, e: {c == l : Nat}, h: {Nat.is_le(c, k) == True{} : Bool}) ->
  {Nat.is_le(l, k) == True{} : Bool}:
  %e : {Nat.is_le(_, k) == True{} : Bool}
  h

def lt_eq(-c: Nat, -l: Nat, -k: Nat, e: {c == l : Nat}, h: {Nat.is_lt(c, k) == True{} : Bool}) ->
  {Nat.is_lt(l, k) == True{} : Bool}:
  %e : {Nat.is_lt(_, k) == True{} : Bool}
  h

# c = l and c = k: l is k
def eq_len(+c: Nat, -l: Nat, -k: Nat, e: {c == l : Nat}, ck: {c == k : Nat}) ->
  {Nat.is_eq(l, k) == True{} : Bool}:
  %e : {Nat.is_eq(_, k) == True{} : Bool}
  %ck : {Nat.is_eq(c, _) == True{} : Bool}
  L.eq_refl(c)

# a slot with a value lies in the list
def sc_lt1(+full: List<&2, Nat>, +id: Nat, +s: Nat,
  +e: {Laws.Spec.scored.at(List.get(&2, Nat, full, id), s) == True{} : Bool}
) -> {Nat.is_lt(id, List.length(&2, Nat, full)) == True{} : Bool}:
  match full id:
    case Nil{} _:
      L.absurd({Nat.is_lt(id, 0n) == True{} : Bool}, L.ft(e))
    case Con{+x, +r} 0n:
      {==}
    case Con{+x, +r} 1n+p:
      sc_lt1(r, p, s, e)

# scored hits name slots of the list
def sc_lt(+full: List<&2, Nat>, +hs: List<&2, C.Hit>,
  +e: {Laws.Spec.all_scored(full, hs) == True{} : Bool}
) -> {Laws.Spec.all_lt(hs, List.length(&2, Nat, full)) == True{} : Bool}:
  match hs:
    case Nil{}:
      {==}
    case Con{C.Hit{+s, +id}, +t}:
      +hx = Laws.Spec.scored.at(List.get(&2, Nat, full, id), s)
      +n = List.length(&2, Nat, full)
      L.and_i(Nat.is_lt(id, n), Laws.Spec.all_lt(t, n),
        sc_lt1(full, id, s, L.and_l(hx, Laws.Spec.all_scored(full, t), e)),
        sc_lt(full, t, L.and_r(hx, Laws.Spec.all_scored(full, t), e)))

# slot i holds 1 + w: the new hit carries it
def sc_new(+full: List<&2, Nat>, +i: Nat, +w: Nat,
  +eg: {List.get(&2, Nat, full, i) == Some{1n+w} : Maybe<&2, Nat>}
) -> {Laws.Spec.scored.at(List.get(&2, Nat, full, i), 1n+w) == True{} : Bool}:
  %Equal.sym(Maybe<&2, Nat>, List.get(&2, Nat, full, i), Some{1n+w}, eg) :
    {Laws.Spec.scored.at(_, 1n+w) == True{} : Bool}
  L.and_i(Nat.is_eq(1n+w, 1n+w), Nat.is_lt(0n, 1n+w), L.eq_refl(1n+w), {==})

# ix is j: slot j carries what slot ix does
def sc_at(+full: List<&2, Nat>, +ix: Nat, +j: Nat, +sx: Nat, q: {Nat.is_eq(ix, j) == True{} : Bool},
  e: {Laws.Spec.scored.at(List.get(&2, Nat, full, ix), sx) == True{} : Bool}
) -> {Laws.Spec.scored.at(List.get(&2, Nat, full, j), sx) == True{} : Bool}:
  %L.eq_prop(ix, j, q) : {Laws.Spec.scored.at(List.get(&2, Nat, full, _), sx) == True{} : Bool}
  e

# slot i holds 0: it is complete
def comp_zero(+full: List<&2, Nat>, +i: Nat,
  +eg: {List.get(&2, Nat, full, i) == Some{0n} : Maybe<&2, Nat>}, +hs: List<&2, C.Hit>, +k: Nat
) -> {Laws.Spec.complete.at(List.get(&2, Nat, full, i), hs, k, i) == True{} : Bool}:
  %Equal.sym(Maybe<&2, Nat>, List.get(&2, Nat, full, i), Some{0n}, eg) :
    {Laws.Spec.complete.at(_, hs, k, i) == True{} : Bool}
  {==}

# slot i holds 1 + w and is kept
def comp_new(+full: List<&2, Nat>, +i: Nat, +w: Nat,
  +eg: {List.get(&2, Nat, full, i) == Some{1n+w} : Maybe<&2, Nat>}, +hs: List<&2, C.Hit>, +k: Nat,
  +e: {Laws.Spec.has_id(hs, i) == True{} : Bool}
) -> {Laws.Spec.complete.at(List.get(&2, Nat, full, i), hs, k, i) == True{} : Bool}:
  %Equal.sym(Maybe<&2, Nat>, List.get(&2, Nat, full, i), Some{1n+w}, eg) :
    {Laws.Spec.complete.at(_, hs, k, i) == True{} : Bool}
  or_l(Laws.Spec.has_id(hs, i), Bool.and(Nat.is_eq(List.length(&2, C.Hit, hs), k),
    Laws.Spec.all_before(hs, C.Hit{1n+w, i})), e)

# slot i holds 1 + w, and the k kept hits all come before it
def comp_full(+full: List<&2, Nat>, +i: Nat, +w: Nat,
  +eg: {List.get(&2, Nat, full, i) == Some{1n+w} : Maybe<&2, Nat>}, +hs: List<&2, C.Hit>, +k: Nat,
  +el: {Nat.is_eq(List.length(&2, C.Hit, hs), k) == True{} : Bool},
  +eab: {ab(hs, C.Hit{1n+w, i}) == True{} : Bool}
) -> {Laws.Spec.complete.at(List.get(&2, Nat, full, i), hs, k, i) == True{} : Bool}:
  +h = {C.Hit{1n+w, i} : C.Hit}
  +q = Nat.is_eq(List.length(&2, C.Hit, hs), k)
  %Equal.sym(Maybe<&2, Nat>, List.get(&2, Nat, full, i), Some{1n+w}, eg) :
    {Laws.Spec.complete.at(_, hs, k, i) == True{} : Bool}
  L.or_r(Laws.Spec.has_id(hs, i), Bool.and(q, Laws.Spec.all_before(hs, h)),
    L.and_i(q, Laws.Spec.all_before(hs, h), el, ab_to(hs, h, eab)))

# every slot before i is complete for hs
def Comp(-full: List<&2, Nat>, -k: Nat, -i: Nat, -hs: List<&2, C.Hit>) -> Type:
  @j: Nat -> @hj: {Nat.is_lt(j, i) == True{} : Bool} ->
    {Laws.Spec.complete.at(List.get(&2, Nat, full, j), hs, k, j) == True{} : Bool}

# the slots before i stay complete (f) and slot i is (g)
def comp_up(-full: List<&2, Nat>, -k: Nat, +i: Nat, -hs: List<&2, C.Hit>, f: Comp(full, k, i, hs),
  g: {Laws.Spec.complete.at(List.get(&2, Nat, full, i), hs, k, i) == True{} : Bool}
) -> Comp(full, k, 1n+i, hs):
  +j => hj => lt_split(j, i, hj,
    x => {Laws.Spec.complete.at(List.get(&2, Nat, full, x), hs, k, x) == True{} : Bool},
    e => f(j, e), g)

def Inv(-full: List<&2, Nat>, -k: Nat, -i: Nat, -c: Nat, -hs: List<&2, C.Hit>) -> Type:
  Pair(Pair(Pair({c == List.length(&2, C.Hit, hs) : Nat}, {Nat.is_le(c, k) == True{} : Bool}),
    Pair({Laws.Spec.all_lt(hs, i) == True{} : Bool}, {sw(hs) == True{} : Bool})),
    Pair({Laws.Spec.all_scored(full, hs) == True{} : Bool}, Comp(full, k, i, hs)))

# with room, a slot before i stays complete when a hit comes in
def comp_room(m: Maybe<&2, Nat>, +hs: List<&2, C.Hit>, +k: Nat, +j: Nat, +sh: Nat, +ih: Nat,
  +hl: {Nat.is_lt(List.length(&2, C.Hit, hs), k) == True{} : Bool},
  e: {Laws.Spec.complete.at(m, hs, k, j) == True{} : Bool}
) -> {Laws.Spec.complete.at(m, mins(hs, C.Hit{sh, ih}), k, j) == True{} : Bool}:
  match m:
    case None{}:
      {==}
    case Some{+v}:
      match v:
        case 0n:
          {==}
        case 1n+p:
          +y = {C.Hit{1n+p, j} : C.Hit}
          +mh = mins(hs, C.Hit{sh, ih})
          +q = Nat.is_eq(List.length(&2, C.Hit, hs), k)
          or_l(Laws.Spec.has_id(mh, j),
            Bool.and(Nat.is_eq(List.length(&2, C.Hit, mh), k), Laws.Spec.all_before(mh, y)),
            mins_old(hs, sh, ih, j, or_false_r(Laws.Spec.has_id(hs, j),
              Bool.and(q, Laws.Spec.all_before(hs, y)),
              andF_l(q, Laws.Spec.all_before(hs, y), L.lt_neq(List.length(&2, C.Hit, hs), k, hl)), e)))

# all hits come before Hit{sx, ix}, which is Hit{s, j}
def ab_hit(+l: List<&2, C.Hit>, +sx: Nat, +ix: Nat, +s: Nat, +j: Nat, es: {s == sx : Nat},
  ej: {ix == j : Nat}, h: {Laws.Spec.all_before(l, C.Hit{sx, ix}) == True{} : Bool}
) -> {Laws.Spec.all_before(l, C.Hit{s, j}) == True{} : Bool}:
  %ej : {Laws.Spec.all_before(l, C.Hit{s, _}) == True{} : Bool}
  %Equal.sym(Nat, s, sx, es) : {Laws.Spec.all_before(l, C.Hit{_, ix}) == True{} : Bool}
  h

# the evicted x is slot j itself: the rest and h all come before it
def evict_self(+sx: Nat, +ix: Nat, +t: List<&2, C.Hit>, +k: Nat, +j: Nat, +sh: Nat, +ih: Nat, +p: Nat,
  +emk: {Nat.is_eq(List.length(&2, C.Hit, mins(t, C.Hit{sh, ih})), k) == True{} : Bool},
  +eat: {ab(t, C.Hit{sx, ix}) == True{} : Bool},
  +ehx: {C.before(C.Hit{sh, ih}, C.Hit{sx, ix}) == True{} : Bool},
  +q: {Nat.is_eq(ix, j) == True{} : Bool},
  +hx: {Bool.and(Nat.is_eq(1n+p, sx), Nat.is_lt(0n, sx)) == True{} : Bool}
) -> {Bool.or(Laws.Spec.has_id(mins(t, C.Hit{sh, ih}), j),
    Bool.and(Nat.is_eq(List.length(&2, C.Hit, mins(t, C.Hit{sh, ih})), k),
      Laws.Spec.all_before(mins(t, C.Hit{sh, ih}), C.Hit{1n+p, j}))) == True{} : Bool}:
  +x = {C.Hit{sx, ix} : C.Hit}
  +y = {C.Hit{1n+p, j} : C.Hit}
  +mh = mins(t, C.Hit{sh, ih})
  +q2 = Nat.is_eq(List.length(&2, C.Hit, mh), k)
  L.or_r(Laws.Spec.has_id(mh, j), Bool.and(q2, Laws.Spec.all_before(mh, y)),
    L.and_i(q2, Laws.Spec.all_before(mh, y), emk,
      ab_hit(mh, sx, ix, 1n+p, j, L.eq_prop(1n+p, sx, L.and_l(Nat.is_eq(1n+p, sx), Nat.is_lt(0n, sx), hx)),
        L.eq_prop(ix, j, q), ab_to(mh, x, mins_ab(t, sh, ih, x, eat, ehx)))))

# the k kept hits all came before y: so do the rest and h, which beats x
def evict_full(+sx: Nat, +ix: Nat, +t: List<&2, C.Hit>, +k: Nat, +sh: Nat, +ih: Nat, +y: C.Hit, +j: Nat,
  +emk: {Nat.is_eq(List.length(&2, C.Hit, mins(t, C.Hit{sh, ih})), k) == True{} : Bool},
  +ehx: {C.before(C.Hit{sh, ih}, C.Hit{sx, ix}) == True{} : Bool},
  +b: {Bool.and(Nat.is_eq(1n+List.length(&2, C.Hit, t), k),
    Laws.Spec.all_before(C.Hit{sx, ix} <> t, y)) == True{} : Bool}
) -> {Bool.or(Laws.Spec.has_id(mins(t, C.Hit{sh, ih}), j),
    Bool.and(Nat.is_eq(List.length(&2, C.Hit, mins(t, C.Hit{sh, ih})), k),
      Laws.Spec.all_before(mins(t, C.Hit{sh, ih}), y))) == True{} : Bool}:
  +x = {C.Hit{sx, ix} : C.Hit}
  +h = {C.Hit{sh, ih} : C.Hit}
  +mh = mins(t, h)
  +q2 = Nat.is_eq(List.length(&2, C.Hit, mh), k)
  +abx = ab_of(x <> t, y, L.and_r(Nat.is_eq(1n+List.length(&2, C.Hit, t), k),
    Laws.Spec.all_before(x <> t, y), b))
  L.or_r(Laws.Spec.has_id(mh, j), Bool.and(q2, Laws.Spec.all_before(mh, y)),
    L.and_i(q2, Laws.Spec.all_before(mh, y), emk,
      ab_to(mh, y, mins_ab(t, sh, ih, y, L.and_r(C.before(x, y), ab(t, y), abx),
        L.trans(h, x, y, ehx, L.and_l(C.before(x, y), ab(t, y), abx))))))

# no room, h beats the worst kept x: a slot before i stays complete
def comp_evict(m: Maybe<&2, Nat>, +sx: Nat, +ix: Nat, +t: List<&2, C.Hit>, +k: Nat, +j: Nat, +sh: Nat,
  +ih: Nat, +emk: {Nat.is_eq(List.length(&2, C.Hit, mins(t, C.Hit{sh, ih})), k) == True{} : Bool},
  +eat: {ab(t, C.Hit{sx, ix}) == True{} : Bool},
  +ehx: {C.before(C.Hit{sh, ih}, C.Hit{sx, ix}) == True{} : Bool},
  hx: @q: {Nat.is_eq(ix, j) == True{} : Bool} -> {Laws.Spec.scored.at(m, sx) == True{} : Bool},
  e: {Laws.Spec.complete.at(m, C.Hit{sx, ix} <> t, k, j) == True{} : Bool}
) -> {Laws.Spec.complete.at(m, mins(t, C.Hit{sh, ih}), k, j) == True{} : Bool}:
  match m:
    case None{}:
      {==}
    case Some{+v}:
      match v:
        case 0n:
          {==}
        case 1n+p:
          +y = {C.Hit{1n+p, j} : C.Hit}
          +mh = mins(t, C.Hit{sh, ih})
          -G = {Bool.or(Laws.Spec.has_id(mh, j), Bool.and(Nat.is_eq(List.length(&2, C.Hit, mh), k),
            Laws.Spec.all_before(mh, y))) == True{} : Bool}
          or_elim(Laws.Spec.has_id(C.Hit{sx, ix} <> t, j),
            Bool.and(Nat.is_eq(1n+List.length(&2, C.Hit, t), k), Laws.Spec.all_before(C.Hit{sx, ix} <> t, y)),
            e, G,
            a => or_elim(Nat.is_eq(ix, j), Laws.Spec.has_id(t, j), a, G,
              +q => evict_self(sx, ix, t, k, j, sh, ih, p, emk, eat, ehx, q, hx(q)),
              q => or_l(Laws.Spec.has_id(mh, j), Bool.and(Nat.is_eq(List.length(&2, C.Hit, mh), k),
                Laws.Spec.all_before(mh, y)), mins_old(t, sh, ih, j, q))),
            b => evict_full(sx, ix, t, k, sh, ih, y, j, emk, ehx, b))

# room: the new hit comes in
def inv_room(+full: List<&2, Nat>, +k: Nat, +i: Nat, +c: Nat, +hs: List<&2, C.Hit>, +w: Nat,
  +eg: {List.get(&2, Nat, full, i) == Some{1n+w} : Maybe<&2, Nat>},
  +ec: {c == List.length(&2, C.Hit, hs) : Nat}, +er: {Nat.is_lt(c, k) == True{} : Bool},
  +el: {Laws.Spec.all_lt(hs, i) == True{} : Bool}, +es: {sw(hs) == True{} : Bool},
  +esc: {Laws.Spec.all_scored(full, hs) == True{} : Bool}, comp: Comp(full, k, i, hs)
) -> Inv(full, k, 1n+i, 1n+c, mins(hs, C.Hit{1n+w, i})):
  +h = {C.Hit{1n+w, i} : C.Hit}
  +mh = mins(hs, h)
  +lh = List.length(&2, C.Hit, hs)
  +hl = lt_eq(c, lh, k, ec, er)
  (((Equal.trans(Nat, 1n+c, 1n+lh, List.length(&2, C.Hit, mh), succ_eq(c, lh, ec),
      Equal.sym(Nat, List.length(&2, C.Hit, mh), 1n+lh, mins_len(hs, h))), L.lt_sle(c, k, er)),
    (mins_lt(hs, 1n+w, i, 1n+i, lt_up(hs, i, el), L.lt_succ_self(i)), mins_sw(hs, 1n+w, i, es, el))),
    (mins_sc(full, hs, 1n+w, i, esc, sc_new(full, i, w, eg)),
      comp_up(full, k, i, mh,
        +j => e => comp_room(List.get(&2, Nat, full, j), hs, k, j, 1n+w, i, hl, comp(j, e)),
        comp_new(full, i, w, eg, mh, k, mins_new(hs, 1n+w, i)))))

# no room (c = k) and hs stays: h does not beat the worst kept hit, or k = 0
def inv_same(+full: List<&2, Nat>, +k: Nat, +i: Nat, +c: Nat, +hs: List<&2, C.Hit>, +w: Nat,
  +eg: {List.get(&2, Nat, full, i) == Some{1n+w} : Maybe<&2, Nat>},
  +ec: {c == List.length(&2, C.Hit, hs) : Nat}, +ek: {Nat.is_le(c, k) == True{} : Bool},
  +er: {Nat.is_lt(c, k) == False{} : Bool}, +el: {Laws.Spec.all_lt(hs, i) == True{} : Bool},
  +es: {sw(hs) == True{} : Bool}, +esc: {Laws.Spec.all_scored(full, hs) == True{} : Bool},
  comp: Comp(full, k, i, hs), +eab: {ab(hs, C.Hit{1n+w, i}) == True{} : Bool}
) -> Inv(full, k, 1n+i, c, hs):
  (((ec, ek), (lt_up(hs, i, el), es)), (esc, comp_up(full, k, i, hs, comp,
    comp_full(full, i, w, eg, hs, k,
      eq_len(c, List.length(&2, C.Hit, hs), k, ec, L.le_nlt_eq(c, k, ek, er)), eab))))

# no room (c = k), h beats the worst kept x: x goes, h comes in
def inv_evict(+full: List<&2, Nat>, +k: Nat, +i: Nat, +c: Nat, +sx: Nat, +ix: Nat, +t: List<&2, C.Hit>,
  +w: Nat, +eg: {List.get(&2, Nat, full, i) == Some{1n+w} : Maybe<&2, Nat>},
  +ec: {c == 1n+List.length(&2, C.Hit, t) : Nat}, +ek: {Nat.is_le(c, k) == True{} : Bool},
  +er: {Nat.is_lt(c, k) == False{} : Bool}, +el: {Laws.Spec.all_lt(C.Hit{sx, ix} <> t, i) == True{} : Bool},
  +es: {sw(C.Hit{sx, ix} <> t) == True{} : Bool},
  +esc: {Laws.Spec.all_scored(full, C.Hit{sx, ix} <> t) == True{} : Bool},
  comp: Comp(full, k, i, C.Hit{sx, ix} <> t),
  +ehx: {C.before(C.Hit{1n+w, i}, C.Hit{sx, ix}) == True{} : Bool}
) -> Inv(full, k, 1n+i, c, mins(t, C.Hit{1n+w, i})):
  +x = {C.Hit{sx, ix} : C.Hit}
  +h = {C.Hit{1n+w, i} : C.Hit}
  +mh = mins(t, h)
  +lt = List.length(&2, C.Hit, t)
  +elt = L.and_r(Nat.is_lt(ix, i), Laws.Spec.all_lt(t, i), el)
  +hx = Laws.Spec.scored.at(List.get(&2, Nat, full, ix), sx)
  +ecm = Equal.trans(Nat, c, 1n+lt, List.length(&2, C.Hit, mh), ec,
    Equal.sym(Nat, List.length(&2, C.Hit, mh), 1n+lt, mins_len(t, h)))
  +emk = eq_len(c, List.length(&2, C.Hit, mh), k, ecm, L.le_nlt_eq(c, k, ek, er))
  +eat = L.and_l(ab(t, x), sw(t), es)
  +esx = L.and_l(hx, Laws.Spec.all_scored(full, t), esc)
  (((ecm, ek), (mins_lt(t, 1n+w, i, 1n+i, lt_up(t, i, elt), L.lt_succ_self(i)),
    mins_sw(t, 1n+w, i, L.and_r(ab(t, x), sw(t), es), elt))),
    (mins_sc(full, t, 1n+w, i, L.and_r(hx, Laws.Spec.all_scored(full, t), esc), sc_new(full, i, w, eg)),
      comp_up(full, k, i, mh,
        +j => e => comp_evict(List.get(&2, Nat, full, j), sx, ix, t, k, j, 1n+w, i, emk, eat, ehx,
          q => sc_at(full, ix, j, sx, q, esx), comp(j, e)),
        comp_new(full, i, w, eg, mh, k, mins_new(t, 1n+w, i)))))

# no room, the worst kept hit is x: g = before(h, x)
def inv_con.go(+full: List<&2, Nat>, +k: Nat, +i: Nat, +c: Nat, +sx: Nat, +ix: Nat, +t: List<&2, C.Hit>,
  +w: Nat, g: Bool, +eg2: {C.before(C.Hit{1n+w, i}, C.Hit{sx, ix}) == g : Bool},
  +eg: {List.get(&2, Nat, full, i) == Some{1n+w} : Maybe<&2, Nat>},
  +ec: {c == 1n+List.length(&2, C.Hit, t) : Nat}, +ek: {Nat.is_le(c, k) == True{} : Bool},
  +er: {Nat.is_lt(c, k) == False{} : Bool}, +el: {Laws.Spec.all_lt(C.Hit{sx, ix} <> t, i) == True{} : Bool},
  +es: {sw(C.Hit{sx, ix} <> t) == True{} : Bool},
  +esc: {Laws.Spec.all_scored(full, C.Hit{sx, ix} <> t) == True{} : Bool},
  comp: Comp(full, k, i, C.Hit{sx, ix} <> t)
) -> Inv(full, k, 1n+i, c, C.keep.if(g, C.Hit{1n+w, i}, C.Hit{sx, ix}, t)):
  match g:
    case True{}:
      inv_evict(full, k, i, c, sx, ix, t, w, eg, ec, ek, er, el, es, esc, comp, eg2)
    case False{}:
      +x = {C.Hit{sx, ix} : C.Hit}
      +h = {C.Hit{1n+w, i} : C.Hit}
      +bxh = L.total(sx, ix, 1n+w, i, eg2,
        L.lt_neq(ix, i, L.and_l(Nat.is_lt(ix, i), Laws.Spec.all_lt(t, i), el)))
      inv_same(full, k, i, c, x <> t, w, eg, ec, ek, er, el, es, esc, comp,
        L.and_i(C.before(x, h), ab(t, h), bxh, ab_trans(t, x, h, L.and_l(ab(t, x), sw(t), es), bxh)))

# no room: c = k
def inv_full(+full: List<&2, Nat>, +k: Nat, +i: Nat, +c: Nat, +hs: List<&2, C.Hit>, +w: Nat,
  +eg: {List.get(&2, Nat, full, i) == Some{1n+w} : Maybe<&2, Nat>},
  +ec: {c == List.length(&2, C.Hit, hs) : Nat}, +ek: {Nat.is_le(c, k) == True{} : Bool},
  +er: {Nat.is_lt(c, k) == False{} : Bool}, +el: {Laws.Spec.all_lt(hs, i) == True{} : Bool},
  +es: {sw(hs) == True{} : Bool}, +esc: {Laws.Spec.all_scored(full, hs) == True{} : Bool},
  comp: Comp(full, k, i, hs)
) -> Inv(full, k, 1n+i, c, C.keep(False{}, C.Hit{1n+w, i}, hs)):
  match hs:
    case Nil{}:
      inv_same(full, k, i, c, Nil{}, w, eg, ec, ek, er, el, es, esc, comp, {==})
    case Con{C.Hit{+sx, +ix}, +t}:
      inv_con.go(full, k, i, c, sx, ix, t, w, C.before(C.Hit{1n+w, i}, C.Hit{sx, ix}), {==}, eg, ec, ek,
        er, el, es, esc, comp)

# room = (c < k)
def inv_hit.go(+full: List<&2, Nat>, +k: Nat, +i: Nat, +c: Nat, +hs: List<&2, C.Hit>, +w: Nat,
  room: Bool, +er: {Nat.is_lt(c, k) == room : Bool},
  +eg: {List.get(&2, Nat, full, i) == Some{1n+w} : Maybe<&2, Nat>},
  +ec: {c == List.length(&2, C.Hit, hs) : Nat}, +ek: {Nat.is_le(c, k) == True{} : Bool},
  +el: {Laws.Spec.all_lt(hs, i) == True{} : Bool}, +es: {sw(hs) == True{} : Bool},
  +esc: {Laws.Spec.all_scored(full, hs) == True{} : Bool}, comp: Comp(full, k, i, hs)
) -> Inv(full, k, 1n+i, F.pick(room, 1n+c, c), C.keep(room, C.Hit{1n+w, i}, hs)):
  match room:
    case True{}:
      inv_room(full, k, i, c, hs, w, eg, ec, er, el, es, esc, comp)
    case False{}:
      inv_full(full, k, i, c, hs, w, eg, ec, ek, er, el, es, esc, comp)

# slot i holds 1 + w
def inv_hit(+full: List<&2, Nat>, +k: Nat, +i: Nat, +c: Nat, +hs: List<&2, C.Hit>, +w: Nat,
  +eg: {List.get(&2, Nat, full, i) == Some{1n+w} : Maybe<&2, Nat>}, inv: Inv(full, k, i, c, hs)
) -> Inv(full, k, 1n+i, F.pick(Nat.is_lt(c, k), 1n+c, c), C.keep(Nat.is_lt(c, k), C.Hit{1n+w, i}, hs)):
  (pa, pb) = inv
  (pc, pl) = pa
  (ec, ek) = pc
  (el, es) = pl
  (esc, comp) = pb
  inv_hit.go(full, k, i, c, hs, w, Nat.is_lt(c, k), {==}, eg, ec, ek, el, es, esc, comp)

# slot i holds 0
def inv_zero(+full: List<&2, Nat>, +k: Nat, +i: Nat, +c: Nat, +hs: List<&2, C.Hit>,
  +eg: {List.get(&2, Nat, full, i) == Some{0n} : Maybe<&2, Nat>}, inv: Inv(full, k, i, c, hs)
) -> Inv(full, k, 1n+i, c, hs):
  (pa, pb) = inv
  (pc, pl) = pa
  (el, es) = pl
  (esc, comp) = pb
  ((pc, (lt_up(hs, i, el), es)), (esc, comp_up(full, k, i, hs, comp, comp_zero(full, i, eg, hs, k))))

# what the selection yields: at most k hits, worst first, each carrying its
# slot's value, and every slot complete
def Out(-full: List<&2, Nat>, -k: Nat, -hs: List<&2, C.Hit>) -> Type:
  Pair(Pair({Nat.is_le(List.length(&2, C.Hit, hs), k) == True{} : Bool}, {sw(hs) == True{} : Bool}),
    Pair({Laws.Spec.all_scored(full, hs) == True{} : Bool},
      @j: Nat -> {Laws.Spec.complete.at(List.get(&2, Nat, full, j), hs, k, j) == True{} : Bool}))

# past the last slot: slot j is before i, or it is no slot
def comp_end.go(+full: List<&2, Nat>, +k: Nat, +i: Nat, +hs: List<&2, C.Hit>, +j: Nat, b: Bool,
  +eb: {Nat.is_lt(j, i) == b : Bool}, +ed: {List.drop(&2, Nat, full, i) == Nil{} : List<&2, Nat>},
  f: @hj: {Nat.is_lt(j, i) == True{} : Bool} ->
    {Laws.Spec.complete.at(List.get(&2, Nat, full, j), hs, k, j) == True{} : Bool}
) -> {Laws.Spec.complete.at(List.get(&2, Nat, full, j), hs, k, j) == True{} : Bool}:
  match b:
    case True{}:
      f(eb)
    case False{}:
      %Equal.sym(Maybe<&2, Nat>, List.get(&2, Nat, full, j), None{},
        drop_none(full, i, j, ed, L.nlt_le(j, i, eb))) :
        {Laws.Spec.complete.at(_, hs, k, j) == True{} : Bool}
      {==}

def sel_end(+full: List<&2, Nat>, +k: Nat, +i: Nat, +c: Nat, +hs: List<&2, C.Hit>,
  +ed: {List.drop(&2, Nat, full, i) == Nil{} : List<&2, Nat>}, inv: Inv(full, k, i, c, hs)
) -> Out(full, k, hs):
  (pa, pb) = inv
  (pc, pl) = pa
  (ec, ek) = pc
  (el, es) = pl
  (esc, comp) = pb
  ((le_eq(c, List.length(&2, C.Hit, hs), k, ec, ek), es),
    (esc, +j => comp_end.go(full, k, i, hs, j, Nat.is_lt(j, i), {==}, ed, comp(j))))

# msel over the slots from i keeps the invariant to the end
def sel_go(xs: List<&2, Nat>, +full: List<&2, Nat>, +k: Nat, +i: Nat, +c: Nat, +hs: List<&2, C.Hit>,
  +ed: {List.drop(&2, Nat, full, i) == xs : List<&2, Nat>}, inv: Inv(full, k, i, c, hs)
) -> Out(full, k, msel(xs, k, i, c, hs)):
  match xs:
    case Nil{}:
      sel_end(full, k, i, c, hs, ed, inv)
    case Con{+v, +t}:
      match v:
        case 0n:
          sel_go(t, full, k, 1n+i, c, hs, drop_next(full, i, 0n, t, ed),
            inv_zero(full, k, i, c, hs, drop_get(full, i, 0n, t, ed), inv))
        case 1n+w:
          sel_go(t, full, k, 1n+i, F.pick(Nat.is_lt(c, k), 1n+c, c),
            C.keep(Nat.is_lt(c, k), C.Hit{1n+w, i}, hs), drop_next(full, i, 1n+w, t, ed),
            inv_hit(full, k, i, c, hs, w, drop_get(full, i, 1n+w, t, ed), inv))

def inv0(+full: List<&2, Nat>, +k: Nat) -> Inv(full, k, 0n, 0n, Nil{}):
  ((({==}, L.le0(k)), ({==}, {==})), ({==}, +j => hj =>
    L.absurd({Laws.Spec.complete.at(List.get(&2, Nat, full, j), Nil{}, k, j) == True{} : Bool},
      L.not_lt0(j, hj))))

def out0(+full: List<&2, Nat>, +k: Nat) -> Out(full, k, msel(full, k, 0n, 0n, Nil{})):
  sel_go(full, full, k, 0n, 0n, Nil{}, drop0(full), inv0(full, k))

def out_le(-full: List<&2, Nat>, -k: Nat, -hs: List<&2, C.Hit>, o: Out(full, k, hs)) ->
  {Nat.is_le(List.length(&2, C.Hit, hs), k) == True{} : Bool}:
  (pa, pb) = o
  (e, es) = pa
  e

def out_sw(-full: List<&2, Nat>, -k: Nat, -hs: List<&2, C.Hit>, o: Out(full, k, hs)) ->
  {sw(hs) == True{} : Bool}:
  (pa, pb) = o
  (e, es) = pa
  es

def out_sc(-full: List<&2, Nat>, -k: Nat, -hs: List<&2, C.Hit>, o: Out(full, k, hs)) ->
  {Laws.Spec.all_scored(full, hs) == True{} : Bool}:
  (pa, pb) = o
  (esc, f) = pb
  esc

def out_comp(-full: List<&2, Nat>, -k: Nat, -hs: List<&2, C.Hit>, +j: Nat, o: Out(full, k, hs)) ->
  {Laws.Spec.complete.at(List.get(&2, Nat, full, j), hs, k, j) == True{} : Bool}:
  (pa, pb) = o
  (esc, f) = pb
  f(j)

def rev_len0(+hs: List<&2, C.Hit>) ->
  {List.length(&2, C.Hit, List.reverse(&2, C.Hit, hs)) == List.length(&2, C.Hit, hs) : Nat}:
  Equal.trans(Nat, List.length(&2, C.Hit, List.reverse(&2, C.Hit, hs)),
    Nat.add(List.length(&2, C.Hit, hs), 0n), List.length(&2, C.Hit, hs), rev_len(hs, Nil{}),
    L.add0(List.length(&2, C.Hit, hs)))

# the full branch of completeness survives the reversal
def rev_full(+hs: List<&2, C.Hit>, +k: Nat, +y: C.Hit,
  +b: {Bool.and(Nat.is_eq(List.length(&2, C.Hit, hs), k), Laws.Spec.all_before(hs, y)) == True{} : Bool}
) -> {Bool.and(Nat.is_eq(List.length(&2, C.Hit, List.reverse(&2, C.Hit, hs)), k),
    Laws.Spec.all_before(List.reverse(&2, C.Hit, hs), y)) == True{} : Bool}:
  +lh = List.length(&2, C.Hit, hs)
  +rh = List.reverse(&2, C.Hit, hs)
  %Equal.sym(Nat, List.length(&2, C.Hit, rh), lh, rev_len0(hs)) :
    {Bool.and(Nat.is_eq(_, k), Laws.Spec.all_before(rh, y)) == True{} : Bool}
  L.and_i(Nat.is_eq(lh, k), Laws.Spec.all_before(rh, y),
    L.and_l(Nat.is_eq(lh, k), Laws.Spec.all_before(hs, y), b),
    rev_ab(hs, Nil{}, y, ab_of(hs, y, L.and_r(Nat.is_eq(lh, k), Laws.Spec.all_before(hs, y), b)), {==}))

# completeness survives the reversal
def rev_comp(m: Maybe<&2, Nat>, +hs: List<&2, C.Hit>, +k: Nat, +c: Nat,
  e: {Laws.Spec.complete.at(m, hs, k, c) == True{} : Bool}
) -> {Laws.Spec.complete.at(m, List.reverse(&2, C.Hit, hs), k, c) == True{} : Bool}:
  match m:
    case None{}:
      {==}
    case Some{+v}:
      match v:
        case 0n:
          {==}
        case 1n+p:
          +y = {C.Hit{1n+p, c} : C.Hit}
          +rh = List.reverse(&2, C.Hit, hs)
          +q = Bool.and(Nat.is_eq(List.length(&2, C.Hit, rh), k), Laws.Spec.all_before(rh, y))
          -G = {Bool.or(Laws.Spec.has_id(rh, c), q) == True{} : Bool}
          or_elim(Laws.Spec.has_id(hs, c),
            Bool.and(Nat.is_eq(List.length(&2, C.Hit, hs), k), Laws.Spec.all_before(hs, y)), e, G,
            a => or_l(Laws.Spec.has_id(rh, c), q,
              rev_has(hs, Nil{}, c, or_l(Laws.Spec.has_id(hs, c), False{}, a))),
            b => L.or_r(Laws.Spec.has_id(rh, c), q, rev_full(hs, k, y, b)))

# Ranking: what the selection keeps
# ==================================
# R is the selection over the list full of slot values, best first.

def R(+full: List<&2, Nat>, +k: Nat) -> List<&2, C.Hit>:
  List.reverse(&2, C.Hit, msel(full, k, 0n, 0n, Nil{}))

# at most k hits
def sel_bounded(+full: List<&2, Nat>, +k: Nat) ->
  {Nat.is_le(List.length(&2, C.Hit, R(full, k)), k) == True{} : Bool}:
  +hs = msel(full, k, 0n, 0n, Nil{})
  le_eq(List.length(&2, C.Hit, hs), List.length(&2, C.Hit, R(full, k)), k,
    Equal.sym(Nat, List.length(&2, C.Hit, R(full, k)), List.length(&2, C.Hit, hs), rev_len0(hs)),
    out_le(full, k, hs, out0(full, k)))

# every id names a slot
def sel_lt(+full: List<&2, Nat>, +k: Nat) ->
  {Laws.Spec.all_lt(R(full, k), List.length(&2, Nat, full)) == True{} : Bool}:
  +hs = msel(full, k, 0n, 0n, Nil{})
  rev_lt(hs, Nil{}, List.length(&2, Nat, full), sc_lt(full, hs, out_sc(full, k, hs, out0(full, k))), {==})

# best first
def sel_sorted(+full: List<&2, Nat>, +k: Nat) -> {Laws.Spec.sorted(R(full, k)) == True{} : Bool}:
  +hs = msel(full, k, 0n, 0n, Nil{})
  rev_sorted(hs, Nil{}, out_sw(full, k, hs, out0(full, k)), {==}, {==})

# every hit carries its slot's positive value
def sel_scored(+full: List<&2, Nat>, +k: Nat) ->
  {Laws.Spec.all_scored(full, R(full, k)) == True{} : Bool}:
  +hs = msel(full, k, 0n, 0n, Nil{})
  rev_sc(hs, Nil{}, full, out_sc(full, k, hs, out0(full, k)), {==})

# slot c is kept, or k hits all come before it
def sel_complete(+full: List<&2, Nat>, +k: Nat, +c: Nat) ->
  {Laws.Spec.complete.at(List.get(&2, Nat, full, c), R(full, k), k, c) == True{} : Bool}:
  +hs = msel(full, k, 0n, 0n, Nil{})
  rev_comp(List.get(&2, Nat, full, c), hs, k, c, out_comp(full, k, hs, c, out0(full, k)))

# Ranking: the laws
# =================
# rank reads its n score slots into a list lv (bsel) and keeps R(lv, k).

def RankEq(-k: Nat, -n: Nat, -x: C.Tq) -> Type:
  &lv: List<&2, Nat> -> Pair({List.length(&2, Nat, lv) == n : Nat},
    {R(lv, k) == Laws.Spec.hits(C.rank.sel(k, n, x)) : List<&2, C.Hit>})

def rank_eq.of(+k: Nat, +n: Nat, -p: Array<U32>, -r: Array<Nat> & Nat,
  bs: BS(n, k, 0n, 0n, Nil{}, r, p)
) -> &lv: List<&2, Nat> -> Pair({List.length(&2, Nat, lv) == n : Nat},
  {R(lv, k) == Laws.Spec.hits(C.rank.fin(p, C.sel(n, k, 0n, C.sel.of(0n, Nil{}, r)))) :
    List<&2, C.Hit>}):
  (+lv, pr) = bs
  (pa, e3) = pr
  (e1, e2) = pa
  (lv, (e2, Equal.sym(List<&2, C.Hit>, Laws.Spec.hits(C.rank.fin(p, C.sel(n, k, 0n, C.sel.of(0n, Nil{}, r)))),
    R(lv, k), e3)))

# the hits of rank.sel are R over the n slot values it reads
def rank_eq(+k: Nat, +n: Nat, x: C.Tq) -> RankEq(k, n, x):
  match x:
    case C.Tq{p, s, q}:
      rank_eq.of(k, n, p, Array.get(Nat, s, 0), bsel(n, k, 0n, 0n, Nil{}, Array.get(Nat, s, 0), p))

def rank_bounded.of(+k: Nat, +n: Nat, -x: C.Tq, eq: RankEq(k, n, x)) ->
  {Nat.is_le(List.length(&2, C.Hit, Laws.Spec.hits(C.rank.sel(k, n, x))), k) == True{} : Bool}:
  (+lv, pr) = eq
  (el, eh) = pr
  %eh : {Nat.is_le(List.length(&2, C.Hit, _), k) == True{} : Bool}
  sel_bounded(lv, k)

def rank_ids_valid.of(+k: Nat, +n: Nat, -x: C.Tq, eq: RankEq(k, n, x)) ->
  {Laws.Spec.all_lt(Laws.Spec.hits(C.rank.sel(k, n, x)), n) == True{} : Bool}:
  (+lv, pr) = eq
  (el, eh) = pr
  %eh : {Laws.Spec.all_lt(_, n) == True{} : Bool}
  %el : {Laws.Spec.all_lt(R(lv, k), _) == True{} : Bool}
  sel_lt(lv, k)

def rank_sorted.of(+k: Nat, +n: Nat, -x: C.Tq, eq: RankEq(k, n, x)) ->
  {Laws.Spec.sorted(Laws.Spec.hits(C.rank.sel(k, n, x))) == True{} : Bool}:
  (+lv, pr) = eq
  (el, eh) = pr
  %eh : {Laws.Spec.sorted(_) == True{} : Bool}
  sel_sorted(lv, k)

def Laws.rank_bounded(k, n, tot, p, t):
  rank_bounded.of(k, n, C.add.terms(t, tot, n, C.Tq{p, [0n : Nat^F.depth(n)], 0n}),
    rank_eq(k, n, C.add.terms(t, tot, n, C.Tq{p, [0n : Nat^F.depth(n)], 0n})))

def Laws.rank_ids_valid(k, n, tot, p, t):
  rank_ids_valid.of(k, n, C.add.terms(t, tot, n, C.Tq{p, [0n : Nat^F.depth(n)], 0n}),
    rank_eq(k, n, C.add.terms(t, tot, n, C.Tq{p, [0n : Nat^F.depth(n)], 0n})))

def Laws.rank_sorted(k, n, tot, p, t):
  rank_sorted.of(k, n, C.add.terms(t, tot, n, C.Tq{p, [0n : Nat^F.depth(n)], 0n}),
    rank_eq(k, n, C.add.terms(t, tot, n, C.Tq{p, [0n : Nat^F.depth(n)], 0n})))
