# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

Firmware SemVer lives in `src/config/version.hpp` (`kAppVersion`) and is published via GitHub Releases.

## [Unreleased]

### Changed

- Weekly dependencies (#92): `@lucide/svelte` →1.78, `svelte` →5.59.1, `prettier-plugin-svelte` →4.2.0, `typescript-eslint` →8.73, `vitest` / `@vitest/coverage-v8` →5.1.1, plus PlatformIO digest pins for `pioarduino/platform-espressif32`, `espressif/arduino-esp32`, and `framework-arduinoespressif32-libs`.
- Reformat Svelte sources for `prettier-plugin-svelte` 4.2 (element content no longer hugged to `>` / `</`): `frontend/src/components/Layout.svelte`, `flasher/src/App.svelte`, `flasher/src/NoPortPickedDialog.svelte`.
- `@testing-library/jest-dom` 7→8 (#93); requires Vitest 5 (already on main).

### Security

- Transitive `minimatch` DoS advisory in `flasher/package-lock.json` resolved by the weekly lockfile refresh (Dependabot alert #9 fixed).

### Notes

- Firmware app sources were not functionally changed; bump `kAppVersion` and tag when the next firmware release is cut.
