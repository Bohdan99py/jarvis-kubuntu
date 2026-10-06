#include "app_settings.h"

#include <QRegularExpression>

#include "config.h"

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
            return tr("В ключе не должно быть пробелов и переносов строк.");
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
