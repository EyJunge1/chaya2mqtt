# Security Policy

## Supported versions

Security fixes are provided for the latest published firmware release and the current `main` branch.

| Version | Supported |
|---------|-----------|
| Latest release | Yes |
| `main` | Yes |
| Older releases | No |

## Reporting a vulnerability

Please do not open a public issue for a suspected vulnerability.

Use GitHub's **Report a vulnerability** function in this repository's Security tab. Include the affected version or commit, reproduction steps, expected impact and any prerequisites such as LAN or physical access.

You should receive an acknowledgement within seven days. Confirmed issues will be coordinated privately until a fix or mitigation is available.

The device intentionally uses an open setup access point, HTTP administration without a login, plaintext credentials in ESP32 NVS and unsigned OTA firmware. These limitations and their threat model are documented in [`docs/SECURITY.md`](../docs/SECURITY.md). Reports that demonstrate additional impact or a bypass of the documented boundaries are welcome.
