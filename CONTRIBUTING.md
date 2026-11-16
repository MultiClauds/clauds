# Contributing

Thank you for your help. This is a small project. Small, clear changes are the easiest to merge.

## Before you start

- For a bug: open an issue with the bug form.
- For a new feature: open an issue first, so that we can agree on the idea before you write code.
- For a security problem: do not open an issue. Read [SECURITY.md](SECURITY.md).

## Set up

You need Kubuntu 25.04 or later (or another distribution with Qt 6.4+ and KDE Frameworks 6).

```bash
git clone https://github.com/atterdag/clauds.git
cd clauds
make deps    # build dependencies (sudo apt)
make test    # configure, build, run the test suites
make run     # start the app from the build folder
```

The code is in `kde/`: `src/core` (Qt Core only, with the tests) and `src/app` (tray, panel, web
views, KDE integration). See [kde/README.md](kde/README.md#development).

Keep your changes inside `kde/`. The other source folders (`Sources/`, `Package.swift`, `build.sh`)
are the upstream app, kept as they are so that upstream changes can be merged. Send changes for
that app to the [upstream repository](https://github.com/codeFi/clauds).

## Test safely

This app moves real Claude data folders. Be careful when you test.

- Make a backup first:
  `tar -czf ~/claude-backup.tgz -C ~/.config Claude -C ~/.local/share clauds`
- Use the session test hooks on a copy of the data, not on the real folders:
  `clauds --link-sessions <claude data dir> <shared dir> [account tag]` and
  `clauds --unlink-sessions <claude data dir> <shared dir> [account tag]`.
- Run the app on a copy of the data: set `CLAUDS_CLAUDE_DATA_DIR` and `CLAUDS_DATA_DIR` to folders
  of your own. See [kde/README.md](kde/README.md#settings-for-unusual-installs).
- Look at the panel without a display:
  `QT_QPA_PLATFORM=offscreen build-kde/src/app/clauds --render-panel <png>`.

## Rules for the code

- Keep the data safety rules in [docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md). The most important ones:
  move files (do not copy), delete nothing, and stop when the owner record cannot be read.
- Do not add code that reads tokens, cookies, or passwords.
- Do not add network calls to new hosts. The app contacts `claude.ai` only.
- Do not add analytics or telemetry.
- Follow the style of the code near your change. Write short comments that say why.
- Do not hard-code colors. Use the color scheme of the user.
- If you change the panel, make the picture again:
  `QT_QPA_PLATFORM=offscreen build-kde/src/app/clauds --render-panel kde/docs/panel.png`.

## Branches (Gitflow)

This project uses the Gitflow branch model.

| Branch | What it is | Starts from | Merges into |
| --- | --- | --- | --- |
| `main` | The released versions. Each merge is a release, and has a tag `v<version>`. | | |
| `develop` | The next version. This is the default branch. | | |
| `feature/<name>` | One new function or change. | `develop` | `develop` |
| `release/<version>` | The last steps before a release: version number, changelog, release notes. | `develop` | `main` and `develop` |
| `hotfix/<version>` | An urgent fix for the released version. | `main` | `main` and `develop` |

Rules that GitHub applies:

- `main` and `develop` change only through a pull request. The build check must pass.
- No force push to `main` or `develop`. They cannot be deleted.
- `main` gets merge commits only, so that each release stays one clear point in the history.
- A release tag (`v*`) cannot be changed or deleted.

Send your pull request to `develop`. Only release and hotfix branches go to `main`.

## Sign your commits (DCO)

Every commit must have a `Signed-off-by` line with your name and the email address of the commit.
With it, you state that you have the right to send the change under the license of this project
(the [Developer Certificate of Origin](https://developercertificate.org)).

```bash
git commit -s                      # adds the line
git rebase --signoff develop       # adds it to commits that you already made
```

A check on each pull request fails when a commit has no such line.

## Checks on a pull request

| Check | What it does |
| --- | --- |
| KDE port | Builds with plain Qt 6 and with KDE Frameworks 6, runs the tests, makes the `.deb` |
| Build | Builds the upstream app, to see that a merge from upstream did not break it |
| DCO | Every commit has a `Signed-off-by` line |
| CodeQL | Looks for security problems in the C++ code and in the workflows |
| Dependency review | Stops a new dependency that has a known vulnerability |

Dependabot sends pull requests for new versions of the GitHub Actions. OpenSSF Scorecard checks the
supply-chain practices of the repository each week.

## Pull requests

1. Make a branch from `develop`: `git switch -c feature/<name> develop`.
2. Make sure that `make test` passes and that the build gives no new warnings.
3. Add a line to `CHANGELOG.md` below "Unreleased".
4. Fill in the pull request form. Say how you tested the change.

Never put real account names, emails, account ids, or session files in commits, issues, or pictures.

## License

Your contributions use the [MIT License](LICENSE) of this project.
