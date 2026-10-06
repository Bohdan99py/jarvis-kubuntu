#include "config.h"

#include <QCoreApplication>
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
namespace {

QString languageValue(const QJsonObject &o, const QString &key)
{
    const QString v = o.value(key).toString();
    return (v == u"ru" || v == u"en") ? v : u"auto"_s;
}

} // namespace

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
        d.language = languageValue(o, u"language"_s);
        d.replyLanguage = languageValue(o, u"reply_language"_s);
        d.learnDialog = o.value(u"learn_dialog"_s).toBool(true);
        d.trackActivity = o.value(u"track_activity"_s).toBool(false);
        d.trackTitles = o.value(u"track_titles"_s).toBool(false);
        d.shareActivity = o.value(u"share_activity"_s).toBool(false);
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
        return fail(QCoreApplication::translate("jarvis", "Cannot create folder %1").arg(dir.absolutePath()));
    QFile::setPermissions(dir.absolutePath(),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    // QSaveFile writes to a temp file and renames: no half-written config.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return fail(QCoreApplication::translate("jarvis", "Cannot open %1: %2").arg(path, f.errorString()));
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    QJsonObject o;
    o.insert(u"api_key"_s, data.apiKey);
    o.insert(u"model"_s, data.model);
    o.insert(u"language"_s, data.language);
    o.insert(u"reply_language"_s, data.replyLanguage);
    o.insert(u"learn_dialog"_s, data.learnDialog);
    o.insert(u"track_activity"_s, data.trackActivity);
    o.insert(u"track_titles"_s, data.trackTitles);
    o.insert(u"share_activity"_s, data.shareActivity);
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));

    if (!f.commit())
        return fail(QCoreApplication::translate("jarvis", "Cannot save %1: %2").arg(path, f.errorString()));
    return true;
}

QString Config::keyHint(const QString &key)
{
    if (key.size() <= 8)
        return {};
    return QChar(0x2026) + key.right(4);
}

} // namespace jarvis
