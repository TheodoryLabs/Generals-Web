# Security Policy

This repository is the **Generals-Web** fork (WebAssembly/WebGL port) maintained by
[TheodoryLabs](https://github.com/TheodoryLabs). Security reports for this fork should
come to this repository, not to the upstream TheSuperHackers project.

## Reporting a Vulnerability

**For sensitive issues** (anything exploitable, memory corruption reachable from
untrusted game data, cross-origin issues in the web shell, problems in `serve.py`,
supply-chain concerns in the build pipeline):

1. Go to the repository's **Security** tab.
2. Click **"Report a vulnerability"** to open a private GitHub Security Advisory.

This keeps the report private until a fix is available. Please include reproduction
steps, the browser and build you tested, and any relevant console output.

**For non-sensitive matters** (hardening suggestions, dependency bumps, defense-in-depth
ideas), a regular [GitHub issue](../../issues) is fine.

## Scope Notes

- If a vulnerability lives in engine code shared with the upstream
  [TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode)
  project, we will coordinate disclosure with upstream after triage, you do not need
  to report it in both places.
- Never attach game asset files (`.big`, `.bik`, etc.) to a report. Screenshots and
  console logs are enough. See [NOTICE](NOTICE).

## Supported Versions

This is a rolling project: only `main` and the most recent tagged release and the most recent tagged release
receive security fixes.
