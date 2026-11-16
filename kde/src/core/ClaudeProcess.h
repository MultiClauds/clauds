// SPDX-License-Identifier: MIT
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace clauds {

/// Finds, starts and quits the Claude desktop app on Linux.
///
/// The macOS app finds Claude by its bundle id. Linux has no bundle id, so a Claude process is a
/// process whose executable is inside one of the Claude install folders. Claude Desktop is an
/// Electron app:
///   - the main process is the Electron executable (for example /usr/lib/claude-desktop/claude-desktop)
///     with no "--type=" argument; Chromium helpers (zygote, renderer, GPU, utility) have one;
///   - other programs in the install folder (crashpad handler, the "disclaimer" wrapper that starts
///     MCP servers and Code sessions, the Cowork helper) are helpers too. chrome-native-host is
///     started by the user's web browser, so it is never touched;
///   - Code sessions run "claude" from <data dir>/claude-code/<version>/..., which keeps the data
///     folder busy, so a quit also waits for processes that run from a data folder;
///   - a copy started with "--user-data-dir=<dir>" uses that data folder, and Electron's
///     single-instance lock is per data folder, so one copy per account can run at the same time.
class ClaudeProcess
{
public:
    struct Proc {
        qint64 pid = 0;
        QString exe;
        QStringList args;

        /// The value of --user-data-dir=, or empty for the normal data folder.
        QString userDataDir() const;
    };

    /// Uses CLAUDS_CLAUDE_LAUNCHER / CLAUDS_CLAUDE_INSTALL_DIRS when set, else looks for the
    /// official "claude-desktop" package and the unofficial "claude-desktop-unofficial" build.
    static ClaudeProcess discover();

    ClaudeProcess(QString launcher, QStringList installDirs, QString procRoot = QStringLiteral("/proc"));

    QString launcher() const { return m_launcher; }
    QStringList installDirs() const { return m_installDirs; }
    bool isInstalled() const { return !m_launcher.isEmpty(); }

    /// True for an Electron main process (one per open Claude copy).
    bool isMain(const Proc &p) const;

    /// Every running process of the Claude app (main and helpers).
    QList<Proc> all() const;
    /// Main processes only: one per open Claude copy.
    QList<Proc> mains() const;
    /// The main process that uses this data folder. An empty `dataDir` means the normal folder.
    QList<Proc> mainsFor(const QString &dataDir) const;
    bool anyRunning() const { return !mains().isEmpty(); }

    /// Starts a copy. `dataDir` empty: the normal data folder. Starting a copy whose data folder
    /// is already open hands over to the open copy, which brings its window to the front.
    bool launch(const QString &dataDir = {}) const;

    /// Processes whose executable is inside one of these folders (Code sessions in a data folder).
    QList<Proc> runningFrom(const QStringList &dirs) const;

    /// Asks every Claude copy to quit (SIGTERM, which Electron treats as a normal quit), waits up
    /// to 20 s for the main processes, then up to 10 s more for helpers and for processes that run
    /// from one of `dataDirs`. Blocking: run off the GUI thread.
    /// Returns false if a main process did not quit.
    bool quitAll(const QStringList &dataDirs = {}) const;

private:
    QString m_launcher;
    QStringList m_installDirs;
    QString m_procRoot;
};

} // namespace clauds
