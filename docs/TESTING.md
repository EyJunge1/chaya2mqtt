# Testing

Host tests use `cargo test` (`chaya-pure` / `chaya-fw`). The Svelte UI uses Vitest + Playwright against the device mock. No CI HIL.

Primary firmware gate is Rust (`make check-firmware` → `cargo test` + release images).

```bash
make check                 # full hardware-free gate
make check-frontend
make check-flasher
make check-firmware        # cargo test (chaya-pure, chaya-fw) + rust-build + size check
cd frontend && npm test
cd frontend && npm run test:e2e
python3 scripts/simulator.py --smoke
```

Playwright once: `cd frontend && npx playwright install chromium`.

PRs run only the jobs matching changed paths. Docs-only PRs skip builds. `main` and release tags run the full gate.

REST/SSE source of truth: [api/openapi.yaml](api/openapi.yaml), [api/asyncapi.yaml](api/asyncapi.yaml). Contract: `frontend/src/api/contract.test.ts`. Pairing / extra own devices: [README.md](README.md#pairing).

Hardware (SKU 34586 + TLS broker, not in CI):

```bash
python3 scripts/simulator.py --hardware-smoke
```

Do not commit broker credentials. Before a commit: `make check`. New logic → cargo test or Vitest case. API change → update the YAML + mock.
