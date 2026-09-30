"""Terminal output in the style of oh-my-pi: a shimmering working line over the live draft,
Markdown blocks with checked citations, one line per source and a stats line. Degrades to
plain text when the output is not a terminal."""

import re
import time
from collections import Counter
from collections.abc import Iterator, Sequence
from contextlib import contextmanager
from typing import ClassVar, override

from rich.console import (
    Console,
    ConsoleOptions,
    Group,
    JustifyMethod,
    RenderableType,
    RenderResult,
)
from rich.live import Live
from rich.markdown import CodeBlock, Heading, Markdown, MarkdownElement
from rich.segment import Segment, Segments
from rich.style import Style
from rich.syntax import Syntax
from rich.text import Text
from rich.theme import Theme

from heph.answer import CITATION, Citation, Evidence, Result, Status, location
from heph.index import Report

SEP = "   "  # separates fields on one line: spacing, no dots
ARROW = "\N{HEAVY RIGHT-POINTING ANGLE QUOTATION MARK ORNAMENT}"  # the prompt, as in oh-my-pi

# The dusk-apple palette of the oh-my-pi setup Heph follows. Body text keeps the terminal's
# own colour; these only mark things.
ACCENT = "#c4a77d"
DIM = "#9b9690"
MUTED = "#d9d0c6"
OK = "#95e7a6"
WARN = "#ffcc85"
ERROR = "#ffc3bd"
_CODE = "#c7dda3"
_LINK = "#a3dcfd"

_THEME = Theme(
    {
        "markdown.h1": f"bold underline {ACCENT}",
        **{f"markdown.h{level}": f"bold {ACCENT}" for level in range(2, 7)},
        "markdown.code": _CODE,
        "markdown.item.bullet": ACCENT,
        "markdown.item.number": ACCENT,
        "markdown.link": _LINK,
        "markdown.link_url": DIM,
        "markdown.block_quote": MUTED,
        "markdown.hr": DIM,
    }
)
_COLOR: dict[Status, str] = {"verified": OK, "unquoted": WARN, "failed": ERROR, "badid": ERROR}
_MARK: dict[Status, str] = {"verified": "✓", "unquoted": "?", "failed": "✗", "badid": "✗"}
_SEVERITY: tuple[Status, ...] = ("verified", "unquoted", "failed", "badid")
_FILES_SHOWN = 4  # file names on the Read line before "+N more"
# oh-my-pi's "kitt" scanner: speed in cells per second, head half-width, trail length, and
# the intensity at which a cell turns muted, then accent
_SCAN_SPEED = 30
_SCAN_HEAD = 0.6
_SCAN_TRAIL = 7
_SCAN_MID = 0.22
_SCAN_HIGH = 0.65
_ONE_DECIMAL_BELOW = 10  # 1.7K but 12K


class _Heading(Heading):
    """Headings flush left, as oh-my-pi draws them (rich centres h1)."""

    LEVEL_ALIGN: ClassVar[dict[str, JustifyMethod]] = {f"h{n}": "left" for n in range(1, 7)}


class _Code(CodeBlock):
    """Highlighted code on the terminal's own background, without padding."""

    @override
    def __rich_console__(self, console: Console, options: ConsoleOptions) -> RenderResult:
        code = str(self.text).rstrip()
        yield Syntax(code, self.lexer_name, theme=self.theme, word_wrap=True)


class _Markdown(Markdown):
    elements: ClassVar[dict[str, type[MarkdownElement]]] = {
        **Markdown.elements,
        "heading_open": _Heading,
        "fence": _Code,
        "code_block": _Code,
    }

    def __init__(self, text: str) -> None:
        super().__init__(text, code_theme="ansi_dark")


def console(*, stderr: bool = False) -> Console:
    """Rich console; honors NO_COLOR and degrades to plain text when not a terminal."""
    return Console(stderr=stderr, highlight=False, soft_wrap=True, theme=_THEME)


def shimmer(text: str, now: float) -> Text:
    """A bright head sweeps back and forth over the text, trailing a fade."""
    span = len(text) - 1
    if span <= 0:
        return Text(text, style=f"bold {ACCENT}")
    cycle = 2 * span
    sweep = now * _SCAN_SPEED % cycle
    rightward = sweep < span
    head = sweep if rightward else cycle - sweep
    out = Text()
    for index, char in enumerate(text):
        behind = head - index if rightward else index - head
        if abs(index - head) <= _SCAN_HEAD:
            level = 1.0
        elif behind <= _SCAN_HEAD:
            level = 0.0
        else:
            level = max(0.0, 1 - (behind - _SCAN_HEAD) / _SCAN_TRAIL) ** 2
        style = MUTED if level >= _SCAN_MID else DIM
        out.append(char, style=f"bold {ACCENT}" if level >= _SCAN_HIGH else style)
    return out


def compact(count: int) -> str:
    """Token counts the way oh-my-pi prints them: 950, 1.7K, 12K, 1.2M."""
    for size, unit in ((1_000_000, "M"), (1_000, "K")):
        if count >= size:
            value = count / size
            return f"{value:.1f}{unit}" if value < _ONE_DECIMAL_BELOW else f"{value:.0f}{unit}"
    return str(count)


def _labels(draft: str) -> str:
    """A draft with each finished citation shortened to its label, as it will render."""
    return CITATION.sub(lambda m: f"[E{m[1]}]" if m[1] else m[0], draft)


def _placeholder(number: int, width: int) -> str:
    """ASCII token Markdown leaves alone, same cell width as the label it stands for."""
    letters = ""
    while True:
        number, digit = divmod(number, 26)
        letters += chr(ord("a") + digit)
        if not number:
            break
    return f"Zq{letters}".ljust(width, "q")


def _plural(count: int, word: str) -> str:
    return f"{count} {word}{'' if count == 1 else 's'}"


def _trim(lines: list[list[Segment]]) -> list[list[Segment]]:
    """Rendered lines without blank ones at either end (rich opens lists with one)."""

    def blank(line: list[Segment]) -> bool:
        return not "".join(segment.text for segment in line).strip()

    start, end = 0, len(lines)
    while start < end and blank(lines[start]):
        start += 1
    while end > start and blank(lines[end - 1]):
        end -= 1
    return lines[start:end]


def _joined(lines: Sequence[list[Segment]], indent: str = "") -> Segments:
    segments: list[Segment] = []
    for line in lines:
        segments += [Segment(indent), *line, Segment.line()] if indent else [*line, Segment.line()]
    return Segments(segments)


class Renderer:
    def __init__(self, out: Console) -> None:
        self.console = out
        self.show_thinking = False  # ctrl+t: print the model's thinking, not just its length
        self._blank = True  # the last line printed is blank (or nothing is printed yet)
        self._label = ""
        self._esc = False
        self._since = 0.0
        self._asked = 0.0
        self._thought = ""
        self._thinking = False
        self._draft = ""

    def _out(self, renderable: RenderableType, *, end: str = "\n") -> None:
        self.console.print(renderable, end=end)
        self._blank = False

    def gap(self) -> None:
        """One blank line between blocks, never two."""
        if not self._blank:
            self.console.print()
            self._blank = True

    # Chrome

    def fullscreen(self) -> None:
        """Starts a session on a blank terminal: clears the screen and its scrollback."""
        if self.console.is_terminal:
            self.console.file.write("\x1b[H\x1b[2J\x1b[3J")
            self.console.file.flush()
            self._blank = True

    def title(self, text: str) -> None:
        """Names the terminal window or tab."""
        if self.console.is_terminal:
            clean = "".join(char for char in text if char.isprintable())
            self.console.file.write(f"\x1b]0;{clean}\x07")
            self.console.file.flush()

    @contextmanager
    def working(self, label: str, *, esc: bool = False) -> Iterator[None]:
        """A live region while Heph works: the draft being written or the tail of the model's
        thinking, over a shimmering status line. Printed blocks scroll above it."""
        self._label, self._esc = label, esc
        self._since = self._asked = time.monotonic()
        self._thought, self._thinking, self._draft = "", False, ""
        if not self.console.is_terminal:
            yield
            return
        with Live(
            console=self.console,
            transient=True,
            refresh_per_second=24,
            redirect_stdout=False,
            redirect_stderr=False,
            get_renderable=self._view,
        ):
            yield

    def update(self, label: str) -> None:
        self._label = label

    def _view(self) -> RenderableType:
        now = time.monotonic()
        options = self.console.options.update_width(self.console.width)
        parts: list[RenderableType] = []
        if self._draft.strip():
            draft = _Markdown(_labels(self._draft).strip())
            lines = _trim(self.console.render_lines(draft, options, pad=False))
            parts += [_joined(lines[-max(3, self.console.height - 4) :]), Text()]
        elif self._thinking and self._thought.strip():
            words = " ".join(self._thought[-4 * self.console.width :].split())
            tail = self.console.render_lines(Text(words, style=f"italic {DIM}"), options)
            parts += [_joined(tail[-2:]), Text()]
        row = Text("Esc ", style=DIM) if self._esc else Text()
        row.append_text(shimmer(f"{self._label}…", now))
        row.append(f"  {now - self._since:.0f}s", style=DIM)
        parts.append(row)
        return Group(*parts)

    # Transcript

    def question(self, text: str) -> None:
        """Echoes the question into the transcript, as the prompt showed it."""
        out = Text.assemble((f"{ARROW} ", ACCENT), "\n  ".join(text.splitlines()))
        self._out(out)
        self.gap()

    def rows(self, rows: Sequence[tuple[str, str]]) -> None:
        """Two aligned columns, the second dimmed, as the command menu shows them; an empty
        row is a blank line."""
        width = max((len(name) for name, _ in rows), default=0) + 3
        for name, about in rows:
            if name:
                self._out(Text.assemble(name.ljust(width), (about, DIM)))
            else:
                self.gap()

    def note(self, text: str) -> None:
        self._out(Text(text, style=DIM))

    def plain(self, text: str) -> None:
        self._out(Text(text))

    def warn(self, text: str) -> None:
        self._out(Text(text, style=WARN))

    def error(self, text: str) -> None:
        self._out(Text(f"Error: {text}", style=ERROR))

    def report(self, report: Report, *, quiet: bool) -> None:
        """Index outcome: skipped files always; when not quiet, what was indexed."""
        for source, reason in report.skipped:
            self._out(Text.assemble(("Skipped ", "bold"), (f"{source}: {reason}", WARN)))
        if quiet:
            return
        line = Text("Indexed ", style="bold")
        if report.built or report.removed:
            line.append(f"{report.built} of {_plural(report.files, 'file')}", style=DIM)
            if report.removed:
                line.append(f"{SEP}{report.removed} removed", style=DIM)
        else:
            line.append(f"{_plural(report.files, 'file')}{SEP}all up to date", style=DIM)
        self._out(line)

    # Answer events (heph.answer.Events)

    def evidence(self, items: Sequence[Evidence]) -> None:
        """The passages found for the question, before the model sees them."""
        self._label, self._asked = "Waiting for the model", time.monotonic()
        if not items:
            self._out(Text.assemble(("Read ", "bold"), ("nothing: no passage matches", WARN)))
            return
        files = list(dict.fromkeys(item.source for item in items))
        line = Text.assemble(("Read ", "bold"), _plural(len(items), "passage"), SEP)
        for index, name in enumerate(files[:_FILES_SHOWN]):
            line.append(("  " if index else "") + name, style=ACCENT)
        if len(files) > _FILES_SHOWN:
            line.append(f"  +{len(files) - _FILES_SHOWN} more", style=DIM)
        self._out(line)

    def reasoning(self, text: str) -> None:
        self._thinking, self._label = True, "Thinking"
        self._thought += text

    def _thought_done(self) -> None:
        """Replaces the live thinking with how long the model took before answering (from the
        request, as some servers deliver the whole stream at once), or with all of it."""
        if not self._thinking:
            return
        self._thinking = False
        seconds = time.monotonic() - self._asked
        self._out(Text.assemble(("Thought ", "bold"), (f"for {seconds:.1f}s", DIM)))
        if self.show_thinking and self._thought.strip():
            thought = Text(self._thought.strip(), style=f"italic {DIM}")
            self.console.print(thought, soft_wrap=False)  # wrapped at words, unlike URLs

    def draft(self, text: str) -> None:
        """The block being written, shown live until it is complete."""
        self._thought_done()
        self._label = "Writing"
        self._draft = text

    def block(self, text: str, offset: int, citations: Sequence[Citation]) -> None:
        self._thought_done()
        self._draft = ""
        self.gap()
        if not self.console.is_terminal:
            body = text if text.endswith("\n") else text + "\n"
            self.console.file.write(body)
            self.console.file.flush()
            self._blank = body.endswith("\n\n")
            return
        spans: dict[tuple[int, int], list[Citation]] = {}
        for citation in citations:
            spans.setdefault((citation.start - offset, citation.end - offset), []).append(citation)
        labels: dict[str, tuple[str, Style]] = {}
        source: list[str] = []
        position = 0
        for (start, end), group in spans.items():
            label = "[" + ", ".join(f"E{c.eid}" for c in group) + "]"
            worst = max((c.status for c in group), key=_SEVERITY.index)
            token = _placeholder(len(labels), len(label))
            labels[token] = (label, Style.parse(_COLOR[worst]))
            source += [text[position:start], token]
            position = end
        source.append(text[position:])
        rendered = self.console.render_lines(_Markdown("".join(source).strip()), pad=False)
        tokens = "|".join(sorted(labels, key=lambda token: (len(token), token), reverse=True))
        pattern = re.compile(f"({tokens})") if labels else None
        lines: list[list[Segment]] = []
        for line in _trim(rendered):
            out: list[Segment] = []
            for segment in line:
                if pattern is None or segment.control:
                    out.append(segment)
                    continue
                for part in pattern.split(segment.text):
                    if part in labels:
                        label, style = labels[part]
                        out.append(Segment(label, (segment.style or Style()) + style))
                    elif part:
                        out.append(Segment(part, segment.style))
            lines.append(out)
        self._out(_joined(lines), end="")

    def finish(self, result: Result) -> None:
        """One line per cited passage (its worst verdict in the mark), then the stats."""
        self.gap()
        groups: dict[int, list[Citation]] = {}
        for citation in result.citations:
            groups.setdefault(citation.eid, []).append(citation)
        for eid, group in groups.items():
            worst = max((c.status for c in group), key=_SEVERITY.index)
            line = Text.assemble((f"{_MARK[worst]} ", _COLOR[worst]), (f"E{eid}", "bold"))
            first = group[0]
            if first.source is not None:
                line.append("  " + location(first.source, first.page), style=ACCENT)
            counts: Counter[Status] = Counter(c.status for c in group)
            verdicts: dict[Status, str] = {
                "verified": _plural(counts["verified"], "quote") + " verified",
                "unquoted": _plural(counts["unquoted"], "citation") + " without a quote",
                "failed": _plural(counts["failed"], "quote") + " not found",
                "badid": "no such passage",
            }
            details = [text for status, text in verdicts.items() if counts[status]]
            line.append(SEP + SEP.join(details), style=DIM)
            self._out(line)
            for citation in group:
                if citation.status == "failed" and citation.quote:
                    quote = " ".join(citation.quote.split())
                    self.console.print(
                        Text(f'  "{quote}"', style=f"italic {ERROR}"),
                        overflow="ellipsis",
                        no_wrap=True,
                    )
        if not groups:
            self.note("No citations.")
        if result.truncated:
            self.warn("The answer was cut off at max_tokens.")
        stats = [result.model]
        if result.usage is not None:
            stats += [
                f"in {compact(result.usage.prompt_tokens)}",
                f"out {compact(result.usage.completion_tokens)}",
            ]
        stats.append(f"{result.seconds:.1f}s")
        if result.usage is not None and result.seconds > 0:
            # end to end, including prefill: some servers buffer the whole stream
            stats.append(f"{result.usage.completion_tokens / result.seconds:.0f} tok/s")
        self._out(Text(SEP.join(stats), style=DIM))

    def sources(self, evidence: Sequence[Evidence], citations: Sequence[Citation]) -> None:
        """Every passage the model was given for the last answer, in full."""
        if not evidence:
            self.note("No passage matched the last question.")
            return
        cited = Counter(citation.eid for citation in citations)
        options = self.console.options.update_width(max(20, self.console.width - 4))
        for item in evidence:
            self.gap()
            head = Text.assemble((f"E{item.eid}", "bold"))
            head.append("  " + location(item.source, item.page), style=ACCENT)
            if cited[item.eid]:
                head.append(f"{SEP}cited {_plural(cited[item.eid], 'time')}", style=OK)
            self._out(head)
            body = self.console.render_lines(Text(item.text.strip(), style=DIM), options)
            self._out(_joined(_trim(body), "    "), end="")
