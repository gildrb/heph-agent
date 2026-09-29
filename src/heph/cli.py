"""`heph` command line: init, index, ask, config, and the interactive REPL."""

import argparse
import json
import sys
from collections.abc import Sequence
from importlib.metadata import version

from rich.text import Text

from heph import HephError, armory, logins, repl
from heph.answer import Citation, ask
from heph.config import load
from heph.render import SEP, Renderer, console
from heph.session import Chat

_COMMANDS = frozenset({"init", "index", "ask", "config", "repl"})


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


def _run(args: Args, out: Renderer, err: Renderer) -> None:
    match args.command:
        case "init":
            root = armory.init(args.target)
            run = "heph" if args.target is None else f"heph {args.target}"
            out.note(f"Created armory {root}. Every file in it is material.", "")
            out.note(f"Run `{run}` to ask questions; add files any time.", "")
        case "index":
            _ = repl.index_armory(armory.resolve(args.armory), out, quiet=False)
        case "ask":
            root = armory.resolve(args.armory)
            question = " ".join(args.question)
            engine = repl.engine_for(load(), repl.index_armory(root, err, quiet=False))
            if args.json:
                result = ask(engine, [], question, _Quiet())
                Chat.new(root).add(result)
                out.console.file.write(json.dumps(result.json(), ensure_ascii=False, indent=2))
                out.console.file.write("\n")
            else:
                _ = repl.turn(engine, Chat.new(root), question, out)
        case "config":
            config = load()
            state = "" if config.path.exists() else " (not created; defaults in effect)"
            out.note(f"settings: {config.path}{state}", "")
            for key, value in (
                ("max_tokens", config.max_tokens),
                ("temperature", config.temperature),
                ("evidence_tokens", config.evidence_tokens),
            ):
                out.note(f"  {key} = {value}", "")
            saved = logins.load()
            login, model = logins.active(saved)
            out.note(f"logins: {saved.path}", "")
            for item in saved.items or (login,):
                title = logins.PROVIDERS[item.provider].title
                where = item.base_url or title
                using = f"{SEP}(active, {model or 'first listed model'})" if item == login else ""
                out.note(f"  {item.name}{SEP}{where}{using}", "")
        case _:
            repl.run(args.armory, out)


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
