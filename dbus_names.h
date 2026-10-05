#pragma once

#include <QLatin1String>

// One place for the D-Bus identity of the daemon (session bus).
namespace jarvis::dbus {

inline constexpr QLatin1String kService{"org.jarvis.Daemon1"};
inline constexpr QLatin1String kPath{"/org/jarvis/Daemon1"};
inline constexpr QLatin1String kInterface{"org.jarvis.Daemon1"};

} // namespace jarvis::dbus
