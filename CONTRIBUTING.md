# Contributing to Chaya2MQTT

Thank you for your interest in Chaya2MQTT.

## Before making a change

- Use the **Bug report** issue template for reproducible bugs.
- Use the **Feature or change request** template for improvements and proposed changes.
- Discuss major architecture, protocol, or hardware changes in the feature form before implementing them.
- Do not include passwords, tokens, private MQTT topics, or other credentials in issues.

## Development environment

PlatformIO, Node.js 22, and the tools described in [docs/TESTING.md](docs/TESTING.md) are required.

```bash
cd frontend
npm ci
cd ..
make check
```

The browser web flasher under `flasher/` is a separate Svelte/Vite/Tailwind app.
Run its checks and generate a release-aware preview as described in
[flasher/README.md](flasher/README.md).

## Pull requests

- Keep changes small and easy to understand.
- Cover new or changed logic with appropriate tests.
- Update implementation, mock, and documentation together when changing REST, SSE, or MQTT contracts.
- Do not commit credentials, `.env` files, or generated build artifacts.
- Pin every GitHub Action `uses:` entry to a verified 40-character release commit SHA and keep
  the semantic release version as an inline comment; Dependabot updates these pins.
- Run `make check` successfully in full before pushing.
- The required status check is `check`. A green result stays valid if `main` moves
  and the pull request has no conflicts; updating the branch is optional and
  re-runs CI.

The complete quality gates and manual hardware checks are documented in [docs/TESTING.md](docs/TESTING.md). Target hardware is only the Waveshare ESP32-S3-ePaper-1.54G (SKU 34586); see [docs/HARDWARE.md](docs/HARDWARE.md).

## Style

- Follow the existing C++, TypeScript, and documentation conventions.
- Format frontend code with the existing Prettier configuration.
- Document public interfaces and security-relevant design decisions.

By contributing, you confirm that you may publish your contribution under the repository's
[GNU General Public License v3.0 only](LICENSE).
