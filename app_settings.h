#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
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
    // Cognitive awareness: level (off, manual, assistive, proactive) and the
    // separate permissions; see ConfigData.
    Q_PROPERTY(QString awarenessLevel READ awarenessLevel NOTIFY changed FINAL)
    Q_PROPERTY(bool awarenessWindow READ awarenessWindow NOTIFY changed FINAL)
    Q_PROPERTY(bool awarenessScreen READ awarenessScreen NOTIFY changed FINAL)
    Q_PROPERTY(bool awarenessCloudImages READ awarenessCloudImages NOTIFY changed FINAL)
    Q_PROPERTY(int screenIntervalMinutes READ screenIntervalMinutes NOTIFY changed FINAL)
    Q_PROPERTY(QStringList awarenessMuted READ awarenessMuted NOTIFY changed FINAL)
    Q_PROPERTY(QStringList awarenessExcluded READ awarenessExcluded NOTIFY changed FINAL)
    Q_PROPERTY(int suggestionCooldownMinutes READ suggestionCooldownMinutes NOTIFY changed FINAL)
    Q_PROPERTY(int suggestionsPerDay READ suggestionsPerDay NOTIFY changed FINAL)
    Q_PROPERTY(bool awarenessFullscreen READ awarenessFullscreen NOTIFY changed FINAL)
    Q_PROPERTY(bool webSearch READ webSearch NOTIFY changed FINAL)
    // Desktop integration (0.19), see ConfigData.
    Q_PROPERTY(bool background READ background NOTIFY changed FINAL)
    Q_PROPERTY(bool desktopIntegration READ desktopIntegration NOTIFY changed FINAL)
    Q_PROPERTY(bool shareActiveApp READ shareActiveApp NOTIFY changed FINAL)
    Q_PROPERTY(QString hudAnimation READ hudAnimation NOTIFY changed FINAL)
    // JARVIS DESKTOP (0.19.2): theme, layout and quality of the desktop shell (see integration::normalizeDesktop).
    Q_PROPERTY(QVariantMap desktop READ desktop NOTIFY changed FINAL)
    // The cinematic HUD, kept per user in Jarvis.conf (not in config.json: the
    // daemon has nothing to do with it). Quality: off (classic window), low,
    // balanced, cinematic. memoryOpen: the memory panel beside the HUD.
    // overlay: the compact always-on-top core.
    Q_PROPERTY(QString hudQuality READ hudQuality WRITE setHudQuality NOTIFY hudChanged FINAL)
    Q_PROPERTY(bool memoryOpen READ memoryOpen WRITE setMemoryOpen NOTIFY hudChanged FINAL)
    Q_PROPERTY(bool overlay READ overlay WRITE setOverlay NOTIFY hudChanged FINAL)

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
    QString awarenessLevel() const { return m_data.awarenessLevel; }
    bool awarenessWindow() const { return m_data.awarenessWindow; }
    bool awarenessScreen() const { return m_data.awarenessScreen; }
    bool awarenessCloudImages() const { return m_data.awarenessCloudImages; }
    int screenIntervalMinutes() const { return m_data.screenIntervalMinutes; }
    QStringList awarenessMuted() const { return m_data.awarenessMuted; }
    QStringList awarenessExcluded() const { return m_data.awarenessExcluded; }
    int suggestionCooldownMinutes() const { return m_data.suggestionCooldownMinutes; }
    int suggestionsPerDay() const { return m_data.suggestionsPerDay; }
    bool awarenessFullscreen() const { return m_data.awarenessFullscreen; }
    bool webSearch() const { return m_data.webSearch; }
    bool background() const { return m_data.background; }
    bool desktopIntegration() const { return m_data.desktopIntegration; }
    bool shareActiveApp() const { return m_data.shareActiveApp; }
    QString hudAnimation() const { return m_data.hudAnimation; }
    QVariantMap desktop() const { return m_data.desktop.toVariantMap(); }
    QString hudQuality() const;
    void setHudQuality(const QString &quality);
    bool memoryOpen() const;
    void setMemoryOpen(bool open);
    bool overlay() const;
    void setOverlay(bool on);

    // An empty `apiKey` keeps the stored key. Both return "" on success,
    // otherwise a message for the user.
    Q_INVOKABLE QString save(const QString &apiKey, const QString &model);
    Q_INVOKABLE QString removeKey();
    // Applies the interface language immediately (retranslates QML).
    Q_INVOKABLE QString setLanguage(const QString &language);
    Q_INVOKABLE QString setReplyLanguage(const QString &language);
    // name: learnDialog, curiosity, trackActivity, trackTitles, shareActivity,
    // proactive, morningSummary, deepReflection, notifyReplies, background,
    // desktopIntegration, shareActiveApp.
    Q_INVOKABLE QString setOption(const QString &name, bool value);
    // The desktop HUD's animations: off, low, normal or high.
    Q_INVOKABLE QString setHudAnimation(const QString &level);
    // One desktop shell setting (not the mode: DesktopShell changes that).
    Q_INVOKABLE QString setDesktop(const QString &key, const QVariant &value);
    // Re-reads the file (after DesktopShell changed the mode).
    Q_INVOKABLE void reload() { refresh(true); }
    // 0 turns break reminders off.
    Q_INVOKABLE QString setBreakMinutes(int minutes);
    // name: snoozeMinutes, morningHour, afternoonHour, eveningHour, nagMinutes,
    // screenIntervalMinutes, suggestionCooldownMinutes, suggestionsPerDay.
    Q_INVOKABLE QString setNumber(const QString &name, int value);
    // off, manual, assistive or proactive.
    Q_INVOKABLE QString setAwarenessLevel(const QString &level);
    // name: awarenessMuted (suggestion categories) or awarenessExcluded (application ids).
    Q_INVOKABLE QString setList(const QString &name, const QStringList &values);

signals:
    void changed();
    void saved();
    // The interface language was switched (after QML retranslation).
    void languageApplied();
    void hudChanged();

private:
    void refresh(bool notify);
    QString store(const jarvis::ConfigData &data);

    bool m_hasKey = false;
    QString m_keyHint;
    QString m_model;
    jarvis::ConfigData m_data;
};
