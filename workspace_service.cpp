#include "workspace_service.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>

#include "assistant.h"
#include "dbus_names.h"
#include "runtime_paths.h"
#include "telemetry_service.h"

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

QString pick(Lang lang, const QString &ru, const QString &en)
{
    return lang == Lang::Ru ? ru : en;
}

QDBusMessage kwinCall(const QString &method, const QVariantList &args = {})
{
    auto msg = QDBusMessage::createMethodCall(u"org.kde.KWin"_s, u"/Scripting"_s, u"org.kde.kwin.Scripting"_s, method);
    msg.setArguments(args);
    return QDBusConnection::sessionBus().call(msg, QDBus::Block, 3000);
}

QString pluginName()
{
    const QString service = dbus::daemonService();
    return service == QLatin1String(dbus::kService) ? u"jarvis-workspace"_s : u"jarvis-workspace-"_s + service;
}

void unloadScript()
{
    if (kwinCall(u"isScriptLoaded"_s, {pluginName()}).arguments().value(0).toBool())
        kwinCall(u"unloadScript"_s, {pluginName()});
}

QString profileLabel(const workspace::Profile &p)
{
    return p.label.isEmpty() ? p.id.toUpper() : p.label;
}

} // namespace

WorkspaceService::WorkspaceService(Assistant *assistant, TelemetryService *telemetry, GuiCall gui, QObject *parent)
    : QObject(parent)
    , m_assistant(assistant)
    , m_telemetry(telemetry)
    , m_gui(std::move(gui))
    , m_state(workspace::load())
{
    const workspace::Profile p = current();
    m_assistant->setWorkspace(p.id, p.focus, p.activity);
    // A KWin script left by a daemon that died mid-switch.
    QTimer::singleShot(2000, this, [] {
        if (QDBusConnection::sessionBus().interface()->isServiceRegistered(u"org.kde.KWin"_s).value())
            unloadScript();
    });
}

QJsonObject WorkspaceService::summary() const
{
    // From the file: the Control center edits the profiles there. An edited activity
    // level of the current workspace applies at once (not at the next switch).
    const workspace::State state = workspace::load();
    const workspace::Profile p = workspace::find(state, state.current);
    if (p.activity != m_appliedActivity) {
        m_appliedActivity = p.activity;
        m_assistant->setWorkspace(p.id, p.focus, p.activity);
    }
    return workspace::summary(state);
}

bool WorkspaceService::runScript(const QString &mode, const workspace::Profile &profile, QString *error)
{
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    if (!bus || !bus->isServiceRegistered(u"org.kde.KWin"_s).value()) {
        *error = u"KWin is not running"_s;
        return false;
    }
    QFile templ(jarvisDataFile(u"kwin/workspace.js"_s));
    if (!templ.open(QIODevice::ReadOnly)) {
        *error = templ.fileName() + u": "_s + templ.errorString();
        return false;
    }
    QStringList apps;
    for (const QString &alternatives : profile.apps)
        if (const QString id = workspace::resolveApp(alternatives); !id.isEmpty())
            apps.append(id);
    m_token = QString::number(QRandomGenerator::global()->generate64(), 36);
    const QJsonObject data{{u"token"_s, m_token}, {u"mode"_s, mode}, {u"desktop"_s, profile.desktop},
                           {u"apps"_s, QJsonArray::fromStringList(apps)}, {u"layout"_s, profile.layout}};
    QByteArray script = templ.readAll();
    // The data is JSON (a valid JavaScript literal); the service name is checked by dbus_names.
    script.replace("%DATA%", QJsonDocument(data).toJson(QJsonDocument::Compact));
    script.replace("%SERVICE%", dbus::daemonService().toUtf8());
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    const QString path = dir + u'/' + pluginName() + u".js"_s;
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(script) != script.size()
        || !out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) || !out.commit()) {
        *error = path + u": "_s + out.errorString();
        return false;
    }
    unloadScript();
    const QDBusMessage loaded = kwinCall(u"loadScript"_s, {path, pluginName()});
    if (loaded.type() == QDBusMessage::ErrorMessage || loaded.arguments().value(0).toInt() < 0) {
        *error = loaded.type() == QDBusMessage::ErrorMessage ? loaded.errorMessage() : u"KWin rejected the script"_s;
        return false;
    }
    kwinCall(u"start"_s);
    m_pendingMode = mode;
    // Never leave the script loaded: unloaded after the report, or after 10 s.
    const QString token = m_token;
    QTimer::singleShot(10000, this, [this, token] {
        if (m_token == token) {
            unloadScript();
            m_token.clear();
        }
    });
    return true;
}

QString WorkspaceService::switchTo(const QString &id, bool prepare, Lang lang, QString *message)
{
    if (!workspace::profileIds().contains(id))
        return u"unknown workspace"_s;
    m_state = workspace::load(); // the Control center may have edited the profiles
    if (m_state.current != id)
        m_state.previous = m_state.current;
    m_state.current = id;
    workspace::save(m_state);
    const workspace::Profile p = current();
    m_assistant->setWorkspace(p.id, p.focus, p.activity);
    m_pendingPrepare = prepare;
    QString error;
    const bool scripted = runScript(u"switch"_s, p, &error);
    if (!scripted) {
        // Without KWin (another desktop, a test) the profile still changes; the applications start anyway.
        qWarning().noquote() << "workspace: no KWin script:" << error;
        m_token = u"local"_s;
        report(QJsonDocument(QJsonObject{{u"token"_s, m_token}, {u"mode"_s, u"switch"_s}, {u"running"_s, QJsonArray()}}).toJson());
    }
    emit changed();
    emit event(u"workspace"_s, profileLabel(p));
    if (message)
        *message = (prepare ? pick(lang, u"Готовлю рабочий стол %1: рабочий стол %2, нужные программы и файлы."_s,
                                   u"Preparing the %1 workspace: desktop %2, its applications and files."_s)
                            : pick(lang, u"Профиль %1: рабочий стол %2."_s, u"Workspace %1: desktop %2."_s))
                       .arg(profileLabel(p))
                       .arg(p.desktop)
                   + (scripted ? QString() : pick(lang, u" (KWin недоступен: окна не перемещены.)"_s, u" (KWin is not available: windows were not moved.)"_s));
    return {};
}

QString WorkspaceService::previous(Lang lang, QString *message)
{
    m_state = workspace::load();
    if (m_state.previous.isEmpty()) {
        if (message)
            *message = pick(lang, u"Предыдущего профиля нет."_s, u"There is no previous workspace."_s);
        return u"no previous workspace"_s;
    }
    const QString target = m_state.previous; // a copy: switchTo rewrites m_state
    return switchTo(target, false, lang, message);
}

QString WorkspaceService::saveLayout(Lang lang, QString *message)
{
    QString error;
    if (!runScript(u"save"_s, current(), &error)) {
        if (message)
            *message = pick(lang, u"Не удалось сохранить расположение окон: %1"_s, u"Could not save the window layout: %1"_s).arg(error);
        return error;
    }
    if (message)
        *message = pick(lang, u"Сохраняю расположение окон профиля %1."_s, u"Saving the window layout of %1."_s).arg(profileLabel(current()));
    return {};
}

void WorkspaceService::report(const QString &json)
{
    const QJsonObject o = QJsonDocument::fromJson(json.left(65536).toUtf8()).object();
    if (o.value(u"token"_s).toString() != m_token || m_token.isEmpty())
        return; // not the script we loaded (or already handled)
    m_token.clear();
    QTimer::singleShot(0, this, [] { unloadScript(); });
    const QString mode = o.value(u"mode"_s).toString();
    workspace::Profile p = current();
    if (mode == u"save") {
        p.layout = o.value(u"geometries"_s).toObject();
        for (workspace::Profile &q : m_state.profiles)
            if (q.id == p.id)
                q.layout = p.layout;
        workspace::save(m_state);
        emit event(u"workspace"_s, u"layout %1"_s.arg(profileLabel(p)));
        return;
    }
    QStringList running;
    for (const QJsonValue &v : o.value(u"running"_s).toArray())
        running.append(v.toString().toLower());
    for (const QString &alternatives : p.apps) {
        const QString id = workspace::resolveApp(alternatives);
        if (id.isEmpty() || running.contains(id.toLower()))
            continue;
        running.append(id.toLower()); // once, even if two entries name it
        m_gui(u"Launch"_s, {id});
    }
    if (m_pendingPrepare)
        for (const QString &path : p.paths)
            m_gui(u"OpenPath"_s, {path});
    m_pendingPrepare = false;
}

QList<workspace::Project> WorkspaceService::projects(const QString &words)
{
    if (QDateTime::currentMSecsSinceEpoch() - m_projectsAt > 10 * 60 * 1000)
        scanProjects();
    QList<workspace::Project> out;
    const QStringList wanted = words.toLower().split(u' ', Qt::SkipEmptyParts);
    for (const workspace::Project &p : m_projects) {
        const QString hay = (p.name + u' ' + p.kind + u' ' + p.path).toLower();
        if (std::all_of(wanted.cbegin(), wanted.cend(), [&](const QString &w) { return hay.contains(w); }))
            out.append(p);
    }
    return out.mid(0, 8);
}

void WorkspaceService::scanProjects()
{
    if (m_scanning)
        return;
    m_scanning = true;
    QPointer<WorkspaceService> self(this);
    const QString home = QDir::homePath();
    // A few thousand directories: a worker thread, never the event loop.
    QThreadPool::globalInstance()->start([self, home] {
        const QList<workspace::Project> found = workspace::findProjects(QString(), home, 200);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, found] {
            if (!self)
                return;
            self->m_projects = found;
            self->m_projectsAt = QDateTime::currentMSecsSinceEpoch();
            self->m_scanning = false;
        });
    });
}

QString WorkspaceService::handle(const workspace::Command &c, Lang lang)
{
    QString message;
    if (c.kind == u"workspace") {
        switchTo(c.arg, c.prepare, lang, &message);
        return message;
    }
    if (c.kind == u"previous-workspace") {
        previous(lang, &message);
        return message;
    }
    if (c.kind == u"open-app") {
        const QList<workspace::App> apps = workspace::findApplications(c.arg, 1);
        if (apps.isEmpty() || apps.first().score < 0.6)
            return {}; // not an application: the request goes on as a question
        m_gui(u"Launch"_s, {apps.first().desktopId});
        emit event(u"launch"_s, apps.first().name);
        return pick(lang, u"Открываю %1."_s, u"Opening %1."_s).arg(apps.first().name);
    }
    if (c.kind == u"open-folder") {
        const QString path = workspace::folderPath(c.arg);
        if (path.isEmpty())
            return {};
        m_gui(u"OpenPath"_s, {path});
        return pick(lang, u"Открываю папку %1."_s, u"Opening %1."_s).arg(QDir::toNativeSeparators(path));
    }
    if (c.kind == u"find-project") {
        const QList<workspace::Project> found = projects(c.arg);
        if (found.isEmpty())
            return m_scanning ? pick(lang, u"Ищу проекты в домашней папке — спросите ещё раз через пару секунд."_s,
                                     u"Looking for projects in your home folder — ask again in a couple of seconds."_s)
                              : pick(lang, u"Проект «%1» не найден."_s, u"No project matches “%1”."_s).arg(c.arg);
        m_gui(u"OpenPath"_s, {found.first().path});
        QStringList lines;
        for (const workspace::Project &p : found.mid(0, 3))
            lines.append(u"• %1 (%2) — %3"_s.arg(p.name, p.kind, QDateTime::fromMSecsSinceEpoch(p.modified).toString(u"dd.MM.yyyy"_s)));
        return pick(lang, u"Открываю %1.\n%2"_s, u"Opening %1.\n%2"_s).arg(found.first().path, lines.join(u'\n'));
    }
    if (c.kind == u"show-metric")
        return m_telemetry->describe(c.arg);
    if (c.kind == u"settings") {
        if (workspace::settingsModule(c.arg).isEmpty())
            return {};
        m_gui(u"OpenSettings"_s, {c.arg});
        return pick(lang, u"Открываю настройки."_s, u"Opening the settings."_s);
    }
    if (c.kind == u"power-profile") {
        const QString error = m_telemetry->setPowerProfile(c.arg);
        if (!error.isEmpty())
            return error;
        emit event(u"power"_s, c.arg);
        return pick(lang, u"Профиль питания: %1."_s, u"Power profile: %1."_s).arg(c.arg);
    }
    return {};
}

} // namespace jarvis
