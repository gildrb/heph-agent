"""`heph` command line: init, index, ask, config, and the interactive REPL."""

import argparse
import json
import readline
import shlex
import sys
from collections.abc import Sequence
from dataclasses import dataclass
from importlib.metadata import version
from pathlib import Path

from rich.text import Text

from heph import HephError, armory
from heph.answer import Citation, Engine, Result, ask
from heph.config import Config, load
from heph.index import Index, build
from heph.llm import Client
from heph.render import Renderer, console
from heph.session import Chat

_PROMPT = "\N{SINGLE RIGHT-POINTING ANGLE QUOTATION MARK} "
_COMMANDS = frozenset({"init", "index", "ask", "config", "repl"})
_HELP = """/armory [name]  list armories, or open one by number, name or path
/init [name]    make this folder an armory, or create one in the armory home
/add <path>...  copy files or folders into the armory, then index
/new            start a new chat (forget the conversation so far)
/sources        list the evidence given to the model for the last answer
/index          re-index the armory
/help           show this help
/exit           quit (Ctrl-D works too)"""


class Args(argparse.Namespace):
    command: str | None
    version: bool
    target: str | None
    armory: str | None
    question: list[str]
    json: bool


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="heph",
        usage="heph [armory] | heph {init,index,ask,config} ...",
        description="Cited answers from the files in an armory.",
    )
    parser.add_argument("--version", action="store_true", help="print the version and exit")
    commands = parser.add_subparsers(dest="command", metavar="{init,index,ask,config}")
    init = commands.add_parser("init", help="make this folder (or a name or path) an armory")
    init.add_argument("target", nargs="?", metavar="name|path")
    index = commands.add_parser("index", help="index an armory's materials")
    index.add_argument("armory", nargs="?")
    question = commands.add_parser("ask", help="answer one question with citations")
    question.add_argument("armory")
    question.add_argument("question", nargs="+")
    question.add_argument("--json", action="store_true", help="print a JSON result")
    commands.add_parser("config", help="show the settings in effect")
    repl = commands.add_parser("repl")  # no help=: keeps it out of the command list
    repl.add_argument("armory", nargs="?")
    return parser


class _Quiet:
    """Event sink for --json: the result is printed once, at the end."""

    def reasoning(self, text: str) -> None:
        del text

    def block(self, text: str, offset: int, citations: Sequence[Citation]) -> None:
        del text, offset, citations


def _index(root: Path, renderer: Renderer) -> Index:
    with renderer.status("Indexing"):
        index, report = build(
            root, lambda n, total, source: renderer.update(f"Indexing {n}/{total} {source}")
        )
    renderer.report(report)
    return index


def _engine(config: Config, index: Index) -> Engine:
    client = Client(config.base_url, config.api_key, config.path)
    model = config.model
    if not model:
        models = client.models()
        if not models:
            raise HephError(
                f"{config.base_url}/models lists no models; set model in {config.path}"
            )
        model = models[0]
    return Engine(client, model, config, index)


def _turn(engine: Engine, chat: Chat, question: str, renderer: Renderer) -> Result:
    with renderer.status(f"Waiting for {engine.model}"):
        result = ask(engine, chat.turns, question, renderer)
    renderer.finish(result)
    chat.add(result)
    return result


@dataclass(slots=True)
class _Open:
    """The armory a session works in."""

    root: Path
    index: Index
    chat: Chat
    engine: Engine | None = None
    last: Result | None = None


def _history(root: Path) -> Path:
    path = root / armory.INTERNAL / "history"
    if path.is_symlink():
        raise HephError(f"Refusing symlinked history file {path}")
    return path


class _Repl:
    def __init__(self, out: Renderer) -> None:
        self.out = out
        self.config = load()
        self.open: _Open | None = None

    def current(self) -> _Open:
        if self.open is None:
            raise HephError("No armory open: /armory lists them, /init creates one")
        return self.open

    def enter(self, root: Path) -> None:
        """Opens an armory: indexes it and swaps in its input history."""
        index = _index(root, self.out)
        history = _history(root)
        self.save_history()
        readline.clear_history()
        if history.exists():
            readline.read_history_file(history)
        self.open = _Open(root, index, Chat.new(root))
        self.out.note(
            f"heph · {root.name} · {len(index.chunks)} chunks · {self.config.base_url} · /help"
        )

    def save_history(self) -> None:
        if self.open is not None:
            readline.write_history_file(_history(self.open.root))

    def armories(self) -> list[Path]:
        roots = armory.known()
        if not roots:
            self.out.note(f"No armories in {armory.armory_home()} yet.")
        for number, root in enumerate(roots, 1):
            is_open = self.open is not None and self.open.root == root.resolve()
            self.out.note(f"  {number}  {root.name}{'  (open)' if is_open else ''}", "")
        return roots

    def welcome(self) -> None:
        self.out.note(f"heph · {Path.cwd()} is not an armory · /help")
        pick = "Type a number or name to open one, " if self.armories() else ""
        self.out.note(f"{pick}/init to make this folder an armory, /init <name> to create one.")

    def pick(self, choice: str) -> None:
        if choice.isdigit():
            roots = armory.known()
            number = int(choice)
            if not 1 <= number <= len(roots):
                raise HephError(f"No armory number {number}; /armory lists them")
            self.enter(armory.validate(roots[number - 1]))
        else:
            self.enter(armory.resolve(choice))

    def create(self, target: str | None) -> None:
        if target is None and (cwd := armory.here()) is not None:
            self.out.note(f"{cwd} is already an armory.")
            self.enter(cwd)
            return
        root = armory.init(target)
        self.out.note(f"Created armory {root}.")
        self.enter(root)
        if not self.current().index.chunks:
            self.out.note(f"Add files with /add <path>, or copy them into {root}.")

    def reindex(self, current: _Open) -> None:
        current.index, current.engine = _index(current.root, self.out), None

    def add(self, arg: str) -> None:
        current = self.current()
        try:
            paths = shlex.split(arg)
        except ValueError as exc:
            raise HephError(f"Cannot parse {arg!r}: {exc}") from exc
        if not paths:
            raise HephError("Usage: /add <file or folder> ... (quote paths with spaces)")
        for count, raw in enumerate(paths):
            try:
                name = armory.add(current.root, Path(raw).expanduser())
            except HephError:
                if count:
                    self.reindex(current)
                raise
            self.out.note(f"Added {name}")
        self.reindex(current)

    def command(self, line: str) -> bool:
        """Handles a slash command; returns False to quit."""
        name, _, rest = line.partition(" ")
        arg = rest.strip()
        match name:
            case "/exit" | "/quit":
                return False
            case "/help":
                self.out.note(_HELP, style="")
            case "/armory" if arg:
                self.pick(arg)
            case "/armory":
                _ = self.armories()
            case "/init":
                self.create(arg or None)
            case "/add":
                self.add(arg)
            case "/new":
                current = self.current()
                current.chat, current.last = Chat.new(current.root), None
                self.out.note("New chat.")
            case "/sources":
                last = self.current().last
                self.out.sources(last.evidence if last else ())
            case "/index":
                self.reindex(self.current())
            case command:
                self.out.note(f"Unknown command {command}; /help lists commands.", "yellow")
        return True

    def question(self, line: str) -> None:
        current = self.current()
        current.engine = current.engine or _engine(self.config, current.index)
        current.last = _turn(current.engine, current.chat, line, self.out)

    def loop(self) -> None:
        while True:
            try:
                line = input(_PROMPT).strip()
            except KeyboardInterrupt:
                self.out.console.print()
                continue
            except EOFError:
                self.out.console.print()
                return
            try:
                if line.startswith("/"):
                    if not self.command(line):
                        return
                elif line and self.open is None:
                    self.pick(line)
                elif line:
                    self.question(line)
            except HephError as exc:
                self.out.note(f"error: {exc}", "red")
            except KeyboardInterrupt:
                self.out.note("interrupted", "yellow")


def _repl(arg: str | None, out: Renderer) -> None:
    readline.set_history_length(1000)
    repl = _Repl(out)
    if arg is not None:
        repl.enter(armory.resolve(arg))
    elif (cwd := armory.here()) is not None:
        repl.enter(cwd)
    else:
        repl.welcome()
    try:
        repl.loop()
    finally:
        repl.save_history()


def _run(args: Args, out: Renderer, err: Renderer) -> None:
    match args.command:
        case "init":
            root = armory.init(args.target)
            run = "heph" if args.target is None else f"heph {args.target}"
            out.note(f"Created armory {root}. Every file in it is material.", "")
            out.note(f"Run `{run}` to ask questions; add files any time.", "")
        case "index":
            _index(armory.resolve(args.armory), out)
        case "ask":
            root = armory.resolve(args.armory)
            question = " ".join(args.question)
            engine = _engine(load(), _index(root, err))
            if args.json:
                result = ask(engine, [], question, _Quiet())
                Chat.new(root).add(result)
                out.console.file.write(json.dumps(result.json(), ensure_ascii=False, indent=2))
                out.console.file.write("\n")
            else:
                _turn(engine, Chat.new(root), question, out)
        case "config":
            config = load()
            state = "" if config.path.exists() else " (not created; defaults in effect)"
            out.note(f"config file: {config.path}{state}", "")
            for key, value in (
                ("base_url", config.base_url),
                ("model", config.model or "(first model from GET /models)"),
                ("api key", f"from {config.api_key_source}" if config.api_key else "not set"),
                ("max_tokens", config.max_tokens),
                ("temperature", config.temperature),
                ("evidence_tokens", config.evidence_tokens),
            ):
                out.note(f"{key} = {value}", "")
        case _:
            _repl(args.armory, out)


def main(argv: Sequence[str] | None = None) -> int:
    words = list(sys.argv[1:] if argv is None else argv)
    if not words or (words[0] not in _COMMANDS and not words[0].startswith("-")):
        words.insert(0, "repl")
    args = _parser().parse_args(words, namespace=Args())
    if args.version:
        print(f"heph {version('heph')}")
        return 0
    if args.command is None:
        args.armory = None
    err = Renderer(console(stderr=True))
    try:
        _run(args, Renderer(console()), err)
    except HephError as exc:
        err.console.print(Text(f"heph: {exc}", style="red"))
        return 1
    except KeyboardInterrupt:
        return 130
    return 0
