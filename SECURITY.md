# Security Policy

## Supported Versions

Currently only the latest version from the `main` branch is supported.

## Reporting a Vulnerability

If you discover a security vulnerability in Heph, please report it privately.

**Do not** open a public issue.

Instead, send an email to: mail@gildrb.com

Please include:
- A description of the vulnerability
- Steps to reproduce the issue
- Any potential impact you've identified
- If possible, a suggested fix

I will acknowledge receipt within 48 hours and provide a timeline for addressing the issue.

## Security model

- **Zero remote code execution**: Heph downloads nothing at install or run time (no
  engines, models, binaries, plugins or updates) and loads nothing from armories.
- **No model tools**: the model gets no shell, file, or web access; it only writes text.
- **One subprocess**: the packaged `heph-core` binary, run by absolute path inside the
  installed package with a fixed argv, no shell, stdin/stdout only.
- **Known network peers**: the active login's server; for a Codex login, `chatgpt.com` and
  `auth.openai.com`. Heph opens no browser. No telemetry or update checks.
- **Untrusted materials**: symlinks and path escapes are refused, sizes are capped, Office
  archives are checked for zip bombs and traversal, and XML is parsed with `defusedxml`.
- **Keys**: a key pasted into `/model` or `/login`, and Codex tokens, are saved in
  `~/.config/heph/keys/` (0600 files, 0700 folder, written atomically, never through a
  symlink). Keys from the environment or a `key_file` are only read.

See [docs/architecture.md](docs/architecture.md) for details.

## Dependency security

Runtime dependencies are five exactly pinned packages (`pypdfium2`, `defusedxml`, `rich`,
`prompt-toolkit`, `certifi`), locked in `uv.lock`. Bend and clang are build-time tools only.
