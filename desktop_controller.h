#pragma once
#include <QDBusContext>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QNetworkAccessManager>
#include "skill_store.h"
class DesktopController : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.jarvis.Desktop1")
    Q_PROPERTY(QVariantList skills READ skills NOTIFY skillsChanged)
    Q_PROPERTY(QString skillStatus READ skillStatus NOTIFY skillsChanged)
    Q_PROPERTY(QString updateStatus READ updateStatus NOTIFY updateChanged)
    Q_PROPERTY(bool updating READ updating NOTIFY updateChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString vscodeStatus READ vscodeStatus NOTIFY vscodeChanged)
    Q_PROPERTY(bool vscodeAvailable READ vscodeAvailable CONSTANT)
    // Start in the tray at login: an XDG autostart entry plus, where installed,
    // the systemd user unit jarvis-tray.service (restarts Jarvis if it crashes).
    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY autostartChanged)
    // The overlay can be a Wayland layer-shell surface (KDE's layer-shell-qt QML
    // module is installed and the session is Wayland): it then appears above
    // other windows on the active screen and takes the keyboard reliably.
    Q_PROPERTY(bool layerShell READ layerShell CONSTANT)
public:
    explicit DesktopController(QObject *parent=nullptr);
    ~DesktopController() override;
    QVariantList skills() const;
    QString skillStatus() const { return m_skillStatus; }
    QString updateStatus() const { return m_updateStatus; }
    bool updating() const { return m_updater.state()!=QProcess::NotRunning; }
    QString version() const;
    QString vscodeStatus() const { return m_vscodeStatus; }
    bool vscodeAvailable() const { return !vscodeCli().isEmpty(); }
    bool autostart() const;
    bool layerShell() const;
    void setAutostart(bool on);
    // True once: the first time the window is closed, explain that Jarvis stays in the tray.
    Q_INVOKABLE bool takeTrayHint();
    // Installs the bundled Jarvis extension with `code --install-extension`.
    Q_INVOKABLE void installVsCodeExtension();
    // The desktop's XDG activation token for the next window that asks for the focus (Wayland).
    Q_INVOKABLE void prepareActivation(const QString &token);
    Q_INVOKABLE void setSkillEnabled(const QString &id,bool enabled);
    Q_INVOKABLE void importSkill(const QUrl &url);
    Q_INVOKABLE void fetchSkill(const QUrl &url);
    Q_INVOKABLE void openSkillsFolder();
    // Calculators of a skill ("tools") in the interface language, and one calculation.
    Q_INVOKABLE QVariantList calculators(const QStringList &ids) const;
    Q_INVOKABLE QVariantMap calculate(const QString &id, const QVariantMap &values) const;
    // Markdown from a skill file (or anything untrusted), safe to render: no images, no HTML.
    Q_INVOKABLE QString safeMarkdown(const QString &text) const;
    Q_INVOKABLE void update();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void searchWeb(const QString &query);
    Q_INVOKABLE QString launch(const QString &id);
    Q_INVOKABLE void restartDaemon();
    // The voice state for the desktop widgets (jarvisd accepts it from this window only).
    Q_INVOKABLE void reportVoice(bool listening, bool speaking, const QString &engine);
    // Workspaces through jarvisd (it moves the windows and starts the applications).
    Q_INVOKABLE void switchWorkspace(const QString &id);
    Q_INVOKABLE void saveWorkspaceLayout();
    // Quietly asks GitHub whether a newer release exists (no download).
    Q_INVOKABLE void checkForUpdates();
public slots:
    Q_SCRIPTABLE void ShowQuick() { emit quickRequested(); }
    Q_SCRIPTABLE void ShowMain() { emit mainRequested(); }
    // The overlay (Ctrl+J, `jarvis --overlay`). `token`: the caller's XDG activation token, if any.
    Q_SCRIPTABLE void ShowOverlay(const QString &token) { emit overlayRequested(token.left(200)); }
    // From jarvisd's notifications: "main", "tasks", "study", "curious" or "memory".
    // The payload becomes a chat message from Jarvis, so it is taken only from jarvisd.
    Q_SCRIPTABLE void Open(const QString &target, const QString &payload);
    // Workspaces and the launcher (0.19.2), from jarvisd only: jarvisd decides, the
    // window starts what was asked — outside the service's sandbox, in the session.
    // An installed application by desktop id; an existing file or folder of the
    // user; an allowed System Settings page (workspace::settingsKeys).
    Q_SCRIPTABLE void Launch(const QString &desktopId);
    Q_SCRIPTABLE void OpenPath(const QString &path);
    Q_SCRIPTABLE void OpenSettings(const QString &key);
signals:
    void skillsChanged();
    void updateChanged();
    void quickRequested();
    void mainRequested();
    void overlayRequested(const QString &token);
    void vscodeChanged();
    void autostartChanged();
    void openRequested(const QString &target, const QString &payload);
    // A newer release exists; emitted once per version.
    void updateAvailable(const QString &version);
private:
    bool calledByDaemon() const;
    void installSkill(const QByteArray &bytes);
    QString launchApplication(const QString &desktopId);
    static QString vscodeCli();
    static QString vsixPath();
    static QString autostartFile();
    static bool trayUnitInstalled();
    void readUpdateEvents();
    jarvis::SkillStore m_skills;
    QNetworkAccessManager m_network;
    QProcess m_updater;
    QTimer m_timeout;
    QByteArray m_updateBuffer;
    QString m_skillStatus,m_updateStatus,m_vscodeStatus;
    QProcess m_vscode;
    QProcess m_checker;
    QTimer m_checkTimer;
    bool m_fetching=false;
};
