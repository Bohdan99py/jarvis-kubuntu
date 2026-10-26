#include "config.h"
#include "desktop_integration.h"
#include "secret_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtGlobal>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

QString languageValue(const QJsonObject &o, const QString &key)
{
    const QString v = o.value(key).toString();
    return (v == u"ru" || v == u"en") ? v : u"auto"_s;
}

QStringList cleanList(const QJsonArray &values, int max)
{
    QStringList out;
    for (const QJsonValue &v : values) {
        const QString item = v.toString().trimmed().toLower().left(120);
        if (!item.isEmpty() && !out.contains(item) && out.size() < max)
            out.append(item);
    }
    return out;
}


// The file as an object; `damaged`: it exists, is not empty and is not a JSON object.
QJsonObject readObject(const QString &path, bool *damaged = nullptr)
{
    if (damaged)
        *damaged = false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = f.readAll();
    if (bytes.trimmed().isEmpty())
        return {};
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (damaged)
            *damaged = true;
        return {};
    }
    return doc.object();
}

bool writeObject(const QString &path, const QJsonObject &o, QString *error)
{
    const QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.mkpath(QStringLiteral("."))) {
        if (error)
            *error = QCoreApplication::translate("jarvis", "Cannot create folder %1").arg(dir.absolutePath());
        return false;
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QCoreApplication::translate("jarvis", "Cannot open %1: %2").arg(path, f.errorString());
        return false;
    }
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QByteArray bytes = QJsonDocument(o).toJson(QJsonDocument::Indented);
    if (f.write(bytes) != bytes.size() || !f.commit()) {
        if (error)
            *error = QCoreApplication::translate("jarvis", "Cannot save %1: %2").arg(path, f.errorString());
        return false;
    }
    return true;
}

// A damaged file is kept for the user, never written over.
QString moveAside(const QString &path)
{
    const QString target = path + u".damaged-"_s + QDateTime::currentDateTime().toString(u"yyyyMMdd-HHmmss"_s);
    return QFile::rename(path, target) ? target : QString();
}

QJsonObject toJson(const ConfigData &data)
{
    QJsonObject o;
    o.insert(u"api_key"_s, data.apiKey);
    o.insert(u"model"_s, data.model);
    o.insert(u"language"_s, data.language);
    o.insert(u"reply_language"_s, data.replyLanguage);
    o.insert(u"learn_dialog"_s, data.learnDialog);
    o.insert(u"curiosity"_s, data.curiosity);
    o.insert(u"track_activity"_s, data.trackActivity);
    o.insert(u"track_titles"_s, data.trackTitles);
    o.insert(u"share_activity"_s, data.shareActivity);
    o.insert(u"proactive"_s, data.proactive);
    o.insert(u"break_minutes"_s, data.breakMinutes);
    o.insert(u"morning_summary"_s, data.morningSummary);
    o.insert(u"deep_reflection"_s, data.deepReflection);
    o.insert(u"notify_replies"_s, data.notifyReplies);
    o.insert(u"snooze_minutes"_s, data.snoozeMinutes);
    o.insert(u"morning_hour"_s, data.morningHour);
    o.insert(u"afternoon_hour"_s, data.afternoonHour);
    o.insert(u"evening_hour"_s, data.eveningHour);
    o.insert(u"reminder_sound"_s, data.reminderSound);
    o.insert(u"nag_minutes"_s, data.nagMinutes);
    o.insert(u"awareness_level"_s, data.awarenessLevel);
    o.insert(u"awareness_window"_s, data.awarenessWindow);
    o.insert(u"awareness_screen"_s, data.awarenessScreen);
    o.insert(u"awareness_cloud_images"_s, data.awarenessCloudImages);
    o.insert(u"screen_interval_minutes"_s, data.screenIntervalMinutes);
    o.insert(u"awareness_muted"_s, QJsonArray::fromStringList(data.awarenessMuted));
    o.insert(u"awareness_excluded"_s, QJsonArray::fromStringList(data.awarenessExcluded));
    o.insert(u"suggestion_cooldown_minutes"_s, data.suggestionCooldownMinutes);
    o.insert(u"suggestions_per_day"_s, data.suggestionsPerDay);
    o.insert(u"awareness_fullscreen"_s, data.awarenessFullscreen);
    o.insert(u"web_search"_s, data.webSearch);
    o.insert(u"background"_s, data.background);
    o.insert(u"desktop_integration"_s, data.desktopIntegration);
    o.insert(u"share_active_app"_s, data.shareActiveApp);
    o.insert(u"hud_animation"_s, data.hudAnimation);
    o.insert(u"desktop"_s, integration::normalizeDesktop(data.desktop));
    o.insert(u"agent_mode"_s, data.agentMode);
    o.insert(u"agent_cloud"_s, data.agentCloud);
    o.insert(u"agent_model"_s, data.agentModel);
    o.insert(u"agent_device"_s, data.agentDevice);
    o.insert(u"agent_keep_minutes"_s, data.agentKeepMinutes);
    o.insert(u"agent_permissions"_s, data.agentPermissions);
    o.insert(u"agent_max_steps"_s, data.agentMaxSteps);
    o.insert(u"agent_max_seconds"_s, data.agentMaxSeconds);
    return o;
}

QString keyLabel(const QString &account)
{
    return account == QLatin1String(SecretStore::kElevenLabs) ? u"Jarvis: ElevenLabs API key"_s : u"Jarvis: Anthropic API key"_s;
}

} // namespace

QStringList Config::awarenessLevels()
{
    return {u"off"_s, u"manual"_s, u"assistive"_s, u"proactive"_s};
}

QStringList Config::agentModes()
{
    return {u"auto"_s, u"local"_s};
}

QStringList Config::hudAnimationLevels()
{
    return {u"off"_s, u"low"_s, u"normal"_s, u"high"_s};
}

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

    if (QFileInfo::exists(filePath())) {
        const QJsonObject o = readObject(filePath());
        d.apiKey = o.value(u"api_key"_s).toString().trimmed();
        const QString m = o.value(u"model"_s).toString().trimmed();
        if (!m.isEmpty())
            d.model = m;
        d.language = languageValue(o, u"language"_s);
        d.replyLanguage = languageValue(o, u"reply_language"_s);
        d.learnDialog = o.value(u"learn_dialog"_s).toBool(true);
        d.curiosity = o.value(u"curiosity"_s).toBool(true);
        d.trackActivity = o.value(u"track_activity"_s).toBool(false);
        d.trackTitles = o.value(u"track_titles"_s).toBool(false);
        d.shareActivity = o.value(u"share_activity"_s).toBool(false);
        d.proactive = o.value(u"proactive"_s).toBool(true);
        d.breakMinutes = std::clamp(o.value(u"break_minutes"_s).toInt(90), 0, 600);
        d.morningSummary = o.value(u"morning_summary"_s).toBool(true);
        d.deepReflection = o.value(u"deep_reflection"_s).toBool(true);
        d.notifyReplies = o.value(u"notify_replies"_s).toBool(true);
        d.snoozeMinutes = std::clamp(o.value(u"snooze_minutes"_s).toInt(10), 1, 720);
        d.morningHour = std::clamp(o.value(u"morning_hour"_s).toInt(9), 4, 12);
        d.afternoonHour = std::clamp(o.value(u"afternoon_hour"_s).toInt(14), 11, 18);
        d.eveningHour = std::clamp(o.value(u"evening_hour"_s).toInt(19), 16, 23);
        d.reminderSound = o.value(u"reminder_sound"_s).toBool(true);
        d.nagMinutes = std::clamp(o.value(u"nag_minutes"_s).toInt(0), 0, 120);
        const QString level = o.value(u"awareness_level"_s).toString();
        d.awarenessLevel = awarenessLevels().contains(level) ? level : u"manual"_s;
        d.awarenessWindow = o.value(u"awareness_window"_s).toBool(false);
        d.awarenessScreen = o.value(u"awareness_screen"_s).toBool(false);
        d.awarenessCloudImages = o.value(u"awareness_cloud_images"_s).toBool(false);
        d.screenIntervalMinutes = std::clamp(o.value(u"screen_interval_minutes"_s).toInt(0), 0, 60);
        d.awarenessMuted = cleanList(o.value(u"awareness_muted"_s).toArray(), 20);
        d.awarenessExcluded = cleanList(o.value(u"awareness_excluded"_s).toArray(), 100);
        d.suggestionCooldownMinutes = std::clamp(o.value(u"suggestion_cooldown_minutes"_s).toInt(45), 10, 24 * 60);
        d.suggestionsPerDay = std::clamp(o.value(u"suggestions_per_day"_s).toInt(4), 1, 20);
        d.awarenessFullscreen = o.value(u"awareness_fullscreen"_s).toBool(false);
        d.webSearch = o.value(u"web_search"_s).toBool(false);
        d.background = o.value(u"background"_s).toBool(true);
        d.desktopIntegration = o.value(u"desktop_integration"_s).toBool(true);
        d.shareActiveApp = o.value(u"share_active_app"_s).toBool(false);
        const QString animation = o.value(u"hud_animation"_s).toString();
        d.hudAnimation = hudAnimationLevels().contains(animation) ? animation : u"normal"_s;
        d.desktop = integration::normalizeDesktop(o.value(u"desktop"_s).toObject());
        const QString mode = o.value(u"agent_mode"_s).toString();
        d.agentMode = agentModes().contains(mode) ? mode : u"auto"_s;
        d.agentCloud = o.value(u"agent_cloud"_s).toBool(false);
        static const QRegularExpression modelId(u"^[a-z0-9._-]{0,40}$"_s);
        const QString model = o.value(u"agent_model"_s).toString(d.agentModel);
        d.agentModel = modelId.match(model).hasMatch() ? model : d.agentModel;
        const QString device = o.value(u"agent_device"_s).toString();
        d.agentDevice = device == u"cpu" ? device : u"auto"_s;
        d.agentKeepMinutes = std::clamp(o.value(u"agent_keep_minutes"_s).toInt(10), 1, 120);
        const QJsonObject permissions = o.value(u"agent_permissions"_s).toObject();
        for (auto it = permissions.begin(); it != permissions.end(); ++it)
            if (const QString v = it.value().toString(); it.key().size() <= 20 && (v == u"allow" || v == u"ask" || v == u"off"))
                d.agentPermissions.insert(it.key(), v);
        d.agentMaxSteps = std::clamp(o.value(u"agent_max_steps"_s).toInt(6), 1, 12);
        d.agentMaxSeconds = std::clamp(o.value(u"agent_max_seconds"_s).toInt(60), 10, 300);
    } else {
        d.desktop = integration::normalizeDesktop({});
    }
    return d;
}

ConfigData Config::load()
{
    ConfigData d = loadFileOnly();
    d.apiKey = anthropicKey();
    return d;
}

QString Config::anthropicKey(KeyStore *where)
{
    auto at = [where](KeyStore s) {
        if (where)
            *where = s;
    };
    const QString env = qEnvironmentVariable("ANTHROPIC_API_KEY").trimmed();
    if (!env.isEmpty()) {
        at(KeyStore::Environment);
        return env;
    }
    SecretStore::State state = SecretStore::State::Unavailable;
    const QString stored = SecretStore::read(QLatin1String(SecretStore::kAnthropic), &state);
    if (state == SecretStore::State::Found) {
        at(KeyStore::Keyring);
        return stored;
    }
    const QString file = readObject(filePath()).value(u"api_key"_s).toString().trimmed();
    if (!file.isEmpty()) {
        at(KeyStore::File);
        return file;
    }
    at(state == SecretStore::State::Locked ? KeyStore::Locked : KeyStore::None);
    return {};
}

QString Config::setAnthropicKey(const QString &key, KeyStore *where)
{
    const QString value = key.trimmed();
    QString error;
    ConfigData d = loadFileOnly();
    if (SecretStore::available()) {
        if (value.isEmpty() ? !SecretStore::remove(QLatin1String(SecretStore::kAnthropic), &error)
                            : !SecretStore::write(QLatin1String(SecretStore::kAnthropic), keyLabel(QLatin1String(SecretStore::kAnthropic)), value, &error))
            return error;
        if (where)
            *where = value.isEmpty() ? KeyStore::None : KeyStore::Keyring;
        // The key is safe in the store: the file no longer needs it.
        if (!d.apiKey.isEmpty()) {
            d.apiKey.clear();
            if (!save(d, &error))
                return error;
        }
        return {};
    }
    d.apiKey = value;
    if (!save(d, &error))
        return error;
    if (where)
        *where = value.isEmpty() ? KeyStore::None : KeyStore::File;
    return {};
}

bool Config::save(const ConfigData &data, QString *error)
{
    const QString path = filePath();
    const QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.mkpath(QStringLiteral("."))) {
        if (error)
            *error = QCoreApplication::translate("jarvis", "Cannot create folder %1").arg(dir.absolutePath());
        return false;
    }
    QFile::setPermissions(dir.absolutePath(),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    bool damaged = false;
    QJsonObject o = readObject(path, &damaged);
    if (damaged && moveAside(path).isEmpty()) {
        if (error)
            *error = QCoreApplication::translate("jarvis", "%1 is damaged and could not be moved aside, so it was not overwritten.").arg(path);
        return false;
    }
    // Known fields are replaced; fields of another version stay as they were.
    const QJsonObject known = toJson(data);
    for (auto it = known.begin(); it != known.end(); ++it)
        o.insert(it.key(), it.value());
    o.insert(u"config_version"_s, std::max(kVersion, o.value(u"config_version"_s).toInt(0)));
    // QSaveFile writes to a temp file and renames: no half-written config.
    return writeObject(path, o, error);
}

QString Config::backupPath()
{
    return filePath() + u".backup-v1"_s;
}

QString Config::speechPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/jarvis/voice/speech.json"_s;
}

QString Config::migrate()
{
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    // The window and the service may start together: one migrates, the other waits.
    QLockFile lock(path + u".lock"_s);
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(10000))
        return {};
    QStringList done;
    bool damaged = false;
    QJsonObject o = readObject(path, &damaged);
    if (damaged) {
        const QString aside = moveAside(path);
        done << (aside.isEmpty() ? u"config.json is damaged and could not be moved aside"_s
                                 : u"damaged config.json moved to "_s + aside);
        o = {};
    }
    const bool exists = QFileInfo::exists(path);
    const int version = o.value(u"config_version"_s).toInt(exists ? 1 : 0);
    const bool keyring = SecretStore::available();
    QString error;

    // A copy of the 0.20 file before anything changes, without its secrets
    // (they move to the password store; prepareDowngrade writes them back).
    if (exists && version < kVersion && !QFileInfo::exists(backupPath())) {
        QJsonObject copy = o;
        if (keyring && !copy.value(u"api_key"_s).toString().isEmpty())
            copy[u"api_key"_s] = u""_s;
        if (writeObject(backupPath(), copy, &error))
            done << u"backup "_s + backupPath();
    }

    // Keys into the password store: only removed from a file after they read back.
    if (keyring) {
        const QString key = o.value(u"api_key"_s).toString().trimmed();
        if (!key.isEmpty()) {
            if (SecretStore::write(QLatin1String(SecretStore::kAnthropic), keyLabel(QLatin1String(SecretStore::kAnthropic)), key, &error)) {
                o[u"api_key"_s] = u""_s;
                done << u"Claude key moved to the password store"_s;
            } else {
                done << u"Claude key stays in config.json: "_s + error;
            }
        }
        QJsonObject speech = readObject(speechPath());
        QJsonObject eleven = speech.value(u"elevenlabs"_s).toObject();
        const QString voiceKey = eleven.value(u"api_key"_s).toString().trimmed();
        if (!voiceKey.isEmpty()) {
            if (SecretStore::write(QLatin1String(SecretStore::kElevenLabs), keyLabel(QLatin1String(SecretStore::kElevenLabs)), voiceKey, &error)) {
                eleven.remove(u"api_key"_s);
                speech[u"elevenlabs"_s] = eleven;
                if (writeObject(speechPath(), speech, &error))
                    done << u"ElevenLabs key moved to the password store"_s;
            } else {
                done << u"ElevenLabs key stays in speech.json: "_s + error;
            }
        }
    }

    if (exists && (version < kVersion || !done.isEmpty())) {
        o[u"config_version"_s] = std::max(kVersion, version);
        if (!writeObject(path, o, &error))
            done << u"could not write config.json: "_s + error;
    }
    return done.join(u"; "_s);
}

QString Config::prepareDowngrade(QString *error)
{
    QStringList done;
    QString why;
    QJsonObject o = readObject(filePath());
    SecretStore::State state, voiceState;
    const QString key = SecretStore::read(QLatin1String(SecretStore::kAnthropic), &state);
    const QString voiceKey = SecretStore::read(QLatin1String(SecretStore::kElevenLabs), &voiceState);
    if (state == SecretStore::State::Locked || voiceState == SecretStore::State::Locked) {
        if (error)
            *error = QCoreApplication::translate("jarvis", "The password store is locked. Unlock it (open Jarvis's settings) and try again.");
        return {};
    }
    if (!key.isEmpty() && o.value(u"api_key"_s).toString().isEmpty()) {
        o[u"api_key"_s] = key;
        done << u"Claude key written to config.json"_s;
    }
    o[u"config_version"_s] = 1;
    if (!writeObject(filePath(), o, &why)) {
        if (error)
            *error = why;
        return {};
    }
    if (!voiceKey.isEmpty()) {
        QJsonObject speech = readObject(speechPath());
        QJsonObject eleven = speech.value(u"elevenlabs"_s).toObject();
        if (eleven.value(u"api_key"_s).toString().isEmpty()) {
            eleven[u"api_key"_s] = voiceKey;
            speech[u"elevenlabs"_s] = eleven;
            if (!writeObject(speechPath(), speech, &why)) {
                if (error)
                    *error = why;
                return {};
            }
            done << u"ElevenLabs key written to speech.json"_s;
        }
    }
    done << u"config.json marked as version 1"_s;
    return done.join(u"; "_s);
}

ConfigData Config::defaults()
{
    ConfigData d;
    d.model = defaultModel();
    d.desktop = integration::normalizeDesktop({});
    return d;
}

QStringList Config::sections()
{
    return {u"desktop"_s, u"conversation"_s, u"intelligence"_s, u"autonomy"_s, u"privacy"_s, u"actions"_s, u"system"_s, u"all"_s};
}

ConfigData Config::resetSection(const ConfigData &current, const QString &section)
{
    ConfigData d = current;
    const ConfigData def = defaults();
    const bool all = section == u"all";
    // A permission or an opt-in may only go off.
    const auto lower = [](bool now, bool byDefault) { return now && byDefault; };
    if (all || section == u"desktop") {
        QJsonObject desktop = def.desktop;
        desktop[u"mode"_s] = integration::normalizeDesktop(current.desktop).value(u"mode"_s); // the real state of Plasma, not a preference
        d.desktop = desktop;
        d.hudAnimation = def.hudAnimation;
    }
    if (all || section == u"conversation") {
        d.language = def.language;
        d.replyLanguage = def.replyLanguage;
        d.notifyReplies = def.notifyReplies;
        d.snoozeMinutes = def.snoozeMinutes;
        d.morningHour = def.morningHour;
        d.afternoonHour = def.afternoonHour;
        d.eveningHour = def.eveningHour;
        d.reminderSound = def.reminderSound;
        d.nagMinutes = def.nagMinutes;
    }
    if (all || section == u"intelligence") {
        d.model = def.model;
        d.webSearch = lower(current.webSearch, def.webSearch);
    }
    if (all || section == u"privacy") {
        d.learnDialog = lower(current.learnDialog, def.learnDialog);
        d.curiosity = lower(current.curiosity, def.curiosity);
        d.deepReflection = lower(current.deepReflection, def.deepReflection);
        d.trackActivity = lower(current.trackActivity, def.trackActivity);
        d.trackTitles = lower(current.trackTitles, def.trackTitles);
        d.shareActivity = lower(current.shareActivity, def.shareActivity);
        d.awarenessWindow = lower(current.awarenessWindow, def.awarenessWindow);
        d.awarenessScreen = lower(current.awarenessScreen, def.awarenessScreen);
        d.awarenessCloudImages = lower(current.awarenessCloudImages, def.awarenessCloudImages);
        d.screenIntervalMinutes = current.screenIntervalMinutes == 0 ? 0 : def.screenIntervalMinutes;
        // awarenessExcluded stays: removing an exclusion would widen observation.
    }
    if (all || section == u"actions") {
        const QStringList levels = awarenessLevels();
        d.awarenessLevel = levels.at(std::min(levels.indexOf(current.awarenessLevel), levels.indexOf(def.awarenessLevel)));
        d.suggestionCooldownMinutes = std::max(current.suggestionCooldownMinutes, def.suggestionCooldownMinutes);
        d.suggestionsPerDay = std::min(current.suggestionsPerDay, def.suggestionsPerDay);
        d.awarenessFullscreen = lower(current.awarenessFullscreen, def.awarenessFullscreen);
        d.proactive = lower(current.proactive, def.proactive);
        d.breakMinutes = current.breakMinutes == 0 ? 0 : def.breakMinutes;
        d.morningSummary = lower(current.morningSummary, def.morningSummary);
        // awarenessMuted stays: unmuting would bring back offers the user turned off.
    }
    if (all || section == u"autonomy") {
        // "Only local" sends less: a reset keeps it. Permissions only get stricter.
        d.agentMode = current.agentMode == u"local" ? current.agentMode : def.agentMode;
        d.agentCloud = lower(current.agentCloud, def.agentCloud);
        d.agentDevice = def.agentDevice;
        d.agentKeepMinutes = def.agentKeepMinutes;
        static const QStringList strictness{u"off"_s, u"ask"_s, u"allow"_s};
        QJsonObject permissions;
        for (auto it = current.agentPermissions.begin(); it != current.agentPermissions.end(); ++it) {
            const QString byDefault = it.key() == u"apps.close" ? u"ask"_s : u"allow"_s;
            const QString kept = strictness.at(std::min(int(strictness.indexOf(it.value().toString())), int(strictness.indexOf(byDefault))));
            if (kept != byDefault)
                permissions.insert(it.key(), kept);
        }
        d.agentPermissions = permissions;
        d.agentMaxSteps = std::min(current.agentMaxSteps, def.agentMaxSteps);
        d.agentMaxSeconds = std::min(current.agentMaxSeconds, def.agentMaxSeconds);
    }
    if (all || section == u"system") {
        d.background = def.background;
        d.desktopIntegration = lower(current.desktopIntegration, def.desktopIntegration);
        d.shareActiveApp = lower(current.shareActiveApp, def.shareActiveApp);
    }
    return d;
}

QStringList Config::changedKeys(const ConfigData &a, const ConfigData &b)
{
    QStringList out;
    const QJsonObject x = toJson(a), y = toJson(b);
    for (auto it = x.begin(); it != x.end(); ++it) {
        if (it.key() == u"api_key")
            continue;
        if (it.key() == u"desktop") {
            const QJsonObject dx = it.value().toObject(), dy = y.value(it.key()).toObject();
            for (auto d = dx.begin(); d != dx.end(); ++d)
                if (d.value() != dy.value(d.key()))
                    out << u"desktop."_s + d.key();
        } else if (it.value() != y.value(it.key())) {
            out << it.key();
        }
    }
    return out;
}

QString Config::keyHint(const QString &key)
{
    if (key.size() <= 8)
        return {};
    return QChar(0x2026) + key.right(4);
}

} // namespace jarvis
