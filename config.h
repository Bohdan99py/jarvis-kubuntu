#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace jarvis {

struct ConfigData
{
    QString apiKey;
    QString model;
    // "auto" follows the system locale; otherwise "ru" or "en".
    QString language = QStringLiteral("auto");
    // "auto" answers in the language of each message; otherwise "ru" or "en".
    QString replyLanguage = QStringLiteral("auto");
    // Learn facts and topics from the conversation itself.
    bool learnDialog = true;
    // Jarvis asks questions now and then to learn about the user.
    bool curiosity = true;
    // Opt-in: watch the focused window through KWin.
    bool trackActivity = false;
    // Opt-in on top of trackActivity: keep window titles, not only app names.
    bool trackTitles = false;
    // Opt-in: add the current activity summary to Claude requests.
    bool shareActivity = false;
    // Jarvis speaks up by itself in desktop notifications: breaks, words to
    // review, curious questions, insights. Reminders are always delivered.
    bool proactive = true;
    // Suggest a break after this many minutes at the desk; 0 = never.
    int breakMinutes = 90;
    // A short plan for the day with the first activity in the morning.
    bool morningSummary = true;
    // Once a day Claude reviews long-term memory: merges duplicates, drops
    // outdated facts and notes goals and habits.
    bool deepReflection = true;
    // A notification with the answer when the window is hidden (GUI only).
    bool notifyReplies = true;
    // Reminders: the "Snooze" button, what "утром/днём/вечером" mean, a sound,
    // and asking again (every N minutes, up to 3 times) until "Done" is pressed; 0 = once.
    int snoozeMinutes = 10;
    int morningHour = 9;
    int afternoonHour = 14;
    int eveningHour = 19;
    bool reminderSound = true;
    int nagMinutes = 0;

    // ---- Cognitive awareness (0.18) ----
    // How much Jarvis does with what it observes: "off" (nothing), "manual"
    // (context only when the user asks, e.g. from the Ctrl+J overlay),
    // "assistive" (also rare suggestions backed by strong evidence) or
    // "proactive" (suggestions with more kinds of evidence).
    QString awarenessLevel = QStringLiteral("manual");
    // Separate permissions: the focused window's application and title (through
    // the KWin script), the content of the screen (screenshots analysed on this
    // computer) and sending a screenshot to Claude.
    bool awarenessWindow = false;
    bool awarenessScreen = false;
    bool awarenessCloudImages = false;
    // Background screen analysis (assistive/proactive with screen permission):
    // at most every N minutes; 0 = only when asked.
    int screenIntervalMinutes = 0;
    // Suggestion categories switched off: build, electronics, driving, docs, study.
    QStringList awarenessMuted;
    // Applications never observed (desktop ids or window classes, lower case).
    QStringList awarenessExcluded;
    int suggestionCooldownMinutes = 45;
    int suggestionsPerDay = 4;
    // Interrupt fullscreen applications (games, presentations, films).
    bool awarenessFullscreen = false;
    // Claude may search the web (Anthropic's web search tool) for current facts:
    // traffic law, racing results and calendars. Billed per search.
    bool webSearch = false;

    // ---- Desktop integration (0.19) ----
    // jarvisd keeps working (reminders, hints, KRunner, the desktop widget) when
    // the Jarvis window is closed or quit; off: it ends with the window.
    bool background = true;
    // The KDE bridge: org.jarvis.Integration1 actions and notifications, KRunner
    // and the desktop widget. Off: they only report that the integration is off.
    bool desktopIntegration = true;
    // Opt-in: desktop widgets may ask which application is focused (its name,
    // never the title); needs window awareness as well.
    bool shareActiveApp = false;
    // The desktop HUD's animations: off, low, normal, high (one step lower on battery).
    QString hudAnimation = QStringLiteral("normal");
    // ---- JARVIS DESKTOP (0.19.2) ----
    // Mode, theme and layout of the desktop shell; normalised by
    // integration::normalizeDesktop (see there for the keys and limits).
    QJsonObject desktop;

    // ---- Autonomy (0.22) ----
    // "auto": everyday system tasks run locally, the cloud as the other
    // settings allow; "local": nothing is sent to any cloud model (Claude,
    // ElevenLabs), whether or not there is a network.
    QString agentMode = QStringLiteral("auto");
    // auto mode: Claude may plan a system task the local model cannot (needs a key).
    bool agentCloud = false;
    // The local model (models.json id), where it runs (auto, cpu) and how long it
    // stays loaded after the last request.
    QString agentModel = QStringLiteral("qwen3.5-4b");
    QString agentDevice = QStringLiteral("auto");
    int agentKeepMinutes = 10;
    // Tool category -> allow, ask, off (agent_tools.h: categories()).
    QJsonObject agentPermissions;
    // At most this many steps and seconds per task.
    int agentMaxSteps = 6;
    int agentMaxSeconds = 60;
};

// ~/.config/jarvis/config.json, directory 0700, file 0600.
// The key never travels over D-Bus: the GUI stores it, then tells the daemon to
// reload. Since 0.21 the key lives in the desktop's password store
// (SecretStore); the file keeps it only where there is none.
class Config
{
public:
    // 2: 0.21 — keys in the password store, `config_version` written.
    static constexpr int kVersion = 2;
    // Where the Claude key came from.
    enum class KeyStore { None, Environment, Keyring, Locked, File };

    static QString defaultModel();
    static QString filePath();
    // off, manual, assistive, proactive.
    static QStringList awarenessLevels();
    // off, low, normal, high.
    static QStringList hudAnimationLevels();
    // auto, local.
    static QStringList agentModes();

    // File contents only (used by the settings UI so an environment key is
    // never copied into the file).
    static ConfigData loadFileOnly();
    // File contents, with ANTHROPIC_API_KEY from the environment taking priority.
    static ConfigData load();

    // Keeps fields it does not know (from another version) and never writes
    // over a damaged file: that is moved aside first (config.json.damaged-…).
    static bool save(const ConfigData &data, QString *error);

    // ANTHROPIC_API_KEY, then the password store, then the file.
    static QString anthropicKey(KeyStore *where = nullptr);
    // Stores (or, empty, removes) the key: in the password store when there is
    // one (then the file no longer holds it), otherwise in the file. "" or why not.
    static QString setAnthropicKey(const QString &key, KeyStore *where = nullptr);

    // Upgrades the file once (idempotent, safe to run from the window and the
    // service at the same time): a backup (config.json.backup-v1, secrets left
    // out), damaged files moved aside, keys moved into the password store,
    // config_version written. Returns what changed, for the log; "" if nothing.
    static QString migrate();
    // Before installing an older Jarvis: writes the keys back into the files it
    // reads (config.json, speech.json) and marks the file version 1.
    static QString prepareDowngrade(QString *error);
    static QString backupPath();
    // speech.json of the voice settings (ElevenLabs key before 0.21).
    static QString speechPath();

    // ---- Reset (0.21) ----
    static ConfigData defaults();
    // desktop, conversation, intelligence, autonomy, privacy, actions, system, all.
    static QStringList sections();
    // `current` with one section back to its defaults. A reset never switches a
    // permission on, never widens observation and never removes an exclusion:
    // where the default allows more than the current value, the value stays.
    static ConfigData resetSection(const ConfigData &current, const QString &section);
    // The config.json keys whose values differ ("desktop.theme" for the desktop object).
    static QStringList changedKeys(const ConfigData &a, const ConfigData &b);

    // "…abcd" — enough to recognise a key without revealing it.
    static QString keyHint(const QString &key);
};

} // namespace jarvis
