#include "workspace.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace jarvis::workspace {
namespace {

bool hasAny(const QString &text, std::initializer_list<const char16_t *> stems)
{
    return std::any_of(stems.begin(), stems.end(), [&](const char16_t *s) { return text.contains(QStringView(s)); });
}

QString normalized(const QString &text)
{
    QString t = text.toLower().simplified();
    t.replace(u'ё', u'е');
    // The assistant's name in front is not part of the command.
    static const QRegularExpression name(u"^(джарвис|jarvis)[,:!]?\\s*"_s);
    t.remove(name);
    return t;
}

QStringList stringList(const QJsonValue &v, int max)
{
    QStringList out;
    for (const QJsonValue &x : v.toArray()) {
        const QString s = x.toString().trimmed().left(400);
        if (!s.isEmpty() && !out.contains(s) && out.size() < max)
            out.append(s);
    }
    return out;
}

// Words that only shape a request ("open the latest project …").
const QStringList kStopWords{u"открой"_s, u"открыть"_s, u"найди"_s, u"найти"_s, u"покажи"_s, u"последний"_s, u"последнюю"_s,
                             u"мой"_s, u"мою"_s, u"проект"_s, u"проекты"_s, u"папку"_s, u"open"_s, u"find"_s, u"show"_s, u"the"_s,
                             u"latest"_s, u"last"_s, u"recent"_s, u"my"_s, u"project"_s, u"projects"_s, u"a"_s, u"по"_s, u"for"_s};

QStringList contentWords(const QString &text)
{
    QStringList out;
    static const QRegularExpression split(u"[^\\p{L}\\p{N}_+.-]+"_s);
    for (const QString &w : text.toLower().split(split, Qt::SkipEmptyParts))
        if (!kStopWords.contains(w))
            out.append(w);
    return out;
}

struct AppIndex
{
    QList<App> apps;
    QStringList keywords; // per app: lower-case names, keywords, generic name
    qint64 builtAt = 0;
};

AppIndex buildIndex()
{
    AppIndex index;
    QSet<QString> seen;
    QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    const QString ru = u"Name[ru]"_s;
    for (const QString &dir : dirs) {
        QDirIterator it(dir, {u"*.desktop"_s}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString file = it.next();
            QString id = QDir(dir).relativeFilePath(file);
            id.chop(8);
            id.replace(u'/', u'-');
            if (seen.contains(id))
                continue; // the first directory wins (the user's own entries)
            seen.insert(id);
            QSettings entry(file, QSettings::IniFormat);
            entry.beginGroup(u"Desktop Entry"_s);
            if (entry.value(u"Type"_s).toString() != u"Application" || entry.value(u"NoDisplay"_s).toString() == u"true"
                || entry.value(u"Hidden"_s).toString() == u"true")
                continue;
            App app;
            app.desktopId = id;
            app.name = entry.value(u"Name"_s).toString();
            app.icon = entry.value(u"Icon"_s).toString();
            if (app.name.isEmpty())
                continue;
            QStringList words{app.name.toLower(), entry.value(ru).toString().toLower(), entry.value(u"GenericName"_s).toString().toLower(),
                              entry.value(u"GenericName[ru]"_s).toString().toLower(), id.toLower()};
            words << entry.value(u"Keywords"_s).toString().toLower() << entry.value(u"Keywords[ru]"_s).toString().toLower();
            index.apps.append(app);
            index.keywords.append(words.join(u'\n'));
        }
    }
    index.builtAt = QDateTime::currentMSecsSinceEpoch();
    return index;
}

const AppIndex &appIndex()
{
    static QMutex mutex;
    static AppIndex index;
    QMutexLocker lock(&mutex);
    if (QDateTime::currentMSecsSinceEpoch() - index.builtAt > 60000)
        index = buildIndex();
    return index;
}

} // namespace

QStringList profileIds()
{
    return {u"general"_s, u"study"_s, u"work"_s, u"dev"_s};
}

QList<Profile> defaults()
{
    QList<Profile> out;
    Profile general{u"general"_s, u"GENERAL"_s, 1,
                    {u"firefox_firefox|firefox|org.mozilla.firefox"_s, u"org.kde.dolphin"_s},
                    {},
                    {u"org.jarvis.status"_s, u"org.jarvis.system"_s, u"org.jarvis.media"_s},
                    {},
                    {}};
    Profile study{u"study"_s, u"STUDY"_s, 2,
                  {u"org.kde.okular|okularApplication_pdf"_s, u"firefox_firefox|firefox|org.mozilla.firefox"_s},
                  {},
                  {u"org.jarvis.status"_s, u"org.jarvis.power"_s},
                  {u"driving"_s, u"study"_s, u"docs"_s},
                  {}};
    Profile work{u"work"_s, u"WORK"_s, 3,
                 {u"org.kicad.kicad"_s, u"org.kde.dolphin"_s},
                 {},
                 {u"org.jarvis.system"_s, u"org.jarvis.status"_s},
                 {u"electronics"_s, u"docs"_s},
                 {}};
    Profile dev{u"dev"_s, u"DEV"_s, 4,
                {u"code_code|code|com.visualstudio.code|codium"_s, u"jetbrains-clion|clion_clion"_s, u"org.kde.konsole"_s},
                {},
                {u"org.jarvis.system"_s, u"org.jarvis.network"_s, u"org.jarvis.status"_s},
                {u"build"_s, u"docs"_s},
                {}};
    out << general << study << work << dev;
    return out;
}

QString filePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + u"/jarvis/workspaces.json"_s;
}

QJsonObject toJson(const Profile &p)
{
    return QJsonObject{{u"label"_s, p.label},
                       {u"desktop"_s, p.desktop},
                       {u"apps"_s, QJsonArray::fromStringList(p.apps)},
                       {u"paths"_s, QJsonArray::fromStringList(p.paths)},
                       {u"widgets"_s, QJsonArray::fromStringList(p.widgets)},
                       {u"focus"_s, QJsonArray::fromStringList(p.focus)},
                       {u"activity"_s, p.activity},
                       {u"layout"_s, p.layout}};
}

Profile fromJson(const QString &id, const QJsonObject &o)
{
    Profile base;
    for (const Profile &d : defaults())
        if (d.id == id)
            base = d;
    base.id = id;
    if (o.isEmpty())
        return base;
    const QString label = o.value(u"label"_s).toString().trimmed().left(16);
    if (!label.isEmpty())
        base.label = label;
    base.desktop = std::clamp(o.value(u"desktop"_s).toInt(base.desktop), 1, 20);
    if (o.contains(u"apps"_s))
        base.apps = stringList(o.value(u"apps"_s), 12);
    if (o.contains(u"paths"_s))
        base.paths = stringList(o.value(u"paths"_s), 12);
    if (o.contains(u"widgets"_s))
        base.widgets = stringList(o.value(u"widgets"_s), 12);
    if (o.contains(u"focus"_s))
        base.focus = stringList(o.value(u"focus"_s), 8);
    base.layout = o.value(u"layout"_s).toObject();
    const QString activity = o.value(u"activity"_s).toString();
    if (QStringList{u"normal"_s, u"reduced"_s, u"quiet"_s}.contains(activity))
        base.activity = activity;
    return base;
}

State load()
{
    State s;
    QFile f(filePath());
    const QJsonObject o = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
    const QJsonObject profiles = o.value(u"profiles"_s).toObject();
    for (const QString &id : profileIds())
        s.profiles.append(fromJson(id, profiles.value(id).toObject()));
    const QString current = o.value(u"current"_s).toString(), previous = o.value(u"previous"_s).toString();
    if (profileIds().contains(current))
        s.current = current;
    if (profileIds().contains(previous))
        s.previous = previous;
    return s;
}

bool save(const State &state, QString *error)
{
    QJsonObject profiles;
    for (const Profile &p : state.profiles)
        profiles.insert(p.id, toJson(p));
    const QJsonObject o{{u"current"_s, state.current}, {u"previous"_s, state.previous}, {u"profiles"_s, profiles}};
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QSaveFile f(filePath());
    const QByteArray bytes = QJsonDocument(o).toJson(QJsonDocument::Indented);
    if (!f.open(QIODevice::WriteOnly) || !f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || f.write(bytes) != bytes.size() || !f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

Profile find(const State &state, const QString &id)
{
    for (const Profile &p : state.profiles)
        if (p.id == id)
            return p;
    return fromJson(id, {});
}

QJsonObject summary(const State &state)
{
    QJsonArray list;
    for (const Profile &p : state.profiles)
        list.append(QJsonObject{{u"id"_s, p.id}, {u"label"_s, p.label}, {u"desktop"_s, p.desktop},
                                {u"widgets"_s, QJsonArray::fromStringList(p.widgets)}, {u"activity"_s, p.activity.isEmpty() ? u"normal"_s : p.activity}});
    return QJsonObject{{u"current"_s, state.current}, {u"previous"_s, state.previous}, {u"profiles"_s, list}};
}

QString profileFromWords(const QString &raw)
{
    const QString t = normalized(raw);
    if (hasAny(t, {u"программир", u"разработ", u"кодинг", u" код", u"dev", u"coding", u"programming", u"development", u"clion", u"cmake"}))
        return u"dev"_s;
    if (hasAny(t, {u"учеб", u"учёб", u"занят", u"пдд", u"француз", u"конспект", u"study", u"learning", u"french", u"lesson"}))
        return u"study"_s;
    if (hasAny(t, {u"kicad", u"кикад", u"схем", u"электрон", u"инженер", u"паяльн", u"work", u"работ", u"engineering", u"electronics"}))
        return u"work"_s;
    if (hasAny(t, {u"general", u"обычн", u"повседнев", u"общ", u"default", u"everyday", u"standard"}))
        return u"general"_s;
    return {};
}

QString folderKey(const QString &raw)
{
    const QString t = normalized(raw);
    if (hasAny(t, {u"загрузк", u"загрузок", u"скачан", u"download"}))
        return u"downloads"_s;
    if (hasAny(t, {u"документ", u"document"}))
        return u"documents"_s;
    if (hasAny(t, {u"рабочий стол", u"рабочего стола", u"desktop folder"}))
        return u"desktop"_s;
    if (hasAny(t, {u"изображен", u"картин", u"фото", u"picture", u"photo"}))
        return u"pictures"_s;
    if (hasAny(t, {u"музык", u"music"}))
        return u"music"_s;
    if (hasAny(t, {u"видео", u"video"}))
        return u"videos"_s;
    if (hasAny(t, {u"домашн", u"home folder", u"home directory"}))
        return u"home"_s;
    return {};
}

QString folderPath(const QString &key)
{
    static const QList<std::pair<QString, QStandardPaths::StandardLocation>> map{
        {u"downloads"_s, QStandardPaths::DownloadLocation}, {u"documents"_s, QStandardPaths::DocumentsLocation},
        {u"desktop"_s, QStandardPaths::DesktopLocation},    {u"pictures"_s, QStandardPaths::PicturesLocation},
        {u"music"_s, QStandardPaths::MusicLocation},        {u"videos"_s, QStandardPaths::MoviesLocation},
        {u"home"_s, QStandardPaths::HomeLocation}};
    for (const auto &[k, location] : map)
        if (k == key)
            return QStandardPaths::writableLocation(location);
    return {};
}

QStringList settingsKeys()
{
    return {u"sound"_s, u"network"_s, u"display"_s, u"power"_s, u"bluetooth"_s, u"keyboard"_s, u"shortcuts"_s, u"notifications"_s};
}

QString settingsKey(const QString &raw)
{
    const QString t = normalized(raw);
    if (hasAny(t, {u"звук", u"громкост", u"sound", u"audio"}))
        return u"sound"_s;
    if (hasAny(t, {u"сет", u"wi-fi", u"wifi", u"network"}))
        return u"network"_s;
    if (hasAny(t, {u"экран", u"монитор", u"дисплей", u"display", u"screen"}))
        return u"display"_s;
    if (hasAny(t, {u"питан", u"энерг", u"батаре", u"power", u"energy"}))
        return u"power"_s;
    if (hasAny(t, {u"bluetooth", u"блютуз"}))
        return u"bluetooth"_s;
    if (hasAny(t, {u"комбинац", u"сочетан", u"горяч", u"shortcut"}))
        return u"shortcuts"_s;
    if (hasAny(t, {u"клавиатур", u"раскладк", u"keyboard"}))
        return u"keyboard"_s;
    if (hasAny(t, {u"уведомлен", u"notification"}))
        return u"notifications"_s;
    return {};
}

QString settingsModule(const QString &key)
{
    static const QHash<QString, QString> map{{u"sound"_s, u"kcm_pulseaudio"_s},      {u"network"_s, u"kcm_networkmanagement"_s},
                                             {u"display"_s, u"kcm_kscreen"_s},        {u"power"_s, u"kcm_powerdevilprofilesconfig"_s},
                                             {u"bluetooth"_s, u"kcm_bluetooth"_s},    {u"keyboard"_s, u"kcm_keyboard"_s},
                                             {u"shortcuts"_s, u"kcm_keys"_s},         {u"notifications"_s, u"kcm_notifications"_s}};
    return map.value(key);
}

Command parseCommand(const QString &raw)
{
    const QString t = normalized(raw);
    Command c;
    if (t.isEmpty() || t.size() > 300)
        return c;
    const bool switchVerb = hasAny(t, {u"переключи", u"перейди", u"включи режим", u"режим ", u"профиль", u"подготовь", u"настрой рабочий",
                                       u"switch to", u"switch workspace", u"workspace", u"profile", u"prepare", u"mode"});
    if (hasAny(t, {u"предыдущ", u"прошл", u"previous", u"go back"}) && hasAny(t, {u"профил", u"рабоч", u"режим", u"workspace", u"profile", u"вернись", u"верни", u"back"})) {
        c.kind = u"previous-workspace"_s;
        return c;
    }
    // Power profiles: an allowed system setting.
    if (hasAny(t, {u"энергосбережен", u"экономи", u"power saver", u"power-saver", u"battery saver"})) {
        c.kind = u"power-profile"_s;
        c.arg = u"power-saver"_s;
        return c;
    }
    if (hasAny(t, {u"режим производительн", u"производительный режим", u"performance mode", u"power profile performance"})) {
        c.kind = u"power-profile"_s;
        c.arg = u"performance"_s;
        return c;
    }
    if (hasAny(t, {u"сбалансированн", u"balanced mode", u"balanced power"})) {
        c.kind = u"power-profile"_s;
        c.arg = u"balanced"_s;
        return c;
    }
    if (switchVerb) {
        const QString profile = profileFromWords(t);
        if (!profile.isEmpty()) {
            c.kind = u"workspace"_s;
            c.arg = profile;
            c.prepare = hasAny(t, {u"подготовь", u"prepare", u"настрой"});
            return c;
        }
    }
    const bool show = hasAny(t, {u"покажи", u"какая", u"какой", u"сколько", u"show", u"what is", u"what's", u"how much"});
    if (show && hasAny(t, {u"нагрузк", u"загрузк", u"температур", u"load", u"usage", u"temperature", u"скорост", u"speed", u"заряд", u"charge"})) {
        if (hasAny(t, {u"gpu", u"видеокарт", u"граф"}))
            c.arg = u"gpu"_s;
        else if (hasAny(t, {u"памят", u"memory", u"ram", u"озу"}))
            c.arg = u"memory"_s;
        else if (hasAny(t, {u"сет", u"network", u"интернет"}))
            c.arg = u"net"_s;
        else if (hasAny(t, {u"батаре", u"заряд", u"battery"}))
            c.arg = u"power"_s;
        else if (hasAny(t, {u"температур", u"temperature"}))
            c.arg = u"thermal"_s;
        else
            c.arg = u"cpu"_s;
        c.kind = u"show-metric"_s;
        return c;
    }
    const bool open = hasAny(t, {u"открой", u"открыть", u"запусти", u"запустить", u"open", u"launch", u"start ", u"run "});
    const bool find = hasAny(t, {u"найди", u"найти", u"find", u"search"});
    if ((open || find) && hasAny(t, {u"настройк", u"параметр", u"settings"})) {
        const QString key = settingsKey(t);
        if (!key.isEmpty()) {
            c.kind = u"settings"_s;
            c.arg = key;
            return c;
        }
    }
    if ((open || find) && hasAny(t, {u"проект", u"project"})) {
        c.kind = u"find-project"_s;
        c.arg = contentWords(t).join(u' ');
        return c;
    }
    if (open && hasAny(t, {u"папк", u"каталог", u"folder", u"directory", u"загрузк", u"downloads", u"документы", u"documents"})) {
        const QString key = folderKey(t);
        if (!key.isEmpty()) {
            c.kind = u"open-folder"_s;
            c.arg = key;
            return c;
        }
    }
    if (open) {
        static const QRegularExpression verb(u"^.*?(открой|открыть|запусти|запустить|open|launch|start|run)\\s+"_s);
        QString rest = t;
        rest.remove(verb);
        rest = rest.simplified();
        if (!rest.isEmpty() && rest.size() <= 60) {
            c.kind = u"open-app"_s;
            c.arg = rest;
        }
    }
    return c;
}

QList<App> findApplications(const QString &words, int max)
{
    const QString q = words.toLower().simplified();
    QList<App> out;
    if (q.size() < 2)
        return out;
    const AppIndex &index = appIndex();
    for (int i = 0; i < index.apps.size(); ++i) {
        App a = index.apps.at(i);
        const QString name = a.name.toLower();
        const QStringList keys = index.keywords.at(i).split(u'\n');
        double score = 0;
        if (name == q || keys.value(1) == q)
            score = 1;
        else if (name.startsWith(q) || keys.value(1).startsWith(q))
            score = 0.85;
        else if (name.contains(q) || keys.value(1).contains(q))
            score = 0.65;
        else if (a.desktopId.toLower().contains(q))
            score = 0.55;
        else if (index.keywords.at(i).contains(q))
            score = 0.45;
        if (score <= 0)
            continue;
        a.score = score;
        out.append(a);
    }
    std::stable_sort(out.begin(), out.end(), [](const App &a, const App &b) { return a.score > b.score; });
    return out.mid(0, max);
}

QString resolveApp(const QString &alternatives)
{
    for (const QString &id : alternatives.split(u'|', Qt::SkipEmptyParts)) {
        static const QRegularExpression valid(u"^[A-Za-z0-9._-]{1,120}$"_s);
        if (valid.match(id).hasMatch() && !QStandardPaths::locate(QStandardPaths::ApplicationsLocation, id + u".desktop"_s).isEmpty())
            return id;
    }
    return {};
}

QList<Project> findProjects(const QString &words, const QString &home, int max)
{
    const QStringList wanted = contentWords(words);
    static const QStringList skipped{u"node_modules"_s, u"snap"_s, u"build"_s, u"dist"_s, u"target"_s, u"venv"_s, u"__pycache__"_s};
    QList<Project> found;
    QList<std::pair<QString, int>> queue{{home, 0}};
    int visited = 0;
    while (!queue.isEmpty() && visited < 6000) {
        const auto [dir, depth] = queue.takeFirst();
        ++visited;
        const QDir d(dir);
        const QStringList files = d.entryList(QDir::Files | QDir::Hidden);
        QString kind;
        const auto has = [&](const QString &f) { return files.contains(f); };
        bool esp32 = has(u"sdkconfig"_s); // ESP-IDF
        if (!esp32 && has(u"platformio.ini"_s)) {
            QFile ini(d.filePath(u"platformio.ini"_s));
            esp32 = ini.open(QIODevice::ReadOnly) && ini.read(8192).toLower().contains("esp32");
        }
        if (esp32)
            kind = u"esp32"_s;
        else if (has(u"platformio.ini"_s))
            kind = u"platformio"_s;
        else if (std::any_of(files.cbegin(), files.cend(), [](const QString &f) { return f.endsWith(u".kicad_pro"_s); }))
            kind = u"kicad"_s;
        else if (has(u"CMakeLists.txt"_s))
            kind = u"cmake"_s;
        else if (has(u"Cargo.toml"_s))
            kind = u"rust"_s;
        else if (has(u"package.json"_s))
            kind = u"node"_s;
        else if (has(u"pyproject.toml"_s) || has(u"setup.py"_s))
            kind = u"python"_s;
        else if (QFileInfo::exists(d.filePath(u".git"_s)))
            kind = u"git"_s;
        if (!kind.isEmpty() && dir != home) {
            const QString haystack = (d.dirName() + u' ' + kind + u' ' + QDir(home).relativeFilePath(dir)).toLower();
            if (std::all_of(wanted.cbegin(), wanted.cend(), [&](const QString &w) { return haystack.contains(w); }))
                found.append(Project{dir, d.dirName(), kind, QFileInfo(dir).lastModified().toMSecsSinceEpoch()});
            continue; // a project's subdirectories are its own business
        }
        if (depth >= 4)
            continue;
        for (const QString &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (sub.startsWith(u'.') || skipped.contains(sub) || sub.startsWith(u"build"_s))
                continue;
            queue.append({d.filePath(sub), depth + 1});
        }
    }
    std::sort(found.begin(), found.end(), [](const Project &a, const Project &b) { return a.modified > b.modified; });
    return found.mid(0, max);
}

} // namespace jarvis::workspace
