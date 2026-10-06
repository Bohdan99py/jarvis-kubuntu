#pragma once
#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>
#include "build_config.h"
inline QString jarvisScript(const QString &name) {
    const QString installed=QCoreApplication::applicationDirPath()+"/../share/jarvis/scripts/"+name;
    if(QFileInfo::exists(installed)) return installed;
    return QString(JARVIS_SOURCE_DIR)+"/scripts/"+name;
}
