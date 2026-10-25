#include "kde_settings.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QFileInfo>
#include <QKeySequence>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace {

const QString kAccel = u"org.kde.kglobalaccel"_s;
const QString kAccelPath = u"/kglobalaccel"_s;
const QString kAccelIface = u"org.kde.KGlobalAccel"_s;

QDBusMessage accel(const QString &method, const QVariantList &args)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kAccel, kAccelPath, kAccelIface, method);
    msg.setArguments(args);
    msg.setAutoStartService(false);
    return QDBusConnection::sessionBus().call(msg, QDBus::Block, 3000);
}

int combine(int key, int modifiers)
{
    const int mods = modifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    return key | mods;
}

bool isModifierKey(int key)
{
    return key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta || key == Qt::Key_AltGr
        || key == Qt::Key_Super_L || key == Qt::Key_Super_R || key == Qt::Key_Hyper_L || key == Qt::Key_Hyper_R || key == 0
        || key == Qt::Key_unknown;
}


QString findTool(const QStringList &names)
{
    for (const QString &n : names)
        if (const QString path = QStandardPaths::findExecutable(n); !path.isEmpty())
            return path;
    return {};
}

} // namespace

KdeSettings::KdeSettings(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<QList<int>>();
}

bool KdeSettings::systemSettings() const
{
    return !findTool({u"systemsettings"_s, u"systemsettings5"_s, u"kcmshell6"_s, u"kcmshell5"_s}).isEmpty();
}

bool KdeSettings::hasModule(const QString &kcm) const
{
    static const QRegularExpression valid(u"^kcm_?[a-z0-9_]{2,60}$"_s);
    if (!valid.match(kcm).hasMatch())
        return false;
    // Plasma 6 modules sit with Qt 6's plugins; Plasma 5's (Kubuntu 24.04) with Qt 5's.
    QStringList dirs;
    for (const QString &dir : QCoreApplication::libraryPaths()) {
        dirs << dir;
        if (dir.contains(u"qt6"_s))
            dirs << QString(dir).replace(u"qt6"_s, u"qt5"_s);
    }
    for (const QString &dir : dirs)
        for (const QString &sub : {u"/plasma/kcms/systemsettings/"_s, u"/plasma/kcms/systemsettings_qwidgets/"_s, u"/plasma/kcms/"_s})
            if (QFileInfo::exists(dir + sub + kcm + u".so"_s))
                return true;
    return false;
}

QString KdeSettings::openModule(const QString &kcm) const
{
    if (!hasModule(kcm))
        return QCoreApplication::translate("KdeSettings", "This part of System Settings is not installed here.");
    const QString settings = findTool({u"systemsettings"_s, u"systemsettings5"_s});
    if (!settings.isEmpty() && QProcess::startDetached(settings, {kcm}))
        return {};
    const QString shell = findTool({u"kcmshell6"_s, u"kcmshell5"_s});
    if (!shell.isEmpty() && QProcess::startDetached(shell, {kcm}))
        return {};
    return QCoreApplication::translate("KdeSettings", "KDE System Settings could not be started.");
}

QVariantMap KdeSettings::shortcut(const QString &component, const QString &action) const
{
    const QDBusMessage reply = accel(u"shortcut"_s, {QStringList{component, action, QString(), QString()}});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {{u"available"_s, false}, {u"keys"_s, QString()}};
    const QList<int> keys = qdbus_cast<QList<int>>(reply.arguments().first());
    QStringList native;
    QStringList portable;
    for (int k : keys) {
        if (k == 0)
            continue;
        native << QKeySequence(k).toString(QKeySequence::NativeText);
        portable << QKeySequence(k).toString(QKeySequence::PortableText);
    }
    return {{u"available"_s, true}, {u"keys"_s, native.join(u", "_s)}, {u"portable"_s, portable.join(u", "_s)}};
}

QString KdeSettings::keyText(int key, int modifiers) const
{
    if (isModifierKey(key))
        return {};
    return QKeySequence(combine(key, modifiers)).toString(QKeySequence::NativeText);
}

QString KdeSettings::portableKey(int key, int modifiers) const
{
    if (isModifierKey(key))
        return {};
    return QKeySequence(combine(key, modifiers)).toString(QKeySequence::PortableText);
}

QVariantMap KdeSettings::conflict(int key, int modifiers, const QString &component, const QString &action) const
{
    const bool fkey = key >= Qt::Key_F1 && key <= Qt::Key_F35;
    if (isModifierKey(key) || (!fkey && !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))))
        return {{u"invalid"_s, true}, {u"taken"_s, false}};
    const QDBusMessage reply = accel(u"action"_s, {combine(key, modifiers)});
    if (reply.type() != QDBusMessage::ReplyMessage)
        return {{u"invalid"_s, false}, {u"taken"_s, false}, {u"unknown"_s, true}};
    const QStringList owner = reply.arguments().value(0).toStringList();
    if (owner.size() < 2 || owner.at(0).isEmpty())
        return {{u"invalid"_s, false}, {u"taken"_s, false}};
    const bool self = owner.at(0) == component && owner.at(1) == action;
    const QString componentName = owner.value(2).isEmpty() ? owner.at(0) : owner.value(2);
    const QString actionName = owner.value(3).isEmpty() ? owner.at(1) : owner.value(3);
    return {{u"invalid"_s, false}, {u"taken"_s, !self}, {u"self"_s, self}, {u"owner"_s, componentName + u" — "_s + actionName}};
}

QString KdeSettings::setShortcut(const QString &component, const QString &action, const QString &componentName,
                                 const QString &actionName, int key, int modifiers) const
{
    QList<int> keys;
    if (key != 0) {
        const QVariantMap c = conflict(key, modifiers, component, action);
        if (c.value(u"invalid"_s).toBool())
            return QCoreApplication::translate("KdeSettings", "Use Ctrl, Alt or Meta together with a key (or an F-key alone).");
        if (c.value(u"taken"_s).toBool())
            return QCoreApplication::translate("KdeSettings", "%1 is already used by %2. Choose another key, or free it in System Settings → Shortcuts.")
                .arg(keyText(key, modifiers), c.value(u"owner"_s).toString());
        keys << combine(key, modifiers);
    }
    const QDBusMessage reply = accel(u"setForeignShortcut"_s, {QStringList{component, action, componentName, actionName}, QVariant::fromValue(keys)});
    if (reply.type() == QDBusMessage::ErrorMessage)
        return QCoreApplication::translate("KdeSettings", "KDE's shortcut service is not available.");
    // Only a key KDE reports back counts as set.
    const QString now = shortcut(component, action).value(u"portable"_s).toString();
    const QString wanted = key ? portableKey(key, modifiers) : QString();
    if (now != wanted)
        return QCoreApplication::translate("KdeSettings", "KDE did not accept the key.");
    return {};
}

void KdeSettings::blockGlobalShortcuts(bool block) const
{
    accel(u"blockGlobalShortcuts"_s, {block});
}

QVariantMap KdeSettings::defaultKey(const QString &action) const
{
    if (action == u"overlay")
        return {{u"key"_s, int(Qt::Key_J)}, {u"modifiers"_s, int(Qt::ControlModifier)}};
    return {{u"key"_s, int(Qt::Key_J)}, {u"modifiers"_s, int(Qt::MetaModifier)}};
}
