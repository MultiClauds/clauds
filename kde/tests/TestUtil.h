// SPDX-License-Identifier: MIT
#pragma once
#include <QDir>
#include <QFile>
#include <QString>
#include <filesystem>

inline void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write(data);
}

inline QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

inline std::filesystem::path P(const QString &s) { return std::filesystem::path(s.toStdString()); }
