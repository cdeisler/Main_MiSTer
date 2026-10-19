---
description: "Use when connecting over SSH to the MiSTer Pi at 192.168.68.185, authenticating, reconnecting, or performing remote MiSTer diagnostics."
---

# MiSTer Pi SSH Access

- Use the workspace-local `.copilot/ssh_credentials_192.168.68.185.json` to authenticate SSH connections to this machine.
- Parse the JSON locally with a structured parser and pass the credentials directly to an SSH library or a supported secure credential mechanism. Do not assume field names; inspect only key names if needed, without displaying values.
- Never read the credentials file into chat context, print its contents or secret values, or include secrets in command-line arguments, terminal history, logs, screenshots, or generated files.
- Prefer an existing authenticated session. If none exists, establish a new session using the local credentials file rather than asking the user to repeat credentials.
- If available SSH tooling cannot securely consume the credentials, explain the limitation and have the user enter the secret directly into a visible terminal. Never request secrets through chat or question tools.
- Preserve SSH host-key verification. Do not bypass a changed-host-key warning.
- Keep the credentials file untracked and never commit it.
- Start remote diagnostics read-only. Using saved credentials does not authorize reboots, configuration changes, or other actions beyond the user's request.