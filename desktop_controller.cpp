#include "desktop_controller.h"
#include "runtime_paths.h"
#include "config.h"
#include "language.h"
#include <QDesktopServices>
#include <QDBusConnection>
#include <memory>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QStandardPaths>

DesktopController::DesktopController(QObject *parent):QObject(parent) {
    m_updateStatus=tr("Version %1 · updates from GitHub Releases").arg(version());
    m_timeout.setSingleShot(true);
    connect(&m_timeout,&QTimer::timeout,this,[this]{m_updater.kill();m_updateStatus=tr("The update server did not answer in time.");emit updateChanged();});
    connect(&m_updater,&QProcess::readyReadStandardOutput,this,&DesktopController::readUpdateEvents);
    connect(&m_updater,&QProcess::readyReadStandardError,this,[this]{m_updater.readAllStandardError();});
    connect(&m_updater,&QProcess::started,this,&DesktopController::updateChanged);
    connect(&m_updater,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){m_timeout.stop();m_updateStatus=m_updater.errorString();emit updateChanged();});
    connect(&m_updater,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this]{readUpdateEvents();m_timeout.stop();emit updateChanged();});
}
DesktopController::~DesktopController(){if(updating()){m_updater.kill();m_updater.waitForFinished(1000);}}
QString DesktopController::version() const { return QString(JARVIS_VERSION); }
QVariantList DesktopController::skills() const { return m_skills.list().toVariantList(); }
void DesktopController::setSkillEnabled(const QString &id,bool enabled) {
    const auto error=m_skills.setEnabled(id,enabled);
    m_skillStatus=error.isEmpty()?tr("Skill state saved. It applies from the next request."):error;emit skillsChanged();
}
void DesktopController::installSkill(const QByteArray &bytes) {
    const auto error=m_skills.install(bytes);
    m_skillStatus=error.isEmpty()?tr("Skill installed and disabled. Enable it in the list."):error;emit skillsChanged();
}
void DesktopController::importSkill(const QUrl &url) {
    QFile file(url.toLocalFile());
    if(!url.isLocalFile() || !file.open(QIODevice::ReadOnly) || file.size()>65536) {m_skillStatus=tr("Choose a skill JSON file up to 64 KB.");emit skillsChanged();return;}
    installSkill(file.readAll());
}
void DesktopController::fetchSkill(const QUrl &url) {
    if(m_fetching)return;
    if(!url.isValid() || url.scheme()!="https" || !url.userInfo().isEmpty()) {m_skillStatus=tr("An HTTPS link to the skill JSON is required.");emit skillsChanged();return;}
    m_fetching=true;m_skillStatus=tr("Fetching the skill…");emit skillsChanged();
    QNetworkRequest request(url);request.setTransferTimeout(15000);
    auto *reply=m_network.get(request);
    auto bytes=std::make_shared<QByteArray>();
    connect(reply,&QNetworkReply::readyRead,this,[reply,bytes]{*bytes+=reply->readAll();if(bytes->size()>65536)reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,bytes]{
        m_fetching=false;*bytes+=reply->readAll();
        if(reply->error()!=QNetworkReply::NoError){m_skillStatus=reply->errorString();emit skillsChanged();}
        else installSkill(*bytes);
        reply->deleteLater();
    });
}
void DesktopController::openSkillsFolder() {
    const QString path=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/jarvis/skills";
    QDir().mkpath(path);QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
void DesktopController::update() {
    if(updating())return;
    m_updateBuffer.clear();m_updateStatus=tr("Checking for updates…");
    const QString lang=jarvis::languageCode(jarvis::resolveLanguage(jarvis::Config::loadFileOnly().language));
    m_updater.start(systemPython(),{jarvisScript("update.py"),"--current",version(),"--cache",QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/updates","--lang",lang});
    m_timeout.start(300000);emit updateChanged();
}
void DesktopController::readUpdateEvents() {
    m_updateBuffer+=m_updater.readAllStandardOutput();
    while(m_updateBuffer.contains('\n')) {
        const int end=m_updateBuffer.indexOf('\n');const auto line=m_updateBuffer.left(end);m_updateBuffer.remove(0,end+1);
        const auto o=QJsonDocument::fromJson(line).object();if(o.isEmpty())continue;
        m_updateStatus=o["text"].toString();
        if(o["event"].toString()=="package") {
            const QString file=o["path"].toString();
            if(!QProcess::startDetached("plasma-discover",{"--local-filename",file})) {
                if(!QDesktopServices::openUrl(QUrl::fromLocalFile(file)))m_updateStatus=tr("Open the package manually: %1").arg(file);
            }
        }
        emit updateChanged();
    }
}
void DesktopController::searchWeb(const QString &query) {
    if(query.trimmed().isEmpty())return;
    QUrl url("https://duckduckgo.com/");QUrlQuery parameters;parameters.addQueryItem("q",query.trimmed());url.setQuery(parameters);QDesktopServices::openUrl(url);
}
QString DesktopController::launch(const QString &id) {
    if(id.startsWith("app:")) return launchApplication(id.mid(4));
    const QMap<QString,QString> commands{{"files","dolphin"},{"terminal","konsole"},{"settings","systemsettings"}};
    if(!commands.contains(id))return tr("Unknown action.");
    return QProcess::startDetached(commands[id],{}) ? QString() : tr("Application not installed: %1").arg(commands[id]);
}
void DesktopController::restartDaemon() {
    auto *process=new QProcess(this);
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,process](int code,QProcess::ExitStatus){
        m_updateStatus=code==0?tr("Background service restarted."):tr("Could not restart the service: %1").arg(QString::fromUtf8(process->readAllStandardError()).left(500));
        process->deleteLater();emit updateChanged();
    });
    connect(process,&QProcess::errorOccurred,this,[this,process](QProcess::ProcessError){m_updateStatus=process->errorString();emit updateChanged();process->deleteLater();});
    process->start("systemctl",{"--user","restart","jarvis.service"});
}
void DesktopController::restart() {
    // Release the desktop name before launching so the new process creates fresh windows.
    QDBusConnection::sessionBus().unregisterService("org.jarvis.Desktop1");
    if(QProcess::startDetached(QCoreApplication::applicationFilePath(),{}))QCoreApplication::quit();
    else {QDBusConnection::sessionBus().registerService("org.jarvis.Desktop1");m_updateStatus=tr("Could not restart the application.");emit updateChanged();}
}
// Only installed applications learned from activity: the id must name an existing .desktop file.
QString DesktopController::launchApplication(const QString &desktopId) {
    static const QRegularExpression valid("^[A-Za-z0-9._-]{1,120}$");
    const QString file=QStandardPaths::locate(QStandardPaths::ApplicationsLocation,desktopId+".desktop");
    if(!valid.match(desktopId).hasMatch() || file.isEmpty()) return tr("Application not installed: %1").arg(desktopId);
    for(const QString &tool:{QStringLiteral("kstart"),QStringLiteral("kstart5")})
        if(!QStandardPaths::findExecutable(tool).isEmpty() && QProcess::startDetached(tool,{"--application",desktopId})) return {};
    if(!QStandardPaths::findExecutable("gio").isEmpty() && QProcess::startDetached("gio",{"launch",file})) return {};
    return tr("Could not start %1.").arg(desktopId);
}
QString DesktopController::vscodeCli() {
    for(const QString &name:{QStringLiteral("code"),QStringLiteral("codium"),QStringLiteral("code-insiders")})
        if(const QString path=QStandardPaths::findExecutable(name);!path.isEmpty()) return path;
    for(const QString &path:{QStringLiteral("/snap/bin/code"),QStringLiteral("/usr/share/code/bin/code")})
        if(QFileInfo(path).isExecutable()) return path;
    return {};
}
QString DesktopController::vsixPath() {
    const QString installed=jarvisDataFile("vscode/jarvis-vscode.vsix");
    if(QFileInfo::exists(installed)) return installed;
    return QString(JARVIS_BINARY_DIR)+"/jarvis-vscode.vsix"; // development build
}
void DesktopController::installVsCodeExtension() {
    if(m_vscode.state()!=QProcess::NotRunning) return;
    const QString cli=vscodeCli(), vsix=vsixPath();
    if(cli.isEmpty()) {m_vscodeStatus=tr("VS Code is not installed.");emit vscodeChanged();return;}
    if(!QFileInfo::exists(vsix)) {m_vscodeStatus=tr("The extension package is missing: %1").arg(vsix);emit vscodeChanged();return;}
    m_vscodeStatus=tr("Installing the extension…");emit vscodeChanged();
    connect(&m_vscode,&QProcess::finished,this,[this](int code,QProcess::ExitStatus){
        m_vscodeStatus=code==0?tr("Extension installed. In open VS Code windows run \"Developer: Reload Window\"; the status bar shows Jarvis when it is connected.")
                              :tr("VS Code could not install the extension: %1").arg(QString::fromUtf8(m_vscode.readAllStandardError()).trimmed().left(300));
        emit vscodeChanged();
    },Qt::SingleShotConnection);
    m_vscode.start(cli,{"--install-extension",vsix,"--force"});
}
