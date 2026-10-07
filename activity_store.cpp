#include "activity_store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

QString todayKey(qint64 nowMs)
{
    return QDateTime::fromMSecsSinceEpoch(nowMs).date().toString(Qt::ISODate);
}

int hourOf(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms).time().hour();
}

double rounded(double v)
{
    return std::round(v * 10.0) / 10.0;
}

QJsonArray addToHour(QJsonArray hours, int hour, double amount)
{
    while (hours.size() < 24)
        hours.append(0);
    hours[hour] = rounded(hours.at(hour).toDouble() + amount);
    return hours;
}

double around(const QJsonArray &hours, int hour)
{
    double sum = 0;
    for (int d = -1; d <= 1; ++d)
        sum += hours.at((hour + d + 24) % 24).toDouble();
    return sum;
}

// Shell surfaces and Jarvis itself say nothing about what the user is doing.
bool ignored(const QString &key, const QString &cls)
{
    static const QSet<QString> list = {
        u"plasmashell"_s, u"org.kde.plasmashell"_s, u"krunner"_s, u"org.kde.krunner"_s,
        u"jarvis"_s, u"org.jarvis.jarvis"_s, u"kwin"_s, u"kwin_wayland"_s, u"kwin_x11"_s,
        u"ksmserver"_s, u"ksmserver-logout-greeter"_s, u"org.kde.ksmserver-logout-greeter"_s,
        u"xdg-desktop-portal-kde"_s, u"org.freedesktop.impl.portal.desktop.kde"_s,
        u"polkit-kde-authentication-agent-1"_s, u"org.kde.polkit-kde-authentication-agent-1"_s,
        u"kscreenlocker_greet"_s, u"org.kde.plasma.emojier"_s,
    };
    return key.isEmpty() || list.contains(key) || list.contains(cls);
}

} // namespace

ActivityStore::ActivityStore()
{
    load();
}

ActivityStore::~ActivityStore()
{
    flush();
}

QString ActivityStore::path()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/jarvis/activity.json"_s;
}

void ActivityStore::load()
{
    QFile f(path());
    m_root = (f.open(QIODevice::ReadOnly) && f.size() < 8 * 1024 * 1024)
                 ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject{};
}

void ActivityStore::reloadIfIdle()
{
    if (!m_dirty && m_current.key.isEmpty())
        load();
}

bool ActivityStore::flush()
{
    if (!m_dirty)
        return true;
    pruneDays(QDateTime::currentMSecsSinceEpoch());
    QJsonObject apps = m_root.value(u"apps"_s).toObject();
    if (apps.size() > kMaxApps) {
        QList<std::pair<double, QString>> order;
        for (auto it = apps.begin(); it != apps.end(); ++it)
            order.append({it.value().toObject().value(u"seconds"_s).toDouble(), it.key()});
        std::sort(order.begin(), order.end());
        for (int i = 0; i < order.size() - kMaxApps; ++i)
            apps.remove(order.at(i).second);
        m_root[u"apps"_s] = apps;
    }
    m_root[u"version"_s] = 1;

    QDir dir(QFileInfo(path()).absolutePath());
    if (!dir.mkpath(u"."_s))
        return false;
    QFile::setPermissions(dir.absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile f(path());
    if (!f.open(QIODevice::WriteOnly) || !f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return false;
    const QByteArray data = QJsonDocument(m_root).toJson(QJsonDocument::Compact);
    if (f.write(data) != data.size() || !f.commit())
        return false;
    m_dirty = false;
    return true;
}

void ActivityStore::pruneDays(qint64 nowMs)
{
    QJsonObject days = m_root.value(u"days"_s).toObject();
    const QDate oldest = QDateTime::fromMSecsSinceEpoch(nowMs).date().addDays(-(kKeepDays - 1));
    for (auto it = days.begin(); it != days.end();) {
        const QDate d = QDate::fromString(it.key(), Qt::ISODate);
        if (!d.isValid() || d < oldest)
            it = days.erase(it);
        else
            ++it;
    }
    m_root[u"days"_s] = days;
}

void ActivityStore::setKeepTitles(bool keep)
{
    m_keepTitles = keep;
    if (!keep)
        m_current.title.clear();
}

QString ActivityStore::category(const QString &appClass, const QString &desktopId)
{
    static const QList<std::pair<QString, QSet<QString>>> table = {
        {u"coding"_s, {u"code"_s, u"codium"_s, u"vscode"_s, u"vscodium"_s, u"kate"_s, u"kwrite"_s,
                       u"kdevelop"_s, u"qtcreator"_s, u"clion"_s, u"pycharm"_s, u"idea"_s, u"intellij"_s,
                       u"webstorm"_s, u"goland"_s, u"rider"_s, u"rustrover"_s, u"phpstorm"_s,
                       u"android"_s, u"sublime"_s, u"sublime_text"_s, u"zed"_s, u"emacs"_s, u"gvim"_s,
                       u"nvim"_s, u"neovim"_s, u"vim"_s, u"geany"_s, u"jetbrains"_s, u"cursor"_s,
                       u"windsurf"_s, u"arduino"_s, u"platformio"_s, u"github"_s, u"gitkraken"_s,
                       u"texteditor"_s, u"gedit"_s, u"mousepad"_s, u"featherpad"_s, u"notepadqq"_s}},
        {u"terminal"_s, {u"konsole"_s, u"yakuake"_s, u"alacritty"_s, u"kitty"_s, u"terminal"_s,
                         u"terminator"_s, u"wezterm"_s, u"foot"_s, u"xterm"_s, u"tilix"_s,
                         u"ghostty"_s, u"warp"_s, u"ptyxis"_s}},
        {u"browsing"_s, {u"firefox"_s, u"chromium"_s, u"chrome"_s, u"brave"_s, u"vivaldi"_s, u"opera"_s,
                         u"falkon"_s, u"librewolf"_s, u"zen"_s, u"edge"_s, u"epiphany"_s, u"browser"_s,
                         u"torbrowser"_s, u"waterfox"_s, u"floorp"_s}},
        {u"communication"_s, {u"telegram"_s, u"telegramdesktop"_s, u"discord"_s, u"discordapp"_s,
                              u"slack"_s, u"thunderbird"_s, u"kmail"_s, u"kmail2"_s, u"signal"_s,
                              u"element"_s, u"riot"_s, u"skype"_s, u"zoom"_s, u"teams"_s,
                              u"whatsapp"_s, u"neochat"_s, u"mattermost"_s, u"evolution"_s,
                              u"viber"_s, u"vesktop"_s, u"betterbird"_s}},
        {u"office"_s, {u"libreoffice"_s, u"soffice"_s, u"okular"_s, u"calligra"_s, u"onlyoffice"_s,
                       u"desktopeditors"_s, u"wps"_s, u"evince"_s, u"zotero"_s, u"obsidian"_s,
                       u"joplin"_s, u"logseq"_s, u"notion"_s, u"xournalpp"_s, u"kile"_s,
                       u"texstudio"_s, u"lyx"_s, u"marknote"_s, u"ghostwriter"_s}},
        {u"design"_s, {u"krita"_s, u"gimp"_s, u"inkscape"_s, u"blender"_s, u"kdenlive"_s,
                       u"darktable"_s, u"freecad"_s, u"kicad"_s, u"figma"_s, u"shotcut"_s,
                       u"audacity"_s, u"obs"_s, u"obsproject"_s, u"rawtherapee"_s, u"digikam"_s,
                       u"scribus"_s, u"pinta"_s, u"ardour"_s, u"lmms"_s, u"openshot"_s, u"glaxnimate"_s}},
        {u"media"_s, {u"vlc"_s, u"mpv"_s, u"elisa"_s, u"spotify"_s, u"haruna"_s, u"smplayer"_s,
                      u"celluloid"_s, u"rhythmbox"_s, u"totem"_s, u"strawberry"_s, u"clementine"_s,
                      u"amarok"_s, u"audacious"_s, u"plex"_s, u"jellyfin"_s, u"stremio"_s, u"freetube"_s}},
        {u"gaming"_s, {u"steam"_s, u"lutris"_s, u"heroic"_s, u"heroicgameslauncher"_s, u"hgl"_s,
                       u"minecraft"_s, u"retroarch"_s, u"libretro"_s, u"bottles"_s, u"prismlauncher"_s,
                       u"gamescope"_s, u"itch"_s, u"dosbox"_s}},
        {u"files"_s, {u"dolphin"_s, u"nautilus"_s, u"thunar"_s, u"nemo"_s, u"pcmanfm"_s, u"ark"_s,
                      u"filelight"_s, u"krusader"_s, u"doublecmd"_s}},
        {u"system"_s, {u"systemsettings"_s, u"discover"_s, u"systemmonitor"_s, u"ksysguard"_s,
                       u"partitionmanager"_s, u"kinfocenter"_s, u"gparted"_s, u"synaptic"_s,
                       u"kwalletmanager"_s, u"kwalletmanager5"_s, u"keepassxc"_s, u"bitwarden"_s}},
    };
    static const QRegularExpression separators(u"[.\\-_ /]+"_s);
    const QStringList tokens = (appClass + u' ' + desktopId).toLower().split(separators, Qt::SkipEmptyParts);
    for (const auto &[id, words] : table)
        for (const QString &t : tokens)
            if (words.contains(t))
                return id;
    return u"other"_s;
}

bool ActivityStore::privateTitle(const QString &caption)
{
    static const QRegularExpression re(
        u"private browsing|private window|incognito|inprivate|приватн|инкогнито|keepass|bitwarden|1password|kwallet|password|пароль"_s,
        QRegularExpression::CaseInsensitiveOption);
    return re.match(caption).hasMatch();
}

QString ActivityStore::displayName(const QString &appClass, const QString &desktopId)
{
    const QString cacheKey = desktopId + u'|' + appClass;
    const auto cached = m_names.constFind(cacheKey);
    if (cached != m_names.cend())
        return *cached;

    QString name;
    if (!desktopId.isEmpty()) {
        const QString file = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktopId + u".desktop"_s);
        QFile f(file);
        if (!file.isEmpty() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            bool entry = false;
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (line.startsWith(u'['))
                    entry = line == u"[Desktop Entry]";
                else if (entry && line.startsWith(u"Name=")) {
                    name = line.mid(5).trimmed();
                    break;
                }
            }
        }
    }
    if (name.isEmpty()) {
        // A name resolved earlier (or by another session) beats a guess from the class.
        const QString key = desktopId.isEmpty() ? appClass : desktopId.toLower();
        name = m_root.value(u"apps"_s).toObject().value(key).toObject().value(u"name"_s).toString();
    }
    if (name.isEmpty()) {
        QString base = (desktopId.isEmpty() ? appClass : desktopId).section(u'.', -1);
        if (base.isEmpty())
            base = appClass;
        if (!base.isEmpty())
            base[0] = base.at(0).toUpper();
        name = base;
    }
    name = name.left(60);
    m_names.insert(cacheKey, name);
    return name;
}

void ActivityStore::accrue(qint64 nowMs)
{
    if (m_current.key.isEmpty() || m_locked) {
        m_current.lastAccrued = nowMs;
        return;
    }
    const qint64 end = std::min(nowMs, m_current.lastEvent + kIdleCapMs);
    if (end > m_current.lastAccrued) {
        const double seconds = double(end - m_current.lastAccrued) / 1000.0;
        const int hour = hourOf(m_current.lastAccrued);
        const QString day = todayKey(m_current.lastAccrued);

        QJsonObject apps = m_root.value(u"apps"_s).toObject();
        QJsonObject app = apps.value(m_current.key).toObject();
        app[u"name"_s] = m_current.name;
        app[u"desktop"_s] = m_current.desktopId;
        app[u"category"_s] = m_current.category;
        app[u"seconds"_s] = rounded(app.value(u"seconds"_s).toDouble() + seconds);
        app[u"hours"_s] = addToHour(app.value(u"hours"_s).toArray(), hour, seconds);
        app[u"last"_s] = double(nowMs);
        apps[m_current.key] = app;
        m_root[u"apps"_s] = apps;

        QJsonObject days = m_root.value(u"days"_s).toObject();
        QJsonObject today = days.value(day).toObject();
        today[m_current.key] = rounded(today.value(m_current.key).toDouble() + seconds);
        days[day] = today;
        m_root[u"days"_s] = days;
        m_dirty = true;
    }
    m_current.lastAccrued = nowMs;
}

void ActivityStore::closeSpan(qint64 nowMs)
{
    if (m_current.key.isEmpty())
        return;
    const qint64 end = std::min(nowMs, m_current.lastEvent + kIdleCapMs);
    const double seconds = double(end - m_current.since) / 1000.0;
    if (seconds < 5)
        return;
    QJsonArray recent = m_root.value(u"recent"_s).toArray();
    QJsonObject span{{u"t"_s, double(m_current.since)}, {u"app"_s, m_current.name},
                     {u"category"_s, m_current.category}, {u"seconds"_s, rounded(seconds)}};
    if (!m_current.title.isEmpty())
        span[u"title"_s] = m_current.title;
    recent.prepend(span);
    while (recent.size() > kMaxRecent)
        recent.removeLast();
    m_root[u"recent"_s] = recent;
    m_dirty = true;
}

void ActivityStore::windowActivated(const QString &caption, const QString &appClass,
                                    const QString &desktopId, qint64 nowMs)
{
    accrue(nowMs);
    const QString cls = appClass.trimmed().toLower().left(120);
    const QString desktop = desktopId.trimmed().left(120);
    const QString key = desktop.isEmpty() ? cls : desktop.toLower();
    if (ignored(key, cls)) {
        closeSpan(nowMs);
        m_current = {};
        m_current.lastAccrued = nowMs;
        return;
    }
    const QString title = (m_keepTitles && !privateTitle(caption)) ? caption.simplified().left(120) : QString();
    if (key == m_current.key) {
        m_current.title = title;
        m_current.lastEvent = nowMs;
        return;
    }
    closeSpan(nowMs);
    if (!m_current.key.isEmpty()) {
        QJsonObject transitions = m_root.value(u"transitions"_s).toObject();
        const QString t = m_current.key + u'>' + key;
        transitions[t] = transitions.value(t).toInt() + 1;
        if (transitions.size() > kMaxTransitions) {
            QString weakest;
            int min = INT_MAX;
            for (auto it = transitions.begin(); it != transitions.end(); ++it)
                if (it.value().toInt() < min) {
                    min = it.value().toInt();
                    weakest = it.key();
                }
            transitions.remove(weakest);
        }
        m_root[u"transitions"_s] = transitions;
        m_dirty = true;
    }
    m_current = Current{key, displayName(cls, desktop), desktop, category(cls, desktop), title, nowMs, nowMs, nowMs};
}

void ActivityStore::tick(qint64 nowMs)
{
    accrue(nowMs);
}

void ActivityStore::setLocked(bool locked, qint64 nowMs)
{
    accrue(nowMs);
    m_locked = locked;
    if (!locked)
        m_current.lastEvent = nowMs; // unlocking is user activity
}

void ActivityStore::stop(qint64 nowMs)
{
    accrue(nowMs);
    closeSpan(nowMs);
    m_current = {};
}

void ActivityStore::recordAction(const QString &id, const QString &label, qint64 nowMs)
{
    if (id.isEmpty() || id.size() > 200)
        return;
    reloadIfIdle();
    QJsonObject actions = m_root.value(u"actions"_s).toObject();
    QJsonObject a = actions.value(id).toObject();
    a[u"label"_s] = label.left(80);
    a[u"count"_s] = a.value(u"count"_s).toInt() + 1;
    a[u"hours"_s] = addToHour(a.value(u"hours"_s).toArray(), hourOf(nowMs), 1);
    a[u"last"_s] = double(nowMs);
    actions[id] = a;
    if (actions.size() > kMaxActions) {
        QString weakest;
        double min = 1e18;
        for (auto it = actions.begin(); it != actions.end(); ++it) {
            const double score = it.value().toObject().value(u"last"_s).toDouble();
            if (score < min) {
                min = score;
                weakest = it.key();
            }
        }
        actions.remove(weakest);
    }
    m_root[u"actions"_s] = actions;
    m_dirty = true;
}

void ActivityStore::clear()
{
    m_root = QJsonObject{};
    m_current = {};
    m_dirty = true;
    flush();
}

QList<std::pair<QString, double>> ActivityStore::todayApps(qint64 nowMs) const
{
    QList<std::pair<QString, double>> list;
    const QJsonObject today = m_root.value(u"days"_s).toObject().value(todayKey(nowMs)).toObject();
    for (auto it = today.begin(); it != today.end(); ++it)
        list.append({it.key(), it.value().toDouble()});
    std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    return list;
}

QJsonArray ActivityStore::suggestions(qint64 nowMs, int max) const
{
    struct Item
    {
        double score;
        QJsonObject data;
    };
    QList<Item> items;
    const int hour = hourOf(nowMs);

    const QJsonObject actions = m_root.value(u"actions"_s).toObject();
    for (auto it = actions.begin(); it != actions.end(); ++it) {
        const QJsonObject a = it.value().toObject();
        const int count = a.value(u"count"_s).toInt();
        if (count < 2)
            continue;
        const double score = count + 2.0 * around(a.value(u"hours"_s).toArray(), hour);
        items.append({score, QJsonObject{{u"id"_s, it.key()}, {u"label"_s, a.value(u"label"_s)},
                                         {u"kind"_s, u"action"_s}}});
    }

    QList<Item> apps;
    const QJsonObject appMap = m_root.value(u"apps"_s).toObject();
    for (auto it = appMap.begin(); it != appMap.end(); ++it) {
        const QJsonObject a = it.value().toObject();
        const QString desktop = a.value(u"desktop"_s).toString();
        if (desktop.isEmpty() || it.key() == m_current.key || a.value(u"seconds"_s).toDouble() < 600)
            continue;
        // Ten minutes around this hour of day count like one action use.
        const double score = around(a.value(u"hours"_s).toArray(), hour) / 600.0
                             + a.value(u"seconds"_s).toDouble() / 36000.0;
        apps.append({score, QJsonObject{{u"id"_s, u"app:"_s + desktop}, {u"label"_s, a.value(u"name"_s)},
                                        {u"kind"_s, u"app"_s}, {u"category"_s, a.value(u"category"_s)}}});
    }
    std::sort(apps.begin(), apps.end(), [](const Item &a, const Item &b) { return a.score > b.score; });
    items.append(apps.mid(0, 3));

    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.score > b.score; });
    QJsonArray out;
    for (const Item &i : std::as_const(items)) {
        if (out.size() >= max)
            break;
        out.append(i.data);
    }
    return out;
}

QJsonObject ActivityStore::snapshot(qint64 nowMs) const
{
    QJsonObject out;
    if (!m_current.key.isEmpty()) {
        QJsonObject cur{{u"name"_s, m_current.name}, {u"category"_s, m_current.category},
                        {u"seconds"_s, rounded(double(nowMs - m_current.since) / 1000.0)}};
        if (!m_current.title.isEmpty())
            cur[u"title"_s] = m_current.title;
        out[u"current"_s] = cur;
    }

    const QJsonObject apps = m_root.value(u"apps"_s).toObject();
    QJsonArray todayList;
    QMap<QString, double> categories;
    double total = 0;
    for (const auto &[key, seconds] : todayApps(nowMs)) {
        const QJsonObject a = apps.value(key).toObject();
        const QString cat = a.value(u"category"_s).toString(u"other"_s);
        categories[cat] += seconds;
        total += seconds;
        if (todayList.size() < 8)
            todayList.append(QJsonObject{{u"name"_s, a.value(u"name"_s).toString(key)},
                                         {u"category"_s, cat}, {u"seconds"_s, seconds}});
    }
    QJsonArray catList;
    QList<std::pair<double, QString>> catOrder;
    for (auto it = categories.cbegin(); it != categories.cend(); ++it)
        catOrder.append({it.value(), it.key()});
    std::sort(catOrder.begin(), catOrder.end(), std::greater<>());
    for (const auto &[seconds, id] : catOrder)
        catList.append(QJsonObject{{u"id"_s, id}, {u"seconds"_s, rounded(seconds)}});
    out[u"today"_s] = QJsonObject{{u"total"_s, rounded(total)}, {u"apps"_s, todayList}, {u"categories"_s, catList}};

    // Typical day: all recorded time by hour.
    QJsonArray hours;
    for (int h = 0; h < 24; ++h)
        hours.append(0.0);
    for (auto it = apps.begin(); it != apps.end(); ++it) {
        const QJsonArray ah = it.value().toObject().value(u"hours"_s).toArray();
        for (int h = 0; h < 24 && h < ah.size(); ++h)
            hours[h] = hours.at(h).toDouble() + ah.at(h).toDouble();
    }
    out[u"hours"_s] = hours;

    QJsonArray recent;
    const QJsonArray stored = m_root.value(u"recent"_s).toArray();
    for (int i = 0; i < stored.size() && i < 12; ++i)
        recent.append(stored.at(i));
    out[u"recent"_s] = recent;

    QList<std::pair<int, QJsonObject>> actionOrder;
    const QJsonObject actions = m_root.value(u"actions"_s).toObject();
    for (auto it = actions.begin(); it != actions.end(); ++it) {
        const QJsonObject a = it.value().toObject();
        actionOrder.append({a.value(u"count"_s).toInt(),
                            QJsonObject{{u"id"_s, it.key()}, {u"label"_s, a.value(u"label"_s)},
                                        {u"count"_s, a.value(u"count"_s)}}});
    }
    std::sort(actionOrder.begin(), actionOrder.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QJsonArray actionList;
    for (int i = 0; i < actionOrder.size() && i < 8; ++i)
        actionList.append(actionOrder.at(i).second);
    out[u"actions"_s] = actionList;

    QList<std::pair<int, QString>> transitionOrder;
    const QJsonObject transitions = m_root.value(u"transitions"_s).toObject();
    for (auto it = transitions.begin(); it != transitions.end(); ++it)
        transitionOrder.append({it.value().toInt(), it.key()});
    std::sort(transitionOrder.begin(), transitionOrder.end(), std::greater<>());
    QJsonArray transitionList;
    for (int i = 0; i < transitionOrder.size() && i < 5; ++i) {
        const QStringList pair = transitionOrder.at(i).second.split(u'>');
        if (pair.size() != 2)
            continue;
        transitionList.append(QJsonObject{
            {u"from"_s, apps.value(pair.at(0)).toObject().value(u"name"_s).toString(pair.at(0))},
            {u"to"_s, apps.value(pair.at(1)).toObject().value(u"name"_s).toString(pair.at(1))},
            {u"count"_s, transitionOrder.at(i).first}});
    }
    out[u"transitions"_s] = transitionList;
    out[u"suggestions"_s] = suggestions(nowMs);
    return out;
}

QString ActivityStore::formatDuration(double seconds, Lang lang)
{
    const bool ru = lang == Lang::Ru;
    const int minutes = int(seconds / 60.0 + 0.5);
    if (minutes < 1)
        return ru ? u"меньше минуты"_s : u"under a minute"_s;
    if (minutes < 60)
        return ru ? u"%1 мин"_s.arg(minutes) : u"%1 min"_s.arg(minutes);
    const int h = minutes / 60;
    const int m = minutes % 60;
    if (m == 0)
        return ru ? u"%1 ч"_s.arg(h) : u"%1 h"_s.arg(h);
    return ru ? u"%1 ч %2 мин"_s.arg(h).arg(m) : u"%1 h %2 min"_s.arg(h).arg(m);
}

QString ActivityStore::categoryName(const QString &id, Lang lang)
{
    static const QMap<QString, std::pair<QString, QString>> names = {
        {u"coding"_s, {u"программирование"_s, u"coding"_s}},
        {u"terminal"_s, {u"терминал"_s, u"terminal"_s}},
        {u"browsing"_s, {u"браузер"_s, u"web browsing"_s}},
        {u"communication"_s, {u"общение"_s, u"communication"_s}},
        {u"office"_s, {u"документы"_s, u"documents"_s}},
        {u"design"_s, {u"творчество"_s, u"creative work"_s}},
        {u"media"_s, {u"видео и музыка"_s, u"media"_s}},
        {u"gaming"_s, {u"игры"_s, u"gaming"_s}},
        {u"files"_s, {u"файлы"_s, u"files"_s}},
        {u"system"_s, {u"система"_s, u"system"_s}},
        {u"other"_s, {u"другое"_s, u"other"_s}},
    };
    const auto it = names.constFind(id);
    const auto pair = it == names.cend() ? names.value(u"other"_s) : *it;
    return lang == Lang::Ru ? pair.first : pair.second;
}

QString ActivityStore::currentText(Lang lang, qint64 nowMs, bool tracking) const
{
    const bool ru = lang == Lang::Ru;
    if (!tracking)
        return ru ? u"Отслеживание активности выключено. Включите его в «Центр управления → Память», "
                    u"и я буду видеть, в каком приложении вы работаете."_s
                  : u"Activity tracking is off. Turn it on in Control Center → Memory and I will see "
                    u"which application you are working in."_s;
    if (m_current.key.isEmpty())
        return ru ? u"Сейчас я не вижу активного приложения: рабочий стол, экран блокировки или окно Jarvis."_s
                  : u"I don't see an active application right now: desktop, lock screen or a Jarvis window."_s;
    QString out = ru ? u"Сейчас вы в «%1» (%2) — %3."_s : u"You're in %1 (%2) — %3."_s;
    out = out.arg(m_current.name, categoryName(m_current.category, lang),
                  formatDuration(double(nowMs - m_current.since) / 1000.0, lang));
    if (!m_current.title.isEmpty())
        out += (ru ? u" Окно: «%1»."_s : u" Window: \"%1\"."_s).arg(m_current.title);
    const QJsonArray recent = m_root.value(u"recent"_s).toArray();
    if (!recent.isEmpty()) {
        const QJsonObject prev = recent.first().toObject();
        out += (ru ? u" До этого: «%1», %2."_s : u" Before that: %1, %2."_s)
                   .arg(prev.value(u"app"_s).toString(), formatDuration(prev.value(u"seconds"_s).toDouble(), lang));
    }
    return out;
}

QString ActivityStore::todayText(Lang lang, qint64 nowMs, bool tracking) const
{
    const bool ru = lang == Lang::Ru;
    const auto list = todayApps(nowMs);
    if (list.isEmpty())
        return tracking ? (ru ? u"Сегодня я ещё не записал активности."_s : u"No activity recorded today yet."_s)
                        : currentText(lang, nowMs, false);
    const QJsonObject apps = m_root.value(u"apps"_s).toObject();
    QMap<QString, double> categories;
    double total = 0;
    QStringList appParts;
    for (const auto &[key, seconds] : list) {
        const QJsonObject a = apps.value(key).toObject();
        categories[a.value(u"category"_s).toString(u"other"_s)] += seconds;
        total += seconds;
        if (appParts.size() < 5)
            appParts.append(a.value(u"name"_s).toString(key) + u' ' + formatDuration(seconds, lang));
    }
    QList<std::pair<double, QString>> catOrder;
    for (auto it = categories.cbegin(); it != categories.cend(); ++it)
        catOrder.append({it.value(), it.key()});
    std::sort(catOrder.begin(), catOrder.end(), std::greater<>());
    QStringList catParts;
    for (const auto &[seconds, id] : catOrder)
        if (catParts.size() < 4)
            catParts.append(categoryName(id, lang) + u' ' + formatDuration(seconds, lang));
    return (ru ? u"Сегодня учтено %1 за компьютером.\nПо видам: %2.\nПриложения: %3."_s
               : u"Today: %1 in front of the computer.\nBy kind: %2.\nApps: %3."_s)
        .arg(formatDuration(total, lang), catParts.join(u", "_s), appParts.join(u", "_s));
}

QString ActivityStore::promptContext(qint64 nowMs) const
{
    QString out;
    if (!m_current.key.isEmpty()) {
        out += u"Focused application: %1 (%2), for %3"_s.arg(
            m_current.name, categoryName(m_current.category, Lang::En),
            formatDuration(double(nowMs - m_current.since) / 1000.0, Lang::En));
        if (!m_current.title.isEmpty()) {
            // Titles come from web pages and documents: never let them look like markup.
            QString title = m_current.title;
            title.remove(QRegularExpression(u"[<>{}\"]"_s));
            out += u", window title: \"%1\""_s.arg(title);
        }
        out += u".\n"_s;
    }
    const auto list = todayApps(nowMs);
    if (!list.isEmpty()) {
        const QJsonObject apps = m_root.value(u"apps"_s).toObject();
        QStringList parts;
        for (const auto &[key, seconds] : list) {
            if (parts.size() >= 5)
                break;
            parts.append(apps.value(key).toObject().value(u"name"_s).toString(key) + u' '
                         + formatDuration(seconds, Lang::En));
        }
        out += u"Most used today: "_s + parts.join(u", "_s) + u".\n"_s;
    }
    return out;
}

QStringList ActivityStore::heavyApps(double minSeconds) const
{
    QList<std::pair<double, QString>> list;
    const QJsonObject apps = m_root.value(u"apps"_s).toObject();
    for (auto it = apps.begin(); it != apps.end(); ++it) {
        const QJsonObject a = it.value().toObject();
        if (a.value(u"seconds"_s).toDouble() >= minSeconds && a.value(u"category"_s).toString() != u"system")
            list.append({a.value(u"seconds"_s).toDouble(), a.value(u"name"_s).toString(it.key())});
    }
    std::sort(list.begin(), list.end(), std::greater<>());
    QStringList out;
    for (const auto &[seconds, name] : list)
        out.append(name);
    return out;
}

bool ActivityStore::activeHours(int *from, int *to) const
{
    double hours[24] = {};
    double total = 0;
    const QJsonObject apps = m_root.value(u"apps"_s).toObject();
    for (auto it = apps.begin(); it != apps.end(); ++it) {
        const QJsonArray h = it.value().toObject().value(u"hours"_s).toArray();
        for (int i = 0; i < 24 && i < h.size(); ++i) {
            hours[i] += h.at(i).toDouble();
            total += h.at(i).toDouble();
        }
    }
    // Needs a few days of data before it means anything.
    if (total < 10 * 3600)
        return false;
    for (int length = 1; length <= 24; ++length)
        for (int start = 0; start < 24; ++start) {
            double sum = 0;
            for (int i = 0; i < length; ++i)
                sum += hours[(start + i) % 24];
            if (sum >= 0.7 * total) {
                *from = start;
                *to = (start + length) % 24;
                return true;
            }
        }
    return false;
}

} // namespace jarvis
