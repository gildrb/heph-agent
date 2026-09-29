# Privacy

Heph runs on your machine and keeps everything local by default.

- **Files stay put.** Materials, the index and chats live in the armory folder. Settings
  and logins live in `~/.config/heph/`.
- **Network peers.** Heph talks only to the active login: `GET /models` and one chat call
  per question. With no login that is a server on your own machine
  (`http://127.0.0.1:8080/v1`), so nothing leaves it. A Codex login also talks to
  `auth.openai.com` to sign in and refresh.
- **What the model sees.** Each request carries the system prompt, the chat's earlier
  questions and answers, the evidence passages retrieved for the question, and the
  question. A hosted login (OpenAI, OpenRouter, DeepSeek, Z.AI, Codex) receives this text
  under that provider's terms.
- **No telemetry.** No analytics, crash reports, update checks or install identifiers.
- **Keys.** Local servers usually need none. A key you paste into `/model` or `/login`, and
  Codex tokens, are saved in `~/.config/heph/keys/` (files 0600, folder 0700). Keys can
  also come from environment variables or a `key_file` you point at; Heph never copies
  them.

Delete an armory's `.harness/chats/` to remove its chat history, or `.harness/` to remove
all Heph state for that armory.
