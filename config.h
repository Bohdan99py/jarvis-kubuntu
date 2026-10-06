#pragma once

#include <QString>

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
    // Opt-in: watch the focused window through KWin.
    bool trackActivity = false;
    // Opt-in on top of trackActivity: keep window titles, not only app names.
    bool trackTitles = false;
    // Opt-in: add the current activity summary to Claude requests.
    bool shareActivity = false;
};

// ~/.config/jarvis/config.json, directory 0700, file 0600.
// The key never travels over D-Bus: the GUI writes the file, then tells the
// daemon to reload it.
class Config
{
public:
    static QString defaultModel();
    static QString filePath();

    // File contents only (used by the settings UI so an environment key is
    // never copied into the file).
    static ConfigData loadFileOnly();
    // File contents, with ANTHROPIC_API_KEY from the environment taking priority.
    static ConfigData load();

    static bool save(const ConfigData &data, QString *error);

    // "…abcd" — enough to recognise a key without revealing it.
    static QString keyHint(const QString &key);
};

} // namespace jarvis
