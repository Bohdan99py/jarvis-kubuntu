#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QNetworkAccessManager>
#include "skill_store.h"
class DesktopController : public QObject {
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
    void setAutostart(bool on);
    // True once: the first time the window is closed, explain that Jarvis stays in the tray.
    Q_INVOKABLE bool takeTrayHint();
    // Installs the bundled Jarvis extension with `code --install-extension`.
    Q_INVOKABLE void installVsCodeExtension();
    Q_INVOKABLE void setSkillEnabled(const QString &id,bool enabled);
    Q_INVOKABLE void importSkill(const QUrl &url);
    Q_INVOKABLE void fetchSkill(const QUrl &url);
    Q_INVOKABLE void openSkillsFolder();
    Q_INVOKABLE void update();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void searchWeb(const QString &query);
    Q_INVOKABLE QString launch(const QString &id);
    Q_INVOKABLE void restartDaemon();
    // Quietly asks GitHub whether a newer release exists (no download).
    Q_INVOKABLE void checkForUpdates();
public slots:
    Q_SCRIPTABLE void ShowQuick() { emit quickRequested(); }
    Q_SCRIPTABLE void ShowMain() { emit mainRequested(); }
    // From jarvisd's notifications: "main", "tasks", "study", "curious" or "memory".
    Q_SCRIPTABLE void Open(const QString &target, const QString &payload) { emit openRequested(target.left(20), payload.left(1000)); }
signals:
    void skillsChanged();
    void updateChanged();
    void quickRequested();
    void mainRequested();
    void vscodeChanged();
    void autostartChanged();
    void openRequested(const QString &target, const QString &payload);
    // A newer release exists; emitted once per version.
    void updateAvailable(const QString &version);
private:
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
