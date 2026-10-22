# Security policy

## Reporting a vulnerability

Please report security problems privately. Do not open a public issue.

Use GitHub's **Report a vulnerability** button on the repository's
[Security tab](https://github.com/angeldev07/ninja-gaiden-black-port/security/advisories/new)
(private security advisories). Include what you found, how to reproduce it, and which version or
commit it affects.

You can expect an acknowledgement as soon as the maintainer can get to it. This is a volunteer
project, so there is no guaranteed response time.

## Scope

In scope:

- **The launcher:** downloads, disc image (ISO) import and extraction, zip handling (for example
  path traversal or unsafe extraction), and anything that writes outside its data folder.
- **The loader** (`ngb-loader.exe`) and how it handles files it reads from disk.

Out of scope:

- Game crashes, glitches and rendering or audio bugs. These are not security issues; open a
  normal [issue](https://github.com/angeldev07/ninja-gaiden-black-port/issues).
- Vulnerabilities in the original game itself, which runs as-is inside the port.

## Supported versions

Only the latest release is supported. Fixes land on `main` and ship in the next release.

## Not a security report

Do not send game files, keys or anything derived from the Microsoft XDK, even as proof of
concept. See the Legal rules in [CONTRIBUTING.md](CONTRIBUTING.md).
