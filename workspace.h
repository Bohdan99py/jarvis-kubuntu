#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// Workspaces (0.19.2): four profiles — general, study, work, dev — each with a
// KDE virtual desktop, applications to have open, files or projects to open
// when the user asks to "prepare" it, the desktop widgets it shows, the
// awareness categories it cares about and the window layout saved for it.
// Plus the desktop commands the launcher, KRunner, the chat and the voice
// share: switch a workspace, open an application, a folder or a project, show
// a figure, open an allowed settings page. Pure logic; jarvisd does the rest.
namespace jarvis::workspace {

struct Profile
{
    QString id;           // general, study, work, dev
    QString label;        // shown in the dock: GENERAL…
    int desktop = 1;      // KDE virtual desktop, 1-based
    QStringList apps;     // desktop ids; "a|b|c" = the first one installed
    QStringList paths;    // files, folders or projects opened by "prepare"
    QStringList widgets;  // Plasma widget ids shown in this profile
    QStringList focus;    // awareness categories that matter here (build, electronics, driving, docs, study)
    QJsonObject layout;   // desktop id -> {x, y, width, height}, saved by the user
    QString activity;     // Jarvis's suggestions here (0.19.3): "normal", "reduced", "quiet" — only lowers the user's level
};

struct State
{
    QString current = QStringLiteral("general");
    QString previous;
    QList<Profile> profiles;
};

QStringList profileIds();
QList<Profile> defaults();
// ~/.config/jarvis/workspaces.json (0600); missing or broken parts fall back to the defaults.
QString filePath();
State load();
bool save(const State &state, QString *error = nullptr);
Profile find(const State &state, const QString &id);
QJsonObject toJson(const Profile &p);
Profile fromJson(const QString &id, const QJsonObject &o);
// Status for the desktop: {current, previous, profiles: [{id, label, desktop, widgets}]}.
QJsonObject summary(const State &state);

// A desktop command in plain words.
struct Command
{
    // workspace, previous-workspace, open-app, open-folder, find-project,
    // show-metric, settings, power-profile; "" when the text is none of them.
    QString kind;
    QString arg;          // profile id, application words, folder key, project words, metric, settings key, profile name
    bool prepare = false; // "prepare the desktop for…": also open the profile's files
};
Command parseCommand(const QString &text);
// "программирования", "coding", "KiCad" → dev, work…; "" if no profile is meant.
QString profileFromWords(const QString &text);

// Known folders: downloads, documents, desktop, pictures, music, videos, home.
QString folderKey(const QString &text);
QString folderPath(const QString &key);

// Settings pages a command may open (System Settings modules), by key.
QStringList settingsKeys();
QString settingsKey(const QString &text);
QString settingsModule(const QString &key);

// Installed applications from .desktop files (cached for a minute).
struct App
{
    QString desktopId;   // org.kicad.kicad
    QString name;        // KiCad
    QString icon;
    double score = 0;    // 0..1
};
QList<App> findApplications(const QString &words, int max = 5);
// The first installed id of "a|b|c"; "" when none is.
QString resolveApp(const QString &alternatives);

// Projects under `home` (CMake, PlatformIO, ESP-IDF, KiCad, Cargo, npm, Python,
// Git…), newest first, whose name, kind or path contain every word. Walks at
// most a few thousand directories: call it off the GUI thread.
struct Project
{
    QString path;
    QString name;
    QString kind;        // esp32, platformio, kicad, cmake, rust, node, python, git
    qint64 modified = 0; // ms since the epoch
};
QList<Project> findProjects(const QString &words, const QString &home, int max = 8);

} // namespace jarvis::workspace
