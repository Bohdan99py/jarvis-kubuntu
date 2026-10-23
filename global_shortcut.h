#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>

// A system-wide shortcut (Ctrl+J by default) through the desktop portal
// org.freedesktop.portal.GlobalShortcuts: KDE Plasma (X11 and Wayland) asks the
// user once to confirm it, keeps it in System Settings → Shortcuts where it can
// be changed, and tells Jarvis when it is pressed. Jarvis never sees any other
// key: there is no keyboard grab and no input monitoring.
//
// The portal calls run on their own bus connection, registered with the
// portal under the application id org.jarvis.Jarvis (xdg-desktop-portal 1.19+),
// so the shortcut belongs to Jarvis even when systemd starts it.
class GlobalShortcut : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool bound READ bound NOTIFY changed)
    // How the desktop describes the key, e.g. "Ctrl+J"; "" while not bound.
    Q_PROPERTY(QString trigger READ trigger NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    // The desktop can open its own dialog to change the key (portal version 2).
    Q_PROPERTY(bool configurable READ configurable NOTIFY changed)

public:
    static constexpr const char *kId = "overlay";
    static constexpr const char *kPreferred = "CTRL+j";

    explicit GlobalShortcut(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool bound() const { return !m_trigger.isEmpty(); }
    QString trigger() const { return m_trigger; }
    QString status() const { return m_status; }
    bool configurable() const { return m_version >= 2 && !m_session.isEmpty(); }

    // Asks the desktop to bind the shortcut (it shows a confirmation dialog).
    Q_INVOKABLE void bind();
    // Opens the desktop's dialog to change the key (or the Shortcuts settings).
    Q_INVOKABLE void configure();

signals:
    void changed();
    // The shortcut was pressed. `token`: an XDG activation token, when the desktop gave one.
    void activated(const QString &token);

private slots:
    void onResponse(const QDBusMessage &message);
    void onActivated(const QDBusMessage &message);

private:
    using Callback = std::function<void(uint, const QVariantMap &)>;
    void start();
    void createSession();
    void listShortcuts();
    void request(const QString &method, QVariantList args, QVariantMap options, const Callback &done);
    void takeShortcuts(const QVariantMap &results);
    void setStatus(const QString &status);

    QDBusConnection m_bus;
    QHash<QString, Callback> m_pending; // request object path -> what to do with its Response
    QString m_session;
    QString m_trigger;
    QString m_status;
    uint m_version = 0;
    int m_tokens = 0;
    bool m_available = false;
    bool m_binding = false;
};
