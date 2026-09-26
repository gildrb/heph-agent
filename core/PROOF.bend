import Base
import ./LAWS.bend as Laws
import ./proof/logic.bend as L
import ./proof/array.bend as A
import ./proof/word.bend as W
import ./proof/cite.bend as Cite
import ./proof/chunk.bend as Chunk
import ./proof/select.bend as Select
import ./proof/table.bend as Table
import ./proof/rank.bend as Rank

# Heph core proofs. Every law of LAWS.bend is proven in one of these modules:
#   proof/logic.bend   Bool and Nat facts, the hit order
#   proof/array.bend   arrays as trees: what get, set and new compute
#   proof/word.bend    what the U32 index arithmetic computes
#   proof/cite.bend    cite_badid, cite_sound
#   proof/chunk.bend   chunk_cover, chunk_text
#   proof/select.bend  rank_bounded, rank_ids_valid, rank_sorted: the top-k
#   proof/table.bend   the score table as a tree, and the scatter into it
#   proof/rank.bend    rank_scores, rank_complete: the table is the spec's fold
