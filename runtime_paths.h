#pragma once
#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>
#include "build_config.h"
// Installed data (/usr/share/jarvis/…) next to the binary's prefix, or the
// source tree when running from a build directory.
inline QString jarvisDataFile(const QString &relative) {
    const QString installed=QCoreApplication::applicationDirPath()+"/../share/jarvis/"+relative;
    if(QFileInfo::exists(installed)) return installed;
    return QString(JARVIS_SOURCE_DIR)+"/"+relative;
}
inline QString jarvisScript(const QString &name) { return jarvisDataFile("scripts/"+name); }
// The system interpreter, not whatever "python3" comes first in PATH.
inline QString systemPython() { return QFileInfo::exists("/usr/bin/python3") ? QStringLiteral("/usr/bin/python3") : QStringLiteral("python3"); }
