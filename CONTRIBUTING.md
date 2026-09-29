# Contributing

## Setup

```sh
uv sync --group dev      # needs clang 14+; compiles the core from core/build/heph-core.c
uv run heph --help
```

## Checks

```sh
uv run ruff check
uv run ruff format --check
uv run ty check
uv run vulture
uv lock --check
```

After changing `core/*.bend`, run `core/build.sh`: it runs `bend PROOF.bend` (must print
`ALL PROOFS CHECK`), regenerates `core/build/heph-core.c` and compiles the binary. Commit
the regenerated C with the change; CI fails when it is stale. The proofs are checked with
one Bend version, pinned in `mise.toml` (mise selects it inside the repo) and in
`.github/actions/setup-bend/action.yml` for CI; bump both together.

Keep armories portable. Keep answers grounded in local files with checked citations. Keep
the model without tools and Heph without downloads. Change a law in `core/LAWS.bend` only
on purpose, and say why. Prefer deleting layers over adding wrappers; keep `src/heph` under
45k tokens and the core implementation (proofs excluded) under 25k.
