#include "app_settings.h"
#include "desktop_integration.h"
#include <QJsonValue>

#include <QQmlEngine>
#include <QRegularExpression>
#include <QSettings>

#include <algorithm>

#include "config.h"
#include "language.h"

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
{
    refresh(false);
}

QString AppSettings::configPath() const
{
    return jarvis::Config::filePath();
}

void AppSettings::refresh(bool notify)
{
    const jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    m_hasKey = !d.apiKey.isEmpty();
    m_keyHint = jarvis::Config::keyHint(d.apiKey);
    m_model = d.model;
    m_data = d;
    if (notify)
        emit changed();
}

QString AppSettings::save(const QString &apiKey, const QString &model)
{
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();

    const QString key = apiKey.trimmed();
    if (!key.isEmpty()) {
        static const QRegularExpression whitespace(QStringLiteral("\\s"));
        if (key.contains(whitespace))
            return tr("The key must not contain spaces or line breaks.");
        d.apiKey = key;
    }

    const QString m = model.trimmed();
    d.model = m.isEmpty() ? jarvis::Config::defaultModel() : m;

    QString error;
    if (!jarvis::Config::save(d, &error))
        return error;

    refresh(true);
    emit saved();
    return {};
}

QString AppSettings::removeKey()
{
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    d.apiKey.clear();

    QString error;
    if (!jarvis::Config::save(d, &error))
        return error;

    refresh(true);
    emit saved();
    return {};
}

QString AppSettings::uiLanguage() const
{
    return jarvis::languageCode(jarvis::resolveLanguage(m_data.language));
}

QString AppSettings::store(const jarvis::ConfigData &data)
{
    QString error;
    if (!jarvis::Config::save(data, &error))
        return error;
    refresh(true);
    emit saved();
    return {};
}

QString AppSettings::setLanguage(const QString &language)
{
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    d.language = (language == QLatin1String("ru") || language == QLatin1String("en")) ? language : QStringLiteral("auto");
    const QString error = store(d);
    if (!error.isEmpty())
        return error;
    jarvis::applyUiLanguage(d.language);
    if (QQmlEngine *engine = qmlEngine(this))
        engine->retranslate();
    emit languageApplied();
    return {};
}

QString AppSettings::setReplyLanguage(const QString &language)
{
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    d.replyLanguage = (language == QLatin1String("ru") || language == QLatin1String("en")) ? language : QStringLiteral("auto");
    return store(d);
}

QString AppSettings::setOption(const QString &name, bool value)
{
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    if (name == QLatin1String("learnDialog"))
        d.learnDialog = value;
    else if (name == QLatin1String("curiosity"))
        d.curiosity = value;
    else if (name == QLatin1String("trackActivity"))
        d.trackActivity = value;
    else if (name == QLatin1String("trackTitles"))
        d.trackTitles = value;
    else if (name == QLatin1String("shareActivity"))
        d.shareActivity = value;
    else if (name == QLatin1String("proactive"))
        d.proactive = value;
    else if (name == QLatin1String("morningSummary"))
        d.morningSummary = value;
    else if (name == QLatin1String("deepReflection"))
        d.deepReflection = value;
    else if (name == QLatin1String("notifyReplies"))
        d.notifyReplies = value;
    else if (name == QLatin1String("reminderSound"))
        d.reminderSound = value;
    else if (name == QLatin1String("awarenessWindow"))
        d.awarenessWindow = value;
    else if (name == QLatin1String("awarenessScreen")) {
        d.awarenessScreen = value;
        if (!value)
            d.awarenessCloudImages = false; // no screen, nothing to send
    } else if (name == QLatin1String("awarenessCloudImages"))
        d.awarenessCloudImages = value && d.awarenessScreen;
    else if (name == QLatin1String("awarenessFullscreen"))
        d.awarenessFullscreen = value;
    else if (name == QLatin1String("webSearch"))
        d.webSearch = value;
    else if (name == QLatin1String("background"))
        d.background = value;
    else if (name == QLatin1String("desktopIntegration")) {
        d.desktopIntegration = value;
        if (!value)
            d.shareActiveApp = false; // nothing to share through
    } else if (name == QLatin1String("shareActiveApp"))
        d.shareActiveApp = value && d.desktopIntegration;
    else
        return tr("Unknown option.");
    return store(d);
}

QString AppSettings::setDesktop(const QString &key, const QVariant &value)
{
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    const QString error = jarvis::integration::setDesktopValue(d.desktop, key, QJsonValue::fromVariant(value));
    if (!error.isEmpty())
        return error == u"unknown setting" ? tr("Unknown option.") : tr("This value is not allowed.");
    return store(d);
}

QString AppSettings::setHudAnimation(const QString &level)
{
    if (!jarvis::Config::hudAnimationLevels().contains(level))
        return tr("Unknown option.");
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    d.hudAnimation = level;
    return store(d);
}

QString AppSettings::setBreakMinutes(int minutes)
{
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    d.breakMinutes = std::clamp(minutes, 0, 600);
    return store(d);
}

QString AppSettings::setNumber(const QString &name, int value)
{
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    if (name == QLatin1String("snoozeMinutes"))
        d.snoozeMinutes = std::clamp(value, 1, 720);
    else if (name == QLatin1String("morningHour"))
        d.morningHour = std::clamp(value, 4, 12);
    else if (name == QLatin1String("afternoonHour"))
        d.afternoonHour = std::clamp(value, 11, 18);
    else if (name == QLatin1String("eveningHour"))
        d.eveningHour = std::clamp(value, 16, 23);
    else if (name == QLatin1String("nagMinutes"))
        d.nagMinutes = std::clamp(value, 0, 120);
    else if (name == QLatin1String("screenIntervalMinutes"))
        d.screenIntervalMinutes = std::clamp(value, 0, 60);
    else if (name == QLatin1String("suggestionCooldownMinutes"))
        d.suggestionCooldownMinutes = std::clamp(value, 10, 24 * 60);
    else if (name == QLatin1String("suggestionsPerDay"))
        d.suggestionsPerDay = std::clamp(value, 1, 20);
    else
        return tr("Unknown option.");
    return store(d);
}

QString AppSettings::setAwarenessLevel(const QString &level)
{
    if (!jarvis::Config::awarenessLevels().contains(level))
        return tr("Unknown option.");
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    d.awarenessLevel = level;
    return store(d);
}

QString AppSettings::setList(const QString &name, const QStringList &values)
{
    QStringList clean;
    for (const QString &v : values) {
        const QString item = v.trimmed().toLower().left(120);
        if (!item.isEmpty() && !clean.contains(item))
            clean.append(item);
    }
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    if (name == QLatin1String("awarenessMuted"))
        d.awarenessMuted = clean.mid(0, 20);
    else if (name == QLatin1String("awarenessExcluded"))
        d.awarenessExcluded = clean.mid(0, 100);
    else
        return tr("Unknown option.");
    return store(d);
}

QString AppSettings::hudQuality() const
{
    const QString q = QSettings().value(QStringLiteral("hud/quality"), QStringLiteral("balanced")).toString();
    static const QStringList known{QStringLiteral("off"), QStringLiteral("low"), QStringLiteral("balanced"), QStringLiteral("cinematic")};
    return known.contains(q) ? q : QStringLiteral("balanced");
}

void AppSettings::setHudQuality(const QString &quality)
{
    static const QStringList known{QStringLiteral("off"), QStringLiteral("low"), QStringLiteral("balanced"), QStringLiteral("cinematic")};
    if (!known.contains(quality) || quality == hudQuality())
        return;
    QSettings().setValue(QStringLiteral("hud/quality"), quality);
    emit hudChanged();
}

bool AppSettings::memoryOpen() const
{
    return QSettings().value(QStringLiteral("hud/memoryOpen"), false).toBool();
}

void AppSettings::setMemoryOpen(bool open)
{
    if (open == memoryOpen())
        return;
    QSettings().setValue(QStringLiteral("hud/memoryOpen"), open);
    emit hudChanged();
}

bool AppSettings::overlay() const
{
    return QSettings().value(QStringLiteral("hud/overlay"), false).toBool();
}

void AppSettings::setOverlay(bool on)
{
    if (on == overlay())
        return;
    QSettings().setValue(QStringLiteral("hud/overlay"), on);
    emit hudChanged();
}
