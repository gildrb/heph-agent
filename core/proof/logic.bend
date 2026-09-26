import Base
import ../core.bend as C
import ../LAWS.bend as Laws
import ../score.bend as F

# Shared facts: Bool, Nat, and the hit order

# Logic
# =====

def absurd(-A: Type, x: Empty) -> A:
  match x:

def Bool.disc(b: Bool) -> Type:
  match b:
    case False{}:
      Empty
    case True{}:
      Unit

# False is not True
def ft(e: {False{} == True{} : Bool}) -> Empty:
  %Equal.sym(Bool, False{}, True{}, e) : Bool.disc(_)
  Unit{}

def and_l(a: Bool, -b: Bool, e: {Bool.and(a, b) == True{} : Bool}) -> {a == True{} : Bool}:
  match a:
    case True{}:
      {==}
    case False{}:
      absurd({False{} == True{} : Bool}, ft(e))

def and_r(a: Bool, -b: Bool, e: {Bool.and(a, b) == True{} : Bool}) -> {b == True{} : Bool}:
  match a:
    case True{}:
      e
    case False{}:
      absurd({b == True{} : Bool}, ft(e))

def and_i(-a: Bool, -b: Bool, ea: {a == True{} : Bool}, eb: {b == True{} : Bool}) ->
  {Bool.and(a, b) == True{} : Bool}:
  %Equal.sym(Bool, a, True{}, ea) : {Bool.and(_, b) == True{} : Bool}
  eb

# Nat and hit order facts
# ========================

def le0(k: Nat) -> {Nat.is_le(0n, k) == True{} : Bool}:
  match k:
    case 0n:
      {==}
    case 1n+p:
      {==}

def lt_succ_self(i: Nat) -> {Nat.is_lt(i, 1n+i) == True{} : Bool}:
  match i:
    case 0n:
      {==}
    case 1n+p:
      lt_succ_self(p)

def add0(i: Nat) -> {Nat.add(i, 0n) == i : Nat}:
  match i:
    case 0n:
      {==}
    case 1n+p:
      %add0(p) : {1n+Nat.add(p, 0n) == 1n+_ : Nat}
      {==}

def add_s(i: Nat, +f: Nat) -> {Nat.add(i, 1n+f) == 1n+Nat.add(i, f) : Nat}:
  match i:
    case 0n:
      {==}
    case 1n+p:
      %add_s(p, f) : {1n+Nat.add(p, 1n+f) == 1n+_ : Nat}
      {==}

def lt_asym(a: Nat, b: Nat, h: {Nat.is_lt(a, b) == True{} : Bool}) ->
  {Nat.is_lt(b, a) == False{} : Bool}:
  match a b:
    case 0n 0n:
      absurd({Nat.is_lt(0n, 0n) == False{} : Bool}, ft(h))
    case 0n 1n+p:
      {==}
    case 1n+p 0n:
      absurd({Nat.is_lt(0n, 1n+p) == False{} : Bool}, ft(h))
    case 1n+x 1n+y:
      lt_asym(x, y, h)

def lt_neq(a: Nat, b: Nat, h: {Nat.is_lt(a, b) == True{} : Bool}) ->
  {Nat.is_eq(a, b) == False{} : Bool}:
  match a b:
    case 0n 0n:
      absurd({Nat.is_eq(0n, 0n) == False{} : Bool}, ft(h))
    case 0n 1n+p:
      {==}
    case 1n+p 0n:
      {==}
    case 1n+x 1n+y:
      lt_neq(x, y, h)

def eq_nlt(a: Nat, b: Nat, h: {Nat.is_eq(a, b) == True{} : Bool}) ->
  {Nat.is_lt(a, b) == False{} : Bool}:
  match a b:
    case 0n 0n:
      {==}
    case 0n 1n+p:
      absurd({Nat.is_lt(0n, 1n+p) == False{} : Bool}, ft(h))
    case 1n+p 0n:
      {==}
    case 1n+x 1n+y:
      eq_nlt(x, y, h)

def eq_flip(a: Nat, b: Nat, h: {Nat.is_eq(a, b) == True{} : Bool}) ->
  {Nat.is_eq(b, a) == True{} : Bool}:
  match a b:
    case 0n 0n:
      {==}
    case 0n 1n+p:
      absurd({Nat.is_eq(1n+p, 0n) == True{} : Bool}, ft(h))
    case 1n+p 0n:
      absurd({Nat.is_eq(0n, 1n+p) == True{} : Bool}, ft(h))
    case 1n+x 1n+y:
      eq_flip(x, y, h)

def asym.go(
  +sa: Nat, +ia: Nat, +sb: Nat, +ib: Nat, c: Bool, +e: {Nat.is_lt(sb, sa) == c : Bool},
  +h: {Bool.or(c, Bool.and(Nat.is_eq(sa, sb), Nat.is_lt(ia, ib))) == True{} : Bool}
) -> {Bool.or(Nat.is_lt(sa, sb), Bool.and(Nat.is_eq(sb, sa), Nat.is_lt(ib, ia))) == False{} : Bool}:
  match c:
    case True{}:
      %Equal.sym(Bool, Nat.is_lt(sa, sb), False{}, lt_asym(sb, sa, e)) :
        {Bool.or(_, Bool.and(Nat.is_eq(sb, sa), Nat.is_lt(ib, ia))) == False{} : Bool}
      %Equal.sym(Bool, Nat.is_eq(sb, sa), False{}, lt_neq(sb, sa, e)) :
        {Bool.and(_, Nat.is_lt(ib, ia)) == False{} : Bool}
      {==}
    case False{}:
      +ea = and_l(Nat.is_eq(sa, sb), Nat.is_lt(ia, ib), h)
      +eb = and_r(Nat.is_eq(sa, sb), Nat.is_lt(ia, ib), h)
      %Equal.sym(Bool, Nat.is_lt(sa, sb), False{}, eq_nlt(sa, sb, ea)) :
        {Bool.or(_, Bool.and(Nat.is_eq(sb, sa), Nat.is_lt(ib, ia))) == False{} : Bool}
      %Equal.sym(Bool, Nat.is_eq(sb, sa), True{}, eq_flip(sa, sb, ea)) :
        {Bool.and(_, Nat.is_lt(ib, ia)) == False{} : Bool}
      %Equal.sym(Bool, Nat.is_lt(ib, ia), False{}, lt_asym(ia, ib, eb)) :
        {_ == False{} : Bool}
      {==}

# before is asymmetric
def asym(x: C.Hit, y: C.Hit, h: {C.before(x, y) == True{} : Bool}) ->
  {C.before(y, x) == False{} : Bool}:
  match x y:
    case C.Hit{+sa, +ia} C.Hit{+sb, +ib}:
      asym.go(sa, ia, sb, ib, Nat.is_lt(sb, sa), {==}, h)

def nb(x: C.Hit, y: C.Hit, e: {C.before(x, y) == False{} : Bool}) ->
  {Bool.not(Laws.Spec.before(x, y)) == True{} : Bool}:
  match x y:
    case C.Hit{+sa, +ia} C.Hit{+sb, +ib}:
      %Equal.sym(Bool,
        Bool.or(Nat.is_lt(sb, sa), Bool.and(Nat.is_eq(sa, sb), Nat.is_lt(ia, ib))),
        False{}, e) : {Bool.not(_) == True{} : Bool}
      {==}

# More Nat facts
# ==============

def not_lt0(s: Nat, h: {Nat.is_lt(s, 0n) == True{} : Bool}) -> Empty:
  match s:
    case 0n:
      ft(h)
    case 1n+p:
      ft(h)

def tf(e: {True{} == False{} : Bool}) -> Empty:
  %e : Bool.disc(_)
  Unit{}

def lt_pred(s: Nat, x: Nat, h: {Nat.is_lt(1n+s, x) == True{} : Bool}) ->
  {Nat.is_lt(s, x) == True{} : Bool}:
  match s x:
    case 0n 0n:
      absurd({Nat.is_lt(0n, 0n) == True{} : Bool}, ft(h))
    case 0n 1n+p:
      {==}
    case 1n+q 0n:
      absurd({Nat.is_lt(1n+q, 0n) == True{} : Bool}, ft(h))
    case 1n+q 1n+p:
      lt_pred(q, p, h)

def lt_s.go(+s: Nat, +x: Nat, c: Bool, +e: {Nat.is_lt(1n+s, x) == c : Bool}) ->
  {Nat.is_lt(s, F.pick(c, x, 1n+s)) == True{} : Bool}:
  match c:
    case True{}:
      lt_pred(s, x, e)
    case False{}:
      lt_succ_self(s)

def lt_s(+s: Nat, +x: Nat) -> {Nat.is_lt(s, C.max(1n+s, x)) == True{} : Bool}:
  lt_s.go(s, x, Nat.is_lt(1n+s, x), {==})

def lt_min.go(+s: Nat, +a: Nat, +b: Nat, c: Bool, ha: {Nat.is_lt(s, a) == True{} : Bool},
  hb: {Nat.is_lt(s, b) == True{} : Bool}
) -> {Nat.is_lt(s, F.pick(c, a, b)) == True{} : Bool}:
  match c:
    case True{}:
      ha
    case False{}:
      hb

def lt_min(+s: Nat, +a: Nat, +b: Nat, ha: {Nat.is_lt(s, a) == True{} : Bool},
  hb: {Nat.is_lt(s, b) == True{} : Bool}
) -> {Nat.is_lt(s, C.min(a, b)) == True{} : Bool}:
  lt_min.go(s, a, b, Nat.is_lt(a, b), ha, hb)

def nlt_le(e: Nat, y: Nat, h: {Nat.is_lt(e, y) == False{} : Bool}) ->
  {Nat.is_le(y, e) == True{} : Bool}:
  match e y:
    case 0n 0n:
      {==}
    case 0n 1n+p:
      absurd({Nat.is_le(1n+p, 0n) == True{} : Bool}, tf(h))
    case 1n+p 0n:
      {==}
    case 1n+a 1n+b:
      nlt_le(a, b, h)

def lt_le(a: Nat, b: Nat, h: {Nat.is_lt(a, b) == True{} : Bool}) ->
  {Nat.is_le(a, b) == True{} : Bool}:
  match a b:
    case 0n 0n:
      {==}
    case 0n 1n+p:
      {==}
    case 1n+p 0n:
      absurd({Nat.is_le(1n+p, 0n) == True{} : Bool}, ft(h))
    case 1n+x 1n+y:
      lt_le(x, y, h)

def le_lt(a: Nat, b: Nat, c: Nat, hab: {Nat.is_le(a, b) == True{} : Bool},
  hbc: {Nat.is_lt(b, c) == True{} : Bool}
) -> {Nat.is_lt(a, c) == True{} : Bool}:
  match a b c:
    case _ _ 0n:
      absurd({Nat.is_lt(a, 0n) == True{} : Bool}, not_lt0(b, hbc))
    case 0n _ 1n+cp:
      {==}
    case 1n+ap 0n 1n+cp:
      absurd({Nat.is_lt(1n+ap, 1n+cp) == True{} : Bool}, ft(hab))
    case 1n+ap 1n+bp 1n+cp:
      le_lt(ap, bp, cp, hab, hbc)

def le_refl(n: Nat) -> {Nat.is_le(n, n) == True{} : Bool}:
  match n:
    case 0n:
      {==}
    case 1n+p:
      le_refl(p)

def eq_refl(n: Nat) -> {Nat.is_eq(n, n) == True{} : Bool}:
  match n:
    case 0n:
      {==}
    case 1n+p:
      eq_refl(p)

def min_le.go(+e: Nat, +y: Nat, c: Bool, +h: {Nat.is_lt(e, y) == c : Bool}) ->
  {Nat.is_le(F.pick(c, e, y), e) == True{} : Bool}:
  match c:
    case True{}:
      le_refl(e)
    case False{}:
      nlt_le(e, y, h)

def min_le(+e: Nat, +y: Nat) -> {Nat.is_le(C.min(e, y), e) == True{} : Bool}:
  min_le.go(e, y, Nat.is_lt(e, y), {==})

def clamp_gt(+s: Nat, +e: Nat, +x: Nat, h: {Nat.is_lt(s, e) == True{} : Bool}) ->
  {Nat.is_lt(s, C.clamp(s, e, x)) == True{} : Bool}:
  lt_min(s, e, C.max(1n+s, x), h, lt_s(s, x))

def clamp_le(+s: Nat, +e: Nat, +x: Nat) -> {Nat.is_le(C.clamp(s, e, x), e) == True{} : Bool}:
  min_le(e, C.max(1n+s, x))

# Ranking: more logic
# ===================

def or_r(a: Bool, -b: Bool, e: {b == True{} : Bool}) -> {Bool.or(a, b) == True{} : Bool}:
  match a:
    case True{}:
      {==}
    case False{}:
      e

def orF_l(a: Bool, -b: Bool, e: {Bool.or(a, b) == False{} : Bool}) -> {a == False{} : Bool}:
  match a:
    case False{}:
      {==}
    case True{}:
      absurd({True{} == False{} : Bool}, tf(e))

def orF_r(a: Bool, -b: Bool, e: {Bool.or(a, b) == False{} : Bool}) -> {b == False{} : Bool}:
  match a:
    case False{}:
      e
    case True{}:
      absurd({b == False{} : Bool}, tf(e))

def andF_r(a: Bool, -b: Bool, ea: {a == True{} : Bool}, e: {Bool.and(a, b) == False{} : Bool}) ->
  {b == False{} : Bool}:
  match a:
    case True{}:
      e
    case False{}:
      absurd({b == False{} : Bool}, ft(ea))

def not_t(b: Bool, e: {Bool.not(b) == True{} : Bool}) -> {b == False{} : Bool}:
  match b:
    case False{}:
      {==}
    case True{}:
      absurd({True{} == False{} : Bool}, ft(e))

def clash(-b: Bool, t: {b == True{} : Bool}, f: {b == False{} : Bool}) -> Empty:
  tf(Equal.trans(Bool, True{}, b, False{}, Equal.sym(Bool, b, True{}, t), f))

# Ranking: Nat facts
# ==================

def lt_irr(a: Nat) -> {Nat.is_lt(a, a) == False{} : Bool}:
  match a:
    case 0n:
      {==}
    case 1n+p:
      lt_irr(p)

def eq_prop(a: Nat, b: Nat, h: {Nat.is_eq(a, b) == True{} : Bool}) -> {a == b : Nat}:
  match a b:
    case 0n 0n:
      {==}
    case 0n 1n+p:
      absurd({0n == 1n+p : Nat}, ft(h))
    case 1n+p 0n:
      absurd({1n+p == 0n : Nat}, ft(h))
    case 1n+x 1n+y:
      %eq_prop(x, y, h) : {1n+x == 1n+_ : Nat}
      {==}

def neq_flip(a: Nat, b: Nat, h: {Nat.is_eq(a, b) == False{} : Bool}) ->
  {Nat.is_eq(b, a) == False{} : Bool}:
  match a b:
    case 0n 0n:
      absurd({Nat.is_eq(0n, 0n) == False{} : Bool}, tf(h))
    case 0n 1n+p:
      {==}
    case 1n+p 0n:
      {==}
    case 1n+x 1n+y:
      neq_flip(x, y, h)

# neither is below the other: equal
def tri(a: Nat, b: Nat, h1: {Nat.is_lt(a, b) == False{} : Bool},
  h2: {Nat.is_lt(b, a) == False{} : Bool}
) -> {Nat.is_eq(a, b) == True{} : Bool}:
  match a b:
    case 0n 0n:
      {==}
    case 0n 1n+p:
      absurd({Nat.is_eq(0n, 1n+p) == True{} : Bool}, tf(h1))
    case 1n+p 0n:
      absurd({Nat.is_eq(1n+p, 0n) == True{} : Bool}, tf(h2))
    case 1n+x 1n+y:
      tri(x, y, h1, h2)

# not below and not equal: above
def tri2(a: Nat, b: Nat, h1: {Nat.is_lt(a, b) == False{} : Bool},
  h2: {Nat.is_eq(a, b) == False{} : Bool}
) -> {Nat.is_lt(b, a) == True{} : Bool}:
  match a b:
    case 0n 0n:
      absurd({Nat.is_lt(0n, 0n) == True{} : Bool}, tf(h2))
    case 0n 1n+p:
      absurd({Nat.is_lt(1n+p, 0n) == True{} : Bool}, tf(h1))
    case 1n+p 0n:
      {==}
    case 1n+x 1n+y:
      tri2(x, y, h1, h2)

def lt_sle(a: Nat, b: Nat, h: {Nat.is_lt(a, b) == True{} : Bool}) ->
  {Nat.is_le(1n+a, b) == True{} : Bool}:
  match a b:
    case 0n 0n:
      absurd({Nat.is_le(1n, 0n) == True{} : Bool}, ft(h))
    case 0n 1n+p:
      le0(p)
    case 1n+p 0n:
      absurd({Nat.is_le(2n+p, 0n) == True{} : Bool}, ft(h))
    case 1n+x 1n+y:
      lt_sle(x, y, h)

def le_nlt_eq(a: Nat, b: Nat, h1: {Nat.is_le(a, b) == True{} : Bool},
  h2: {Nat.is_lt(a, b) == False{} : Bool}
) -> {a == b : Nat}:
  match a b:
    case 0n 0n:
      {==}
    case 0n 1n+p:
      absurd({0n == 1n+p : Nat}, tf(h2))
    case 1n+p 0n:
      absurd({1n+p == 0n : Nat}, ft(h1))
    case 1n+x 1n+y:
      %le_nlt_eq(x, y, h1, h2) : {1n+x == 1n+_ : Nat}
      {==}

# Ranking: the hit order
# ======================

def irr(y: C.Hit) -> {C.before(y, y) == False{} : Bool}:
  match y:
    case C.Hit{+s, +i}:
      %Equal.sym(Bool, Nat.is_lt(s, s), False{}, lt_irr(s)) :
        {Bool.or(_, Bool.and(Nat.is_eq(s, s), Nat.is_lt(i, i))) == False{} : Bool}
      %Equal.sym(Bool, Nat.is_eq(s, s), True{}, eq_refl(s)) :
        {Bool.or(False{}, Bool.and(_, Nat.is_lt(i, i))) == False{} : Bool}
      %Equal.sym(Bool, Nat.is_lt(i, i), False{}, lt_irr(i)) :
        {Bool.or(False{}, Bool.and(True{}, _)) == False{} : Bool}
      {==}

# c1 = sb < sa, c2 = sc < sb
def trans.go(
  +sa: Nat, +ia: Nat, +sb: Nat, +ib: Nat, +sc: Nat, +ic: Nat,
  c1: Bool, +e1: {Nat.is_lt(sb, sa) == c1 : Bool}, c2: Bool, +e2: {Nat.is_lt(sc, sb) == c2 : Bool},
  +h1: {Bool.or(c1, Bool.and(Nat.is_eq(sa, sb), Nat.is_lt(ia, ib))) == True{} : Bool},
  +h2: {Bool.or(c2, Bool.and(Nat.is_eq(sb, sc), Nat.is_lt(ib, ic))) == True{} : Bool}
) -> {Bool.or(Nat.is_lt(sc, sa), Bool.and(Nat.is_eq(sa, sc), Nat.is_lt(ia, ic))) == True{} : Bool}:
  match c1 c2:
    case True{} True{}:
      %Equal.sym(Bool, Nat.is_lt(sc, sa), True{}, le_lt(sc, sb, sa, lt_le(sc, sb, e2), e1)) :
        {Bool.or(_, Bool.and(Nat.is_eq(sa, sc), Nat.is_lt(ia, ic))) == True{} : Bool}
      {==}
    case True{} False{}:
      +eb = eq_prop(sb, sc, and_l(Nat.is_eq(sb, sc), Nat.is_lt(ib, ic), h2))
      %eb : {Bool.or(Nat.is_lt(_, sa), Bool.and(Nat.is_eq(sa, _), Nat.is_lt(ia, ic))) == True{} : Bool}
      %Equal.sym(Bool, Nat.is_lt(sb, sa), True{}, e1) :
        {Bool.or(_, Bool.and(Nat.is_eq(sa, sb), Nat.is_lt(ia, ic))) == True{} : Bool}
      {==}
    case False{} True{}:
      +ea = eq_prop(sa, sb, and_l(Nat.is_eq(sa, sb), Nat.is_lt(ia, ib), h1))
      %Equal.sym(Nat, sa, sb, ea) :
        {Bool.or(Nat.is_lt(sc, _), Bool.and(Nat.is_eq(_, sc), Nat.is_lt(ia, ic))) == True{} : Bool}
      %Equal.sym(Bool, Nat.is_lt(sc, sb), True{}, e2) :
        {Bool.or(_, Bool.and(Nat.is_eq(sb, sc), Nat.is_lt(ia, ic))) == True{} : Bool}
      {==}
    case False{} False{}:
      +ea = eq_prop(sa, sb, and_l(Nat.is_eq(sa, sb), Nat.is_lt(ia, ib), h1))
      +eb = eq_prop(sb, sc, and_l(Nat.is_eq(sb, sc), Nat.is_lt(ib, ic), h2))
      +lab = and_r(Nat.is_eq(sa, sb), Nat.is_lt(ia, ib), h1)
      +lbc = and_r(Nat.is_eq(sb, sc), Nat.is_lt(ib, ic), h2)
      %Equal.sym(Nat, sa, sb, ea) :
        {Bool.or(Nat.is_lt(sc, _), Bool.and(Nat.is_eq(_, sc), Nat.is_lt(ia, ic))) == True{} : Bool}
      %eb : {Bool.or(Nat.is_lt(_, sb), Bool.and(Nat.is_eq(sb, _), Nat.is_lt(ia, ic))) == True{} : Bool}
      or_r(Nat.is_lt(sb, sb), Bool.and(Nat.is_eq(sb, sb), Nat.is_lt(ia, ic)),
        and_i(Nat.is_eq(sb, sb), Nat.is_lt(ia, ic), eq_refl(sb),
          le_lt(ia, ib, ic, lt_le(ia, ib, lab), lbc)))

# before is transitive
def trans(x: C.Hit, y: C.Hit, z: C.Hit, h1: {C.before(x, y) == True{} : Bool},
  h2: {C.before(y, z) == True{} : Bool}
) -> {C.before(x, z) == True{} : Bool}:
  match x y z:
    case C.Hit{+sa, +ia} C.Hit{+sb, +ib} C.Hit{+sc, +ic}:
      trans.go(sa, ia, sb, ib, sc, ic, Nat.is_lt(sb, sa), {==}, Nat.is_lt(sc, sb), {==}, h1, h2)

# c = sy < sh
def total.go(+sh: Nat, +ih: Nat, +sy: Nat, +iy: Nat, c: Bool, +ec: {Nat.is_lt(sy, sh) == c : Bool},
  +e1: {Bool.or(Nat.is_lt(sh, sy), Bool.and(Nat.is_eq(sy, sh), Nat.is_lt(iy, ih))) == False{} : Bool},
  +e2: {Nat.is_eq(ih, iy) == False{} : Bool}
) -> {Bool.or(c, Bool.and(Nat.is_eq(sh, sy), Nat.is_lt(ih, iy))) == True{} : Bool}:
  match c:
    case True{}:
      {==}
    case False{}:
      +l1 = orF_l(Nat.is_lt(sh, sy), Bool.and(Nat.is_eq(sy, sh), Nat.is_lt(iy, ih)), e1)
      +es = tri(sh, sy, l1, ec)
      +l2 = andF_r(Nat.is_eq(sy, sh), Nat.is_lt(iy, ih), eq_flip(sh, sy, es),
        orF_r(Nat.is_lt(sh, sy), Bool.and(Nat.is_eq(sy, sh), Nat.is_lt(iy, ih)), e1))
      and_i(Nat.is_eq(sh, sy), Nat.is_lt(ih, iy), es, tri2(iy, ih, l2, neq_flip(ih, iy, e2)))

# of two hits on different chunks, one comes before the other
def total(+sh: Nat, +ih: Nat, +sy: Nat, +iy: Nat,
  e1: {C.before(C.Hit{sy, iy}, C.Hit{sh, ih}) == False{} : Bool},
  e2: {Nat.is_eq(ih, iy) == False{} : Bool}
) -> {C.before(C.Hit{sh, ih}, C.Hit{sy, iy}) == True{} : Bool}:
  total.go(sh, ih, sy, iy, Nat.is_lt(sy, sh), {==}, e1, e2)
