"""`heph` command line: init, index, ask, config, and the interactive REPL."""

import argparse
import json
import readline
import sys
from collections.abc import Sequence
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
_HELP = """/help     show this help
/new      start a new chat (forget the conversation so far)
/sources  list the evidence given to the model for the last answer
/index    re-index the armory's materials
/exit     quit (Ctrl-D works too)"""


class Args(argparse.Namespace):
    command: str | None
    version: bool
    target: str
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
    init = commands.add_parser("init", help="create an armory (name or path)")
    init.add_argument("target", metavar="name|path")
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


class _Repl:
    def __init__(self, root: Path, out: Renderer) -> None:
        self.root = root
        self.out = out
        self.config = load()
        self.index = _index(root, out)
        self.engine: Engine | None = None
        self.chat = Chat.new(root)
        self.last: Result | None = None

    def command(self, line: str) -> bool:
        """Handles a slash command; returns False to quit."""
        match line.split(" ", 1)[0]:
            case "/exit" | "/quit":
                return False
            case "/help":
                self.out.note(_HELP, style="")
            case "/new":
                self.chat, self.last = Chat.new(self.root), None
                self.out.note("New chat.")
            case "/sources":
                self.out.sources(self.last.evidence if self.last else ())
            case "/index":
                self.index, self.engine = _index(self.root, self.out), None
            case command:
                self.out.note(f"Unknown command {command}; /help lists commands.", "yellow")
        return True

    def question(self, line: str) -> None:
        try:
            self.engine = self.engine or _engine(self.config, self.index)
            self.last = _turn(self.engine, self.chat, line, self.out)
        except HephError as exc:
            self.out.note(f"error: {exc}", "red")
        except KeyboardInterrupt:
            self.out.note("interrupted", "yellow")

    def loop(self) -> None:
        chunks = len(self.index.chunks)
        self.out.note(
            f"heph · {self.root.name} · {chunks} chunks · {self.config.base_url} · /help"
        )
        while True:
            try:
                line = input(_PROMPT).strip()
            except KeyboardInterrupt:
                self.out.console.print()
                continue
            except EOFError:
                self.out.console.print()
                return
            if line.startswith("/"):
                if not self.command(line):
                    return
            elif line:
                self.question(line)


def _repl(root: Path, out: Renderer) -> None:
    history = root / armory.INTERNAL / "history"
    if history.is_symlink():
        raise HephError(f"Refusing symlinked history file {history}")
    if history.exists():
        readline.read_history_file(history)
    readline.set_history_length(1000)
    repl = _Repl(root, out)
    try:
        repl.loop()
    finally:
        readline.write_history_file(history)


def _run(args: Args, out: Renderer, err: Renderer) -> None:
    match args.command:
        case "init":
            root = armory.init(args.target)
            out.note(f"Created {root}. Put files in {root / armory.MATERIALS}, then run:", "")
            out.note(f"  heph index {args.target}", "")
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
                ("api_key_env", config.api_key_env or "(none)"),
                ("api key", "set" if config.api_key else "not set"),
                ("max_tokens", config.max_tokens),
                ("temperature", config.temperature),
                ("evidence_tokens", config.evidence_tokens),
            ):
                out.note(f"{key} = {value}", "")
        case _:
            _repl(armory.resolve(args.armory), out)


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
