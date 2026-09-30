"""Interactive input in the style of oh-my-pi: a borderless editor with the status bar under
it, a fuzzy slash-command menu, arrow-key pickers, and Esc to stop an answer."""

import asyncio
import os
import select
import signal
import sys
import termios
import threading
import time
import tty
from collections.abc import Callable, Iterator, Mapping, Sequence
from contextlib import contextmanager
from dataclasses import dataclass
from getpass import getpass
from pathlib import Path
from typing import Literal

from prompt_toolkit import prompt
from prompt_toolkit.application import Application, get_app
from prompt_toolkit.buffer import Buffer
from prompt_toolkit.completion import CompleteEvent, PathCompleter
from prompt_toolkit.document import Document
from prompt_toolkit.filters import Condition
from prompt_toolkit.formatted_text import StyleAndTextTuples
from prompt_toolkit.history import FileHistory, History, InMemoryHistory
from prompt_toolkit.key_binding import KeyBindings, KeyPressEvent, merge_key_bindings
from prompt_toolkit.key_binding.defaults import load_key_bindings
from prompt_toolkit.layout import ConditionalContainer, HSplit, Layout, Window
from prompt_toolkit.layout.controls import BufferControl, FormattedTextControl
from prompt_toolkit.layout.processors import AfterInput, ConditionalProcessor
from prompt_toolkit.styles import Style
from prompt_toolkit.utils import get_cwidth

from heph.fuzzy import order, rank
from heph.render import ACCENT, ARROW, DIM, SEP, WARN

# name: (arguments, what it does); the order is the menu order for an empty query
COMMANDS: dict[str, tuple[str, str]] = {
    "model": ("", "pick the model, add a local server, or log in"),
    "armory": ("", "open or create an armory"),
    "add": ("[path]", "copy a file or folder into this armory"),
    "new": ("", "start a new chat"),
    "sources": ("", "show the passages behind the last answer"),
    "login": ("", "log in to OpenAI, OpenRouter, DeepSeek, Z.AI or ChatGPT"),
    "logout": ("", "remove a login"),
    "help": ("", "show commands and keys"),
    "exit": ("", "quit"),
}
KEYS: tuple[tuple[str, str], ...] = (
    ("esc", "stop an answer"),
    ("ctrl+c", "clear the line; twice to quit"),
    ("ctrl+d", "quit"),
    ("ctrl+l", "pick a model"),
    ("ctrl+o", "show the passages behind the last answer"),
    ("ctrl+t", "show or hide the model's thinking"),
    ("alt+enter", "new line"),
    ("up, down", "earlier questions"),
)
_ALIASES = {"quit": "exit"}
_ROWS = 10  # menu rows shown at once
_NAME_WIDTH = 32  # widest name column, as in oh-my-pi
_TWICE = 1.0  # seconds within which a second ctrl+c on an empty line quits
_ESC = b"\x1b"
_ERASE = frozenset("\x7f\b")
_STYLE = Style.from_dict(
    {
        # own class names: prompt_toolkit's built-in style paints anything named "menu" grey
        "hp.prompt": ACCENT,
        "hp.placeholder": DIM,
        "hp.current": ACCENT,
        "hp.dim": DIM,
        "hp.status": DIM,
        "hp.model": "",
        "hp.hint": WARN,
        "hp.title": f"bold {ACCENT}",
        "completion-menu": "bg:default",
        "completion-menu.completion": "bg:default fg:default",
        "completion-menu.completion.current": f"bg:default {ACCENT}",
        "completion-menu.meta.completion": f"bg:default {DIM}",
        "completion-menu.meta.completion.current": f"bg:default {ACCENT}",
        "scrollbar.background": "bg:default",
        "scrollbar.button": f"bg:{DIM}",
    }
)


@dataclass(frozen=True, slots=True)
class Option:
    value: str
    label: str
    detail: str = ""


type Action = Literal["model", "sources", "thinking"]


@dataclass(frozen=True, slots=True)
class Key:
    """A key that asks the session to do something, instead of a typed line."""

    name: Action


@dataclass(frozen=True, slots=True)
class _Match:
    text: str  # the whole line once applied
    name: str
    about: str = ""
    runs: bool = True  # Enter applies and submits; a path only completes (Tab)


def _rows(items: Sequence[tuple[str, str]], selected: int, limit: int) -> StyleAndTextTuples:
    """Rows as oh-my-pi's select list draws them: the selection marked and accented, names in a
    column, descriptions dimmed, a window that follows the selection, a count when it scrolls."""
    start = min(max(selected - limit // 2, 0), max(len(items) - limit, 0))
    shown = list(enumerate(items))[start : start + limit]
    width = min(max((get_cwidth(name) for _, (name, _) in shown), default=0), _NAME_WIDTH)
    lines: list[StyleAndTextTuples] = []
    for index, (name, about) in shown:
        current = index == selected
        style = "class:hp.current" if current else ""
        line: StyleAndTextTuples = []
        line.append((style, f"{ARROW if current else ' '} "))
        line.append((style, name + " " * max(width - get_cwidth(name), 0)))
        if about:
            line.append((style or "class:hp.dim", f"  {about}"))
        lines.append(line)
    if len(items) > limit:
        lines.append([("class:hp.dim", f"  ({selected + 1}/{len(items)})")])
    out: StyleAndTextTuples = []
    for number, line in enumerate(lines):
        out += [("", "\n")] if number else []
        out += line
    return out


def _typed(text: str) -> str:
    """What a burst of keystrokes leaves on a line: printable text, with backspace applied."""
    out: list[str] = []
    for char in text:
        if char in _ERASE:
            out = out[:-1]
        elif char.isprintable():
            out.append(char)
    return "".join(out)


class _Picker:
    """A list under a filter line: typing narrows it (fuzzy), arrows move, Enter picks."""

    def __init__(self, title: str, options: Sequence[Option], rows: int) -> None:
        self.title = title
        self.options = options
        self.rows = rows
        self.selected = 0
        self.query = Buffer(multiline=False, on_text_changed=self._reset)

    def _reset(self, _buffer: Buffer) -> None:
        self.selected = 0

    def found(self) -> list[Option]:
        texts = [f"{option.label} {option.detail}" for option in self.options]
        return [self.options[index] for index in order(self.query.text, texts)]

    def lines(self) -> StyleAndTextTuples:
        found = self.found()
        self.selected = min(self.selected, max(len(found) - 1, 0))
        items = [(option.label, option.detail) for option in found]
        out = _rows(items, self.selected, self.rows) if items else []
        if not items:
            out.append(("class:hp.dim", "  nothing matches"))
        out.append(("class:hp.dim", "\n  type to filter   ↑↓ move   enter pick   esc cancel"))
        return out

    def run(self) -> str | None:
        keys = KeyBindings()

        @keys.add("up")
        @keys.add("c-p")
        @keys.add("s-tab")
        def _up(event: KeyPressEvent) -> None:
            del event
            count = len(self.found())
            self.selected = (self.selected - 1) % count if count else 0

        @keys.add("down")
        @keys.add("c-n")
        @keys.add("tab")
        def _down(event: KeyPressEvent) -> None:
            del event
            count = len(self.found())
            self.selected = (self.selected + 1) % count if count else 0

        @keys.add("enter")
        def _pick(event: KeyPressEvent) -> None:
            found = self.found()
            event.app.exit(result=found[self.selected].value if found else None)

        @keys.add("escape", eager=True)
        @keys.add("c-c")
        @keys.add("c-d")
        def _skip(event: KeyPressEvent) -> None:
            event.app.exit(result=None)

        def prefix(_line: int, _wrap: int) -> StyleAndTextTuples:
            return [("class:hp.placeholder", "  filter ")]

        layout = Layout(
            HSplit(
                [
                    Window(FormattedTextControl([("class:hp.title", self.title)]), height=1),
                    Window(BufferControl(self.query), height=1, get_line_prefix=prefix),
                    Window(FormattedTextControl(self.lines)),
                ]
            ),
            focused_element=self.query,
        )
        app: Application[str | None] = Application(
            layout=layout, key_bindings=keys, style=_STYLE, erase_when_done=True
        )
        return app.run()


def pick(title: str, options: Sequence[Option], rows: int = 12) -> str | None:
    """Lets the user choose one option; None when they skip (Esc, Ctrl+C) or there is none."""
    if not options:
        return None
    if sys.stdin.isatty():
        return _Picker(title, options, rows).run()
    print(title)
    for number, option in enumerate(options, 1):
        print(f"  {number}  {option.label}  {option.detail}".rstrip())
    try:
        answer = input(f"{ARROW} ").strip()
    except EOFError:
        return None
    if answer.isdigit() and 1 <= int(answer) <= len(options):
        return options[int(answer) - 1].value
    found = order(answer, [option.label for option in options]) if answer else []
    return options[found[0]].value if found else None


def command(name: str) -> str | None:
    """The command a typed name most likely means (exact, prefix, letters in order, typos)."""
    ranked = rank(name, [*COMMANDS, *_ALIASES])
    return _ALIASES.get(ranked[0], ranked[0]) if ranked else None


def help_rows() -> list[tuple[str, str]]:
    """What /help shows: the commands, a blank row, then the keys."""
    commands = [(f"/{name} {args}".rstrip(), about) for name, (args, about) in COMMANDS.items()]
    return [*commands, ("", ""), *KEYS]


class Input:
    """Reads what the user types: in a terminal, a borderless editor with history, the
    slash-command menu and the status bar under it; elsewhere, plain lines."""

    def __init__(
        self,
        choices: Mapping[str, Callable[[], list[str]]],
        status: Callable[[], tuple[list[str], list[str]]],
    ) -> None:
        self._choices = choices
        self._status = status
        self._placeholder = ""
        self._carry = ""  # typed while an answer streamed, or left in the editor by a key
        self._matches: list[_Match] = []
        self._selected = 0
        self._closed = False  # Esc hid the menu, or history filled the line
        self._recalling = False
        self._armed = 0.0  # when ctrl+c last found the line empty
        self._saved = termios.tcgetattr(sys.stdin.fileno()) if sys.stdin.isatty() else None
        self._buffer, self._app = self._build(InMemoryHistory())

    def history(self, path: Path) -> None:
        """Switches to an armory's input history file."""
        self._buffer, self._app = self._build(FileHistory(str(path)))

    def close(self) -> None:
        """Leaves the terminal in the mode Heph found it in."""
        if self._saved is not None:
            termios.tcsetattr(sys.stdin.fileno(), termios.TCSADRAIN, self._saved)

    # The slash-command menu

    def _find(self, text: str) -> list[_Match]:
        if not text.startswith("/") or "\n" in text:
            return []
        name, space, arg = text[1:].partition(" ")
        if not space:
            commands = rank(name, [*COMMANDS])
            return [_Match(f"/{found} ", found, COMMANDS[found][1]) for found in commands]
        found = command(name)
        if found == "add":
            head, _, token = arg.rpartition(" ")
            base = f"/add {head} " if head else "/add "
            completer = PathCompleter(expanduser=True)
            paths = completer.get_completions(Document(token), CompleteEvent())
            return [
                _Match(
                    base + token + path.text + ("/" if path.display_text.endswith("/") else ""),
                    path.display_text,
                    runs=False,
                )
                for path in paths
            ]
        if found is not None and found in self._choices:
            chosen = rank(arg.strip(), self._choices[found]())
            return [_Match(f"/{found} {choice}", choice) for choice in chosen]
        return []

    def _changed(self, buffer: Buffer) -> None:
        self._matches = self._find(buffer.text)
        self._selected = 0
        self._closed = self._recalling

    def _open(self) -> bool:
        return bool(self._matches) and not self._closed

    def _apply(self, match: _Match) -> None:
        self._buffer.document = Document(match.text, len(match.text))

    def _move(self, step: int) -> None:
        self._selected = (self._selected + step) % len(self._matches)

    # The editor

    def _gutter(self, line: int, wrap: int) -> StyleAndTextTuples:
        return [("class:hp.prompt", f"{ARROW} ")] if line == 0 and wrap == 0 else [("", "  ")]

    def _menu(self) -> StyleAndTextTuples:
        return _rows([(m.name, m.about) for m in self._matches], self._selected, _ROWS)

    def _bar(self) -> StyleAndTextTuples:
        """The status bar: what is in use on the left, where it runs on the right."""
        if time.monotonic() - self._armed < _TWICE:
            return [("class:hp.hint", "Press ctrl+c again to quit")]
        left, right = self._status()
        out: StyleAndTextTuples = [("class:hp.model", left[0])] if left else []
        out += [("class:hp.status", SEP + part) for part in left[1:]]
        tail = SEP.join(right)
        used = sum(get_cwidth(fragment[1]) for fragment in out)
        gap = get_app().output.get_size().columns - used - get_cwidth(tail)
        if tail and gap >= len(SEP):
            out.append(("class:hp.status", " " * gap + tail))
        return out

    def _accept(self, buffer: Buffer) -> bool:
        get_app().exit(result=buffer.text)
        return False

    def _keys(self) -> KeyBindings:
        menu = Condition(self._open)
        empty = Condition(lambda: not get_app().current_buffer.text)
        keys = KeyBindings()

        @keys.add("enter")
        def _enter(event: KeyPressEvent) -> None:
            if self._open() and self._matches[self._selected].runs:
                self._apply(self._matches[self._selected])
            event.current_buffer.validate_and_handle()

        @keys.add("tab", filter=menu)
        def _complete(event: KeyPressEvent) -> None:
            del event
            self._apply(self._matches[self._selected])

        @keys.add("up")
        @keys.add("down")
        def _arrow(event: KeyPressEvent) -> None:
            up = event.key_sequence[0].key == "up"
            if self._open():
                self._move(-1 if up else 1)
                return
            self._recalling = True
            if up:
                event.current_buffer.auto_up(count=event.arg)
            else:
                event.current_buffer.auto_down(count=event.arg)
            self._recalling = False

        @keys.add("s-tab", filter=menu)
        @keys.add("c-p", filter=menu)
        def _previous(event: KeyPressEvent) -> None:
            del event
            self._move(-1)

        @keys.add("c-n", filter=menu)
        def _next(event: KeyPressEvent) -> None:
            del event
            self._move(1)

        @keys.add("escape", eager=True, filter=menu)
        def _close(event: KeyPressEvent) -> None:
            del event
            self._closed = True

        @keys.add("escape", "enter")
        @keys.add("c-j")
        def _newline(event: KeyPressEvent) -> None:
            event.current_buffer.insert_text("\n")

        @keys.add("c-c")
        def _clear(event: KeyPressEvent) -> None:
            if event.current_buffer.text:
                event.current_buffer.reset()
                return
            now = time.monotonic()
            if now - self._armed < _TWICE:
                event.app.exit(exception=EOFError())
                return
            self._armed = now
            _ = asyncio.get_running_loop().call_later(_TWICE, event.app.invalidate)

        @keys.add("c-d", filter=empty)
        def _quit(event: KeyPressEvent) -> None:
            event.app.exit(exception=EOFError())

        actions: tuple[tuple[str, Action], ...] = (
            ("c-l", "model"),
            ("c-o", "sources"),
            ("c-t", "thinking"),
        )
        for key, name in actions:
            keys.add(key)(self._action(name))
        return keys

    def _action(self, name: Action) -> Callable[[KeyPressEvent], None]:
        def run(event: KeyPressEvent) -> None:
            self._carry = event.current_buffer.text
            event.app.exit(result=Key(name))

        return run

    def _build(self, history: History) -> tuple[Buffer, Application[str | Key]]:
        buffer = Buffer(
            history=history,
            multiline=True,
            accept_handler=self._accept,
            on_text_changed=self._changed,
        )
        menu = Condition(self._open)
        placeholder = ConditionalProcessor(
            AfterInput(lambda: self._placeholder, style="class:hp.placeholder"),
            filter=Condition(lambda: not buffer.text),
        )
        editor = Window(
            BufferControl(buffer, input_processors=[placeholder]),
            get_line_prefix=self._gutter,
            wrap_lines=True,
            dont_extend_height=True,
        )
        layout = Layout(
            HSplit(
                [
                    editor,
                    ConditionalContainer(
                        Window(FormattedTextControl(self._menu), dont_extend_height=True),
                        filter=menu,
                    ),
                    ConditionalContainer(
                        Window(FormattedTextControl(self._bar), height=1), filter=~menu
                    ),
                ]
            ),
            focused_element=editor,
        )
        app: Application[str | Key] = Application(
            layout=layout,
            key_bindings=merge_key_bindings([load_key_bindings(), self._keys()]),
            style=_STYLE,
            erase_when_done=True,
        )
        return buffer, app

    def read(self, placeholder: str) -> str | Key:
        """One question, command or key; raises EOFError when the user quits."""
        if not sys.stdin.isatty():
            return input(f"{ARROW} ")
        self._placeholder = placeholder
        text, self._carry = self._carry, ""
        self._armed = 0.0

        def start() -> None:
            self._buffer.reset(Document(text, len(text)))
            self._changed(self._buffer)

        return self._app.run(pre_run=start)

    @contextmanager
    def busy(self) -> Iterator[None]:
        """While Heph answers: Esc stops the answer as ctrl+c does, and what the user types is
        kept for the next prompt."""
        if self._saved is None:
            yield
            return
        fd = sys.stdin.fileno()
        mode = termios.tcgetattr(fd)
        tty.setcbreak(fd)  # keys arrive one by one and unechoed; ctrl+c still interrupts
        stop = threading.Event()
        typed: list[str] = []

        def watch() -> None:
            signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGINT})  # the main thread gets it
            while not stop.is_set():
                if not select.select([fd], [], [], 0.05)[0]:
                    continue
                data = os.read(fd, 1024)
                if data == _ESC:  # a lone Esc: key sequences start with it but arrive whole
                    os.kill(os.getpid(), signal.SIGINT)
                    return
                if not data.startswith(_ESC):
                    typed.append(data.decode(errors="ignore"))

        watcher = threading.Thread(target=watch, daemon=True)
        watcher.start()
        try:
            yield
        finally:
            stop.set()
            termios.tcsetattr(fd, termios.TCSADRAIN, mode)
            watcher.join()
            self._carry += _typed("".join(typed))

    def ask(self, label: str, default: str = "") -> str:
        """One plain answer, outside history and completion. The default shows dimmed until
        the user types, and Enter on an empty line takes it."""
        if not sys.stdin.isatty():
            return input(label) or default
        hint: StyleAndTextTuples = [("class:hp.placeholder", default)]
        return prompt(label, placeholder=hint, style=_STYLE) or default

    def path(self, label: str) -> str:
        """One file or folder path, completed as it is typed."""
        if not sys.stdin.isatty():
            return input(label)
        return prompt(label, completer=PathCompleter(expanduser=True), style=_STYLE)

    def secret(self, label: str) -> str:
        """One hidden answer (API keys)."""
        return getpass(label) if not sys.stdin.isatty() else prompt(label, is_password=True)
