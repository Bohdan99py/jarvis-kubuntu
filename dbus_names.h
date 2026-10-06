#pragma once

#include <QLatin1String>
#include <QString>
#include <QtGlobal>

// One place for the D-Bus identity of the daemon (session bus).
namespace jarvis::dbus {

inline constexpr QLatin1String kService{"org.jarvis.Daemon1"};
inline constexpr QLatin1String kPath{"/org/jarvis/Daemon1"};
inline constexpr QLatin1String kInterface{"org.jarvis.Daemon1"};

// The daemon's bus name. JARVIS_DBUS_SERVICE lets a development build run next
// to an installed daemon; clients always use kService.
inline QString daemonService()
{
    const QString name = qEnvironmentVariable("JARVIS_DBUS_SERVICE");
    return name.isEmpty() ? QString(kService) : name;
}

} // namespace jarvis::dbus
