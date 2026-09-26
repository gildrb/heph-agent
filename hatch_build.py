"""Hatch build hook: compile the Bend-emitted C core into ``heph/_bin/heph-core``.

``core/build.sh`` (which needs ``bend``) writes ``core/build/heph-core.c``; the sdist
ships that C, so building a wheel needs only a C compiler and no network access.
Bend's C needs clang >= 14 (it uses statement-level ``musttail``); ``CC`` overrides.
"""

import os
import subprocess
import sysconfig
from pathlib import Path
from typing import override

from hatchling.builders.hooks.plugin.interface import BuildHookInterface

C_SOURCE = Path("core/build/heph-core.c")
BINARY = Path("src/heph/_bin/heph-core")


class CoreBuildHook(BuildHookInterface):
    PLUGIN_NAME = "custom"

    @override
    def initialize(self, version: str, build_data: dict[str, object]) -> None:
        root = Path(self.root)
        source = root / C_SOURCE
        if not source.is_file():
            raise FileNotFoundError(f"{source} is missing: run core/build.sh to emit it")
        if self.target_name != "wheel":
            return
        binary = root / BINARY
        binary.parent.mkdir(exist_ok=True)
        compiler = os.environ.get("CC", "clang")
        command = [compiler, "-O2", str(source), "-o", str(binary), "-lm", "-lpthread"]
        subprocess.run(command, check=True)
        platform = sysconfig.get_platform().replace("-", "_").replace(".", "_")
        build_data["pure_python"] = False
        build_data["tag"] = f"py3-none-{platform}"
