# How Clauds works

This page is for people who want to know what the app does to the files on the computer, and for
contributors. It describes the KDE Plasma port. Paths follow the XDG folders of a normal Linux user.

## Folders

| Path | What is in it |
| --- | --- |
| `~/.config/Claude` | The data folder of the Claude desktop app. It holds the login of the main account. |
| `~/.local/share/clauds/accounts.json` | The account list, the active account, and the two switches. |
| `~/.local/share/clauds/Profiles/<id>` | The Claude data folder of each account that is not the main one. |
| `~/.local/share/clauds/SharedSessions/orgs` | The pool of shared Code session files. |
| `~/.local/share/clauds/SharedSessions/owners.json` | Which account made each Code session. `owners.json.bak` is the last good copy. |
| `~/.local/share/clauds/SharedSessions/_conflicts` | Older versions of files that lost a "newer wins" decision. Nothing is deleted. |
| `~/.local/share/clauds/QtWebEngine/account-<id>` | The claude.ai login of each account, for live usage and chat windows. |
| `~/.config/autostart/io.github.atterdag.clauds.desktop` | The Start at Login entry, when it is on. |
| `~/.claude/projects` | The conversation text of Code sessions. Claude owns this folder. All accounts use it. |

`CLAUDS_CLAUDE_DATA_DIR` and `CLAUDS_DATA_DIR` move the first two groups. See
[kde/README.md](../kde/README.md#settings-for-unusual-installs).

`~/.config` and `~/.local/share` must be on the same disk, because a switch renames folders.

## The two ways to switch

### Restart mode (Side by Side off)

1. The app sends `SIGTERM` to the Claude processes and waits for them, and for their helper
   processes, to end.
2. It renames `~/.config/Claude` to `Profiles/<old account>`.
3. It renames `Profiles/<new account>` to `~/.config/Claude`.
4. It starts Claude again.

A rename on the same disk is immediate and moves no data. If a step fails, the app renames the
folders back.

### Side by Side mode

- The main account uses the normal folder.
- Each other account runs a second copy of Claude with `--user-data-dir=<Profiles/id>`.
- A click on an account starts its copy.
- The app finds which copy belongs to which account from the command line of the process, which it
  reads in `/proc`.

### Finding the Claude processes

`ClaudeProcess` scans `/proc` for processes whose executable is in an install folder of Claude
(`/usr/lib/claude-desktop`, `/opt/Claude`, and the folders of the unofficial build), or whose first
arguments point there (a shared `electron` that runs `app.asar`). A process with `--type=` is a
helper. The `chrome-native-host` of Claude belongs to the web browser and is never touched.
`CLAUDS_CLAUDE_LAUNCHER` and `CLAUDS_CLAUDE_INSTALL_DIRS` override what it looks for.

## Code sessions

Claude keeps the metadata of each Code session in
`<data folder>/claude-code-sessions/<Claude account id>/<organization id>/local_<id>.json`.

### Shared layout (Share Code Sessions on)

- `claude-code-sessions/<Claude account id>` is a symlink to `SharedSessions/orgs`.
- All accounts in the same organization see the same session files. There are no copies.
- The symlink must be at the account level. Claude opens the organization folder in a way that
  refuses a symlink there, and saves fail with `ENOTDIR`.

### Separate layout (Share Code Sessions off, and the default in Side by Side)

- Each account has a real `claude-code-sessions/<Claude account id>` folder.
- That folder holds only the sessions that the account owns. Forks that the account made are its own.
- Sessions of other accounts stay in the pool.

Why: two open Claude copies that write the same session file make a session split, or make it go
back to an old state. So Side by Side does not share session files by default.

The user can turn on sharing in Side by Side (`shareInSideBySide` in `AccountStore`). The
panel shows a warning first. In that state the app cannot know which open copy made a new session,
so the next layout change gives new sessions to the main account.

### Owner record

`owners.json` maps each session id to its owner.

- The owner is a Claude account id, or a tag `switcher:<id>` for a session that was made while the
  files were shared.
- `@account-of:<tag>` maps a tag to the Claude account that was signed in.
- A session with no record gets the account whose folder it was made in.

### Archive list

Each session folder has `archived-sessions.idx`. On each layout change, the app builds this file
again from the `isArchived` mark in each session file of that folder.

## Data safety rules

1. **One live file for each session.** Session files move between the pool and the owner's folder.
   They are not copied.
2. **Nothing is deleted.** Same name, different content: the newer file wins, and the older one goes
   to `SharedSessions/_conflicts/<label>/<time>_<name>`.
3. **A folder is removed only when it is empty.**
4. **If `owners.json` cannot be read, every session operation stops.** The last good version is
   `owners.json.bak`.
5. **One session operation at a time.** A lock guards them. Only one Clauds can run.
6. **A symlink that this app did not make is not touched.**
7. **Remove account:** its sessions go to the pool first. Then its Claude data goes to the Trash.
8. **Private files.** `umask 077`; folders are `700`, files are `600`.

## Usage numbers

Two sources:

- **Local.** Claude writes `<data folder>/plan-usage-history.json`. It has percent numbers only
  (5-hour and weekly), no reset times, and Claude does not update it often.
- **Live.** A hidden Qt WebEngine page for each account loads a small page on `claude.ai`, then asks
  `/api/organizations/<organization>/usage`, as the website does. The answer has each limit with its
  percent and reset time. The app asks every 2 minutes.

The page of an account uses its own persistent web profile (`QWebEngineProfile` named
`account-<id>`). The chat window of that account uses the same profile. That is why one sign-in in
the window is enough for the live numbers.

The live source is **not an official interface**. Anthropic can change it at any time. When it
fails, the card shows why (see "Usage numbers" in the [README](../README.md#usage-numbers)), and the
panel keeps the local numbers.

The Claude app and the web views keep separate logins. The app does not take the login out of the
Claude app. That is why a new account needs two sign-ins.

## The panel

`Tray` makes a `KStatusNotifierItem` (a `QSystemTrayIcon` when KDE Frameworks 6 is not there) and a
borderless, transparent Qt Quick window (`qml/Panel.qml`, `qml/AccountCard.qml`). The colors come
from the color scheme of the user (`Theme`, with `KColorScheme`), so nothing is hard-coded. On X11
the app places the panel next to the tray icon. On Wayland, KWin places it.

Only one Clauds can run. A second start asks the running one, through a local socket, to open its
panel.

## Source files

| File | Job |
| --- | --- |
| `src/app/main.cpp` | Start, command line hooks, single instance. |
| `src/app/Tray.cpp` | The tray icon, its quick menu, and the panel window. |
| `src/app/qml/Panel.qml`, `AccountCard.qml` | The panel and the account cards. |
| `src/app/Actions.cpp` | The actions of the panel and the menus, and the dialogs. |
| `src/app/Theme.cpp` | The colors from the color scheme. |
| `src/app/LiveUsage.cpp` | Live usage from claude.ai. |
| `src/app/ChatWindows.cpp`, `Profiles.cpp` | One claude.ai window for each account, the sign-in flow, and the web profiles. |
| `src/app/LoginItem.cpp` | The autostart entry. |
| `src/core/AccountStore.cpp` | The account list, the switch, Side by Side, usage refresh. |
| `src/core/AutoSwitch.cpp` | The auto-switch rules: when to go to a different account, and to which one. |
| `src/core/SessionLinker.cpp` | The session layouts, the owner record, the archive list. |
| `src/core/ClaudeProcess.cpp` | Finding, quitting, and starting the Claude processes. |
| `src/core/Usage.cpp` | The usage model, and reading the local and live answers. |
| `tests/` | The unit tests of `src/core` (`make test`). |

`src/core` uses Qt Core only, so that the file and process logic can be tested without a display.

## Command line hooks

Run the installed binary, or `build-kde/src/app/clauds`.

| Hook | What it does |
| --- | --- |
| `--render-panel <png>` | Draws the panel with the accounts in `CLAUDS_DATA_DIR` (set it to a folder with fake accounts for a picture to share), without live usage. Use `QT_QPA_PLATFORM=offscreen`. |
| `--show-panel` | Asks the running app to open its panel. |
| `--link-sessions <data dir> <shared dir> [tag]` | Applies the shared layout to a data folder. |
| `--unlink-sessions <data dir> <shared dir> [tag]` | Applies the separate layout to a data folder. |
