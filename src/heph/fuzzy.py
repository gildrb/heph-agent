"""Forgiving name matching for commands and armories: exact, prefix, letters in order, typos."""

from collections.abc import Sequence


def _span(query: str, name: str) -> int | None:
    """How many characters of name the letters of query cover, in order; None if they don't fit."""
    start = at = name.find(query[0])
    if at < 0:
        return None
    for char in query[1:]:
        at = name.find(char, at + 1)
        if at < 0:
            return None
    return at - start + 1


def _typos(a: str, b: str) -> int:
    """Edits between a and b: insert, delete, substitute, or swap two neighbours."""
    before: list[int] = []
    previous = list(range(len(b) + 1))
    for i, x in enumerate(a, 1):
        current = [i, *([0] * len(b))]
        for j, y in enumerate(b, 1):
            current[j] = min(previous[j] + 1, current[j - 1] + 1, previous[j - 1] + (x != y))
            if i > 1 and j > 1 and x == b[j - 2] and a[i - 2] == y:
                current[j] = min(current[j], before[j - 2] + 1)
        before, previous = previous, current
    return previous[-1]


def rank(query: str, names: Sequence[str]) -> list[str]:
    """The names query could mean, best first; an empty query keeps every name in order.

    Tiers: exact, prefix (shorter first), letters in order (tighter first), then up to one
    typo per three letters (at least one). Ties keep the order of names.
    """
    wanted = query.casefold()
    if not wanted:
        return list(names)
    scored: list[tuple[int, int, int, str]] = []
    for order, name in enumerate(names):
        candidate = name.casefold()
        if candidate == wanted:
            tier, score = 0, 0
        elif candidate.startswith(wanted):
            tier, score = 1, len(candidate)
        elif (span := _span(wanted, candidate)) is not None:
            tier, score = 2, span
        elif (typos := _typos(wanted, candidate)) <= max(1, len(wanted) // 3):
            tier, score = 3, typos
        else:
            continue
        scored.append((tier, score, order, name))
    return [name for *_, name in sorted(scored)]
