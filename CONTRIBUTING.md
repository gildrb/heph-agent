# Contributing

Thanks for helping improve Heph.

## Setup

You need [uv](https://docs.astral.sh/uv/) and clang 14 or newer. To change the core you
also need [Bend](https://bend-lang.com), at the version pinned in
`.github/actions/setup-bend/action.yml`.

```bash
git clone https://github.com/gildrb/heph-agent
cd heph-agent
uv sync --group dev      # installs Heph editable (compiles src/heph/_bin/heph-core)
uv run heph --version
```

The Bend-emitted C, `core/build/heph-core.c`, is committed, so building Heph needs only
clang. After changing `core/*.bend`, run `core/build.sh`: it proves the laws, emits the C
(the output is deterministic) and compiles the binary. Commit the regenerated C with the
Bend change; CI rebuilds it and fails if it differs.

## Gates

CI runs these; run them before opening a pull request:

```bash
uv run ruff check
uv run ruff format --check
uv run ty check
uv run vulture
bend core/PROOF.bend     # must print "All terms check."
uv lock --check
```

`pre-commit install` runs ruff, ty, vulture and gitleaks on commit.

## Guidelines

- Keep it small: Python in `src/heph/` stays under 45k tokens, the Bend implementation in
  `core/` (proofs excluded) under 25k.
- Strict typing: no `Any`, no casts, no `# type: ignore`.
- No swallowed exceptions and no silent fallbacks; errors say what failed and how to fix it.
- Zero remote code execution: no downloads, no plugins, no model tools, no subprocess other
  than the packaged `heph-core`, no network peer other than `base_url`. See
  [Architecture](docs/architecture.md).
- A change to the core keeps every law in `core/LAWS.bend` proven; change a law only on
  purpose and say why in the pull request.
- Dependencies are pinned exactly; treat `pyproject.toml` and `uv.lock` changes as code.
- Update `docs/` when behavior users see changes.
