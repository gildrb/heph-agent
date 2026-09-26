# Privacy

Heph runs on your machine and keeps everything local by default.

- **Files stay put.** Materials, the index and chats live in the armory folder. Settings
  live in `~/.config/heph/config.toml`.
- **One network peer.** Heph connects only to the configured `base_url`: `GET /models`
  and one `POST /chat/completions` per question. The default is a server on your own
  machine (`http://127.0.0.1:8080/v1`), so nothing leaves it.
- **What the model sees.** Each request carries the system prompt, the chat's earlier
  questions and answers, the evidence passages retrieved for the question, and the
  question. If `base_url` points at a hosted provider, that provider receives this text
  under its own terms.
- **No telemetry.** No analytics, crash reports, update checks or install identifiers.
- **Keys.** Heph needs no key for local servers. It reads a key from the environment
  (`HEPH_API_KEY` or the variable named by `api_key_env`) or from the file named by
  `api_key_file`, and never writes it anywhere.

Delete an armory's `.harness/chats/` to remove its chat history, or `.harness/` to remove
all Heph state for that armory.
