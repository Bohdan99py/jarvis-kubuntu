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
    Q_PROPERTY(bool curiosity READ curiosity NOTIFY changed FINAL)
    Q_PROPERTY(bool trackActivity READ trackActivity NOTIFY changed FINAL)
    Q_PROPERTY(bool trackTitles READ trackTitles NOTIFY changed FINAL)
    Q_PROPERTY(bool shareActivity READ shareActivity NOTIFY changed FINAL)
    Q_PROPERTY(bool proactive READ proactive NOTIFY changed FINAL)
    Q_PROPERTY(int breakMinutes READ breakMinutes NOTIFY changed FINAL)
    Q_PROPERTY(bool morningSummary READ morningSummary NOTIFY changed FINAL)
    Q_PROPERTY(bool deepReflection READ deepReflection NOTIFY changed FINAL)
    Q_PROPERTY(bool notifyReplies READ notifyReplies NOTIFY changed FINAL)
    Q_PROPERTY(int snoozeMinutes READ snoozeMinutes NOTIFY changed FINAL)
    Q_PROPERTY(int morningHour READ morningHour NOTIFY changed FINAL)
    Q_PROPERTY(int afternoonHour READ afternoonHour NOTIFY changed FINAL)
    Q_PROPERTY(int eveningHour READ eveningHour NOTIFY changed FINAL)
    Q_PROPERTY(bool reminderSound READ reminderSound NOTIFY changed FINAL)
    Q_PROPERTY(int nagMinutes READ nagMinutes NOTIFY changed FINAL)

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
    bool curiosity() const { return m_data.curiosity; }
    bool trackActivity() const { return m_data.trackActivity; }
    bool trackTitles() const { return m_data.trackTitles; }
    bool shareActivity() const { return m_data.shareActivity; }
    bool proactive() const { return m_data.proactive; }
    int breakMinutes() const { return m_data.breakMinutes; }
    bool morningSummary() const { return m_data.morningSummary; }
    bool deepReflection() const { return m_data.deepReflection; }
    bool notifyReplies() const { return m_data.notifyReplies; }
    int snoozeMinutes() const { return m_data.snoozeMinutes; }
    int morningHour() const { return m_data.morningHour; }
    int afternoonHour() const { return m_data.afternoonHour; }
    int eveningHour() const { return m_data.eveningHour; }
    bool reminderSound() const { return m_data.reminderSound; }
    int nagMinutes() const { return m_data.nagMinutes; }

    // An empty `apiKey` keeps the stored key. Both return "" on success,
    // otherwise a message for the user.
    Q_INVOKABLE QString save(const QString &apiKey, const QString &model);
    Q_INVOKABLE QString removeKey();
    // Applies the interface language immediately (retranslates QML).
    Q_INVOKABLE QString setLanguage(const QString &language);
    Q_INVOKABLE QString setReplyLanguage(const QString &language);
    // name: learnDialog, curiosity, trackActivity, trackTitles, shareActivity,
    // proactive, morningSummary, deepReflection, notifyReplies.
    Q_INVOKABLE QString setOption(const QString &name, bool value);
    // 0 turns break reminders off.
    Q_INVOKABLE QString setBreakMinutes(int minutes);
    // name: snoozeMinutes, morningHour, afternoonHour, eveningHour, nagMinutes.
    Q_INVOKABLE QString setNumber(const QString &name, int value);

signals:
    void changed();
    void saved();
    // The interface language was switched (after QML retranslation).
    void languageApplied();

private:
    void refresh(bool notify);
    QString store(const jarvis::ConfigData &data);

    bool m_hasKey = false;
    QString m_keyHint;
    QString m_model;
    jarvis::ConfigData m_data;
};
