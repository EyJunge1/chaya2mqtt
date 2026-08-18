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

## Pull requests

- Keep changes small and easy to understand.
- Cover new or changed logic with appropriate tests.
- Update implementation, mock, and documentation together when changing REST, SSE, or MQTT contracts.
- Do not commit credentials, `.env` files, or generated build artifacts.
- Run `make check` successfully in full before pushing.

The complete quality gates and manual hardware checks are documented in [docs/TESTING.md](docs/TESTING.md). Target hardware is only the Waveshare ESP32-S3-ePaper-1.54G (SKU 34586); see [docs/HARDWARE.md](docs/HARDWARE.md).

## Style

- Follow the existing C++, TypeScript, and documentation conventions.
- Format frontend code with the existing Prettier configuration.
- Document public interfaces and security-relevant design decisions.

By contributing, you confirm that you may publish your contribution under the repository's [CC BY-NC-SA 4.0](LICENSE) license.
