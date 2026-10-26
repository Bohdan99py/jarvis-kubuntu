#pragma once

#include <QString>

namespace jarvis {

// API keys in the desktop's password store (0.21): the freedesktop Secret
// Service (org.freedesktop.secrets: KWallet's ksecretd on Plasma 6, kwalletd5
// or GNOME Keyring elsewhere), spoken directly over D-Bus, so Jarvis needs no
// extra library. Items carry {application: org.jarvis.Jarvis, account: <name>}.
//
// Reading never shows a dialog: a locked store reports Locked and the settings
// window offers to unlock it (the store's own password prompt; Jarvis never
// sees that password). Writing and deleting may show the store's prompt.
// JARVIS_SECRET_SERVICE=off turns the store off (the tests use it, so they
// never touch the user's real keyring).
class SecretStore
{
public:
    enum class State { Found, Missing, Locked, Unavailable, Error };

    static constexpr const char *kAnthropic = "anthropic-api-key";
    static constexpr const char *kElevenLabs = "elevenlabs-api-key";

    // Is there a Secret Service with a default collection on the session bus?
    static bool available();
    static QString read(const QString &account, State *state = nullptr);
    // Replaces the item. `label` is what the password manager lists.
    static bool write(const QString &account, const QString &label, const QString &secret, QString *error);
    // Missing is fine; true when no item is left.
    static bool remove(const QString &account, QString *error);
    // The store's own unlock prompt, for a Locked item; waits for the answer.
    static bool unlock(const QString &account, QString *error);
};

} // namespace jarvis
