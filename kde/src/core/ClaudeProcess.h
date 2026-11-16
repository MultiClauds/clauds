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
///   - the main process has no "--type=" argument; helpers (renderer, GPU, utility) have one;
///   - a copy started with "--user-data-dir=<dir>" uses that data folder, and Electron's
///     single-instance lock is per data folder, so one copy per account can run at the same time.
class ClaudeProcess
{
public:
    struct Proc {
        qint64 pid = 0;
        QString exe;
        QStringList args;

        bool isMain() const;
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

    /// Asks every Claude copy to quit (SIGTERM, which Electron treats as a normal quit), waits up
    /// to 20 s for the main processes and 10 s more for helpers. Blocking: run off the GUI thread.
    /// Returns false if a main process did not quit.
    bool quitAll() const;

private:
    QString m_launcher;
    QStringList m_installDirs;
    QString m_procRoot;
};

} // namespace clauds
