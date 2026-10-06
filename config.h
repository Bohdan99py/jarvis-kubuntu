#pragma once

#include <QString>

namespace jarvis {

struct ConfigData
{
    QString apiKey;
    QString model;
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
