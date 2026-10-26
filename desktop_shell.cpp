#include "desktop_shell.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibraryInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <memory>

#include "config.h"
#include "dbus_names.h"
#include "desktop_integration.h"
#include "desktop_shell_scripts.h"
#include "language.h"
#include "workspace.h"

using namespace Qt::StringLiterals;

namespace {

const QString kPlasma = u"org.kde.plasmashell"_s;
// Plasma's layout: the containments and widgets, and the panels' sizes.
const QStringList kLayoutFiles{u"plasma-org.kde.plasma.desktop-appletsrc"_s, u"plasmashellrc"_s};

QString configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

QString dataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/jarvis"_s;
}

QDBusMessage plasmaCall(const QString &method, const QVariantList &args = {})
{
    auto msg = QDBusMessage::createMethodCall(kPlasma, u"/PlasmaShell"_s, u"org.kde.PlasmaShell"_s, method);
    msg.setArguments(args);
    return msg;
}

bool visualModuleInstalled()
{
    QStringList dirs{QLibraryInfo::path(QLibraryInfo::QmlImportsPath)};
    dirs += qEnvironmentVariable("QML_IMPORT_PATH").split(u':', Qt::SkipEmptyParts);
    for (const QString &d : dirs)
        if (QFileInfo::exists(d + u"/org/jarvis/visual/qmldir"_s))
            return true;
    return false;
}

} // namespace

DesktopShell::DesktopShell(QObject *parent)
    : QObject(parent)
{
}

bool DesktopShell::russian() const
{
    return jarvis::resolveLanguage(jarvis::Config::loadFileOnly().language) == jarvis::Lang::Ru;
}

QString DesktopShell::statePath()
{
    return dataDir() + u"/desktop-state.json"_s;
}

QJsonObject DesktopShell::loadState()
{
    QFile f(statePath());
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

bool DesktopShell::saveState(const QJsonObject &state)
{
    QDir().mkpath(dataDir());
    QSaveFile f(statePath());
    const QByteArray bytes = QJsonDocument(state).toJson(QJsonDocument::Indented);
    return f.open(QIODevice::WriteOnly) && f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
           && f.write(bytes) == bytes.size() && f.commit();
}

QString DesktopShell::mode() const
{
    return jarvis::Config::loadFileOnly().desktop.value(u"mode"_s).toString(u"standard"_s);
}

bool DesktopShell::hasBackup() const
{
    return QFileInfo::exists(backupPath() + u"/manifest.json"_s);
}

QString DesktopShell::backupPath() const
{
    return loadState().value(u"backup"_s).toString();
}

void DesktopShell::setStep(const QString &step, const QString &status)
{
    m_step = step;
    m_status = status;
    qInfo().noquote() << "desktop shell:" << status;
    emit changed();
}

void DesktopShell::finish(bool ok, const QString &message)
{
    m_step.clear();
    m_status = message;
    qInfo().noquote() << "desktop shell:" << (ok ? "done:" : "failed:") << message;
    emit changed();
    emit finished(ok, message);
}

void DesktopShell::call(const QDBusMessage &msg, const Reply &done, int timeoutMs)
{
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, timeoutMs), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [w, done] {
        w->deleteLater();
        done(w->reply());
    });
}

void DesktopShell::script(const QString &js, const std::function<void(bool, const QJsonObject &, const QString &)> &done)
{
    call(plasmaCall(u"evaluateScript"_s, {js}), [done](const QDBusMessage &reply) {
        if (reply.type() == QDBusMessage::ErrorMessage)
            done(false, {}, reply.errorMessage());
        else
            done(true, jarvis::shell::parsePrinted(reply.arguments().value(0).toString()), {});
    }, 15000);
}

void DesktopShell::check()
{
    if (busy())
        return;
    const bool ru = russian();
    setStep(u"check"_s, ru ? u"Проверяю Plasma…"_s : u"Checking Plasma…"_s);
    m_checks.clear();
    m_planText.clear();
    m_checked = m_compatible = false;
    const auto add = [this](const QString &name, bool ok, const QString &text) {
        m_checks.append(QVariantMap{{u"name"_s, name}, {u"ok"_s, ok}, {u"text"_s, text}});
    };
    // What is installed (no D-Bus needed).
    QStringList missing;
    for (const QString &id : jarvis::shell::widgetIds())
        if (QStandardPaths::locate(QStandardPaths::GenericDataLocation, u"plasma/plasmoids/"_s + id + u"/metadata.json"_s).isEmpty())
            missing.append(id);
    add(ru ? u"Виджеты Jarvis"_s : u"Jarvis widgets"_s, missing.isEmpty(),
        missing.isEmpty() ? (ru ? u"установлены (7)"_s : u"installed (7)"_s) : (ru ? u"нет: %1"_s : u"missing: %1"_s).arg(missing.join(u", "_s)));
    const bool module = visualModuleInstalled();
    add(ru ? u"Визуальный модуль org.jarvis.visual"_s : u"Visual module org.jarvis.visual"_s, module,
        module ? (ru ? u"установлен"_s : u"installed"_s) : (ru ? u"не установлен: ядро и виджеты не загрузятся"_s : u"not installed: the core and widgets will not load"_s));

    auto daemon = QDBusMessage::createMethodCall(QString(jarvis::dbus::kService), u"/org/jarvis/Integration1"_s, u"org.jarvis.Integration1"_s, u"Status"_s);
    daemon.setAutoStartService(false);
    call(daemon, [this, ru, add, missing, module](const QDBusMessage &reply) {
        const QJsonObject status = QJsonDocument::fromJson(reply.arguments().value(0).toString().toUtf8()).object();
        const bool daemonOk = reply.type() != QDBusMessage::ErrorMessage && status.contains(u"workspace"_s);
        add(ru ? u"Служба jarvisd"_s : u"Service jarvisd"_s, daemonOk,
            daemonOk ? (ru ? u"отвечает, %1"_s : u"answers, %1"_s).arg(status.value(u"version"_s).toString())
                     : (ru ? u"не отвечает или старая версия: ядро и док будут проверены только по раскладке"_s
                           : u"does not answer or is older: the core and the dock are checked by the layout only"_s));
        script(jarvis::shell::inspectScript(), [this, ru, add, missing, module](bool ok, const QJsonObject &inspection, const QString &error) {
            if (!ok) {
                add(u"Plasma"_s, false, (ru ? u"не отвечает: %1"_s : u"does not answer: %1"_s).arg(error));
                m_checked = true;
                finish(false, ru ? u"Plasma недоступна: ничего не изменено."_s : u"Plasma is not available: nothing was changed."_s);
                return;
            }
            m_inspection = inspection;
            const QString version = inspection.value(u"version"_s).toString();
            const bool plasma6 = version.section(u'.', 0, 0).toInt() >= 6;
            add(u"Plasma"_s, plasma6, (ru ? u"версия %1%2"_s : u"version %1%2"_s).arg(version, plasma6 ? QString() : (ru ? u": нужна Plasma 6"_s : u": Plasma 6 is required"_s)));
            const bool unlocked = !inspection.value(u"locked"_s).toBool();
            add(ru ? u"Виджеты не заблокированы"_s : u"Widgets unlocked"_s, unlocked,
                unlocked ? (ru ? u"да"_s : u"yes"_s) : (ru ? u"разблокируйте виджеты (правый щелчок по рабочему столу)"_s : u"unlock the widgets (right click on the desktop)"_s));
            const jarvis::ConfigData cfg = jarvis::Config::loadFileOnly();
            const jarvis::workspace::State ws = jarvis::workspace::load();
            m_plan = jarvis::shell::makePlan(inspection, cfg.desktop, jarvis::workspace::find(ws, ws.current).widgets);
            m_planText = jarvis::shell::describePlan(m_plan, ru);
            m_compatible = plasma6 && unlocked && missing.isEmpty() && module;
            m_checked = true;
            finish(m_compatible, m_compatible ? (ru ? u"Совместимо. Посмотрите план и подтвердите."_s : u"Compatible. Review the plan and confirm."_s)
                                              : (ru ? u"Включить JARVIS DESKTOP сейчас нельзя: см. проверки."_s : u"JARVIS DESKTOP cannot be turned on now: see the checks."_s));
        });
    }, 3000);
}

bool DesktopShell::backup(QString *error)
{
    QJsonObject state = loadState();
    // The first backup is the restore point: the desktop as it was before Jarvis.
    if (QFileInfo::exists(state.value(u"backup"_s).toString() + u"/manifest.json"_s))
        return true;
    const QString dir = dataDir() + u"/desktop-backup/"_s + QDateTime::currentDateTime().toString(u"yyyyMMdd-HHmmss"_s);
    if (!QDir().mkpath(dir)) {
        *error = dir;
        return false;
    }
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QJsonArray files;
    for (const QString &name : kLayoutFiles) {
        const QString source = configDir() + u'/' + name;
        if (!QFileInfo::exists(source))
            continue;
        if (!QFile::copy(source, dir + u'/' + name)) {
            *error = source;
            return false;
        }
        files.append(name);
    }
    // Plasma's own description of the layout, as a second way back (and for people to read).
    const QDBusMessage dump = QDBusConnection::sessionBus().call(plasmaCall(u"dumpCurrentLayoutJS"_s), QDBus::Block, 8000);
    if (dump.type() != QDBusMessage::ErrorMessage) {
        QFile js(dir + u"/layout.js"_s);
        if (js.open(QIODevice::WriteOnly))
            js.write(dump.arguments().value(0).toByteArray());
    }
    QFile manifest(dir + u"/manifest.json"_s);
    if (!manifest.open(QIODevice::WriteOnly)) {
        *error = manifest.fileName();
        return false;
    }
    manifest.write(QJsonDocument(QJsonObject{{u"created"_s, QDateTime::currentDateTime().toString(Qt::ISODate)},
                                             {u"plasma"_s, m_inspection.value(u"version"_s)},
                                             {u"files"_s, files},
                                             {u"panels"_s, m_inspection.value(u"panels"_s)}})
                       .toJson());
    state[u"backup"_s] = dir;
    return saveState(state) || (*error = statePath(), false);
}

void DesktopShell::apply()
{
    const bool ru = russian();
    if (busy())
        return;
    if (!m_checked || !m_compatible) {
        finish(false, ru ? u"Сначала проверка совместимости."_s : u"Run the compatibility check first."_s);
        return;
    }
    setStep(u"backup"_s, ru ? u"Резервная копия раскладки Plasma…"_s : u"Backing up Plasma's layout…"_s);
    QString error;
    if (!backup(&error)) {
        finish(false, (ru ? u"Резервная копия не создана (%1): ничего не изменено."_s : u"No backup (%1): nothing was changed."_s).arg(error));
        return;
    }
    setStep(u"apply"_s, ru ? u"Добавляю ядро, виджеты и док…"_s : u"Adding the core, the widgets and the dock…"_s);
    const qint64 appliedAt = QDateTime::currentMSecsSinceEpoch();
    script(jarvis::shell::applyScript(m_plan), [this, ru, appliedAt](bool ok, const QJsonObject &created, const QString &error) {
        if (!ok || created.isEmpty()) {
            rollback(created, (ru ? u"Plasma не применила изменения: %1"_s : u"Plasma did not apply the changes: %1"_s).arg(error));
            return;
        }
        QJsonObject state = loadState();
        state[u"created"_s] = created;
        state[u"appliedAt"_s] = double(appliedAt);
        saveState(state);
        setStep(u"verify"_s, ru ? u"Проверяю, что док и ядро работают…"_s : u"Checking that the dock and the core work…"_s);
        verify(created, appliedAt, 0);
    });
}

void DesktopShell::verify(const QJsonObject &created, qint64 appliedAt, int attempt)
{
    const bool ru = russian();
    script(jarvis::shell::verifyScript(created), [this, ru, created, appliedAt, attempt](bool ok, const QJsonObject &result, const QString &error) {
        if (!ok || !result.value(u"ok"_s).toBool()) {
            rollback(created, (ru ? u"Части Jarvis не найдены после добавления (%1)."_s : u"Jarvis parts missing after adding them (%1)."_s)
                                  .arg(ok ? QString::fromUtf8(QJsonDocument(result.value(u"missing"_s).toArray()).toJson(QJsonDocument::Compact)) : error));
            return;
        }
        // The parts exist; do they run? The dock and the core report to jarvisd.
        auto status = QDBusMessage::createMethodCall(QString(jarvis::dbus::kService), u"/org/jarvis/Integration1"_s, u"org.jarvis.Integration1"_s, u"Status"_s);
        status.setAutoStartService(false);
        call(status, [this, ru, created, appliedAt, attempt](const QDBusMessage &reply) {
            const QJsonObject s = QJsonDocument::fromJson(reply.arguments().value(0).toString().toUtf8()).object();
            const QJsonObject components = s.value(u"components"_s).toObject();
            const bool daemon = reply.type() != QDBusMessage::ErrorMessage && s.contains(u"components"_s);
            const auto alive = [&](const QString &name) { return components.value(name).toDouble() >= double(appliedAt - 2000); };
            const bool running = !daemon || (alive(u"dock"_s) && alive(u"core"_s));
            if (!running && attempt < 20) {
                QTimer::singleShot(1000, this, [this, created, appliedAt, attempt] { verify(created, appliedAt, attempt + 1); });
                return;
            }
            if (!running) {
                rollback(created, ru ? u"Док или ядро не запустились за 20 секунд (модуль QML или Plasma?)."_s
                                     : u"The dock or the core did not start within 20 seconds (QML module or Plasma?)."_s);
                return;
            }
            const QJsonArray replace = m_plan.value(u"replacePanels"_s).toArray();
            const auto done = [this, ru, daemon](int removed) {
                setMode(u"jarvis"_s);
                finish(true, (ru ? u"JARVIS DESKTOP включён%1%2. Вернуть стандартный рабочий стол: «Восстановить» или jarvis --restore-desktop."_s
                                 : u"JARVIS DESKTOP is on%1%2. To go back: Restore, or jarvis --restore-desktop."_s)
                                 .arg(removed > 0 ? (ru ? u", стандартных панелей убрано: %1"_s : u", standard panels removed: %1"_s).arg(removed) : QString())
                                 .arg(daemon ? QString() : (ru ? u" (без проверки пульса: служба не отвечала)"_s : u" (heartbeat not checked: the service did not answer)"_s)));
            };
            if (replace.isEmpty()) {
                done(0);
                return;
            }
            setStep(u"finalize"_s, ru ? u"Убираю стандартные панели…"_s : u"Removing the standard panels…"_s);
            script(jarvis::shell::finalizeScript(replace), [this, done, replace](bool ok, const QJsonObject &r, const QString &) {
                QJsonObject state = loadState();
                state[u"replaced"_s] = replace;
                saveState(state);
                done(ok ? r.value(u"removed"_s).toInt() : 0);
            });
        }, 3000);
    });
}

void DesktopShell::rollback(const QJsonObject &created, const QString &why)
{
    const bool ru = russian();
    setStep(u"rollback"_s, ru ? u"Откатываю добавленное…"_s : u"Taking the additions away…"_s);
    script(jarvis::shell::removeScript(created), [this, ru, why](bool, const QJsonObject &, const QString &) {
        QJsonObject state = loadState();
        state.remove(u"created"_s);
        saveState(state);
        setMode(u"standard"_s);
        finish(false, why + (ru ? u" Изменения отменены, стандартные панели не тронуты."_s : u" The changes were undone; the standard panels were not touched."_s));
    });
}

void DesktopShell::setMode(const QString &mode)
{
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    d.desktop[u"mode"_s] = mode;
    jarvis::Config::save(d, nullptr);
    QJsonObject state = loadState();
    state[u"mode"_s] = mode;
    saveState(state);
    // jarvisd re-reads its settings (the widgets learn the mode from its status).
    auto reload = QDBusMessage::createMethodCall(QString(jarvis::dbus::kService), QString(jarvis::dbus::kPath), QString(jarvis::dbus::kInterface), u"ReloadConfig"_s);
    reload.setAutoStartService(false);
    QDBusConnection::sessionBus().asyncCall(reload, 3000);
    emit changed();
}

void DesktopShell::update()
{
    if (busy() || mode() != u"jarvis")
        return;
    const QJsonObject created = loadState().value(u"created"_s).toObject();
    script(jarvis::shell::inspectScript(), [this, created](bool ok, const QJsonObject &inspection, const QString &) {
        if (!ok)
            return;
        const jarvis::workspace::State ws = jarvis::workspace::load();
        const QJsonObject plan = jarvis::shell::makePlan(inspection, jarvis::Config::loadFileOnly().desktop, jarvis::workspace::find(ws, ws.current).widgets);
        script(jarvis::shell::updateScript(created, plan), [](bool, const QJsonObject &, const QString &) {});
    });
}

void DesktopShell::waitForPlasma(bool wanted, int attempts, const std::function<void(bool)> &done)
{
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    const bool running = bus && bus->isServiceRegistered(kPlasma).value();
    if (running == wanted || attempts <= 0) {
        done(running == wanted);
        return;
    }
    QTimer::singleShot(250, this, [this, wanted, attempts, done] { waitForPlasma(wanted, attempts - 1, done); });
}

void DesktopShell::stopPlasma(const std::function<void(bool)> &stopped)
{
    // In a systemd session plasmashell is a user unit; elsewhere it quits by itself.
    auto *probe = new QProcess(this);
    connect(probe, &QProcess::finished, this, [this, probe, stopped](int code, QProcess::ExitStatus) {
        probe->deleteLater();
        const bool viaSystemd = code == 0;
        if (viaSystemd)
            QProcess::startDetached(u"systemctl"_s, {u"--user"_s, u"stop"_s, u"plasma-plasmashell.service"_s});
        else
            QProcess::startDetached(u"kquitapp6"_s, {u"plasmashell"_s});
        waitForPlasma(false, 60, [stopped, viaSystemd](bool) { stopped(viaSystemd); });
    });
    probe->start(u"systemctl"_s, {u"--user"_s, u"is-active"_s, u"--quiet"_s, u"plasma-plasmashell.service"_s});
}

void DesktopShell::startPlasma(bool viaSystemd)
{
    if (viaSystemd) {
        QProcess::startDetached(u"systemctl"_s, {u"--user"_s, u"start"_s, u"plasma-plasmashell.service"_s});
        return;
    }
    // Outside systemd plasmashell must not inherit Jarvis's terminal or pipes.
    const auto detached = [](const QString &program, const QStringList &args) {
        QProcess p;
        p.setProgram(program);
        p.setArguments(args);
        p.setStandardInputFile(QProcess::nullDevice());
        p.setStandardOutputFile(QProcess::nullDevice());
        p.setStandardErrorFile(QProcess::nullDevice());
        return p.startDetached();
    };
    if (!detached(u"kstart"_s, {u"plasmashell"_s}))
        detached(u"plasmashell"_s, {});
}

void DesktopShell::restore()
{
    if (busy())
        return;
    const bool ru = russian();
    const QJsonObject state = loadState();
    const QString dir = state.value(u"backup"_s).toString();
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    const bool plasma = bus && bus->isServiceRegistered(kPlasma).value();
    if (!QFileInfo::exists(dir + u"/manifest.json"_s)) {
        // No backup: Jarvis's own parts go, nothing else is touched.
        if (!plasma) {
            setMode(u"standard"_s);
            finish(true, ru ? u"Plasma не запущена; режим переключён на стандартный."_s : u"Plasma is not running; the mode is back to standard."_s);
            return;
        }
        setStep(u"remove"_s, ru ? u"Убираю части Jarvis…"_s : u"Removing the Jarvis parts…"_s);
        script(jarvis::shell::removeScript(state.value(u"created"_s).toObject()), [this, ru](bool ok, const QJsonObject &r, const QString &error) {
            QJsonObject s = loadState();
            s.remove(u"created"_s);
            saveState(s);
            setMode(u"standard"_s);
            finish(ok, ok ? (ru ? u"Частей Jarvis убрано: %1. Резервной копии не было — остальное не менялось."_s
                                : u"Jarvis parts removed: %1. There was no backup — nothing else changed."_s).arg(r.value(u"removed"_s).toInt())
                          : (ru ? u"Plasma отказала: %1"_s : u"Plasma refused: %1"_s).arg(error));
        });
        return;
    }
    setStep(u"restore"_s, ru ? u"Останавливаю plasmashell и возвращаю раскладку из резервной копии…"_s
                             : u"Stopping plasmashell and putting the backed-up layout back…"_s);
    // What the desktop is now, in case the user wants it back.
    const QString before = dataDir() + u"/desktop-backup/before-restore-"_s + QDateTime::currentDateTime().toString(u"yyyyMMdd-HHmmss"_s);
    QDir().mkpath(before);
    for (const QString &name : kLayoutFiles)
        QFile::copy(configDir() + u'/' + name, before + u'/' + name);
    const auto put = [this, dir, ru](bool viaSystemd) {
        QStringList failed;
        QFile manifest(dir + u"/manifest.json"_s);
        const QJsonArray files = manifest.open(QIODevice::ReadOnly)
                                     ? QJsonDocument::fromJson(manifest.readAll()).object().value(u"files"_s).toArray()
                                     : QJsonArray();
        for (const QJsonValue &v : files) {
            const QString name = v.toString();
            if (!kLayoutFiles.contains(name))
                continue; // only Plasma's layout files, whatever the manifest says
            const QString target = configDir() + u'/' + name;
            QFile::remove(target);
            if (!QFile::copy(dir + u'/' + name, target))
                failed.append(name);
        }
        startPlasma(viaSystemd);
        waitForPlasma(true, 80, [this, ru, failed](bool up) {
            QJsonObject s = loadState();
            s.remove(u"created"_s);
            s.remove(u"replaced"_s);
            saveState(s);
            setMode(u"standard"_s);
            finish(failed.isEmpty() && up,
                   !failed.isEmpty() ? (ru ? u"Не удалось вернуть: %1"_s : u"Could not put back: %1"_s).arg(failed.join(u", "_s))
                   : up ? (ru ? u"Стандартный рабочий стол восстановлен из резервной копии."_s : u"The standard desktop is restored from the backup."_s)
                        : (ru ? u"Раскладка возвращена, но plasmashell не запустился: выполните plasmashell --replace."_s
                              : u"The layout is back, but plasmashell did not start: run plasmashell --replace."_s));
        });
    };
    if (plasma)
        stopPlasma(put);
    else
        put(false);
}

void DesktopShell::createWorkspaceDesktops()
{
    const bool ru = russian();
    const jarvis::workspace::State ws = jarvis::workspace::load();
    int needed = 0;
    for (const jarvis::workspace::Profile &p : ws.profiles)
        needed = std::max(needed, p.desktop);
    auto get = QDBusMessage::createMethodCall(u"org.kde.KWin"_s, u"/VirtualDesktopManager"_s, u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
    get << u"org.kde.KWin.VirtualDesktopManager"_s << u"count"_s;
    call(get, [this, ru, ws, needed](const QDBusMessage &reply) {
        if (reply.type() == QDBusMessage::ErrorMessage) {
            finish(false, (ru ? u"KWin недоступен: %1"_s : u"KWin is not available: %1"_s).arg(reply.errorMessage()));
            return;
        }
        const int count = reply.arguments().value(0).value<QDBusVariant>().variant().toInt();
        QJsonObject state = loadState();
        QJsonArray made = state.value(u"desktopsCreated"_s).toArray();
        for (int n = count + 1; n <= needed; ++n) {
            QString name = QString::number(n);
            for (const jarvis::workspace::Profile &p : ws.profiles)
                if (p.desktop == n)
                    name = p.label;
            auto create = QDBusMessage::createMethodCall(u"org.kde.KWin"_s, u"/VirtualDesktopManager"_s, u"org.kde.KWin.VirtualDesktopManager"_s, u"createDesktop"_s);
            create << uint(n - 1) << name;
            QDBusConnection::sessionBus().call(create, QDBus::Block, 3000);
            made.append(n);
        }
        state[u"desktopsCreated"_s] = made;
        saveState(state);
        finish(true, needed > count ? (ru ? u"Создано рабочих столов: %1."_s : u"Desktops created: %1."_s).arg(needed - count)
                                    : (ru ? u"Рабочих столов достаточно (%1)."_s : u"There are enough desktops (%1)."_s).arg(count));
    });
}

QVariantList DesktopShell::profiles() const
{
    QVariantList out;
    for (const jarvis::workspace::Profile &p : jarvis::workspace::load().profiles) {
        QVariantMap m = jarvis::workspace::toJson(p).toVariantMap();
        m.insert(u"id"_s, p.id);
        out.append(m);
    }
    return out;
}

QString DesktopShell::setProfile(const QString &id, const QString &key, const QVariant &value)
{
    jarvis::workspace::State state = jarvis::workspace::load();
    QJsonObject o;
    for (const jarvis::workspace::Profile &p : state.profiles)
        if (p.id == id)
            o = jarvis::workspace::toJson(p);
    if (o.isEmpty() || !QStringList{u"apps"_s, u"paths"_s, u"widgets"_s, u"desktop"_s, u"label"_s, u"activity"_s}.contains(key))
        return tr("Unknown option.");
    o[key] = QJsonValue::fromVariant(value);
    const jarvis::workspace::Profile edited = jarvis::workspace::fromJson(id, o);
    for (jarvis::workspace::Profile &p : state.profiles)
        if (p.id == id)
            p = edited;
    QString error;
    if (!jarvis::workspace::save(state, &error))
        return error;
    emit changed();
    return {};
}

QVariantList DesktopShell::findApps(const QString &words) const
{
    QVariantList out;
    for (const jarvis::workspace::App &a : jarvis::workspace::findApplications(words.left(60), 8))
        out.append(QVariantMap{{u"id"_s, a.desktopId}, {u"name"_s, a.name}, {u"icon"_s, a.icon}});
    return out;
}

QVariantMap DesktopShell::describeApp(const QString &alternatives) const
{
    const QString id = jarvis::workspace::resolveApp(alternatives);
    const QString shown = id.isEmpty() ? alternatives.section(u'|', 0, 0) : id;
    QString name = shown, icon;
    for (const jarvis::workspace::App &a : jarvis::workspace::findApplications(shown, 20))
        if (a.desktopId == shown) {
            name = a.name;
            icon = a.icon;
            break;
        }
    return {{u"id"_s, alternatives}, {u"name"_s, name}, {u"icon"_s, icon}, {u"installed"_s, !id.isEmpty()}};
}

void DesktopShell::setEditMode(bool on)
{
    auto msg = QDBusMessage::createMethodCall(kPlasma, u"/PlasmaShell"_s, u"org.freedesktop.DBus.Properties"_s, u"Set"_s);
    msg << u"org.kde.PlasmaShell"_s << u"editMode"_s << QVariant::fromValue(QDBusVariant(on));
    call(msg, [](const QDBusMessage &) {});
}

int DesktopShell::runFromTerminal(const QString &what)
{
    DesktopShell shell;
    QEventLoop loop;
    bool ok = false;
    connect(&shell, &DesktopShell::finished, &loop, [&](bool success, const QString &message) {
        ok = success;
        std::fprintf(stdout, "%s\n", message.toUtf8().constData());
        loop.quit();
    });
    connect(&shell, &DesktopShell::changed, &loop, [&shell] {
        if (shell.busy())
            std::fprintf(stdout, "… %s\n", shell.status().toUtf8().constData());
    });
    const auto run = [&](const std::function<void()> &start) {
        QTimer::singleShot(0, &shell, start);
        QTimer::singleShot(120000, &loop, &QEventLoop::quit);
        loop.exec();
    };
    if (what == u"standard") {
        run([&] { shell.restore(); });
        return ok ? 0 : 1;
    }
    // jarvis: check, print the plan, apply. The command itself is the user's confirmation.
    run([&] { shell.check(); });
    for (const QVariant &c : shell.checks()) {
        const QVariantMap m = c.toMap();
        std::fprintf(stdout, "[%s] %s: %s\n", m.value(u"ok"_s).toBool() ? "ok" : "!!", m.value(u"name"_s).toString().toUtf8().constData(),
                     m.value(u"text"_s).toString().toUtf8().constData());
    }
    if (!ok)
        return 1;
    for (const QString &line : shell.plan())
        std::fprintf(stdout, "  • %s\n", line.toUtf8().constData());
    run([&] { shell.apply(); });
    return ok ? 0 : 1;
}

// ---- The dock as Plasma has it (0.21) ---------------------------------------

namespace {

// Plasma keeps the widgets' settings in appletsrc and the panels' size and
// hiding in plasmashellrc; either changing may mean the dock changed.
QStringList layoutFiles()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return {dir + u"/plasma-org.kde.plasma.desktop-appletsrc"_s, dir + u"/plasmashellrc"_s};
}

QDateTime layoutStamp()
{
    QDateTime newest;
    for (const QString &f : layoutFiles())
        newest = std::max(newest, QFileInfo(f).lastModified());
    return newest;
}

const QStringList kPanelKeys{u"dockPosition"_s, u"dockSize"_s, u"dockAlignment"_s, u"dockAutohide"_s};

} // namespace

void DesktopShell::setWatchDock(bool on)
{
    if (on == m_watchDock)
        return;
    m_watchDock = on;
    if (on) {
        if (!m_layoutChanged.isSingleShot()) {
            m_layoutChanged.setSingleShot(true);
            m_layoutChanged.setInterval(700);
            connect(&m_layoutChanged, &QTimer::timeout, this, [this] {
                // Plasma saves a file by renaming a new one over it: watch again.
                for (const QString &f : layoutFiles())
                    if (QFileInfo::exists(f) && !m_layoutWatcher.files().contains(f))
                        m_layoutWatcher.addPath(f);
                const QDateTime stamp = layoutStamp();
                if (stamp != m_layoutStamp) {
                    m_layoutStamp = stamp;
                    readDock();
                }
            });
            connect(&m_layoutWatcher, &QFileSystemWatcher::fileChanged, &m_layoutChanged, qOverload<>(&QTimer::start));
            connect(&m_layoutWatcher, &QFileSystemWatcher::directoryChanged, &m_layoutChanged, qOverload<>(&QTimer::start));
        }
        m_layoutWatcher.addPath(QFileInfo(layoutFiles().first()).absolutePath());
        for (const QString &f : layoutFiles())
            if (QFileInfo::exists(f))
                m_layoutWatcher.addPath(f);
        m_layoutStamp = layoutStamp();
        readDock();
    } else {
        const QStringList watched = m_layoutWatcher.files() + m_layoutWatcher.directories();
        if (!watched.isEmpty())
            m_layoutWatcher.removePaths(watched);
    }
    emit dockChanged();
}

void DesktopShell::readDock()
{
    runDock({});
}

void DesktopShell::runDock(const QJsonObject &changes)
{
    m_dock[u"reading"_s] = true;
    emit dockChanged();
    script(jarvis::shell::dockScript(changes), [this](bool ok, const QJsonObject &printed, const QString &error) {
        const QJsonObject keys = jarvis::shell::dockConfigKeys();
        QVariantMap next{{u"reading"_s, false}, {u"found"_s, ok && printed.value(u"found"_s).toBool()}};
        if (!ok) {
            next[u"error"_s] = russian() ? u"Plasma не отвечает: %1"_s.arg(error) : u"Plasma does not answer: %1"_s.arg(error);
        } else if (printed.value(u"found"_s).toBool()) {
            next[u"dockPosition"_s] = printed.value(u"location"_s).toString();
            next[u"dockSize"_s] = printed.value(u"height"_s).toInt();
            next[u"dockAlignment"_s] = printed.value(u"alignment"_s).toString();
            next[u"dockAutohide"_s] = printed.value(u"hiding"_s).toString() != u"none";
            const QJsonObject config = printed.value(u"config"_s).toObject();
            for (auto it = keys.begin(); it != keys.end(); ++it) {
                const QJsonValue def = it.value().toObject().value(u"default"_s);
                const QString raw = config.value(it.key()).toString();
                if (def.isBool())
                    next[it.key()] = raw.isEmpty() ? def.toBool() : raw == u"true";
                else
                    next[it.key()] = raw.isEmpty() ? def.toInt() : std::clamp(raw.toInt(), it.value().toObject().value(u"min"_s).toInt(),
                                                                               it.value().toObject().value(u"max"_s).toInt());
            }
            // Plasma is where the panel really is: config.json follows it.
            jarvis::ConfigData d = jarvis::Config::loadFileOnly();
            bool differs = false;
            for (const QString &k : kPanelKeys) {
                const QJsonValue v = QJsonValue::fromVariant(next.value(k));
                if (d.desktop.value(k) != v && jarvis::integration::setDesktopValue(d.desktop, k, v).isEmpty())
                    differs = true;
            }
            if (differs)
                jarvis::Config::save(d, nullptr);
        }
        m_dock = next;
        emit dockChanged();
    });
}

QString DesktopShell::setDock(const QString &key, const QVariant &value)
{
    const bool ru = russian();
    if (kPanelKeys.contains(key)) {
        jarvis::ConfigData d = jarvis::Config::loadFileOnly();
        if (!jarvis::integration::setDesktopValue(d.desktop, key, QJsonValue::fromVariant(value)).isEmpty())
            return ru ? u"Такое значение недопустимо."_s : u"This value is not allowed."_s;
        QString error;
        if (!jarvis::Config::save(d, &error))
            return error;
        if (!m_dock.value(u"found"_s).toBool())
            return {}; // saved: JARVIS DESKTOP uses it when it adds the dock
        const QJsonObject n = d.desktop;
        QJsonObject panel;
        if (key == u"dockPosition")
            panel[u"location"_s] = n.value(key);
        else if (key == u"dockSize")
            panel[u"height"_s] = n.value(key);
        else if (key == u"dockAlignment")
            panel[u"alignment"_s] = n.value(key);
        else
            panel[u"hiding"_s] = n.value(key).toBool() ? u"autohide"_s : u"none"_s;
        // Shown at once (the slider or spin box does not jump back); Plasma's answer corrects it.
        m_dock[key] = n.value(key).toVariant();
        emit dockChanged();
        runDock({{u"panel"_s, panel}});
        return {};
    }
    const QJsonObject spec = jarvis::shell::dockConfigKeys().value(key).toObject();
    if (spec.isEmpty())
        return ru ? u"Неизвестная настройка."_s : u"Unknown option."_s;
    if (!m_dock.value(u"found"_s).toBool())
        return ru ? u"Дока Jarvis нет на панели: эта настройка появится вместе с ним."_s
                  : u"The Jarvis dock is not on a panel: this setting comes with it."_s;
    QJsonValue v;
    if (spec.value(u"default"_s).isBool()) {
        v = value.toBool();
    } else {
        bool ok = false;
        const int n = value.toInt(&ok);
        if (!ok || n < spec.value(u"min"_s).toInt() || n > spec.value(u"max"_s).toInt())
            return (ru ? u"Допустимо от %1 до %2."_s : u"Allowed: %1 to %2."_s).arg(spec.value(u"min"_s).toInt()).arg(spec.value(u"max"_s).toInt());
        v = n;
    }
    m_dock[key] = v.toVariant();
    emit dockChanged();
    runDock({{u"config"_s, QJsonObject{{key, v}}}});
    return {};
}

void DesktopShell::resetDockLook()
{
    if (!m_dock.value(u"found"_s).toBool())
        return;
    QJsonObject config;
    const QJsonObject keys = jarvis::shell::dockConfigKeys();
    for (auto it = keys.begin(); it != keys.end(); ++it)
        config[it.key()] = it.value().toObject().value(u"default"_s);
    runDock({{u"config"_s, config}});
}
