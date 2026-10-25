#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// KDE's own settings, from Jarvis's settings window (0.21):
//  * System Settings modules (Sound, Bluetooth, Wi-Fi…): available only when
//    the module is installed here; opened in System Settings, or kcmshell when
//    System Settings is missing. Jarvis does not re-implement them.
//  * global shortcuts through kglobalaccel (org.kde.kglobalaccel): the current
//    key of Jarvis's actions, who else uses a key, and changing a key — only
//    after the conflict check, never taking a key from another action.
class KdeSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // System Settings or kcmshell6/5 exists.
    Q_PROPERTY(bool systemSettings READ systemSettings CONSTANT)

public:
    explicit KdeSettings(QObject *parent = nullptr);

    bool systemSettings() const;

    // Is the module (kcm_pulseaudio, …) installed?
    Q_INVOKABLE bool hasModule(const QString &kcm) const;
    // Opens it; "" or why not.
    Q_INVOKABLE QString openModule(const QString &kcm) const;

    // component/action as kglobalaccel names them (org.jarvis.Jarvis.desktop / Quick).
    // {available, keys: "Meta+J" (native text), portable: "Meta+J"}.
    Q_INVOKABLE QVariantMap shortcut(const QString &component, const QString &action) const;
    // A key pressed in QML (event.key, event.modifiers) as text: "Ctrl+Shift+K"
    // in the interface language; "" for a lone modifier.
    Q_INVOKABLE QString keyText(int key, int modifiers) const;
    Q_INVOKABLE QString portableKey(int key, int modifiers) const;
    // Who uses the key: {taken, self, owner: "KRunner — Show KRunner"}; a key
    // without Ctrl, Alt or Meta (except F-keys) is refused with `invalid`.
    Q_INVOKABLE QVariantMap conflict(int key, int modifiers, const QString &component, const QString &action) const;
    // Sets the key after the conflict check; "" or why not. key 0: no key.
    Q_INVOKABLE QString setShortcut(const QString &component, const QString &action, const QString &componentName,
                                    const QString &actionName, int key, int modifiers) const;
    // While a new key is being pressed, global shortcuts must not fire (Meta+D
    // would show the desktop instead of being recorded) — as KDE's own key
    // recorder does. Always switched back on when the recording ends.
    Q_INVOKABLE void blockGlobalShortcuts(bool block) const;
    // The default of a Jarvis action (Ctrl+J, Meta+J) as {key, modifiers}.
    Q_INVOKABLE QVariantMap defaultKey(const QString &action) const;
};
