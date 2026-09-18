# Testing

Host tests run firmware logic with fakes. The Svelte UI uses Vitest + Playwright against the device mock. No CI HIL.

```bash
make check                 # full hardware-free gate
make check-frontend
make check-flasher
make check-firmware
cd frontend && npm test
cd frontend && npm run test:e2e
pio test -e native
pio test -e native-asan
python3 scripts/simulator.py --smoke
```

Playwright once: `cd frontend && npx playwright install chromium`. clang-tidy / clang-format **18** are required in CI; locally `make check-firmware-tests` skips them if missing.

PRs run only the jobs matching changed paths. Docs-only PRs skip builds. `main` and release tags run the full gate.

REST/SSE source of truth: [api/openapi.yaml](api/openapi.yaml), [api/asyncapi.yaml](api/asyncapi.yaml). Contract: `frontend/src/api/contract.test.ts`.

Hardware (SKU 34586 + TLS broker, not in CI):

```bash
python3 scripts/simulator.py --hardware-smoke
```

Do not commit broker credentials. Before a commit: `make check`. New logic → native or Vitest case. API change → update the YAML + mock.
