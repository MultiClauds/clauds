# Security Policy

Clauds moves the data folders of the Claude desktop app, which hold your logins and
your Code sessions. Security reports are important and welcome.

## Supported versions

Only the newest release gets security fixes.

## How to report a vulnerability

**Do not open a public issue for a security problem.**

Use GitHub private vulnerability reporting:

1. Go to <https://github.com/atterdag/clauds/security/advisories/new>.
2. Describe the problem, the steps to reproduce it, and the effect.

You can expect a first answer in 7 days. When the fix is released, the report is published with
credit to you, unless you do not want that.

## Do not send private data

Do not attach these files to a report or an issue. They can identify you or your accounts:

- `~/Library/Application Support/Clauds/accounts.json`
- `~/Library/Application Support/Clauds/SharedSessions/owners.json`
- `~/Library/Application Support/Clauds/usage-debug.json`
- Anything from `~/Library/Application Support/Claude` or `~/Library/Application Support/Clauds/Profiles`

## What the app does and does not do

- All data stays on your Mac. The app has no server, no analytics, and no telemetry.
- The app contacts `claude.ai`, through the system web view, for the chat windows and the live
  usage numbers. It contacts GitHub (`raw.githubusercontent.com`, `github.com`) only to look for
  updates and to download them.
- The app installs an update only if the file has a valid EdDSA signature for the public key that
  is built into the app. The secret key is not in this repository. See `docs/RELEASING.md`.
- The app does not read, copy, or store passwords, tokens, or cookies. It moves whole Claude data
  folders. The web views keep their own logins, as a browser does.
- The app does not read the encrypted login store of the Claude app.
- All files and folders that the app makes are private to your macOS user (`umask 077`, folders `700`).

## About the release download

The DMG file in each release has an ad-hoc signature and is **not notarized by Apple**. macOS shows
a warning at the first start. The README explains this and how to permit the app. Each release
lists the SHA-256 checksum of the file. If you do not want to trust the download, build the app
from the source code.

## Scope

In scope: anything in this repository that can leak, damage, or mix up account data or session
files, or that lets a different local user or process get access to them.

Out of scope: problems in the Claude desktop app or in claude.ai. Report those to Anthropic.
