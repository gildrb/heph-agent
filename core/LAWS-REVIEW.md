# LAWS.bend: list core vs packed core

The claims are the same nine. What changed is what they quantify over: the
packed core reads bytes and postings out of `Array<U32>` buffers, so the laws
now take buffers and offsets, and `Spec.*` reads them back into the lists the
old laws spoke about. Every law is proven (`bend PROOF.bend`: "All terms
check.") and was mutation-checked.

| law | old (list core) | new (packed core) |
|---|---|---|
| cite_sound | `Spec.cited(cs, id, q, C.verify(cs, id, q)) == True` | `Spec.cited(a, Spec.item(ev, c, id), qo, ql, verdict(C.verify(a, ev, c, id, qo, ql)))`: a Found offset needs `Spec.item` to be Some (a real id), and the normalized quote `Spec.read(a, qo, ql)` is the slice of the normalized evidence `Spec.read(a, off, len)` at the offset |
| cite_badid | `is_bad(C.verify(cs, id, q)) == is_none(List.get(cs, id))` | `is_bad(verdict(C.verify(a, ev, c, id, qo, ql))) == is_none(Spec.item(ev, c, id))` |
| rank_scores | `all_scored(Spec.scores(n, tot, ts), C.rank(k, n, tot, ts))` | `all_scored(Spec.scores(n, tot, p, t), hits(C.rank(k, n, tot, p, t)))`, **for n <= 2^d, d <= 31** |
| rank_complete | `complete.at(get(Spec.scores(n, tot, ts), c), C.rank(...), k, c)` | same over `(p, t)`, **for n <= 2^d, d <= 31** |
| rank_bounded | `length(C.rank(k, n, tot, ts)) <= k` | `length(hits(C.rank(k, n, tot, p, t))) <= k` |
| rank_ids_valid | `all_lt(C.rank(k, n, tot, ts), n)` | `all_lt(hits(C.rank(k, n, tot, p, t)), n)` |
| rank_sorted | `sorted(C.rank(k, n, tot, ts))` | `sorted(hits(C.rank(k, n, tot, p, t)))` |
| chunk_cover | `covers(C.chunk.doc(xs, len xs), len xs)` | `covers(chunks(C.chunk.doc(a, o, n)), n)`: the document is the n bytes of `a` at `o` |
| chunk_text | each chunk's text `x == slice(xs, s, e - s)` | each chunk's request bytes `Spec.read(a, o + s, e - s) == slice(Spec.read(a, o, n), s, e - s)` |

## Specs

- `Spec.read(a, p, n)`: the n bytes of a packed buffer from p (byte p at bits `8 * (p % 4)` of word `p / 4`). `Spec.item(ev, c, id)`: evidence item id as (offset, length), None unless `id < c`.
- `Spec.postings(p, t)`: reads the packed postings (per term: df, then df `(id, tf, dl)` words) into one list per term. `Spec.scores` is again the old list fold: n zero slots, each posting adds `idf(n, df) * tfn(tf, dl, tot, n)` to its slot. It no longer uses `Array.get`/`Array.set` on a table.
- `Spec.norm` (whitespace normalization) is now defined in LAWS; the old spec called `C.norm`.

## For review

1. **New hypothesis on rank_scores and rank_complete: `n <= 2^d` for some `d <= 31`.** The core's score table is indexed by U32; beyond 2^31 slots its indices would wrap, and the list spec would not. main.bend already rejects `n >= 2^24`. (The bound is stated through `d` because a literal 2^31 in a law makes the checker expand it.)
2. `idf` and `tfn` come from score.bend, shared with the core: the fixed-point formula is the definition of the score. The old laws called `C.idf`/`C.tfn`.
3. chunk_text now only constrains chunk bounds: chunks carry no text, so the law says every chunk lies inside the document. A core that breaks it also breaks chunk_cover.
4. The proofs trust Bend 2.0.27's checker, compiler and runtime, including Base's `Array` (a tree in proofs, a flat buffer in generated C) and the `nat_chk` 2^48 limit that main.bend's range checks keep clear of.
