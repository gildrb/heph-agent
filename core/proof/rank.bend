import Base
import ../core.bend as C
import ../LAWS.bend as Laws
import ../score.bend as F
import ./logic.bend as L
import ./array.bend as A
import ./select.bend as S
import ./table.bend as T
import ./word.bend as W

# Ranking: the scores are the spec's
# ==================================

# The core's selection reads the score slots through Array.get; on the
# table emb(ts) those reads are the tree's slots.
def slots_ml(+m: Nat, +i: Nat, +ts: A.Tr<Nat>, +e: Nat, +hp: {T.perf(ts, e) == True{} : Bool},
  +hd: {Nat.is_lt(e, 32n) == True{} : Bool}, +hb: {Nat.is_le(Nat.add(i, m), Laws.Spec.pow2(e)) == True{} : Bool}
) -> {S.slots(m, i, (A.emb(Nat, ts), A.tget(Nat, ts, U32.from_nat(i)))) == T.ML(ts, e, m, i) : List<&2, Nat>}:
  match m:
    case 0n:
      {==}
    case 1n+k:
      +j = U32.from_nat(1n+i)
      %Equal.sym(Array<Nat> & Nat, Array.get(Nat, A.emb(Nat, ts), j), (A.emb(Nat, ts), A.tget(Nat, ts, j)),
        A.get_emb(Nat, ts, j)) :
        {A.tget(Nat, ts, U32.from_nat(i)) <> S.slots(k, 1n+i, _) == T.pget(ts, e, i) <> T.ML(ts, e, k, 1n+i) :
          List<&2, Nat>}
      %Equal.sym(Nat, A.tget(Nat, ts, U32.from_nat(i)), T.pget(ts, e, i),
        T.tget_at(ts, e, i, hp, hd, T.hd_lt(i, k, Laws.Spec.pow2(e), hb))) :
        {_ <> S.slots(k, 1n+i, (A.emb(Nat, ts), A.tget(Nat, ts, j))) == T.pget(ts, e, i) <> T.ML(ts, e, k, 1n+i) :
          List<&2, Nat>}
      %Equal.sym(List<&2, Nat>, S.slots(k, 1n+i, (A.emb(Nat, ts), A.tget(Nat, ts, j))), T.ML(ts, e, k, 1n+i),
        slots_ml(k, 1n+i, ts, e, hp, hd, T.tl_le(i, k, Laws.Spec.pow2(e), hb))) :
        {T.pget(ts, e, i) <> _ == T.pget(ts, e, i) <> T.ML(ts, e, k, 1n+i) : List<&2, Nat>}
      {==}

# what a law about the scores and the hits asks, for every slot list lv
def RankG(-G: List<&2, Nat> -> List<&2, C.Hit> -> Type, -k: Nat) -> Type:
  @+lv: List<&2, Nat> -> G(lv, S.R(lv, k))

def rv.fin(+tp: A.Tr<U32>, +ts: A.Tr<Nat>, +k: Nat, +n: Nat, +tot: Nat, +t: Nat,
  sc: {Laws.Spec.scores(n, tot, A.emb(U32, tp), t) == S.slots(n, 0n, (A.emb(Nat, ts), A.tget(Nat, ts, 0))) :
    List<&2, Nat>},
  b: S.BS(n, k, 0n, 0n, Nil{}, (A.emb(Nat, ts), A.tget(Nat, ts, 0)), A.emb(U32, tp)),
  -G: List<&2, Nat> -> List<&2, C.Hit> -> Type, g: RankG(G, k)
) -> G(Laws.Spec.scores(n, tot, A.emb(U32, tp), t), Laws.Spec.hits(C.rank.fin(A.emb(U32, tp),
  C.sel(n, k, 0n, C.sel.of(0n, Nil{}, (A.emb(Nat, ts), A.tget(Nat, ts, 0))))))):
  (+lv, pr) = b
  (pa, e3) = pr
  (e1, e2) = pa
  -r0 = {(A.emb(Nat, ts), A.tget(Nat, ts, 0)) : Array<Nat> & Nat}
  %Equal.sym(List<&2, Nat>, Laws.Spec.scores(n, tot, A.emb(U32, tp), t), lv,
    Equal.trans(List<&2, Nat>, Laws.Spec.scores(n, tot, A.emb(U32, tp), t), S.slots(n, 0n, r0), lv, sc, e1)) :
    G(_, Laws.Spec.hits(C.rank.fin(A.emb(U32, tp), C.sel(n, k, 0n, C.sel.of(0n, Nil{}, r0)))))
  %Equal.sym(List<&2, C.Hit>, Laws.Spec.hits(C.rank.fin(A.emb(U32, tp), C.sel(n, k, 0n, C.sel.of(0n, Nil{}, r0)))),
    List.reverse(&2, C.Hit, S.msel(lv, k, 0n, 0n, Nil{})), e3) :
    G(lv, _)
  g(lv)

# the scores the spec folds are the table the core fills
def scores_eq(+tp: A.Tr<U32>, +n: Nat, +tot: Nat, +t: Nat, +hE: {Nat.is_lt(F.depth(n), 32n) == True{} : Bool},
  +hnE: {Nat.is_le(n, Laws.Spec.pow2(F.depth(n))) == True{} : Bool}
) -> {Laws.Spec.scores(n, tot, A.emb(U32, tp), t) ==
  S.slots(n, 0n, (A.emb(Nat, T.pterms(t, tp, F.depth(n), 0n, tot, n, A.tnew(Nat, F.depth(n), 0n))),
    A.tget(Nat, T.pterms(t, tp, F.depth(n), 0n, tot, n, A.tnew(Nat, F.depth(n), 0n)), 0))) : List<&2, Nat>}:
  +E = F.depth(n)
  +t0 = A.tnew(Nat, E, 0n)
  +TS = T.pterms(t, tp, E, 0n, tot, n, t0)
  +hp0 = T.perf_new(E, 0n)
  %Equal.sym(List<&2, List<&2, Laws.Spec.Post>>, Laws.Spec.postings(A.emb(U32, tp), t), T.dterms(t, tp, 0n),
    T.postings_eq(tp, t)) :
    {Laws.Spec.add_terms(_, tot, n, Laws.Spec.zeros(n)) == S.slots(n, 0n, (A.emb(Nat, TS), A.tget(Nat, TS, 0))) :
      List<&2, Nat>}
  %T.ml_new(E, n, 0n) :
    {Laws.Spec.add_terms(T.dterms(t, tp, 0n), tot, n, _) == S.slots(n, 0n, (A.emb(Nat, TS), A.tget(Nat, TS, 0))) :
      List<&2, Nat>}
  %T.ml_terms(t, tp, E, 0n, tot, n, t0, hp0, hnE) :
    {_ == S.slots(n, 0n, (A.emb(Nat, TS), A.tget(Nat, TS, 0))) : List<&2, Nat>}
  Equal.sym(List<&2, Nat>, S.slots(n, 0n, (A.emb(Nat, TS), A.tget(Nat, TS, 0))), T.ML(TS, E, n, 0n),
    slots_ml(n, 0n, TS, E, T.perf_terms(t, tp, E, 0n, tot, n, t0, hp0), hE, hnE))

def rv.go(+tp: A.Tr<U32>, +k: Nat, +n: Nat, +tot: Nat, +t: Nat, +hE: {Nat.is_lt(F.depth(n), 32n) == True{} : Bool},
  +hnE: {Nat.is_le(n, Laws.Spec.pow2(F.depth(n))) == True{} : Bool},
  -G: List<&2, Nat> -> List<&2, C.Hit> -> Type, g: RankG(G, k)
) -> G(Laws.Spec.scores(n, tot, A.emb(U32, tp), t), Laws.Spec.hits(C.rank(k, n, tot, A.emb(U32, tp), t))):
  +E = F.depth(n)
  +t0 = A.tnew(Nat, E, 0n)
  -SC = {Laws.Spec.scores(n, tot, A.emb(U32, tp), t) : List<&2, Nat>}
  %Equal.sym(Array<Nat>, Array.new(Nat, E, 0n), A.emb(Nat, t0), A.new_emb(Nat, E, 0n)) :
    G(SC, Laws.Spec.hits(C.rank.sel(k, n, C.add.terms(t, tot, n, C.Tq{A.emb(U32, tp), _, 0n}))))
  +TS = T.pterms(t, tp, E, 0n, tot, n, t0)
  %Equal.sym(C.Tq, C.add.terms(t, tot, n, C.Tq{A.emb(U32, tp), A.emb(Nat, t0), 0n}),
    C.Tq{A.emb(U32, tp), A.emb(Nat, TS), T.qterms(t, tp, 0n)},
    T.terms_eq(t, 0n, tot, n, tp, t0, E, T.perf_new(E, 0n), hE, hnE)) :
    G(SC, Laws.Spec.hits(C.rank.sel(k, n, _)))
  %Equal.sym(Array<Nat> & Nat, Array.get(Nat, A.emb(Nat, TS), 0), (A.emb(Nat, TS), A.tget(Nat, TS, 0)),
    A.get_emb(Nat, TS, 0)) :
    G(SC, Laws.Spec.hits(C.rank.fin(A.emb(U32, tp), C.sel(n, k, 0n, C.sel.of(0n, Nil{}, _)))))
  rv.fin(tp, TS, k, n, tot, t, scores_eq(tp, n, tot, t, hE, hnE),
    S.bsel(n, k, 0n, 0n, Nil{}, (A.emb(Nat, TS), A.tget(Nat, TS, 0)), A.emb(U32, tp)), G, g)

def rv.depth(+tp: A.Tr<U32>, +k: Nat, +n: Nat, +tot: Nat, +t: Nat,
  pr: Pair({Nat.is_lt(F.depth(n), 32n) == True{} : Bool}, {Nat.is_le(n, Laws.Spec.pow2(F.depth(n))) == True{} : Bool}),
  -G: List<&2, Nat> -> List<&2, C.Hit> -> Type, g: RankG(G, k)
) -> G(Laws.Spec.scores(n, tot, A.emb(U32, tp), t), Laws.Spec.hits(C.rank(k, n, tot, A.emb(U32, tp), t))):
  (+hE, +hnE) = pr
  rv.go(tp, k, n, tot, t, hE, hnE, G, g)

def rv.view(+k: Nat, +d: Nat, +n: Nat, +tot: Nat, -p: Array<U32>, +t: Nat, +hd: {Nat.is_le(d, 31n) == True{} : Bool},
  +hn: {Nat.is_le(n, Laws.Spec.pow2(d)) == True{} : Bool}, v: &tp: A.Tr<U32> -> {A.emb(U32, tp) == p : Array<U32>},
  -G: List<&2, Nat> -> List<&2, C.Hit> -> Type, g: RankG(G, k)
) -> G(Laws.Spec.scores(n, tot, p, t), Laws.Spec.hits(C.rank(k, n, tot, p, t))):
  (+tp, ev) = v
  %ev : G(Laws.Spec.scores(n, tot, _, t), Laws.Spec.hits(C.rank(k, n, tot, _, t)))
  rv.depth(tp, k, n, tot, t, T.depth_ok(n, d, hd, hn), G, g)

def Laws.rank_scores(k, d, n, hd, hn, tot, p, t):
  rv.view(k, d, n, tot, p, t, hd, hn, A.view(U32, p),
    sc => hs => {Laws.Spec.all_scored(sc, hs) == True{} : Bool}, lv => S.sel_scored(lv, k))

def Laws.rank_complete(k, d, n, hd, hn, tot, p, t, c):
  rv.view(k, d, n, tot, p, t, hd, hn, A.view(U32, p),
    sc => hs => {Laws.Spec.complete.at(List.get(&2, Nat, sc, c), hs, k, c) == True{} : Bool},
    lv => S.sel_complete(lv, k, c))
