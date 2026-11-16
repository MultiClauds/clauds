// SPDX-License-Identifier: MIT
#include "ClaudeProcess.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
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

/// Runs a program on the host through the Flatpak portal and returns its standard output.
QByteArray hostRun(const QStringList &args, int timeoutMs = 15000)
{
    QProcess p;
    p.start(QStringLiteral("flatpak-spawn"), QStringList{QStringLiteral("--host")} + args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        return {};
    }
    return p.readAllStandardOutput();
}

/// Executables of every process of the host, as seen from the host. One program is started
/// for all of them, so this stays fast with hundreds of processes.
QList<ClaudeProcess::Proc> hostExes()
{
    QList<ClaudeProcess::Proc> out;
    const auto lines = hostRun({QStringLiteral("find"), QStringLiteral("/proc"), QStringLiteral("-mindepth"), QStringLiteral("2"),
                                QStringLiteral("-maxdepth"), QStringLiteral("2"), QStringLiteral("-name"), QStringLiteral("exe"),
                                QStringLiteral("-printf"), QStringLiteral("%h\\t%l\\n")}).split('\n');
    for (const auto &line : lines) {
        const auto f = line.split('\t');
        if (f.size() != 2 || f.at(1).isEmpty())
            continue;
        bool ok = false;
        const qint64 pid = f.at(0).mid(6).toLongLong(&ok);   // "/proc/<pid>"
        if (ok)
            out.append({pid, QString::fromLocal8Bit(f.at(1)), {}});
    }
    return out;
}

/// The command lines of these processes, from one host program.
QHash<qint64, QStringList> hostArgs(const QList<qint64> &pids)
{
    QHash<qint64, QStringList> out;
    if (pids.isEmpty())
        return out;
    QStringList argv{QStringLiteral("sh"), QStringLiteral("-c"),
                     QStringLiteral("for p; do printf '%s\\t' \"$p\"; tr '\\0' '\\037' < \"/proc/$p/cmdline\" 2>/dev/null; printf '\\n'; done"),
                     QStringLiteral("sh")};
    for (qint64 pid : pids)
        argv << QString::number(pid);
    for (const auto &line : hostRun(argv).split('\n')) {
        const int tab = line.indexOf('\t');
        if (tab < 0)
            continue;
        QStringList args;
        for (const auto &part : line.mid(tab + 1).split('\x1f'))
            if (!part.isEmpty())
                args << QString::fromLocal8Bit(part);
        out.insert(line.left(tab).toLongLong(), args);
    }
    return out;
}

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

bool ClaudeProcess::inFlatpak()
{
    return QFileInfo::exists(QStringLiteral("/.flatpak-info")) && !qEnvironmentVariableIsSet("CLAUDS_NO_HOST_SPAWN");
}

QString ClaudeProcess::realConfigDir()
{
    if (inFlatpak())
        return QDir::homePath() + QStringLiteral("/.config");
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

ClaudeProcess ClaudeProcess::discover()
{
    const bool host = inFlatpak() && qEnvironmentVariableIsEmpty("CLAUDS_CLAUDE_LAUNCHER");
    QString launcher = qEnvironmentVariable("CLAUDS_CLAUDE_LAUNCHER");
    QStringList dirs = qEnvironmentVariable("CLAUDS_CLAUDE_INSTALL_DIRS").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    // Resolve a path on the host (a symlink in /usr/bin points into the install folder).
    auto hostReal = [](const QString &path) {
        return QString::fromLocal8Bit(hostRun({QStringLiteral("readlink"), QStringLiteral("-f"), path}).trimmed());
    };
    if (launcher.isEmpty()) {
        for (const auto *name : {"claude-desktop", "claude-desktop-unofficial"}) {
            if (host) {
                launcher = QString::fromLocal8Bit(
                    hostRun({QStringLiteral("sh"), QStringLiteral("-c"), QStringLiteral("command -v ") + QLatin1String(name)}).trimmed());
            } else {
                launcher = QStandardPaths::findExecutable(QLatin1String(name));
            }
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
            const QString real = host ? hostReal(launcher) : QFileInfo(launcher).canonicalFilePath();
            const QString dir = QFileInfo(real).absolutePath();
            if (!dir.startsWith(QLatin1String("/usr/bin")) && !dir.startsWith(QLatin1String("/usr/local/bin")))
                dirs.prepend(dir);
        }
    }
    QStringList existing;
    if (host) {
        // The folders are on the host, so only the host can say which of them exist.
        const auto out = hostRun(QStringList{QStringLiteral("sh"), QStringLiteral("-c"),
                                             QStringLiteral("for d; do [ -d \"$d\" ] && readlink -f \"$d\"; done"), QStringLiteral("sh")} + dirs);
        for (const auto &line : out.split('\n'))
            if (!line.isEmpty() && !existing.contains(QString::fromLocal8Bit(line)))
                existing << QString::fromLocal8Bit(line);
        ClaudeProcess cp(launcher, existing);
        cp.m_host = true;
        return cp;
    }
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

QList<ClaudeProcess::Proc> ClaudeProcess::hostScan(const std::function<bool(const QString &, const QStringList &)> &accept) const
{
    // The list is reused for a moment: one panel refresh asks several times.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_cache.at > 1000 || m_cache.at == 0) {
        m_cache.procs = hostExes();
        m_cache.args.clear();
        m_cache.at = now;
    }
    // Only the command lines of possible matches are read. A shared Electron runs the app from
    // the install folder, so it is a possible match too.
    QList<qint64> need;
    for (const auto &p : std::as_const(m_cache.procs))
        if (!m_cache.args.contains(p.pid) && (accept(p.exe, {}) || QFileInfo(p.exe).fileName() == QLatin1String("electron")))
            need << p.pid;
    const auto got = hostArgs(need);
    for (qint64 pid : need)
        m_cache.args.insert(pid, got.value(pid));
    QList<Proc> out;
    for (auto p : std::as_const(m_cache.procs)) {
        if (!m_cache.args.contains(p.pid))
            continue;
        p.args = m_cache.args.value(p.pid);
        if (accept(p.exe, p.args))
            out.append(p);
    }
    return out;
}

QList<ClaudeProcess::Proc> ClaudeProcess::all() const
{
    if (m_installDirs.isEmpty())
        return {};
    const auto accept = [this](const QString &exe, const QStringList &args) {
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
    };
    return m_host ? hostScan(accept) : scan(m_procRoot, accept);
}

QList<ClaudeProcess::Proc> ClaudeProcess::runningFrom(const QStringList &dirs) const
{
    if (dirs.isEmpty())
        return {};
    const auto accept = [&dirs](const QString &exe, const QStringList &) { return under(exe, dirs); };
    return m_host ? hostScan(accept) : scan(m_procRoot, accept);
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
    if (m_host)
    {
        QStringList spawn{QStringLiteral("--host"), QStringLiteral("--directory=") + QDir::homePath()};
        // Lets the host window come to the front when a copy is already open (Wayland).
        for (const char *name : {"XDG_ACTIVATION_TOKEN", "DESKTOP_STARTUP_ID"})
            if (qEnvironmentVariableIsSet(name))
                spawn << QStringLiteral("--env=%1=%2").arg(QLatin1String(name), qEnvironmentVariable(name));
        return QProcess::startDetached(QStringLiteral("flatpak-spawn"), spawn + QStringList{m_launcher} + args);
    }
    return QProcess::startDetached(m_launcher, args);
}

bool ClaudeProcess::quitAll(const QStringList &dataDirs) const
{
    m_cache.at = 0;
    const auto running = mains();
    if (running.isEmpty())
        return true;
    if (m_host) {
        QStringList kill{QStringLiteral("kill"), QStringLiteral("-TERM")};
        for (const auto &p : running)
            kill << QString::number(p.pid);
        hostRun(kill);
    } else {
        for (const auto &p : running)
            ::kill(pid_t(p.pid), SIGTERM);
    }
    auto alive = [this, &running] {
        m_cache.at = 0;   // look again, not at the list from a moment ago
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
    for (int i = 0; i < 40 && ((m_cache.at = 0, !all().isEmpty()) || !runningFrom(dataDirs).isEmpty()); ++i)
        QThread::msleep(250);
    QThread::sleep(1);
    return true;
}

} // namespace clauds
