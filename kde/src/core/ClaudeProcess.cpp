// SPDX-License-Identifier: MIT
#include "ClaudeProcess.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>

#include <algorithm>
#include <csignal>
#include <sys/types.h>

namespace clauds {

bool ClaudeProcess::Proc::isMain() const
{
    for (const auto &a : args)
        if (a.startsWith(QLatin1String("--type=")))
            return false;
    return true;
}

QString ClaudeProcess::Proc::userDataDir() const
{
    for (int i = 0; i < args.size(); ++i) {
        const QString &a = args.at(i);
        if (a.startsWith(QLatin1String("--user-data-dir=")))
            return QDir::cleanPath(a.mid(16));
        if (a == QLatin1String("--user-data-dir") && i + 1 < args.size())
            return QDir::cleanPath(args.at(i + 1));
    }
    return {};
}

ClaudeProcess ClaudeProcess::discover()
{
    QString launcher = qEnvironmentVariable("CLAUDS_CLAUDE_LAUNCHER");
    QStringList dirs = qEnvironmentVariable("CLAUDS_CLAUDE_INSTALL_DIRS").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (launcher.isEmpty()) {
        for (const auto *name : {"claude-desktop", "claude-desktop-unofficial"}) {
            launcher = QStandardPaths::findExecutable(QLatin1String(name));
            if (!launcher.isEmpty())
                break;
        }
    }
    if (dirs.isEmpty()) {
        // Where the Electron files of the known packages live.
        dirs = {QStringLiteral("/usr/lib/claude-desktop"), QStringLiteral("/opt/Claude"),
                QStringLiteral("/opt/claude-desktop"), QStringLiteral("/usr/lib/claude-desktop-unofficial")};
        // The launcher can be a symlink into the install folder.
        if (!launcher.isEmpty()) {
            const QString real = QFileInfo(launcher).canonicalFilePath();
            const QString dir = QFileInfo(real).absolutePath();
            if (!dir.startsWith(QLatin1String("/usr/bin")) && !dir.startsWith(QLatin1String("/usr/local/bin")))
                dirs.prepend(dir);
        }
    }
    QStringList existing;
    for (const auto &d : dirs) {
        const QString c = QFileInfo(d).canonicalFilePath();
        if (!c.isEmpty() && !existing.contains(c))
            existing << c;
    }
    return ClaudeProcess(launcher, existing);
}

ClaudeProcess::ClaudeProcess(QString launcher, QStringList installDirs, QString procRoot)
    : m_launcher(std::move(launcher))
    , m_installDirs(std::move(installDirs))
    , m_procRoot(std::move(procRoot))
{
}

QList<ClaudeProcess::Proc> ClaudeProcess::all() const
{
    QList<Proc> out;
    if (m_installDirs.isEmpty())
        return out;
    const auto entries = QDir(m_procRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &name : entries) {
        bool ok = false;
        const qint64 pid = name.toLongLong(&ok);
        if (!ok)
            continue;
        const QString base = m_procRoot + QLatin1Char('/') + name;
        const QString exe = QFileInfo(base + QStringLiteral("/exe")).symLinkTarget();
        QFile cmd(base + QStringLiteral("/cmdline"));
        if (exe.isEmpty() || !cmd.open(QIODevice::ReadOnly))
            continue;   // not our user's process, or gone
        QStringList args;
        for (const auto &part : cmd.readAll().split('\0'))
            if (!part.isEmpty())
                args << QString::fromLocal8Bit(part);
        // Ours if the executable is in an install folder, or if a shared Electron runs the app
        // from there (for example "electron /usr/lib/claude-desktop/app.asar").
        auto inInstallDir = [this](const QString &path) {
            for (const auto &dir : m_installDirs)
                if (path.startsWith(dir + QLatin1Char('/')))
                    return true;
            return false;
        };
        bool ours = inInstallDir(exe);
        for (int i = 0; !ours && i < std::min<qsizetype>(args.size(), 3); ++i)
            ours = inInstallDir(args.at(i));
        if (ours)
            out.append({pid, exe, args});
    }
    return out;
}

QList<ClaudeProcess::Proc> ClaudeProcess::mains() const
{
    QList<Proc> out;
    for (const auto &p : all())
        if (p.isMain())
            out << p;
    return out;
}

QList<ClaudeProcess::Proc> ClaudeProcess::mainsFor(const QString &dataDir) const
{
    const QString want = dataDir.isEmpty() ? QString() : QDir::cleanPath(dataDir);
    QList<Proc> out;
    for (const auto &p : mains())
        if (p.userDataDir() == want)
            out << p;
    return out;
}

bool ClaudeProcess::launch(const QString &dataDir) const
{
    if (m_launcher.isEmpty())
        return false;
    QStringList args;
    if (!dataDir.isEmpty())
        args << QStringLiteral("--user-data-dir=") + dataDir;
    return QProcess::startDetached(m_launcher, args);
}

bool ClaudeProcess::quitAll() const
{
    const auto running = mains();
    if (running.isEmpty())
        return true;
    for (const auto &p : running)
        ::kill(pid_t(p.pid), SIGTERM);
    auto alive = [this, &running] {
        const auto now = mains();
        for (const auto &p : running)
            for (const auto &n : now)
                if (n.pid == p.pid)
                    return true;
        return false;
    };
    for (int i = 0; i < 80 && alive(); ++i)
        QThread::msleep(250);
    if (alive())
        return false;
    // Helper processes (renderers, Code sessions) must release the data folder too.
    for (int i = 0; i < 40 && !all().isEmpty(); ++i)
        QThread::msleep(250);
    QThread::sleep(1);
    return true;
}

} // namespace clauds
