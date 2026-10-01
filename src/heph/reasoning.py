"""Reasoning levels: which ones a model takes, and how a request asks for one.

OpenRouter advertises each model's levels in its model list. For OpenAI, the ChatGPT
subscription (Codex), DeepSeek and Z.AI they come from the providers' documentation, as the
rules below. A local server cannot say what its model takes, so there only model families
whose chat templates are known get levels: Qwen and gpt-oss.
"""

import re
from collections.abc import Mapping
from dataclasses import dataclass
from typing import Literal

# From least to most thinking. "on" stands for models that can only switch thinking on or off.
ORDER: tuple[str, ...] = ("off", "minimal", "low", "medium", "high", "xhigh", "max", "ultra")
ON = "on"

type Wire = Literal["effort", "openai", "template", "openrouter", "zai", "deepseek", "codex"]


@dataclass(frozen=True, slots=True)
class Dial:
    """One model's reasoning levels, lowest first, the one it uses unasked, and the wire form."""

    levels: tuple[str, ...]
    default: str
    wire: Wire

    def clamp(self, wanted: str | None) -> str:
        """The level to use for a wanted one: itself if the model has it; else, for an effort,
        the nearest effort below it or the lowest one; on an on/off model any effort is on.
        None means the model's default."""
        if wanted is None or wanted in self.levels:
            return self.default if wanted is None else wanted
        if ON in self.levels:
            return "off" if wanted == "off" else ON
        if wanted == ON:
            return self.default
        efforts = [level for level in self.levels if level != "off"]
        rank = ORDER.index(wanted) if wanted in ORDER else len(ORDER)
        below = [level for level in efforts if ORDER.index(level) <= rank]
        return below[-1] if below else efforts[0]

    def after(self, level: str) -> str:
        """The next level, wrapping from the highest back to the lowest (shift+tab)."""
        position = self.levels.index(level) if level in self.levels else -1
        return self.levels[(position + 1) % len(self.levels)]


@dataclass(frozen=True, slots=True)
class Setting:
    """The level chosen on a model's dial, for one request."""

    dial: Dial
    level: str


def _dial(levels: str, default: str, wire: Wire) -> Dial:
    return Dial(tuple(levels.split()), default, wire)


_TO_ULTRA = "low medium high xhigh max ultra"
# provider: (model id pattern, levels or None for none): first match wins. Sources:
# developers.openai.com model pages, openai/codex models.json (b1e72963), api-docs.deepseek.com,
# docs.z.ai, all read 2026-10-01.
_RULES: dict[str, tuple[tuple[str, Dial | None], ...]] = {
    "openai": (
        (r"gpt-[\d.]+-chat", None),
        (r"o\d", _dial("low medium high", "medium", "openai")),
        (
            r"gpt-5(-mini|-nano)?(-\d{4}-\d\d-\d\d)?$",
            _dial("minimal low medium high", "medium", "openai"),
        ),
        (r"gpt-5\.1(?!\d)", _dial("off low medium high", "off", "openai")),
        (r"gpt-5\.[24](?!\d)", _dial("off low medium high xhigh", "off", "openai")),
        (r"gpt-5\.5", _dial("off low medium high xhigh", "medium", "openai")),
        (r"gpt-5\.6", _dial("off low medium high xhigh max", "medium", "openai")),
        (r"gpt-6-astra|gpt-6\.1", _dial("low medium high xhigh max", "medium", "openai")),
        (r"gpt-6", _dial("off low medium high xhigh max", "medium", "openai")),
        (r"gpt-([5-9]|\d\d)", _dial("low medium high", "medium", "openai")),
    ),
    "codex": (
        (r"gpt-6-astra|gpt-6\.1-sol|gpt-5\.6-sol", _dial(_TO_ULTRA, "low", "codex")),
        (r"gpt-6-sol|gpt-5\.6-terra", _dial(_TO_ULTRA, "medium", "codex")),
        (r"gpt-6-luna|gpt-5\.6-luna", _dial("low medium high xhigh max", "medium", "codex")),
        (r"gpt-5\.5", _dial("low medium high xhigh", "medium", "codex")),
        (r"gpt-", _dial("low medium high", "medium", "codex")),
    ),
    "deepseek": ((r"deepseek-(flash|v4)", _dial("off low high max", "high", "deepseek")),),
    "zai": (
        (r"glm-5\.3", _dial("low high max", "max", "zai")),
        (r"glm-5\.2", _dial("off high max", "max", "zai")),
        (r"glm-(4\.5|4\.6|5|5\.1)(?![\dv.])", _dial("off on", "on", "zai")),
    ),
    "local": (
        (r".*qwen-?3\.([89]|\d\d)", _dial("off low medium xhigh", "xhigh", "template")),
        (r"(?!.*(instruct|thinking)).*qwen-?3", _dial("off on", "on", "template")),
        (r".*gpt-oss", _dial("low medium high", "medium", "effort")),
    ),
}


def dial(provider: str, model: str) -> Dial | None:
    """A model's levels from the built-in rules; None when it has no levels to choose."""
    name = model.rsplit("/", 1)[-1].casefold()
    for pattern, found in _RULES.get(provider, ()):
        if re.match(pattern, name):
            return found
    return None


def advertised(entry: Mapping[str, object]) -> Dial | None:
    """The levels OpenRouter lists for a model: `reasoning.supported_efforts` (highest first,
    "none" meaning it can be turned off), `default_effort`, and `mandatory` when thinking
    cannot be turned off."""
    match entry.get("reasoning"):
        case {"mandatory": bool(mandatory), **rest}:
            pass
        case _:
            return None
    match rest.get("supported_efforts"):
        case [*efforts]:
            known = [e for e in ORDER if e in efforts and e != "off"]
        case _:
            known = []
    if not known:
        return None if mandatory else Dial(("off", ON), ON, "openrouter")
    levels = tuple(known if mandatory else ["off", *known])
    default = rest.get("default_effort")
    default = "off" if default == "none" else default
    chosen = default if isinstance(default, str) and default in levels else levels[-1]
    return Dial(levels, chosen, "openrouter")


def fields(found: Dial, level: str) -> dict[str, object]:
    """The request fields that ask a chat-completions server for a level."""
    on = level != "off"
    effort = {} if level in {"off", ON} else {"reasoning_effort": level}
    match found.wire:
        case "effort" | "openai":
            return {"reasoning_effort": level if on else "none"}
        case "template":
            return {"chat_template_kwargs": {"enable_thinking": on, **effort}}
        case "openrouter":
            return {"reasoning": {"effort": level} if effort else {"enabled": on}}
        case "zai" | "deepseek":
            return {"thinking": {"type": "enabled" if on else "disabled"}, **effort}
        case "codex":
            return {"reasoning": {"effort": level, "summary": "auto"}}
