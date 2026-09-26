# Security Policy

## Supported Versions

Only the latest release is supported.

## Reporting a Vulnerability

If you discover a security vulnerability in Heph, please report it privately.

**Do not** open a public issue.

Instead, send an email to: hi@gildrb.com

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
- **One network peer**: the configured `base_url`. No telemetry or update checks.
- **Untrusted materials**: symlinks and path escapes are refused, sizes are capped, Office
  archives are checked for zip bombs and traversal, and XML is parsed with `defusedxml`.
- **Keys stay in the environment**: API keys are read from environment variables and never
  written to disk.

See [docs/architecture.md](docs/architecture.md) for details.

## Dependency security

Runtime dependencies are four exactly pinned packages (`pypdfium2`, `defusedxml`, `rich`,
`certifi`), locked in `uv.lock`. Bend and clang are build-time tools only. CI and
pre-commit run gitleaks.
