import Base

# BM25 (k1 = 6/5, b = 3/4) in fixed point, and the size of the score table.
# Both the core and its laws use these definitions: they are what "the BM25
# score" means. All arithmetic is integer, so a reference implementation
# matches bit for bit. (A def that matches on its argument first stays
# folded in proofs, so the checker never expands the big literals.)

def pick(c: Bool, +a: Nat, +b: Nat) -> Nat:
  match c:
    case True{}:
      a
    case False{}:
      b

# floor(log2(n)) for n < 2^fuel; more: n >= 2
def ilog2(fuel: Nat, more: Bool, +n: Nat) -> Nat:
  match fuel more:
    case 1n+f True{}:
      1n+ilog2(f, Nat.is_le(4n, n), Nat.div(n, 2n))
    case _ _:
      0n

# the 16 fraction bits of log2(y / 2^16), y in [2^16, 2^17), from bit down
def frac(fuel: Nat, +y: Nat, +bit: Nat) -> Nat:
  match fuel:
    case 0n:
      0n
    case 1n+f:
      +y2 = Nat.div(Nat.mul(y, y), U32.to_nat(65536))
      +big = Nat.is_le(U32.to_nat(131072), y2)
      Nat.add(pick(big, bit, 0n), frac(f, pick(big, Nat.div(y2, 2n), y2), Nat.div(bit, 2n)))

# log2(m) with 16 fraction bits
def log2fx(+m: Nat) -> Nat:
  +k = ilog2(64n, Nat.is_le(2n, m), m)
  +y = Nat.div(Nat.mul(m, U32.to_nat(65536)), Nat.pow(2n, k))
  Nat.add(Nat.mul(k, U32.to_nat(65536)), frac(16n, y, U32.to_nat(32768)))

def idf.go(+n: Nat, +df: Nat) -> Nat:
  Nat.sub(log2fx(Nat.add(Nat.mul(2n, n), 2n)), log2fx(Nat.add(Nat.mul(2n, df), 1n)))

# the weight of a term with df postings over n chunks: log2((n + 1) / (df + 1/2))
def idf(+n: Nat, df: Nat) -> Nat:
  match df:
    case 0n:
      idf.go(n, 0n)
    case 1n+d:
      idf.go(n, 1n+d)

def tfn.pos(+tf: Nat, +dl: Nat, +tot: Nat, +n: Nat) -> Nat:
  +rel = Nat.div(Nat.mul(Nat.mul(dl, n), U32.to_nat(1024)), tot)
  Nat.div(Nat.mul(Nat.mul(44n, tf), U32.to_nat(1048576)),
    Nat.add(Nat.mul(U32.to_nat(1024), Nat.add(Nat.mul(20n, tf), 6n)), Nat.mul(18n, rel)))

# 1024 * tf * (k1 + 1) / (tf + k1 * (1 - b + b * dl / avgdl)), with
# avgdl = tot / n, so 1024 * dl / avgdl = 1024 * dl * n / tot
def tfn(tf: Nat, +dl: Nat, +tot: Nat, +n: Nat) -> Nat:
  match tf:
    case 0n:
      0n
    case 1n+p:
      tfn.pos(1n+p, dl, tot, n)

def bump.put(+id: Nat, +x: Nat, r: Array<Nat> & Nat) -> Array<Nat>:
  (s, v) = r
  Array.set(Nat, s, U32.from_nat(id), Nat.add(v, x))

# adds x to slot id of a score table
def bump(s: Array<Nat>, +id: Nat, +x: Nat) -> Array<Nat>:
  bump.put(id, x, Array.get(Nat, s, U32.from_nat(id)))

# the depth d of an array with at least n slots: 2^d = w, while more
def depth.go(fuel: Nat, more: Bool, +n: Nat, +d: Nat, +w: Nat) -> Nat:
  match fuel more:
    case 1n+f True{}:
      depth.go(f, Nat.is_lt(Nat.mul(2n, w), n), n, 1n+d, Nat.mul(2n, w))
    case _ _:
      d

def depth(+n: Nat) -> Nat:
  depth.go(48n, Nat.is_lt(1n, n), n, 0n, 1n)
