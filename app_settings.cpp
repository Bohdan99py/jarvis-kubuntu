#include "app_settings.h"

#include <QQmlEngine>
#include <QRegularExpression>

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
    else if (name == QLatin1String("trackActivity"))
        d.trackActivity = value;
    else if (name == QLatin1String("trackTitles"))
        d.trackTitles = value;
    else if (name == QLatin1String("shareActivity"))
        d.shareActivity = value;
    else
        return tr("Unknown option.");
    return store(d);
}
