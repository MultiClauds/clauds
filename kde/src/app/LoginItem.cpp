// SPDX-License-Identifier: MIT
#include "LoginItem.h"

#include "ClaudeProcess.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace clauds::LoginItem {

QString path()
{
    return ClaudeProcess::realConfigDir()
        + QStringLiteral("/autostart/io.github.multiclauds.clauds-kde.desktop");
}

bool isOn() { return QFile::exists(path()); }

bool set(bool on, QString *error)
{
    if (!on) {
        if (!QFile::exists(path()) || QFile::remove(path()))
            return true;
        if (error)
            *error = QStringLiteral("Could not remove %1").arg(path());
        return false;
    }
    QDir().mkpath(QFileInfo(path()).absolutePath());
    QFile f(path());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const QString exec = QCoreApplication::applicationFilePath();
    f.write(QStringLiteral("[Desktop Entry]\n"
                           "Type=Application\n"
                           "Name=Clauds\n"
                           "Comment=Switch the Claude desktop app between accounts\n"
                           "Exec=%1\n"
                           "Icon=clauds\n"
                           "X-GNOME-Autostart-enabled=true\n"
                           "X-KDE-autostart-phase=2\n")
                .arg(exec)
                .toUtf8());
    return true;
}

} // namespace clauds::LoginItem
