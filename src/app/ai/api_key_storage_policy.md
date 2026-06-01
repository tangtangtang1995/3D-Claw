# API Key Storage Policy

This note documents the current credential behavior in 3D Claw. It is a
developer-facing policy, not a security guarantee.

## DeepSeek AI Chat Key

- The AI Chat API key is stored in memory by `AIChatController`.
- On shutdown, `src/app/bootstrap/app_config.cpp` writes the key to
  `3DClaw_config.json` when a key is configured.
- The key is stored as plaintext. It is not encrypted or protected by a
  platform keychain.
- On startup, `src/app/bootstrap/app_config.cpp` loads the plaintext value from
  `3DClaw_config.json` and injects it back into `AIChatController`.
- This is an explicit desktop-tool trade-off: it avoids asking the user for the
  key on every launch, while keeping the implementation simple and predictable.

## 3D Generation Credentials

- Tencent 3D generation `secret_id` and `secret_key` are entered in the 3D
  Generation dialog.
- They are copied into the request object for the active job.
- They are not written to `3DClaw_config.json` by the current code path.
- Generated archives and extracted models are saved without credentials.

## Developer Rules

- Do not add new credential persistence without updating this file.
- Do not log API keys, secret IDs, secret keys, authorization headers, signed
  payloads, or full request headers.
- Do not ask users to paste raw `3DClaw_config.json` content in public issues;
  any config or log snippet must redact `api_key`, `secret_id`, `secret_key`,
  authorization headers, signatures, and provider tokens.
- If secure storage is introduced later, keep plaintext config migration
  explicit and easy to disable.
- Treat AI Chat and 3D generation credentials separately. They use different
  providers and different lifetimes.

## Future Options

- Add a user-visible "remember this key" checkbox.
- Add a "forget saved AI key" action near the AI Chat key input.
- Use Windows Credential Manager, macOS Keychain, or libsecret on Linux when a
  cross-platform credential backend is worth the added dependency surface.
