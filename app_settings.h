#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "config.h"

// What the settings dialog sees: whether a key is stored, a masked hint of it,
// and the model name. Reads and writes ~/.config/jarvis/config.json.
class AppSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool hasKey READ hasKey NOTIFY changed FINAL)
    Q_PROPERTY(QString keyHint READ keyHint NOTIFY changed FINAL)
    Q_PROPERTY(QString model READ model NOTIFY changed FINAL)
    Q_PROPERTY(QString configPath READ configPath CONSTANT FINAL)
    // "auto", "ru" or "en".
    Q_PROPERTY(QString language READ language NOTIFY changed FINAL)
    // The language actually in effect for the interface: "ru" or "en".
    Q_PROPERTY(QString uiLanguage READ uiLanguage NOTIFY changed FINAL)
    Q_PROPERTY(QString replyLanguage READ replyLanguage NOTIFY changed FINAL)
    Q_PROPERTY(bool learnDialog READ learnDialog NOTIFY changed FINAL)
    Q_PROPERTY(bool trackActivity READ trackActivity NOTIFY changed FINAL)
    Q_PROPERTY(bool trackTitles READ trackTitles NOTIFY changed FINAL)
    Q_PROPERTY(bool shareActivity READ shareActivity NOTIFY changed FINAL)

public:
    explicit AppSettings(QObject *parent = nullptr);

    bool hasKey() const noexcept { return m_hasKey; }
    QString keyHint() const { return m_keyHint; }
    QString model() const { return m_model; }
    QString configPath() const;
    QString language() const { return m_data.language; }
    QString uiLanguage() const;
    QString replyLanguage() const { return m_data.replyLanguage; }
    bool learnDialog() const { return m_data.learnDialog; }
    bool trackActivity() const { return m_data.trackActivity; }
    bool trackTitles() const { return m_data.trackTitles; }
    bool shareActivity() const { return m_data.shareActivity; }

    // An empty `apiKey` keeps the stored key. Both return "" on success,
    // otherwise a message for the user.
    Q_INVOKABLE QString save(const QString &apiKey, const QString &model);
    Q_INVOKABLE QString removeKey();
    // Applies the interface language immediately (retranslates QML).
    Q_INVOKABLE QString setLanguage(const QString &language);
    Q_INVOKABLE QString setReplyLanguage(const QString &language);
    // name: learnDialog, trackActivity, trackTitles, shareActivity.
    Q_INVOKABLE QString setOption(const QString &name, bool value);

signals:
    void changed();
    void saved();

private:
    void refresh(bool notify);
    QString store(const jarvis::ConfigData &data);

    bool m_hasKey = false;
    QString m_keyHint;
    QString m_model;
    jarvis::ConfigData m_data;
};
