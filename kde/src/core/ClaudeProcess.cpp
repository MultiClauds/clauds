// SPDX-License-Identifier: MIT
#include "ClaudeProcess.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>

#include <functional>

#include <algorithm>
#include <csignal>
#include <sys/types.h>

namespace clauds {

namespace {

QList<ClaudeProcess::Proc> scan(const QString &procRoot, const std::function<bool(const QString &, const QStringList &)> &accept)
{
    QList<ClaudeProcess::Proc> out;
    const auto entries = QDir(procRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &name : entries) {
        bool ok = false;
        const qint64 pid = name.toLongLong(&ok);
        if (!ok)
            continue;
        const QString base = procRoot + QLatin1Char('/') + name;
        const QString exe = QFileInfo(base + QStringLiteral("/exe")).symLinkTarget();
        QFile cmd(base + QStringLiteral("/cmdline"));
        if (exe.isEmpty() || !cmd.open(QIODevice::ReadOnly))
            continue;   // not our user's process, or gone
        QStringList args;
        for (const auto &part : cmd.readAll().split('\0'))
            if (!part.isEmpty())
                args << QString::fromLocal8Bit(part);
        if (accept(exe, args))
            out.append({pid, exe, args});
    }
    return out;
}

bool under(const QString &path, const QStringList &dirs)
{
    for (const auto &dir : dirs)
        if (!dir.isEmpty() && path.startsWith(QDir::cleanPath(dir) + QLatin1Char('/')))
            return true;
    return false;
}

/// File names of Electron main executables of the known packages.
const QStringList mainNames = {QStringLiteral("claude-desktop"), QStringLiteral("claude-desktop-unofficial"),
                               QStringLiteral("claude"), QStringLiteral("Claude"), QStringLiteral("electron")};

} // namespace

bool ClaudeProcess::isMain(const Proc &p) const
{
    for (const auto &a : p.args)
        if (a.startsWith(QLatin1String("--type=")))
            return false;
    const QString name = QFileInfo(p.exe).fileName();
    const QString launcherName = QFileInfo(QFileInfo(m_launcher).canonicalFilePath()).fileName();
    return mainNames.contains(name) || (!launcherName.isEmpty() && name == launcherName);
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
    if (m_installDirs.isEmpty())
        return {};
    return scan(m_procRoot, [this](const QString &exe, const QStringList &args) {
        if (QFileInfo(exe).fileName() == QLatin1String("chrome-native-host"))
            return false;   // belongs to the user's web browser
        // Ours if the executable is in an install folder, or if a shared Electron runs the app
        // from there (for example "electron /usr/lib/claude-desktop/app.asar").
        if (under(exe, m_installDirs))
            return true;
        for (int i = 0; i < std::min<qsizetype>(args.size(), 3); ++i)
            if (under(args.at(i), m_installDirs))
                return true;
        return false;
    });
}

QList<ClaudeProcess::Proc> ClaudeProcess::runningFrom(const QStringList &dirs) const
{
    if (dirs.isEmpty())
        return {};
    return scan(m_procRoot, [&dirs](const QString &exe, const QStringList &) { return under(exe, dirs); });
}

QList<ClaudeProcess::Proc> ClaudeProcess::mains() const
{
    QList<Proc> out;
    for (const auto &p : all())
        if (isMain(p))
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

bool ClaudeProcess::quitAll(const QStringList &dataDirs) const
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
    // Helper processes (renderers, MCP servers, Code sessions) must release the data folder too.
    for (int i = 0; i < 40 && (!all().isEmpty() || !runningFrom(dataDirs).isEmpty()); ++i)
        QThread::msleep(250);
    QThread::sleep(1);
    return true;
}

} // namespace clauds
