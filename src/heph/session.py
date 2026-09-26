"""Chat persistence: one JSONL file per chat in `.harness/chats/`."""

import json
import os
from dataclasses import dataclass, field
from datetime import UTC, datetime
from pathlib import Path
from typing import Self

from heph import armory
from heph.answer import Result


@dataclass(frozen=True, slots=True)
class Chat:
    path: Path
    turns: list[tuple[str, str]] = field(default_factory=list)

    @classmethod
    def new(cls, root: Path) -> Self:
        stamp = datetime.now(UTC).strftime("%Y%m%d-%H%M%S-%f")
        return cls(root / armory.CHATS / f"{stamp}.jsonl")

    def add(self, result: Result) -> None:
        self.turns.append((result.question, result.answer))
        record = {
            "time": datetime.now(UTC).isoformat(timespec="seconds"),
            "model": result.model,
            "question": result.question,
            **result.json(),
        }
        flags = os.O_WRONLY | os.O_CREAT | os.O_APPEND | os.O_NOFOLLOW
        with os.fdopen(os.open(self.path, flags, 0o600), "a", encoding="utf-8") as file:
            file.write(json.dumps(record, ensure_ascii=False) + "\n")
