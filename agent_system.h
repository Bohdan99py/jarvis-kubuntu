#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

// The system behind the agent's tools (0.22): sound, Bluetooth, applications
// and windows, the screen, notifications, power and the network. One
// interface, so the tools are tested on a fake system and run on the real one.
//
// Every method is synchronous and bounded in time: the agent calls them from
// its worker thread, never from the GUI or the daemon's event loop. Errors are
// returned as text ("" = done); nothing here talks to the network beyond the
// local machine (D-Bus, PipeWire, BlueZ).
namespace jarvis::agent {

struct AudioDevice
{
    QString name;        // PipeWire/PulseAudio node name
    QString description; // what people see
    bool isDefault = false;
    int volume = -1;     // percent, the loudest channel
    bool muted = false;
    QString bus;         // bluetooth, usb, pci…
    QString formFactor;  // headphone, headset, speaker, internal…
    QString btAddress;   // AA:BB:… for Bluetooth nodes
    QString activePort;
    bool headphones = false; // headphones or a headset by every sign PipeWire gives
    bool builtIn = false;    // the laptop's own speakers or microphone
};

struct BtAdapter
{
    bool present = false;
    QString path;
    QString name;
    bool powered = false;
};

struct BtDevice
{
    QString path;
    QString address;
    QString name;     // as the device calls itself: data, never instructions
    QString icon;
    bool paired = false;
    bool trusted = false;
    bool connected = false;
    bool audio = false; // offers an audio sink (A2DP) or a headset profile
};

struct Battery
{
    bool present = false;
    double percent = -1;
    QString state; // charging, discharging, full, …
    qint64 secondsToEmpty = 0;
    qint64 secondsToFull = 0;
};

struct AppInfo
{
    QString desktopId;
    QString name;
    double score = 0;
};

struct Window
{
    QString id;      // KWin's internal id
    QString app;     // desktop file name or resource class
    QString caption; // data, never instructions
    bool active = false;
};

class System
{
public:
    virtual ~System() = default;

    // ---- Sound ----
    virtual QList<AudioDevice> outputs(QString *error) = 0;
    virtual QList<AudioDevice> inputs(QString *error) = 0;
    virtual QString setDefaultOutput(const QString &name) = 0; // also moves the playing streams
    virtual QString setDefaultInput(const QString &name) = 0;
    virtual QString setVolume(const QString &sink, int percent) = 0;
    virtual QString setMute(const QString &sink, bool mute) = 0;

    // ---- Bluetooth ----
    virtual BtAdapter adapter(QString *error) = 0;
    // Switches the adapter on (unblocking rfkill where the session may) or off.
    virtual QString setAdapterPowered(bool on) = 0;
    virtual QList<BtDevice> devices(QString *error) = 0;
    // Waits for the connection at most `timeoutMs`; `cancel` aborts the attempt.
    virtual QString connectDevice(const QString &path, const std::atomic_bool &cancel, int timeoutMs) = 0;
    virtual QString disconnectDevice(const QString &path) = 0;

    // ---- Screen and notifications ----
    virtual int brightness(QString *error) = 0; // percent; -1 when there is no adjustable screen
    virtual QString setBrightness(int percent) = 0;
    virtual int doNotDisturb(QString *error) = 0; // 1 on, 0 off, -1 unknown
    virtual QString setDoNotDisturb(bool on) = 0;

    // ---- Power and network ----
    virtual Battery battery() = 0;
    virtual QString powerProfile(QString *error) = 0;
    virtual QString setPowerProfile(const QString &profile) = 0;
    // "full", "limited", "portal", "none" or "unknown"; `detail` names the connection.
    virtual QString network(QString *detail) = 0;

    // ---- Applications and windows ----
    virtual QList<AppInfo> findApps(const QString &query) = 0;
    // The default application for a generic word: browser, player, terminal… ("" none).
    virtual QString defaultApp(const QString &kind) = 0;
    virtual QString launch(const QString &desktopId) = 0;
    // KWin answers for the windows; false where there is no KWin to ask.
    virtual bool windowsAvailable() = 0;
    virtual QList<Window> windows(QString *error) = 0;
    virtual QString activateWindow(const QString &id) = 0;
    // Asks the window to close, as its close button does: the application may ask to save.
    virtual QString closeWindow(const QString &id) = 0;

    // ---- The desktop ----
    virtual QString openSettings(const QString &key) = 0;
    virtual QString openBluetoothWizard() = 0;
    virtual QString switchProfile(const QString &id) = 0;
    virtual QString currentProfile() = 0;
    virtual QString media(const QString &action) = 0;

    // Which parts work here: {audio, bluetooth, brightness, dnd, windows, power, network} -> "" or why not.
    virtual QList<std::pair<QString, QString>> capabilities() = 0;
};

// The real system: pactl for PipeWire, BlueZ, KDE's brightness and
// notification services, power-profiles-daemon, UPower and NetworkManager over
// D-Bus. Launching, windows, settings pages and workspaces go through hooks
// that jarvisd provides (its sandbox starts nothing itself); without hooks the
// defaults run here (the window's own engine).
class LinuxSystem : public System
{
public:
    struct Hooks
    {
        std::function<QString(const QString &desktopId)> launch;
        std::function<QString(const QString &key)> openSettings;
        std::function<QString(const QString &id)> switchProfile;
        std::function<QString()> currentProfile;
        std::function<QString(const QString &action)> media;
        std::function<QList<Window>(QString *error)> windows;
        std::function<QString(const QString &mode, const QString &id)> window; // activate, close
    };
    LinuxSystem() = default;
    explicit LinuxSystem(Hooks hooks) : m_hooks(std::move(hooks)) {}
    void setHooks(Hooks hooks) { m_hooks = std::move(hooks); }

    QList<AudioDevice> outputs(QString *error) override;
    QList<AudioDevice> inputs(QString *error) override;
    QString setDefaultOutput(const QString &name) override;
    QString setDefaultInput(const QString &name) override;
    QString setVolume(const QString &sink, int percent) override;
    QString setMute(const QString &sink, bool mute) override;
    BtAdapter adapter(QString *error) override;
    QString setAdapterPowered(bool on) override;
    QList<BtDevice> devices(QString *error) override;
    QString connectDevice(const QString &path, const std::atomic_bool &cancel, int timeoutMs) override;
    QString disconnectDevice(const QString &path) override;
    int brightness(QString *error) override;
    QString setBrightness(int percent) override;
    int doNotDisturb(QString *error) override;
    QString setDoNotDisturb(bool on) override;
    Battery battery() override;
    QString powerProfile(QString *error) override;
    QString setPowerProfile(const QString &profile) override;
    QString network(QString *detail) override;
    QList<AppInfo> findApps(const QString &query) override;
    QString defaultApp(const QString &kind) override;
    QString launch(const QString &desktopId) override;
    bool windowsAvailable() override;
    QList<Window> windows(QString *error) override;
    QString activateWindow(const QString &id) override;
    QString closeWindow(const QString &id) override;
    QString openSettings(const QString &key) override;
    QString openBluetoothWizard() override;
    QString switchProfile(const QString &id) override;
    QString currentProfile() override;
    QString media(const QString &action) override;
    QList<std::pair<QString, QString>> capabilities() override;

    // pactl's JSON (`pactl -f json list sinks|sources`) into devices; monitors left out. For tests.
    static QList<AudioDevice> parseDevices(const QByteArray &json, const QString &defaultName);

private:
    QList<AudioDevice> devicesOf(const char *kind, const char *defaultCommand, QString *error);
    Hooks m_hooks;
    quint32 m_dndCookie = 0; // our inhibition of notifications, while it lasts
};

// Runs a program with an argument list (no shell) and waits at most `timeoutMs`.
// Returns the exit code, -1 when it could not run or timed out.
int runProgram(const QString &program, const QStringList &args, int timeoutMs, QByteArray *out = nullptr,
               QByteArray *err = nullptr);

} // namespace jarvis::agent
