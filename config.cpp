#include "config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtGlobal>

using namespace Qt::StringLiterals;

namespace jarvis {

QString Config::defaultModel()
{
    return u"claude-haiku-4-5-20251001"_s;
}

QString Config::filePath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return base + u"/jarvis/config.json"_s;
}

ConfigData Config::loadFileOnly()
{
    ConfigData d;
    d.model = defaultModel();

    QFile f(filePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        d.apiKey = o.value(u"api_key"_s).toString().trimmed();
        const QString m = o.value(u"model"_s).toString().trimmed();
        if (!m.isEmpty())
            d.model = m;
    }
    return d;
}

ConfigData Config::load()
{
    ConfigData d = loadFileOnly();
    const QString env = qEnvironmentVariable("ANTHROPIC_API_KEY").trimmed();
    if (!env.isEmpty())
        d.apiKey = env;
    return d;
}

bool Config::save(const ConfigData &data, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };

    const QString path = filePath();
    const QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.mkpath(QStringLiteral(".")))
        return fail(u"Не удалось создать папку %1"_s.arg(dir.absolutePath()));
    QFile::setPermissions(dir.absolutePath(),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    // QSaveFile writes to a temp file and renames: no half-written config.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return fail(u"Не удалось открыть %1: %2"_s.arg(path, f.errorString()));
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    QJsonObject o;
    o.insert(u"api_key"_s, data.apiKey);
    o.insert(u"model"_s, data.model);
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));

    if (!f.commit())
        return fail(u"Не удалось сохранить %1: %2"_s.arg(path, f.errorString()));
    return true;
}

QString Config::keyHint(const QString &key)
{
    if (key.size() <= 8)
        return {};
    return QChar(0x2026) + key.right(4);
}

} // namespace jarvis
