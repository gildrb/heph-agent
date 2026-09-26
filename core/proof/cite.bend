import Base
import ../core.bend as C
import ../LAWS.bend as Laws
import ../score.bend as F
import ./logic.bend as L
import ./array.bend as A

# Citations: BadId
# ================
# The search only ever answers Empty, Missing or Found.

def vd(r: Array<U32> & C.Verdict) -> C.Verdict:
  (a, v) = r
  v

def VB(-ev: Array<U32>, -r: Array<U32> & C.Verdict) -> Type:
  {Laws.Spec.is_bad(Laws.Spec.verdict(C.verify.fin(ev, r))) == False{} : Bool}

# the search after the next step, given every lookup that follows
def IH(-ev: Array<U32>, -g: Nat, -j: C.Job, -e: Nat) -> Type:
  @a: Array<U32> -> @t: C.Tab -> @c: C.Cur -> @+i: Nat -> @+k: Nat ->
    VB(ev, C.find(g, j, e, C.FLook{a, t, c, i, k}))

def nb_got(-ev: Array<U32>, g: Nat, -j: C.Job, -e: Nat, -a: Array<U32>, -t: C.Tab, -v: C.Verdict,
  h: {Laws.Spec.is_bad(v) == False{} : Bool}
) -> VB(ev, C.find(g, j, e, C.FGot{a, t, v})):
  match g:
    case 0n:
      h
    case 1n+f:
      h

def nb_again(-ev: Array<U32>, -g: Nat, -j: C.Job, -e: Nat, c: C.Cur, +i: Nat, a: Array<U32>, pat: Array<U32>,
  r: Array<U32> & U32, ih: IH(ev, g, j, e)
) -> VB(ev, C.find(g, j, e, C.find.again(c, i, a, pat, r))):
  match r:
    case (f, k):
      ih(a, C.Tab{pat, f}, c, i, U32.to_nat(k))

def nb_miss(-ev: Array<U32>, -g: Nat, -j: C.Job, -e: Nat, c: C.Cur, +i: Nat, +k: Nat, a: Array<U32>, t: C.Tab,
  ih: IH(ev, g, j, e)
) -> VB(ev, C.find(g, j, e, C.find.miss(c, i, k, a, t))):
  match t:
    case C.Tab{pat, f}:
      nb_again(ev, g, j, e, c, i, a, pat, Array.get(U32, f, U32.from_nat(Nat.sub(k, 1n))), ih)

def nb_ok(-ev: Array<U32>, g: Nat, -j: C.Job, -e: Nat, +at: Nat, c: C.Cur, +i: Nat, +k: Nat, t: C.Tab,
  r: Array<U32> & Bool, ih: IH(ev, g, j, e)
) -> VB(ev, C.find(g, j, e, C.find.ok(at, c, i, k, t, r))):
  match r:
    case (a, ok):
      match ok:
        case True{}:
          nb_got(ev, g, j, e, a, t, C.Found{at}, {==})
        case False{}:
          nb_miss(ev, g, j, e, c, i, k, a, t, ih)

def nb_full(-ev: Array<U32>, g: Nat, +j: C.Job, -e: Nat, full: Bool, c: C.Cur, +i: Nat, +k: Nat, t: C.Tab,
  a: Array<U32>, ih: IH(ev, g, j, e)
) -> VB(ev, C.find(g, j, e, C.find.full(full, j, c, i, k, t, a))):
  match j full:
    case _ False{}:
      ih(a, t, c, i, k)
    case C.Job{+eo, +el, +qo, +ql, +m} True{}:
      nb_ok(ev, g, C.Job{eo, el, qo, ql, m}, e, Nat.sub(i, m), c, i, k, t,
        C.check(a, eo, el, qo, ql, Nat.sub(i, m)), ih)

def nb_k(-ev: Array<U32>, g: Nat, +j: C.Job, -e: Nat, c: C.Cur, +i: Nat, a: Array<U32>, r: C.Tab & Nat,
  ih: IH(ev, g, j, e)
) -> VB(ev, C.find(g, j, e, C.find.k(j, c, i, a, r))):
  match r:
    case (t, +k):
      nb_full(ev, g, j, e, Nat.is_eq(k, C.job.m(j)), c, i, k, t, a, ih)

def nb_byte(-ev: Array<U32>, g: Nat, +j: C.Job, -e: Nat, +i: Nat, +k: Nat, t: C.Tab, r: Array<U32> & C.Emit,
  ih: IH(ev, g, j, e)
) -> VB(ev, C.find(g, j, e, C.find.byte(j, i, k, t, r))):
  match r:
    case (a, x):
      match x:
        case C.EStop{}:
          nb_got(ev, g, j, e, a, t, C.Missing{}, {==})
        case C.EByte{+y, c}:
          nb_k(ev, g, j, e, c, 1n+i, a, C.fall(1n+k, y, C.fall.look(k, y, t)), ih)

def nb_look(-ev: Array<U32>, fuel: Nat, +j: C.Job, +e: Nat, a: Array<U32>, t: C.Tab, c: C.Cur, +i: Nat, +k: Nat) ->
  VB(ev, C.find(fuel, j, e, C.FLook{a, t, c, i, k})):
  match fuel:
    case 0n:
      {==}
    case 1n++g:
      nb_byte(ev, g, j, e, i, k, t, C.step(e, c, a),
        a2 => t2 => c2 => i2 => k2 => nb_look(ev, g, j, e, a2, t2, c2, i2, k2))

def nb_pat(-ev: Array<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat, +d: Nat, x: C.Pat) ->
  VB(ev, C.search.pat(eo, el, qo, ql, d, x)):
  match x:
    case C.Pat{a, pat, m}:
      match m:
        case 0n:
          {==}
        case 1n++mm:
          nb_look(ev, 1n+Nat.mul(2n, el), C.Job{eo, el, qo, ql, 1n+mm}, Nat.add(eo, el), a,
            C.tab.fill(mm, 1n, (C.Tab{pat, [0 : U32^d]}, 0n)), C.Cur{eo, C.SLead{}}, 0n, 0n)

def nb_search(-ev: Array<U32>, a: Array<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat) ->
  VB(ev, C.search(a, eo, el, qo, ql)):
  +d = F.depth(1n+ql)
  +eq = Nat.add(qo, ql)
  nb_pat(ev, eo, el, qo, ql, d,
    C.pat.fill(1n+Nat.mul(2n, ql), eq, 0n, [0 : U32^d], C.step(eq, C.Cur{qo, C.SLead{}}, a)))

def bad.len(+qo: Nat, +ql: Nat, a: Array<U32>, +eo: Nat, r: Array<U32> & U32) ->
  {Laws.Spec.is_bad(Laws.Spec.verdict(C.verify.len(qo, ql, a, eo, r))) ==
    Maybe.is_none(&2, Laws.Spec.Item, Laws.Spec.item.len(eo, r)) : Bool}:
  match r:
    case (ev, el):
      nb_search(ev, a, eo, U32.to_nat(el), qo, ql)

def bad.off(+id: Nat, +qo: Nat, +ql: Nat, a: Array<U32>, r: Array<U32> & U32) ->
  {Laws.Spec.is_bad(Laws.Spec.verdict(C.verify.off(id, qo, ql, a, r))) ==
    Maybe.is_none(&2, Laws.Spec.Item, Laws.Spec.item.off(id, r)) : Bool}:
  match r:
    case (ev, eo):
      bad.len(qo, ql, a, U32.to_nat(eo), Array.get(U32, ev, U32.from_nat(1n+Nat.mul(2n, id))))

def bad.if(ok: Bool, +id: Nat, +qo: Nat, +ql: Nat, a: Array<U32>, ev: Array<U32>) ->
  {Laws.Spec.is_bad(Laws.Spec.verdict(C.verify.if(ok, id, qo, ql, a, ev))) ==
    Maybe.is_none(&2, Laws.Spec.Item, Laws.Spec.item.if(ok, id, ev)) : Bool}:
  match ok:
    case False{}:
      {==}
    case True{}:
      bad.off(id, qo, ql, a, Array.get(U32, ev, U32.from_nat(Nat.mul(2n, id))))

def Laws.cite_badid(a, ev, c, id, qo, ql):
  bad.if(Nat.is_lt(id, c), id, qo, ql, a, ev)

# Citations: soundness
# ====================
# The request array is emb(tr) throughout, so every read of it is a pure
# function of tr. The streaming normalizer (step) is followed against
# Spec.norm.go, check against Spec.quoted, and the search reaches Found only
# through a passing check.

# Nat, Bool and U32 facts
# -----------------------

def ndisc(n: Nat) -> Type:
  match n:
    case 0n:
      Unit
    case 1n+p:
      Empty

# 0 is no successor
def zs(-q: Nat, h: {0n == 1n+q : Nat}) -> Empty:
  %h : ndisc(_)
  Unit{}

def npred(n: Nat) -> Nat:
  match n:
    case 0n:
      0n
    case 1n+p:
      p

def sub0(e: Nat) -> {Nat.sub(e, 0n) == e : Nat}:
  match e:
    case 0n:
      {==}
    case 1n+p:
      {==}

# e - p = q + 1 gives e - (p + 1) = q
def sub_s(e: Nat, p: Nat, -q: Nat, h: {Nat.sub(e, p) == 1n+q : Nat}) -> {Nat.sub(e, 1n+p) == q : Nat}:
  match e p:
    case 0n 0n:
      L.absurd({Nat.sub(0n, 1n) == q : Nat}, zs(q, h))
    case 0n 1n+pp:
      L.absurd({Nat.sub(0n, 1n+1n+pp) == q : Nat}, zs(q, h))
    case 1n+ee 0n:
      Equal.trans(Nat, Nat.sub(ee, 0n), ee, q, sub0(ee), Equal.cong(Nat, Nat, npred, 1n+ee, 1n+q, h))
    case 1n+ee 1n+pp:
      sub_s(ee, pp, q, h)

# e - p = q + 1 gives p < e
def sub_lt(e: Nat, p: Nat, -q: Nat, h: {Nat.sub(e, p) == 1n+q : Nat},
  hi: {Nat.is_lt(p, e) == False{} : Bool}
) -> Empty:
  match e p:
    case 0n 0n:
      zs(q, h)
    case 0n 1n+pp:
      zs(q, h)
    case 1n+ee 0n:
      L.tf(hi)
    case 1n+ee 1n+pp:
      sub_lt(ee, pp, q, h, hi)

# e - p = 0 gives p >= e
def sub_z(e: Nat, p: Nat, h: {Nat.sub(e, p) == 0n : Nat}, hi: {Nat.is_lt(p, e) == True{} : Bool}) -> Empty:
  match e p:
    case 0n 0n:
      L.ft(hi)
    case 0n 1n+pp:
      L.ft(hi)
    case 1n+ee 0n:
      zs(ee, Equal.sym(Nat, 1n+ee, 0n, h))
    case 1n+ee 1n+pp:
      sub_z(ee, pp, h, hi)

# (a + b) - a = b
def sub_add(a: Nat, +b: Nat) -> {Nat.sub(Nat.add(a, b), a) == b : Nat}:
  match a:
    case 0n:
      sub0(b)
    case 1n+p:
      sub_add(p, b)

# take and drop of 0
def take0(xs: List<&2, U32>) -> {List.take(&2, U32, xs, 0n) == Nil{} : List<&2, U32>}:
  match xs:
    case Nil{}:
      {==}
    case Con{x, t}:
      {==}

def drop0(xs: List<&2, U32>) -> {List.drop(&2, U32, xs, 0n) == xs : List<&2, U32>}:
  match xs:
    case Nil{}:
      {==}
    case Con{x, t}:
      {==}

# Bool and Word comparison: EQ one way is EQ the other way
def bsym(a: Bool, b: Bool, h: {Cmp.is_eq(Bool.cmp(a, b)) == True{} : Bool}) ->
  {Cmp.is_eq(Bool.cmp(b, a)) == True{} : Bool}:
  match a b:
    case True{} True{}:
      {==}
    case True{} False{}:
      L.absurd({Cmp.is_eq(Bool.cmp(False{}, True{})) == True{} : Bool}, L.ft(h))
    case False{} True{}:
      L.absurd({Cmp.is_eq(Bool.cmp(True{}, False{})) == True{} : Bool}, L.ft(h))
    case False{} False{}:
      {==}

# is_eq holds only on EQ
def ceq(c: Cmp, h: {Cmp.is_eq(c) == True{} : Bool}) -> {c == EQ{} : Cmp}:
  match c:
    case LT{}:
      L.absurd({LT{} == EQ{} : Cmp}, L.ft(h))
    case EQ{}:
      {==}
    case GT{}:
      L.absurd({GT{} == EQ{} : Cmp}, L.ft(h))

def finsym(ab: Bool, bb: Bool, t1: Cmp, +t2: Cmp, h: {Cmp.is_eq(Word.cmp.fin(ab, bb, t1)) == True{} : Bool},
  ih: @h1: {Cmp.is_eq(t1) == True{} : Bool} -> {Cmp.is_eq(t2) == True{} : Bool}
) -> {Cmp.is_eq(Word.cmp.fin(bb, ab, t2)) == True{} : Bool}:
  match t1:
    case LT{}:
      L.absurd({Cmp.is_eq(Word.cmp.fin(bb, ab, t2)) == True{} : Bool}, L.ft(h))
    case EQ{}:
      %Equal.sym(Cmp, t2, EQ{}, ceq(t2, ih({==}))) : {Cmp.is_eq(Word.cmp.fin(bb, ab, _)) == True{} : Bool}
      bsym(ab, bb, h)
    case GT{}:
      L.absurd({Cmp.is_eq(Word.cmp.fin(bb, ab, t2)) == True{} : Bool}, L.ft(h))

def wsym(n: Nat, a: Word(n), b: Word(n), h: {Cmp.is_eq(Word.cmp(n, a, b)) == True{} : Bool}) ->
  {Cmp.is_eq(Word.cmp(n, b, a)) == True{} : Bool}:
  match n:
    case 0n:
      {==}
    case 1n++p:
      match a b:
        case WCon{ab, +at} WCon{bb, +bt}:
          finsym(ab, bb, Word.cmp(p, at, bt), Word.cmp(p, bt, at), h, h1 => wsym(p, at, bt, h1))

# U32 equality is symmetric
def usym(y: U32, z: U32, h: {U32.is_eq(y, z) == True{} : Bool}) -> {U32.is_eq(z, y) == True{} : Bool}:
  match y z:
    case U32{x} U32{w}:
      wsym(32n, x, w, h)

# The streaming normalizer
# ------------------------
# A cursor state is a (gap, any) pair of Spec.norm.go: Lead (False, False),
# Word (False, True), Gap (True, True). Lead is also the state just after an
# emitted space; there the next byte is a word byte, so any does not matter.

def gap(st: C.St) -> Bool:
  match st:
    case C.SLead{}:
      False{}
    case C.SWord{}:
      False{}
    case C.SGap{}:
      True{}

def any(st: C.St) -> Bool:
  match st:
    case C.SLead{}:
      False{}
    case C.SWord{}:
      True{}
    case C.SGap{}:
      True{}

# the normalized bytes still to come from the m raw bytes at p in state st
def NLm(+tr: A.Tr<U32>, m: Nat, +p: Nat, +st: C.St) -> List<&2, U32>:
  Laws.Spec.norm.go(A.bytes(tr, m, p), gap(st), any(st))

# the same for cursor c before raw end e
def NL(+tr: A.Tr<U32>, +e: Nat, c: C.Cur) -> List<&2, U32>:
  match c:
    case C.Cur{+p, +st}:
      NLm(tr, Nat.sub(e, p), p, st)

# a whitespace byte puts nothing and leaves gap = any
def go_ws(+b: U32, +rest: List<&2, U32>, +gp: Bool, +an: Bool, hw: {C.is_ws(b) == True{} : Bool}) ->
  {Laws.Spec.norm.go(rest, an, an) == Laws.Spec.norm.go(b <> rest, gp, an) : List<&2, U32>}:
  %Equal.sym(Bool, C.is_ws(b), True{}, hw) : {Laws.Spec.norm.go(rest, an, an) ==
    List.append(&2, U32, Laws.Spec.norm.put(_, b, gp),
      Laws.Spec.norm.go(rest, Bool.pick(Bool, _, an, False{}), Bool.pick(Bool, _, an, True{}))) :
    List<&2, U32>}
  {==}

# a word byte puts itself (after a space when gp) and moves to (False, True)
def go_word(+b: U32, +rest: List<&2, U32>, +gp: Bool, +an: Bool, hw: {C.is_ws(b) == False{} : Bool}) ->
  {Laws.Spec.norm.go(b <> rest, gp, an) ==
    List.append(&2, U32, Laws.Spec.norm.put(False{}, b, gp), Laws.Spec.norm.go(rest, False{}, True{})) :
    List<&2, U32>}:
  %Equal.sym(Bool, C.is_ws(b), False{}, hw) : {
    List.append(&2, U32, Laws.Spec.norm.put(_, b, gp),
      Laws.Spec.norm.go(rest, Bool.pick(Bool, _, an, False{}), Bool.pick(Bool, _, an, True{}))) ==
    List.append(&2, U32, Laws.Spec.norm.put(False{}, b, gp), Laws.Spec.norm.go(rest, False{}, True{})) :
    List<&2, U32>}
  {==}

# skipping a whitespace byte at p is ws.st
def nl_ws(+q: Nat, +tr: A.Tr<U32>, +p: Nat, st: C.St, hw: {C.is_ws(A.bv(tr, p)) == True{} : Bool}) ->
  {NLm(tr, q, 1n+p, C.ws.st(st)) == NLm(tr, 1n+q, p, st) : List<&2, U32>}:
  match st:
    case C.SLead{}:
      go_ws(A.bv(tr, p), A.bytes(tr, q, 1n+p), False{}, False{}, hw)
    case C.SWord{}:
      go_ws(A.bv(tr, p), A.bytes(tr, q, 1n+p), False{}, True{}, hw)
    case C.SGap{}:
      go_ws(A.bv(tr, p), A.bytes(tr, q, 1n+p), True{}, True{}, hw)

# a word byte at p, e - p = q + 1: what st puts, then the cursor after p in Word
def nl_word(+q: Nat, +tr: A.Tr<U32>, +p: Nat, +e: Nat, +st: C.St, h: {Nat.sub(e, p) == 1n+q : Nat},
  hw: {C.is_ws(A.bv(tr, p)) == False{} : Bool}
) -> {NLm(tr, 1n+q, p, st) == List.append(&2, U32, Laws.Spec.norm.put(False{}, A.bv(tr, p), gap(st)),
  NL(tr, e, C.Cur{1n+p, C.SWord{}})) : List<&2, U32>}:
  %Equal.sym(Nat, Nat.sub(e, 1n+p), q, sub_s(e, p, q, h)) : {NLm(tr, 1n+q, p, st) ==
    List.append(&2, U32, Laws.Spec.norm.put(False{}, A.bv(tr, p), gap(st)), NLm(tr, _, 1n+p, C.SWord{})) :
    List<&2, U32>}
  go_word(A.bv(tr, p), A.bytes(tr, q, 1n+p), gap(st), any(st), hw)

# Gap before a word byte: a space, then the same bytes from Lead
def nl_gap(+q: Nat, +tr: A.Tr<U32>, +p: Nat, +e: Nat, +h: {Nat.sub(e, p) == 1n+q : Nat},
  +hw: {C.is_ws(A.bv(tr, p)) == False{} : Bool}
) -> {NLm(tr, 1n+q, p, C.SGap{}) == 32 <> NL(tr, e, C.Cur{p, C.SLead{}}) : List<&2, U32>}:
  %Equal.sym(Nat, Nat.sub(e, p), 1n+q, h) : {NLm(tr, 1n+q, p, C.SGap{}) == 32 <> NLm(tr, _, p, C.SLead{}) :
    List<&2, U32>}
  %Equal.sym(List<&2, U32>, NLm(tr, 1n+q, p, C.SLead{}), A.bv(tr, p) <> NL(tr, e, C.Cur{1n+p, C.SWord{}}),
    nl_word(q, tr, p, e, C.SLead{}, h, hw)) : {NLm(tr, 1n+q, p, C.SGap{}) == 32 <> _ : List<&2, U32>}
  nl_word(q, tr, p, e, C.SGap{}, h, hw)

# what a step emitted: Stop when nothing is left of l, else a byte b and a
# cursor c with l = b, then what is left after c
def SP(-tr: A.Tr<U32>, -e: Nat, -l: List<&2, U32>, x: C.Emit) -> Type:
  match x:
    case C.EStop{}:
      {l == Nil{} : List<&2, U32>}
    case C.EByte{b, c}:
      {l == b <> NL(tr, e, c) : List<&2, U32>}

# r hands back emb(tr) with an emitted x that follows l
def NR(-tr: A.Tr<U32>, -e: Nat, -l: List<&2, U32>, -r: Array<U32> & C.Emit) -> Type:
  &x: C.Emit -> Pair({r == (A.emb(U32, tr), x) : Array<U32> & C.Emit}, SP(tr, e, l, x))

# no raw bytes left: Stop
def next_z(inr: Bool, +tr: A.Tr<U32>, +p: Nat, +e: Nat, +st: C.St, h: {Nat.sub(e, p) == 0n : Nat},
  hi: {Nat.is_lt(p, e) == inr : Bool}
) -> NR(tr, e, NLm(tr, 0n, p, st), C.next(1n, inr, p, e, st, C.rd.of((A.emb(U32, tr), A.bv(tr, p))))):
  match inr:
    case False{}:
      (C.EStop{}, ({==}, {==}))
    case True{}:
      L.absurd(NR(tr, e, NLm(tr, 0n, p, st), C.next(1n, True{}, p, e, st, C.rd.of((A.emb(U32, tr),
        A.bv(tr, p))))), sub_z(e, p, h, hi))

# the byte at p is whitespace (w) or not
def next_w(w: Bool, +q: Nat, +tr: A.Tr<U32>, +p: Nat, +e: Nat, +st: C.St, +h: {Nat.sub(e, p) == 1n+q : Nat},
  +hw: {C.is_ws(A.bv(tr, p)) == w : Bool},
  ih: NR(tr, e, NLm(tr, q, 1n+p, C.ws.st(st)), C.next(1n+q, Nat.is_lt(1n+p, e), 1n+p, e, C.ws.st(st),
    C.rd.of((A.emb(U32, tr), A.bv(tr, 1n+p)))))
) -> NR(tr, e, NLm(tr, 1n+q, p, st), C.next(1n+1n+q, True{}, p, e, st,
  C.Rd{A.emb(U32, tr), A.bv(tr, p), w, C.is_cont(A.bv(tr, p)), U32.is_eq(A.bv(tr, p), 10)})):
  match w:
    case True{}:
      %Equal.sym(Array<U32> & U32, C.byte(A.emb(U32, tr), 1n+p), (A.emb(U32, tr), A.bv(tr, 1n+p)),
        A.byte_emb(tr, 1n+p)) : NR(tr, e, NLm(tr, 1n+q, p, st),
        C.next(1n+q, Nat.is_lt(1n+p, e), 1n+p, e, C.ws.st(st), C.rd.of(_)))
      %nl_ws(q, tr, p, st, hw) : NR(tr, e, _,
        C.next(1n+q, Nat.is_lt(1n+p, e), 1n+p, e, C.ws.st(st), C.rd.of((A.emb(U32, tr), A.bv(tr, 1n+p)))))
      ih
    case False{}:
      match st:
        case C.SLead{}:
          (C.EByte{A.bv(tr, p), C.Cur{1n+p, C.SWord{}}}, ({==}, nl_word(q, tr, p, e, C.SLead{}, h, hw)))
        case C.SWord{}:
          (C.EByte{A.bv(tr, p), C.Cur{1n+p, C.SWord{}}}, ({==}, nl_word(q, tr, p, e, C.SWord{}, h, hw)))
        case C.SGap{}:
          (C.EByte{32, C.Cur{p, C.SLead{}}}, ({==}, nl_gap(q, tr, p, e, h, hw)))

# raw bytes left: p < e, so next reads the byte at p
def next_s(inr: Bool, +q: Nat, +tr: A.Tr<U32>, +p: Nat, +e: Nat, +st: C.St,
  +h: {Nat.sub(e, p) == 1n+q : Nat}, hi: {Nat.is_lt(p, e) == inr : Bool},
  ih: NR(tr, e, NLm(tr, q, 1n+p, C.ws.st(st)), C.next(1n+q, Nat.is_lt(1n+p, e), 1n+p, e, C.ws.st(st),
    C.rd.of((A.emb(U32, tr), A.bv(tr, 1n+p)))))
) -> NR(tr, e, NLm(tr, 1n+q, p, st), C.next(1n+1n+q, inr, p, e, st, C.rd.of((A.emb(U32, tr), A.bv(tr, p))))):
  match inr:
    case False{}:
      L.absurd(NR(tr, e, NLm(tr, 1n+q, p, st), C.next(1n+1n+q, False{}, p, e, st,
        C.rd.of((A.emb(U32, tr), A.bv(tr, p))))), sub_lt(e, p, q, h, hi))
    case True{}:
      next_w(C.is_ws(A.bv(tr, p)), q, tr, p, e, st, h, {==}, ih)

# next from p with e - p = m raw bytes left follows NLm
def next_lem(m: Nat, +tr: A.Tr<U32>, +p: Nat, +e: Nat, +st: C.St, +h: {Nat.sub(e, p) == m : Nat}) ->
  NR(tr, e, NLm(tr, m, p, st),
    C.next(1n+m, Nat.is_lt(p, e), p, e, st, C.rd.of((A.emb(U32, tr), A.bv(tr, p))))):
  match m:
    case 0n:
      next_z(Nat.is_lt(p, e), tr, p, e, st, h, {==})
    case 1n++q:
      next_s(Nat.is_lt(p, e), q, tr, p, e, st, h, {==},
        next_lem(q, tr, 1n+p, e, C.ws.st(st), sub_s(e, p, q, h)))

# a step on emb(tr) hands it back and emits the next byte of NL, or stops
# at its end
def step_lem(+tr: A.Tr<U32>, +e: Nat, c: C.Cur) -> NR(tr, e, NL(tr, e, c), C.step(e, c, A.emb(U32, tr))):
  match c:
    case C.Cur{+p, +st}:
      %Equal.sym(Array<U32> & U32, C.byte(A.emb(U32, tr), p), (A.emb(U32, tr), A.bv(tr, p)),
        A.byte_emb(tr, p)) : NR(tr, e, NLm(tr, Nat.sub(e, p), p, st),
        C.next(1n+Nat.sub(e, p), Nat.is_lt(p, e), p, e, st, C.rd.of(_)))
      next_lem(Nat.sub(e, p), tr, p, e, st, {==})

# Check
# -----

# after skipping: nothing to say on a Stop, else l is what is left after c
def SKP(-tr: A.Tr<U32>, -e: Nat, -l: List<&2, U32>, x: C.Emit) -> Type:
  match x:
    case C.EStop{}:
      Unit
    case C.EByte{b, c}:
      {l == NL(tr, e, c) : List<&2, U32>}

# skip.n i times from byte b before cursor c: emb(tr) comes back, and a
# byte lands on the cursor before drop(NL(c), i)
def SKR(+i: Nat, -tr: A.Tr<U32>, -e: Nat, -b: U32, -c: C.Cur) -> Type:
  &x: C.Emit -> Pair({C.skip.n(i, e, (A.emb(U32, tr), C.EByte{b, c})) == (A.emb(U32, tr), x) :
    Array<U32> & C.Emit}, SKP(tr, e, List.drop(&2, U32, NL(tr, e, c), i), x))

# a Stop stays a Stop
def skip_stop(j: Nat, -tr: A.Tr<U32>, +e: Nat) ->
  {C.skip.n(j, e, (A.emb(U32, tr), C.EStop{})) == (A.emb(U32, tr), C.EStop{}) : Array<U32> & C.Emit}:
  match j:
    case 0n:
      {==}
    case 1n+k:
      {==}

# SKP after dropping one more from b, l2
def skp_tr(x: C.Emit, -tr: A.Tr<U32>, -e: Nat, -l: List<&2, U32>, -b: U32, -l2: List<&2, U32>, -j: Nat,
  h: {l == b <> l2 : List<&2, U32>}, s: SKP(tr, e, List.drop(&2, U32, l2, j), x)
) -> SKP(tr, e, List.drop(&2, U32, l, 1n+j), x):
  %Equal.sym(List<&2, U32>, l, b <> l2, h) : SKP(tr, e, List.drop(&2, U32, _, 1n+j), x)
  s

# one skip emitted b2 before c2 (l = b2, NL(c2)); the rest skip from there
def sk_b(+j: Nat, -tr: A.Tr<U32>, -e: Nat, -b2: U32, -c2: C.Cur, -l: List<&2, U32>,
  sp: {l == b2 <> NL(tr, e, c2) : List<&2, U32>}, r: SKR(j, tr, e, b2, c2)
) -> &x: C.Emit -> Pair({C.skip.n(j, e, (A.emb(U32, tr), C.EByte{b2, c2})) == (A.emb(U32, tr), x) :
  Array<U32> & C.Emit}, SKP(tr, e, List.drop(&2, U32, l, 1n+j), x)):
  (+x, pr) = r
  (hx, s) = pr
  (x, (hx, skp_tr(x, tr, e, l, b2, NL(tr, e, c2), j, sp, s)))

# the first of 1 + j skips: a step from c
def sk_s(y: C.Emit, +j: Nat, +tr: A.Tr<U32>, +e: Nat, -b: U32, -c: C.Cur,
  hy: {C.step(e, c, A.emb(U32, tr)) == (A.emb(U32, tr), y) : Array<U32> & C.Emit},
  sp: SP(tr, e, NL(tr, e, c), y), ih: @+b2: U32 -> @+c2: C.Cur -> SKR(j, tr, e, b2, c2)
) -> SKR(1n+j, tr, e, b, c):
  match y:
    case C.EStop{}:
      %Equal.sym(Array<U32> & C.Emit, C.step(e, c, A.emb(U32, tr)), (A.emb(U32, tr), C.EStop{}), hy) :
        &x: C.Emit -> Pair({C.skip.n(j, e, _) == (A.emb(U32, tr), x) : Array<U32> & C.Emit},
          SKP(tr, e, List.drop(&2, U32, NL(tr, e, c), 1n+j), x))
      (C.EStop{}, (skip_stop(j, tr, e), Unit{}))
    case C.EByte{+b2, +c2}:
      %Equal.sym(Array<U32> & C.Emit, C.step(e, c, A.emb(U32, tr)), (A.emb(U32, tr), C.EByte{b2, c2}), hy) :
        &x: C.Emit -> Pair({C.skip.n(j, e, _) == (A.emb(U32, tr), x) : Array<U32> & C.Emit},
          SKP(tr, e, List.drop(&2, U32, NL(tr, e, c), 1n+j), x))
      sk_b(j, tr, e, b2, c2, NL(tr, e, c), sp, ih(b2, c2))

def sk_r(+j: Nat, +tr: A.Tr<U32>, +e: Nat, -b: U32, +c: C.Cur,
  r: NR(tr, e, NL(tr, e, c), C.step(e, c, A.emb(U32, tr))),
  ih: @+b2: U32 -> @+c2: C.Cur -> SKR(j, tr, e, b2, c2)
) -> SKR(1n+j, tr, e, b, c):
  (+y, pr) = r
  (hy, sp) = pr
  sk_s(y, j, tr, e, b, c, hy, sp, ih)

def skip_lem(i: Nat, +tr: A.Tr<U32>, +e: Nat, +b: U32, +c: C.Cur) -> SKR(i, tr, e, b, c):
  match i:
    case 0n:
      (C.EByte{b, c}, ({==}, drop0(NL(tr, e, c))))
    case 1n++j:
      sk_r(j, tr, e, b, c, step_lem(tr, e, c), b2 => c2 => skip_lem(j, tr, e, b2, c2))

# same: when it answers True, the quote's normalized bytes Q are a prefix of
# the evidence's normalized bytes E
def PreP(v: Bool, -ev: List<&2, U32>, -q: List<&2, U32>) -> Type:
  match v:
    case True{}:
      {Laws.Spec.same(List.take(&2, U32, ev, List.length(&2, U32, q)), q) == True{} : Bool}
    case False{}:
      Unit

def SMR(-tr: A.Tr<U32>, -ev: List<&2, U32>, -q: List<&2, U32>, -r: Array<U32> & Bool) -> Type:
  &v: Bool -> Pair({r == (A.emb(U32, tr), v) : Array<U32> & Bool}, PreP(v, ev, q))

# the comparisons still to come, with f fuel
def IHS(-f: Nat, -tr: A.Tr<U32>, -ee: Nat, -eq: Nat) -> Type:
  @+ce: C.Cur -> @+cq: C.Cur ->
    SMR(tr, NL(tr, ee, ce), NL(tr, eq, cq), C.same(f, ee, eq, C.CMore{A.emb(U32, tr), ce, cq}))

# same answers a finished comparison as is
def end_eq(f: Nat, +ee: Nat, +eq: Nat, -a: Array<U32>, +ok: Bool) ->
  {C.same(f, ee, eq, C.CEnd{a, ok}) == (a, ok) : Array<U32> & Bool}:
  match f:
    case 0n:
      {==}
    case 1n+g:
      {==}

# the empty quote is a prefix of anything
def pre_nil(+ev: List<&2, U32>, -q: List<&2, U32>, h: {q == Nil{} : List<&2, U32>}) -> PreP(True{}, ev, q):
  %Equal.sym(List<&2, U32>, q, Nil{}, h) :
    {Laws.Spec.same(List.take(&2, U32, ev, List.length(&2, U32, _)), _) == True{} : Bool}
  %Equal.sym(List<&2, U32>, List.take(&2, U32, ev, 0n), Nil{}, take0(ev)) :
    {Laws.Spec.same(_, Nil{}) == True{} : Bool}
  {==}

# equal first bytes extend a prefix
def pre_cons(v: Bool, +y: U32, +zz: U32, -ev: List<&2, U32>, -q: List<&2, U32>,
  hb: {U32.is_eq(y, zz) == True{} : Bool}, pre: PreP(v, ev, q)
) -> PreP(v, zz <> ev, y <> q):
  match v:
    case False{}:
      Unit{}
    case True{}:
      L.and_i(U32.is_eq(zz, y), Laws.Spec.same(List.take(&2, U32, ev, List.length(&2, U32, q)), q),
        usym(y, zz, hb), pre)

# PreP along equations for both lists
def pre_tr(v: Bool, -ev: List<&2, U32>, -q: List<&2, U32>, -zz: U32, -ev2: List<&2, U32>, -y: U32,
  -q2: List<&2, U32>, he: {ev == zz <> ev2 : List<&2, U32>}, hq: {q == y <> q2 : List<&2, U32>},
  p: PreP(v, zz <> ev2, y <> q2)
) -> PreP(v, ev, q):
  %Equal.sym(List<&2, U32>, ev, zz <> ev2, he) : PreP(v, _, q)
  %Equal.sym(List<&2, U32>, q, y <> q2, hq) : PreP(v, zz <> ev2, _)
  p

# equal bytes: the rest of the comparison decides
def sm_t(+y: U32, +zz: U32, -cq2: C.Cur, -ce2: C.Cur, -f: Nat, -tr: A.Tr<U32>, -ee: Nat, -eq: Nat,
  -ce: C.Cur, -cq: C.Cur, hb: {U32.is_eq(y, zz) == True{} : Bool},
  hq: {NL(tr, eq, cq) == y <> NL(tr, eq, cq2) : List<&2, U32>},
  he: {NL(tr, ee, ce) == zz <> NL(tr, ee, ce2) : List<&2, U32>},
  r: SMR(tr, NL(tr, ee, ce2), NL(tr, eq, cq2), C.same(f, ee, eq, C.CMore{A.emb(U32, tr), ce2, cq2}))
) -> SMR(tr, NL(tr, ee, ce), NL(tr, eq, cq), C.same(f, ee, eq, C.CMore{A.emb(U32, tr), ce2, cq2})):
  (+v, pr) = r
  (hv, pre) = pr
  (v, (hv, pre_tr(v, NL(tr, ee, ce), NL(tr, eq, cq), zz, NL(tr, ee, ce2), y, NL(tr, eq, cq2), he, hq,
    pre_cons(v, y, zz, NL(tr, ee, ce2), NL(tr, eq, cq2), hb, pre))))

# the quote byte y against the evidence byte zz
def sm_if(b: Bool, +y: U32, +zz: U32, +cq2: C.Cur, +ce2: C.Cur, +f: Nat, +tr: A.Tr<U32>, +ee: Nat, +eq: Nat,
  -ce: C.Cur, -cq: C.Cur, hb: {U32.is_eq(y, zz) == b : Bool},
  hq: {NL(tr, eq, cq) == y <> NL(tr, eq, cq2) : List<&2, U32>},
  he: {NL(tr, ee, ce) == zz <> NL(tr, ee, ce2) : List<&2, U32>}, ih: IHS(f, tr, ee, eq)
) -> SMR(tr, NL(tr, ee, ce), NL(tr, eq, cq), C.same(f, ee, eq, C.cmp.if(b, ce2, cq2, A.emb(U32, tr)))):
  match b:
    case False{}:
      (False{}, (end_eq(f, ee, eq, A.emb(U32, tr), False{}), Unit{}))
    case True{}:
      sm_t(y, zz, cq2, ce2, f, tr, ee, eq, ce, cq, hb, hq, he, ih(ce2, cq2))

# the evidence step after quote byte y
def sm_e(z: C.Emit, +y: U32, +cq2: C.Cur, +f: Nat, +tr: A.Tr<U32>, +ee: Nat, +eq: Nat, +ce: C.Cur, -cq: C.Cur,
  hq: {NL(tr, eq, cq) == y <> NL(tr, eq, cq2) : List<&2, U32>},
  hz: {C.step(ee, ce, A.emb(U32, tr)) == (A.emb(U32, tr), z) : Array<U32> & C.Emit},
  sp: SP(tr, ee, NL(tr, ee, ce), z), ih: IHS(f, tr, ee, eq)
) -> SMR(tr, NL(tr, ee, ce), NL(tr, eq, cq),
  C.same(f, ee, eq, C.cmp.e(y, cq2, C.step(ee, ce, A.emb(U32, tr))))):
  match z:
    case C.EStop{}:
      %Equal.sym(Array<U32> & C.Emit, C.step(ee, ce, A.emb(U32, tr)), (A.emb(U32, tr), C.EStop{}), hz) :
        SMR(tr, NL(tr, ee, ce), NL(tr, eq, cq), C.same(f, ee, eq, C.cmp.e(y, cq2, _)))
      (False{}, (end_eq(f, ee, eq, A.emb(U32, tr), False{}), Unit{}))
    case C.EByte{+zz, +ce2}:
      %Equal.sym(Array<U32> & C.Emit, C.step(ee, ce, A.emb(U32, tr)),
        (A.emb(U32, tr), C.EByte{zz, ce2}), hz) :
        SMR(tr, NL(tr, ee, ce), NL(tr, eq, cq), C.same(f, ee, eq, C.cmp.e(y, cq2, _)))
      sm_if(U32.is_eq(y, zz), y, zz, cq2, ce2, f, tr, ee, eq, ce, cq, {==}, hq, sp, ih)

def sm_er(+y: U32, +cq2: C.Cur, +f: Nat, +tr: A.Tr<U32>, +ee: Nat, +eq: Nat, +ce: C.Cur, -cq: C.Cur,
  hq: {NL(tr, eq, cq) == y <> NL(tr, eq, cq2) : List<&2, U32>},
  r: NR(tr, ee, NL(tr, ee, ce), C.step(ee, ce, A.emb(U32, tr))), ih: IHS(f, tr, ee, eq)
) -> SMR(tr, NL(tr, ee, ce), NL(tr, eq, cq),
  C.same(f, ee, eq, C.cmp.e(y, cq2, C.step(ee, ce, A.emb(U32, tr))))):
  (+z, pr) = r
  (hz, sp) = pr
  sm_e(z, y, cq2, f, tr, ee, eq, ce, cq, hq, hz, sp, ih)

# the quote step
def sm_q(x: C.Emit, +f: Nat, +tr: A.Tr<U32>, +ee: Nat, +eq: Nat, +ce: C.Cur, -cq: C.Cur,
  hx: {C.step(eq, cq, A.emb(U32, tr)) == (A.emb(U32, tr), x) : Array<U32> & C.Emit},
  sp: SP(tr, eq, NL(tr, eq, cq), x), ih: IHS(f, tr, ee, eq)
) -> SMR(tr, NL(tr, ee, ce), NL(tr, eq, cq), C.same(1n+f, ee, eq, C.CMore{A.emb(U32, tr), ce, cq})):
  match x:
    case C.EStop{}:
      %Equal.sym(Array<U32> & C.Emit, C.step(eq, cq, A.emb(U32, tr)), (A.emb(U32, tr), C.EStop{}), hx) :
        SMR(tr, NL(tr, ee, ce), NL(tr, eq, cq), C.same(f, ee, eq, C.cmp.q(ee, ce, _)))
      (True{}, (end_eq(f, ee, eq, A.emb(U32, tr), True{}), pre_nil(NL(tr, ee, ce), NL(tr, eq, cq), sp)))
    case C.EByte{+y, +cq2}:
      %Equal.sym(Array<U32> & C.Emit, C.step(eq, cq, A.emb(U32, tr)), (A.emb(U32, tr), C.EByte{y, cq2}), hx) :
        SMR(tr, NL(tr, ee, ce), NL(tr, eq, cq), C.same(f, ee, eq, C.cmp.q(ee, ce, _)))
      sm_er(y, cq2, f, tr, ee, eq, ce, cq, sp, step_lem(tr, ee, ce), ih)

def sm_r(+f: Nat, +tr: A.Tr<U32>, +ee: Nat, +eq: Nat, +ce: C.Cur, -cq: C.Cur,
  r: NR(tr, eq, NL(tr, eq, cq), C.step(eq, cq, A.emb(U32, tr))), ih: IHS(f, tr, ee, eq)
) -> SMR(tr, NL(tr, ee, ce), NL(tr, eq, cq), C.same(1n+f, ee, eq, C.CMore{A.emb(U32, tr), ce, cq})):
  (+x, pr) = r
  (hx, sp) = pr
  sm_q(x, f, tr, ee, eq, ce, cq, hx, sp, ih)

# same on emb(tr) from cursors ce, cq answers True only when NL(cq) is a
# prefix of NL(ce)
def same_lem(fuel: Nat, +tr: A.Tr<U32>, +ee: Nat, +eq: Nat, +ce: C.Cur, +cq: C.Cur) ->
  SMR(tr, NL(tr, ee, ce), NL(tr, eq, cq), C.same(fuel, ee, eq, C.CMore{A.emb(U32, tr), ce, cq})):
  match fuel:
    case 0n:
      (False{}, ({==}, Unit{}))
    case 1n++f:
      sm_r(f, tr, ee, eq, ce, cq, step_lem(tr, eq, cq), ce2 => cq2 => same_lem(f, tr, ee, eq, ce2, cq2))

# check answers True only when Spec.quoted holds at offset i
def CKP(v: Bool, -tr: A.Tr<U32>, -eo: Nat, -el: Nat, -qo: Nat, -ql: Nat, -i: Nat) -> Type:
  match v:
    case True{}:
      {Laws.Spec.quoted(A.bytes(tr, el, eo), A.bytes(tr, ql, qo), i) == True{} : Bool}
    case False{}:
      Unit

def CKR(-tr: A.Tr<U32>, -eo: Nat, -el: Nat, -qo: Nat, -ql: Nat, -i: Nat) -> Type:
  &v: Bool -> Pair({C.check(A.emb(U32, tr), eo, el, qo, ql, i) == (A.emb(U32, tr), v) : Array<U32> & Bool},
    CKP(v, tr, eo, el, qo, ql, i))

# the evidence from its i-th normalized byte starts with the normalized quote
def ck_fin(v: Bool, +tr: A.Tr<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat, +i: Nat, -ce: C.Cur,
  sk: {List.drop(&2, U32, NL(tr, Nat.add(eo, el), C.Cur{eo, C.SLead{}}), i) == NL(tr, Nat.add(eo, el), ce) :
    List<&2, U32>},
  pre: PreP(v, NL(tr, Nat.add(eo, el), ce), NL(tr, Nat.add(qo, ql), C.Cur{qo, C.SLead{}}))
) -> CKP(v, tr, eo, el, qo, ql, i):
  match v:
    case False{}:
      Unit{}
    case True{}:
      %sub_add(eo, el) : {Laws.Spec.quoted(A.bytes(tr, _, eo), A.bytes(tr, ql, qo), i) == True{} : Bool}
      %sub_add(qo, ql) : {Laws.Spec.quoted(A.bytes(tr, Nat.sub(Nat.add(eo, el), eo), eo),
        A.bytes(tr, _, qo), i) == True{} : Bool}
      %Equal.sym(List<&2, U32>, List.drop(&2, U32, NL(tr, Nat.add(eo, el), C.Cur{eo, C.SLead{}}), i),
        NL(tr, Nat.add(eo, el), ce), sk) : {Laws.Spec.same(List.take(&2, U32, _,
        List.length(&2, U32, NL(tr, Nat.add(qo, ql), C.Cur{qo, C.SLead{}}))),
        NL(tr, Nat.add(qo, ql), C.Cur{qo, C.SLead{}})) == True{} : Bool}
      pre

def ck_same(+tr: A.Tr<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat, +i: Nat, -ce: C.Cur,
  sk: {List.drop(&2, U32, NL(tr, Nat.add(eo, el), C.Cur{eo, C.SLead{}}), i) == NL(tr, Nat.add(eo, el), ce) :
    List<&2, U32>},
  r: SMR(tr, NL(tr, Nat.add(eo, el), ce), NL(tr, Nat.add(qo, ql), C.Cur{qo, C.SLead{}}),
    C.same(1n+ql, Nat.add(eo, el), Nat.add(qo, ql), C.CMore{A.emb(U32, tr), ce, C.Cur{qo, C.SLead{}}}))
) -> &v: Bool -> Pair({C.same(1n+ql, Nat.add(eo, el), Nat.add(qo, ql),
  C.CMore{A.emb(U32, tr), ce, C.Cur{qo, C.SLead{}}}) == (A.emb(U32, tr), v) : Array<U32> & Bool},
  CKP(v, tr, eo, el, qo, ql, i)):
  (+v, pr) = r
  (hv, pre) = pr
  (v, (hv, ck_fin(v, tr, eo, el, qo, ql, i, ce, sk, pre)))

def ck_at(x: C.Emit, +tr: A.Tr<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat, +i: Nat,
  hx: {C.skip.n(i, Nat.add(eo, el), (A.emb(U32, tr), C.EByte{0, C.Cur{eo, C.SLead{}}})) ==
    (A.emb(U32, tr), x) : Array<U32> & C.Emit},
  sk: SKP(tr, Nat.add(eo, el), List.drop(&2, U32, NL(tr, Nat.add(eo, el), C.Cur{eo, C.SLead{}}), i), x)
) -> CKR(tr, eo, el, qo, ql, i):
  match x:
    case C.EStop{}:
      %Equal.sym(Array<U32> & C.Emit,
        C.skip.n(i, Nat.add(eo, el), (A.emb(U32, tr), C.EByte{0, C.Cur{eo, C.SLead{}}})),
        (A.emb(U32, tr), C.EStop{}), hx) : &v: Bool -> Pair({C.check.at(Nat.add(eo, el), qo, ql, _) ==
        (A.emb(U32, tr), v) : Array<U32> & Bool}, CKP(v, tr, eo, el, qo, ql, i))
      (False{}, ({==}, Unit{}))
    case C.EByte{+b, +ce}:
      %Equal.sym(Array<U32> & C.Emit,
        C.skip.n(i, Nat.add(eo, el), (A.emb(U32, tr), C.EByte{0, C.Cur{eo, C.SLead{}}})),
        (A.emb(U32, tr), C.EByte{b, ce}), hx) : &v: Bool -> Pair({C.check.at(Nat.add(eo, el), qo, ql, _) ==
        (A.emb(U32, tr), v) : Array<U32> & Bool}, CKP(v, tr, eo, el, qo, ql, i))
      ck_same(tr, eo, el, qo, ql, i, ce, sk,
        same_lem(1n+ql, tr, Nat.add(eo, el), Nat.add(qo, ql), ce, C.Cur{qo, C.SLead{}}))

def ck_r(+tr: A.Tr<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat, +i: Nat,
  r: SKR(i, tr, Nat.add(eo, el), 0, C.Cur{eo, C.SLead{}})
) -> CKR(tr, eo, el, qo, ql, i):
  (+x, pr) = r
  (hx, sk) = pr
  ck_at(x, tr, eo, el, qo, ql, i, hx, sk)

# check on emb(tr) hands it back, and answers True only when the normalized
# quote is the slice of the normalized evidence at i
def check_lem(+tr: A.Tr<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat, +i: Nat) -> CKR(tr, eo, el, qo, ql, i):
  ck_r(tr, eo, el, qo, ql, i, skip_lem(i, tr, Nat.add(eo, el), 0, C.Cur{eo, C.SLead{}}))

# Search
# ------
# The search holds emb(tr) in every state: pat.fill and find only pass it
# through step and check. A Found comes from a check that answered True.

# the claim for a search result on evidence (eo, el) and quote (qo, ql)
def VS(ev: Array<U32>, +tr: A.Tr<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat, r: Array<U32> & C.Verdict) ->
  Type:
  Laws.Spec.cited(A.emb(U32, tr), Some{Laws.Spec.Item{eo, el}}, qo, ql,
    Laws.Spec.verdict(C.verify.fin(ev, r)))

# the search after the next step, given every lookup that follows
def IHF(-ev: Array<U32>, -tr: A.Tr<U32>, -eo: Nat, -el: Nat, -qo: Nat, -ql: Nat, -m: Nat, -g: Nat, -e: Nat) ->
  Type:
  @t: C.Tab -> @+c: C.Cur -> @+i: Nat -> @+k: Nat ->
    VS(ev, tr, eo, el, qo, ql, C.find(g, C.Job{eo, el, qo, ql, m}, e, C.FLook{A.emb(U32, tr), t, c, i, k}))

# a quoted slice satisfies Spec.cited for Found
def found_ok(+tr: A.Tr<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat, +at: Nat,
  h: {Laws.Spec.quoted(A.bytes(tr, el, eo), A.bytes(tr, ql, qo), at) == True{} : Bool}
) -> Laws.Spec.cited(A.emb(U32, tr), Some{Laws.Spec.Item{eo, el}}, qo, ql, C.Found{at}):
  %Equal.sym(Array<U32> & List<&2, U32>, Laws.Spec.read(A.emb(U32, tr), eo, el),
    (A.emb(U32, tr), A.bytes(tr, el, eo)), A.read_emb(tr, eo, el)) : {Laws.Spec.quoted(Laws.Spec.list(U32, _),
    Laws.Spec.list(U32, Laws.Spec.read(A.emb(U32, tr), qo, ql)), at) == True{} : Bool}
  %Equal.sym(Array<U32> & List<&2, U32>, Laws.Spec.read(A.emb(U32, tr), qo, ql),
    (A.emb(U32, tr), A.bytes(tr, ql, qo)), A.read_emb(tr, qo, ql)) :
    {Laws.Spec.quoted(A.bytes(tr, el, eo), Laws.Spec.list(U32, _), at) == True{} : Bool}
  h

# find hands back a finished verdict: Found at a quoted slice, or Missing
def s_found(g: Nat, -ev: Array<U32>, +tr: A.Tr<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat, -m: Nat, -e: Nat,
  +at: Nat, -a: Array<U32>, -t: C.Tab,
  h: {Laws.Spec.quoted(A.bytes(tr, el, eo), A.bytes(tr, ql, qo), at) == True{} : Bool}
) -> VS(ev, tr, eo, el, qo, ql, C.find(g, C.Job{eo, el, qo, ql, m}, e, C.FGot{a, t, C.Found{at}})):
  match g:
    case 0n:
      found_ok(tr, eo, el, qo, ql, at, h)
    case 1n+f:
      found_ok(tr, eo, el, qo, ql, at, h)

def s_miss(g: Nat, -ev: Array<U32>, -tr: A.Tr<U32>, -eo: Nat, -el: Nat, -qo: Nat, -ql: Nat, -m: Nat, -e: Nat,
  -a: Array<U32>, -t: C.Tab
) -> VS(ev, tr, eo, el, qo, ql, C.find(g, C.Job{eo, el, qo, ql, m}, e, C.FGot{a, t, C.Missing{}})):
  match g:
    case 0n:
      Unit{}
    case 1n+f:
      Unit{}

def s_again(-ev: Array<U32>, -tr: A.Tr<U32>, -eo: Nat, -el: Nat, -qo: Nat, -ql: Nat, -m: Nat,
  -g: Nat, -e: Nat, +c: C.Cur, +i: Nat, pat: Array<U32>, r: Array<U32> & U32,
  ih: IHF(ev, tr, eo, el, qo, ql, m, g, e)
) -> VS(ev, tr, eo, el, qo, ql, C.find(g, C.Job{eo, el, qo, ql, m}, e,
  C.find.again(c, i, A.emb(U32, tr), pat, r))):
  match r:
    case (f, k):
      ih(C.Tab{pat, f}, c, i, U32.to_nat(k))

# a failed check falls back along the table and looks on
def s_back(-ev: Array<U32>, -tr: A.Tr<U32>, -eo: Nat, -el: Nat, -qo: Nat, -ql: Nat, -m: Nat, -g: Nat, -e: Nat,
  +c: C.Cur, +i: Nat, +k: Nat, t: C.Tab, ih: IHF(ev, tr, eo, el, qo, ql, m, g, e)
) -> VS(ev, tr, eo, el, qo, ql, C.find(g, C.Job{eo, el, qo, ql, m}, e,
  C.find.miss(c, i, k, A.emb(U32, tr), t))):
  match t:
    case C.Tab{pat, f}:
      s_again(ev, tr, eo, el, qo, ql, m, g, e, c, i, pat, Array.get(U32, f, U32.from_nat(Nat.sub(k, 1n))), ih)

# the verdict of check at offset at
def s_ok(v: Bool, g: Nat, -ev: Array<U32>, +tr: A.Tr<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat, -m: Nat,
  -e: Nat, +at: Nat, +c: C.Cur, +i: Nat, +k: Nat, t: C.Tab, ck: CKP(v, tr, eo, el, qo, ql, at),
  ih: IHF(ev, tr, eo, el, qo, ql, m, g, e)
) -> VS(ev, tr, eo, el, qo, ql, C.find(g, C.Job{eo, el, qo, ql, m}, e,
  C.find.ok(at, c, i, k, t, (A.emb(U32, tr), v)))):
  match v:
    case True{}:
      s_found(g, ev, tr, eo, el, qo, ql, m, e, at, A.emb(U32, tr), t, ck)
    case False{}:
      s_back(ev, tr, eo, el, qo, ql, m, g, e, c, i, k, t, ih)

def s_ck(g: Nat, -ev: Array<U32>, +tr: A.Tr<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat, -m: Nat, -e: Nat,
  +at: Nat, +c: C.Cur, +i: Nat, +k: Nat, t: C.Tab, r: CKR(tr, eo, el, qo, ql, at),
  ih: IHF(ev, tr, eo, el, qo, ql, m, g, e)
) -> VS(ev, tr, eo, el, qo, ql, C.find(g, C.Job{eo, el, qo, ql, m}, e,
  C.find.ok(at, c, i, k, t, C.check(A.emb(U32, tr), eo, el, qo, ql, at)))):
  (+v, pr) = r
  (hv, ck) = pr
  %Equal.sym(Array<U32> & Bool, C.check(A.emb(U32, tr), eo, el, qo, ql, at), (A.emb(U32, tr), v), hv) :
    VS(ev, tr, eo, el, qo, ql, C.find(g, C.Job{eo, el, qo, ql, m}, e, C.find.ok(at, c, i, k, t, _)))
  s_ok(v, g, ev, tr, eo, el, qo, ql, m, e, at, c, i, k, t, ck, ih)

# the whole pattern matched: check confirms the offset
def s_full(full: Bool, g: Nat, -ev: Array<U32>, +tr: A.Tr<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat,
  +m: Nat, -e: Nat, +c: C.Cur, +i: Nat, +k: Nat, t: C.Tab, ih: IHF(ev, tr, eo, el, qo, ql, m, g, e)
) -> VS(ev, tr, eo, el, qo, ql, C.find(g, C.Job{eo, el, qo, ql, m}, e,
  C.find.full(full, C.Job{eo, el, qo, ql, m}, c, i, k, t, A.emb(U32, tr)))):
  match full:
    case False{}:
      ih(t, c, i, k)
    case True{}:
      s_ck(g, ev, tr, eo, el, qo, ql, m, e, Nat.sub(i, m), c, i, k, t,
        check_lem(tr, eo, el, qo, ql, Nat.sub(i, m)), ih)

def s_k(g: Nat, -ev: Array<U32>, +tr: A.Tr<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat, +m: Nat, -e: Nat,
  +c: C.Cur, +i: Nat, r: C.Tab & Nat, ih: IHF(ev, tr, eo, el, qo, ql, m, g, e)
) -> VS(ev, tr, eo, el, qo, ql, C.find(g, C.Job{eo, el, qo, ql, m}, e,
  C.find.k(C.Job{eo, el, qo, ql, m}, c, i, A.emb(U32, tr), r))):
  match r:
    case (t, +k):
      s_full(Nat.is_eq(k, m), g, ev, tr, eo, el, qo, ql, m, e, c, i, k, t, ih)

# the next evidence byte: Missing on Stop, else the KMP border update
def s_byte(x: C.Emit, g: Nat, -ev: Array<U32>, +tr: A.Tr<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat,
  +m: Nat, -e: Nat, +i: Nat, +k: Nat, t: C.Tab, ih: IHF(ev, tr, eo, el, qo, ql, m, g, e)
) -> VS(ev, tr, eo, el, qo, ql, C.find(g, C.Job{eo, el, qo, ql, m}, e,
  C.find.byte(C.Job{eo, el, qo, ql, m}, i, k, t, (A.emb(U32, tr), x)))):
  match x:
    case C.EStop{}:
      s_miss(g, ev, tr, eo, el, qo, ql, m, e, A.emb(U32, tr), t)
    case C.EByte{+y, +c}:
      s_k(g, ev, tr, eo, el, qo, ql, m, e, c, 1n+i, C.fall(1n+k, y, C.fall.look(k, y, t)), ih)

def s_step(g: Nat, -ev: Array<U32>, +tr: A.Tr<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat, +m: Nat, +e: Nat,
  -c: C.Cur, +i: Nat, +k: Nat, t: C.Tab, r: NR(tr, e, NL(tr, e, c), C.step(e, c, A.emb(U32, tr))),
  ih: IHF(ev, tr, eo, el, qo, ql, m, g, e)
) -> VS(ev, tr, eo, el, qo, ql, C.find(g, C.Job{eo, el, qo, ql, m}, e,
  C.find.byte(C.Job{eo, el, qo, ql, m}, i, k, t, C.step(e, c, A.emb(U32, tr))))):
  (+x, pr) = r
  (hx, sp) = pr
  %Equal.sym(Array<U32> & C.Emit, C.step(e, c, A.emb(U32, tr)), (A.emb(U32, tr), x), hx) :
    VS(ev, tr, eo, el, qo, ql, C.find(g, C.Job{eo, el, qo, ql, m}, e,
      C.find.byte(C.Job{eo, el, qo, ql, m}, i, k, t, _)))
  s_byte(x, g, ev, tr, eo, el, qo, ql, m, e, i, k, t, ih)

# every find from a lookup state on emb(tr) satisfies the claim
def s_look(fuel: Nat, -ev: Array<U32>, +tr: A.Tr<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat, +m: Nat,
  +e: Nat, t: C.Tab, +c: C.Cur, +i: Nat, +k: Nat
) -> VS(ev, tr, eo, el, qo, ql, C.find(fuel, C.Job{eo, el, qo, ql, m}, e,
  C.FLook{A.emb(U32, tr), t, c, i, k})):
  match fuel:
    case 0n:
      Unit{}
    case 1n++g:
      s_step(g, ev, tr, eo, el, qo, ql, m, e, c, i, k, t, step_lem(tr, e, c),
        t2 => c2 => i2 => k2 => s_look(g, ev, tr, eo, el, qo, ql, m, e, t2, c2, i2, k2))

# an empty pattern is Empty; else the table is built and find runs
def s_pat(-ev: Array<U32>, +tr: A.Tr<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat, +d: Nat,
  pat: Array<U32>, m: Nat
) -> VS(ev, tr, eo, el, qo, ql, C.search.pat(eo, el, qo, ql, d, C.Pat{A.emb(U32, tr), pat, m})):
  match m:
    case 0n:
      Unit{}
    case 1n++mm:
      s_look(1n+Nat.mul(2n, el), ev, tr, eo, el, qo, ql, 1n+mm, Nat.add(eo, el),
        C.tab.fill(mm, 1n, (C.Tab{pat, [0 : U32^d]}, 0n)), C.Cur{eo, C.SLead{}}, 0n, 0n)

# one pattern byte written, then the next step on emb(tr)
def pf_step(-f: Nat, -tr: A.Tr<U32>, -e: Nat, -m: Nat, -pat: Array<U32>, -b: U32, -c: C.Cur,
  -G: C.Pat -> Type, r: NR(tr, e, NL(tr, e, c), C.step(e, c, A.emb(U32, tr))),
  ih: @x: C.Emit -> G(C.pat.fill(f, e, 1n+m, Array.set(U32, pat, U32.from_nat(m), b), (A.emb(U32, tr), x)))
) -> G(C.pat.fill(f, e, 1n+m, Array.set(U32, pat, U32.from_nat(m), b), C.step(e, c, A.emb(U32, tr)))):
  (+x, pr) = r
  (hx, sp) = pr
  %Equal.sym(Array<U32> & C.Emit, C.step(e, c, A.emb(U32, tr)), (A.emb(U32, tr), x), hx) :
    G(C.pat.fill(f, e, 1n+m, Array.set(U32, pat, U32.from_nat(m), b), _))
  ih(x)

# pat.fill hands emb(tr) back inside the Pat
def pf_lem(fuel: Nat, +tr: A.Tr<U32>, +e: Nat, +m: Nat, pat: Array<U32>, x: C.Emit, -G: C.Pat -> Type,
  k: @pat2: Array<U32> -> @+m2: Nat -> G(C.Pat{A.emb(U32, tr), pat2, m2})
) -> G(C.pat.fill(fuel, e, m, pat, (A.emb(U32, tr), x))):
  match fuel:
    case 0n:
      k(pat, m)
    case 1n++f:
      match x:
        case C.EStop{}:
          k(pat, m)
        case C.EByte{+b, +c}:
          pf_step(f, tr, e, m, pat, b, c, G, step_lem(tr, e, c),
            x2 => pf_lem(f, tr, e, 1n+m, Array.set(U32, pat, U32.from_nat(m), b), x2, G, k))

# the first quote step, then pat.fill
def s_sr(-ev: Array<U32>, +tr: A.Tr<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat,
  r: NR(tr, Nat.add(qo, ql), NL(tr, Nat.add(qo, ql), C.Cur{qo, C.SLead{}}),
    C.step(Nat.add(qo, ql), C.Cur{qo, C.SLead{}}, A.emb(U32, tr)))
) -> VS(ev, tr, eo, el, qo, ql, C.search(A.emb(U32, tr), eo, el, qo, ql)):
  (+x, pr) = r
  (hx, sp) = pr
  +d = F.depth(1n+ql)
  %Equal.sym(Array<U32> & C.Emit, C.step(Nat.add(qo, ql), C.Cur{qo, C.SLead{}}, A.emb(U32, tr)),
    (A.emb(U32, tr), x), hx) : VS(ev, tr, eo, el, qo, ql, C.search.pat(eo, el, qo, ql, d,
    C.pat.fill(1n+Nat.mul(2n, ql), Nat.add(qo, ql), 0n, [0 : U32^d], _)))
  pf_lem(1n+Nat.mul(2n, ql), tr, Nat.add(qo, ql), 0n, [0 : U32^d], x,
    pt => VS(ev, tr, eo, el, qo, ql, C.search.pat(eo, el, qo, ql, d, pt)),
    pat2 => m2 => s_pat(ev, tr, eo, el, qo, ql, d, pat2, m2))

# a verdict of search on emb(tr) is Found only for a quoted slice
def s_search(-ev: Array<U32>, +tr: A.Tr<U32>, +eo: Nat, +el: Nat, +qo: Nat, +ql: Nat) ->
  VS(ev, tr, eo, el, qo, ql, C.search(A.emb(U32, tr), eo, el, qo, ql)):
  s_sr(ev, tr, eo, el, qo, ql, step_lem(tr, Nat.add(qo, ql), C.Cur{qo, C.SLead{}}))

# Verify
# ------
# verify and Spec.item read the same two words of ev.

def sd.len(+tr: A.Tr<U32>, +qo: Nat, +ql: Nat, +eo: Nat, r: Array<U32> & U32) ->
  Laws.Spec.cited(A.emb(U32, tr), Laws.Spec.item.len(eo, r), qo, ql,
    Laws.Spec.verdict(C.verify.len(qo, ql, A.emb(U32, tr), eo, r))):
  match r:
    case (ev, +el):
      s_search(ev, tr, eo, U32.to_nat(el), qo, ql)

def sd.off(+tr: A.Tr<U32>, +id: Nat, +qo: Nat, +ql: Nat, r: Array<U32> & U32) ->
  Laws.Spec.cited(A.emb(U32, tr), Laws.Spec.item.off(id, r), qo, ql,
    Laws.Spec.verdict(C.verify.off(id, qo, ql, A.emb(U32, tr), r))):
  match r:
    case (ev, eo):
      sd.len(tr, qo, ql, U32.to_nat(eo), Array.get(U32, ev, U32.from_nat(1n+Nat.mul(2n, id))))

def sd.if(ok: Bool, +tr: A.Tr<U32>, +id: Nat, +qo: Nat, +ql: Nat, ev: Array<U32>) ->
  Laws.Spec.cited(A.emb(U32, tr), Laws.Spec.item.if(ok, id, ev), qo, ql,
    Laws.Spec.verdict(C.verify.if(ok, id, qo, ql, A.emb(U32, tr), ev))):
  match ok:
    case False{}:
      Unit{}
    case True{}:
      sd.off(tr, id, qo, ql, Array.get(U32, ev, U32.from_nat(Nat.mul(2n, id))))

# the request array is emb(tr) for the tree tr of its view
def sd.view(-a: Array<U32>, ev: Array<U32>, +c: Nat, +id: Nat, +qo: Nat, +ql: Nat,
  v: &t: A.Tr<U32> -> {A.emb(U32, t) == a : Array<U32>}
) -> Laws.Spec.cited(a, Laws.Spec.item(ev, c, id), qo, ql, Laws.Spec.verdict(C.verify(a, ev, c, id, qo, ql))):
  (+tr, e) = v
  %e : Laws.Spec.cited(_, Laws.Spec.item(ev, c, id), qo, ql,
    Laws.Spec.verdict(C.verify(_, ev, c, id, qo, ql)))
  sd.if(Nat.is_lt(id, c), tr, id, qo, ql, ev)

def Laws.cite_sound(a, ev, c, id, qo, ql):
  sd.view(a, ev, c, id, qo, ql, A.view(U32, a))
