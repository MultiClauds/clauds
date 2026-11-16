# Changelog

All notable changes to this project are written in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
This project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

The KDE Plasma port, in [`kde/`](kde/). Its version is 0.1.0 and it has no release yet.

### Added

- KDE Plasma port of Clauds, written in C++ with Qt 6 and KDE Frameworks 6: a StatusNotifierItem
  tray icon, a Qt Quick panel with one card for each Claude account, and the colors of the user's
  color scheme.
- The same features as the macOS app: switch the Claude desktop app between accounts, Side by Side,
  shared Code sessions with a session owner record, auto-switch near a limit, live plan usage,
  chat windows for each account, and Start at Login (an XDG autostart entry).
- Support for the Claude desktop app for Linux: the `claude-desktop` package and the unofficial
  build, with data in `~/.config/Claude`. The process tree of the official package is covered by a
  test.
- Removed accounts go to the freedesktop Trash.
- Messages on an account card that say why there are no live numbers: not signed in, not allowed to
  read usage and billing information, no 5-hour or weekly limits reported, or no connection.
- A `.deb` package (`make deb`) and an experimental Flatpak (`make flatpak`).
- A top-level `Makefile` with the targets `deps`, `build`, `test`, `run`, `deb`, `install`,
  `uninstall`, `flatpak` and `clean`.
- Quality gates on GitHub: a DCO check (every commit needs a `Signed-off-by` line), CodeQL, dependency
  review, OpenSSF Scorecard, and weekly Dependabot updates for the GitHub Actions. Actions are
  pinned by commit.

### Changed

- The README, `CONTRIBUTING.md`, `SECURITY.md`, `SUPPORT.md` and the issue and pull request forms
  describe the KDE port. The macOS app is kept as it is upstream.
- A 403 answer for the usage numbers that comes as JSON is shown as "not allowed to read usage",
  not as "sign in".

### Fixed

- Clauds crashed when it quit. The tray menu was deleted before the tray item that uses it.

### Not in the KDE port

- Self-update (Sparkle). Install a new package instead.
- A release download. Build the `.deb` or the Flatpak from the source.

## macOS app (upstream)

The entries below are the history of the macOS app, as released by its author. They are kept so that
changes from upstream can be merged.

## [1.0.0] - 2026-11-16

First release with the name Clauds.

### Added

- Menu bar panel with one card for each Claude account.
- Switch the Claude desktop app between accounts. Each account stays signed in.
- Side by Side mode: one Claude copy for each account, all open at the same time.
- Shared Code sessions between accounts, with a record of the owner of each session.
- Share Code sessions in Side by Side. Off by default, with a warning, because it is risky.
- Live plan usage for each account: 5-hour, weekly, and weekly per-model limits, with reset times.
- Auto-switch near a limit (… menu): off, ask first, or automatic. Off by default.
- Activity indicator while usage numbers load, and a notice when they cannot load.
- Sign-in window for live usage that closes by itself.
- A claude.ai chat window for each account.
- Self-update from GitHub releases, with Sparkle. The app shows the release notes and lets the user
  install, skip, or wait. Each update must have a valid EdDSA signature.
- Start at Login.
- About window.
- Release download as a DMG file (universal, ad-hoc signed, not notarized).
- Data from the first app name ("Claude Switcher") moves to the new folder at the first start
  while Claude is closed.

[Unreleased]: https://github.com/atterdag/clauds/commits/develop
[1.0.0]: https://github.com/codeFi/clauds/releases/tag/v1.0.0
