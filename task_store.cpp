#include "task_store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

#include "knowledge_store.h"
#include "memory_text.h"
#include "reminder_parser.h"

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

qint64 ms(const QJsonObject &o, const QString &key) { return qint64(o.value(key).toDouble()); }

QDateTime local(qint64 msecs) { return QDateTime::fromMSecsSinceEpoch(msecs); }

// Open reminders by due time, then open tasks by creation, then done items, newest first.
bool before(const QJsonObject &a, const QJsonObject &b)
{
    const bool aDone = ms(a, u"done"_s) > 0, bDone = ms(b, u"done"_s) > 0;
    if (aDone != bDone)
        return !aDone;
    if (aDone)
        return ms(a, u"done"_s) > ms(b, u"done"_s);
    const qint64 aDue = ms(a, u"due"_s), bDue = ms(b, u"due"_s);
    if ((aDue > 0) != (bDue > 0))
        return aDue > 0;
    if (aDue != bDue)
        return aDue < bDue;
    return ms(a, u"created"_s) < ms(b, u"created"_s);
}

} // namespace

QString TaskStore::path()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/jarvis/tasks.json"_s;
}

QJsonArray TaskStore::load() const
{
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024)
        return {};
    return QJsonDocument::fromJson(f.readAll()).array();
}

bool TaskStore::save(const QJsonArray &tasks) const
{
    QDir dir(QFileInfo(path()).absolutePath());
    if (!dir.mkpath(u"."_s))
        return false;
    QFile::setPermissions(dir.absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile f(path());
    if (!f.open(QIODevice::WriteOnly) || !f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return false;
    // Drop items done long ago, keep the list sorted for readers of the file.
    QList<QJsonObject> items;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto &v : tasks) {
        const QJsonObject o = v.toObject();
        if (ms(o, u"done"_s) == 0 || now - ms(o, u"done"_s) < kKeepDoneMs)
            items.append(o);
    }
    std::stable_sort(items.begin(), items.end(), before);
    QJsonArray out;
    for (const auto &o : std::as_const(items))
        out.append(o);
    const QByteArray data = QJsonDocument(out).toJson(QJsonDocument::Compact);
    return f.write(data) == data.size() && f.commit();
}

QString TaskStore::add(const QString &textValue, qint64 due, const QString &repeat, qint64 now, QString *id)
{
    const QString value = textValue.simplified();
    // Tasks go back into Claude's context: same rules as facts about the user.
    if (const QString error = KnowledgeStore::checkText(value, kMaxChars); !error.isEmpty())
        return error;
    QJsonArray tasks = load();
    int open = 0;
    for (const auto &v : std::as_const(tasks))
        open += ms(v.toObject(), u"done"_s) == 0;
    if (open >= kMaxTasks)
        return u"full"_s;
    const QString newId = u"t"_s + QString::number(QRandomGenerator::global()->generate64() & 0xffffffffffffULL, 16);
    const bool repeating = due > 0 && (repeat == u"daily" || repeat == u"weekdays" || repeat == u"weekly");
    tasks.append(QJsonObject{{u"id"_s, newId}, {u"text"_s, value}, {u"due"_s, double(std::max<qint64>(0, due))},
                             {u"repeat"_s, repeating ? repeat : QString()}, {u"created"_s, double(now)},
                             {u"done"_s, 0}, {u"fired"_s, 0}});
    if (!save(tasks))
        return u"io"_s;
    if (id)
        *id = newId;
    return {};
}

bool TaskStore::complete(const QString &id, qint64 now)
{
    QJsonArray tasks = load();
    for (int i = 0; i < tasks.size(); ++i) {
        QJsonObject o = tasks.at(i).toObject();
        if (o.value(u"id"_s).toString() != id)
            continue;
        o[u"done"_s] = double(now);
        tasks[i] = o;
        return save(tasks);
    }
    return false;
}

bool TaskStore::remove(const QString &id)
{
    QJsonArray tasks = load();
    for (int i = 0; i < tasks.size(); ++i)
        if (tasks.at(i).toObject().value(u"id"_s).toString() == id) {
            tasks.removeAt(i);
            return save(tasks);
        }
    return false;
}

bool TaskStore::snooze(const QString &id, qint64 until)
{
    QJsonArray tasks = load();
    for (int i = 0; i < tasks.size(); ++i) {
        QJsonObject o = tasks.at(i).toObject();
        if (o.value(u"id"_s).toString() != id)
            continue;
        // A repeating reminder keeps its schedule: the snooze becomes a one-time copy.
        if (!o.value(u"repeat"_s).toString().isEmpty()) {
            QJsonObject copy = o;
            copy[u"id"_s] = u"t"_s + QString::number(QRandomGenerator::global()->generate64() & 0xffffffffffffULL, 16);
            copy[u"repeat"_s] = QString();
            copy[u"due"_s] = double(until);
            copy[u"done"_s] = 0;
            copy[u"fired"_s] = 0;
            tasks.append(copy);
            return save(tasks);
        }
        o[u"due"_s] = double(until);
        o[u"done"_s] = 0;
        o[u"fired"_s] = 0;
        tasks[i] = o;
        return save(tasks);
    }
    return false;
}

QList<QJsonObject> TaskStore::find(const QString &query) const
{
    const QSet<QString> wanted = text::stems(query);
    QList<QJsonObject> out;
    if (wanted.isEmpty())
        return out;
    for (const auto &v : load()) {
        const QJsonObject o = v.toObject();
        if (ms(o, u"done"_s) > 0)
            continue;
        const QSet<QString> have = text::stems(o.value(u"text"_s).toString());
        if (have.contains(wanted) || text::overlap(wanted, have) >= 0.5)
            out.append(o);
    }
    return out;
}

QJsonArray TaskStore::tasks() const
{
    QList<QJsonObject> items;
    for (const auto &v : load())
        items.append(v.toObject());
    std::stable_sort(items.begin(), items.end(), before);
    QJsonArray out;
    int done = 0;
    for (const auto &o : std::as_const(items)) {
        // The panel shows what is ahead; a handful of finished items is enough.
        if (ms(o, u"done"_s) > 0 && ++done > 10)
            break;
        out.append(o);
    }
    return out;
}

QJsonObject TaskStore::task(const QString &id) const
{
    for (const auto &v : load())
        if (v.toObject().value(u"id"_s).toString() == id)
            return v.toObject();
    return {};
}

QList<QJsonObject> TaskStore::due(qint64 now) const
{
    QList<QJsonObject> out;
    for (const auto &v : load()) {
        const QJsonObject o = v.toObject();
        const qint64 at = ms(o, u"due"_s);
        if (at > 0 && at <= now && ms(o, u"done"_s) == 0 && ms(o, u"fired"_s) < at)
            out.append(o);
    }
    std::stable_sort(out.begin(), out.end(), before);
    return out;
}

void TaskStore::fired(const QString &id, qint64 now)
{
    QJsonArray tasks = load();
    for (int i = 0; i < tasks.size(); ++i) {
        QJsonObject o = tasks.at(i).toObject();
        if (o.value(u"id"_s).toString() != id)
            continue;
        const QString repeat = o.value(u"repeat"_s).toString();
        if (repeat.isEmpty()) {
            o[u"fired"_s] = double(now);
            o[u"done"_s] = double(now);
        } else {
            const QDateTime next = ReminderParser::nextOccurrence(local(ms(o, u"due"_s)), repeat, local(now));
            o[u"due"_s] = double(next.toMSecsSinceEpoch());
            o[u"fired"_s] = double(now);
        }
        tasks[i] = o;
        save(tasks);
        return;
    }
}

qint64 TaskStore::nextDue() const
{
    qint64 next = 0;
    for (const auto &v : load()) {
        const QJsonObject o = v.toObject();
        const qint64 at = ms(o, u"due"_s);
        if (at > 0 && ms(o, u"done"_s) == 0 && (next == 0 || at < next))
            next = at;
    }
    return next;
}

int TaskStore::openCount() const
{
    int count = 0;
    for (const auto &v : load())
        count += ms(v.toObject(), u"done"_s) == 0;
    return count;
}

QString TaskStore::describe(Lang lang, qint64 now) const
{
    const bool ru = lang == Lang::Ru;
    QStringList reminders, todo;
    const QDateTime current = local(now);
    for (const auto &v : tasks()) {
        const QJsonObject o = v.toObject();
        if (ms(o, u"done"_s) > 0)
            continue;
        const QString textValue = o.value(u"text"_s).toString();
        const qint64 at = ms(o, u"due"_s);
        if (at > 0) {
            const QString repeat = ReminderParser::describeRepeat(o.value(u"repeat"_s).toString(), local(at), lang);
            reminders.append(u"• "_s + ReminderParser::describeWhen(local(at), current, lang)
                             + (repeat.isEmpty() ? QString() : u" ("_s + repeat + u')') + u" — "_s + textValue);
        } else {
            todo.append(u"• "_s + textValue);
        }
    }
    if (reminders.isEmpty() && todo.isEmpty())
        return ru ? u"Напоминаний и задач нет. Скажите, например: «напомни завтра в 9 позвонить маме» или «добавь задачу купить паяльник»."_s
                  : u"No reminders or tasks. Try \"remind me tomorrow at 9 to call mum\" or \"add a task buy solder\"."_s;
    QString out;
    if (!reminders.isEmpty())
        out += (ru ? u"Напоминания:\n"_s : u"Reminders:\n"_s) + reminders.mid(0, 15).join(u'\n');
    if (!todo.isEmpty())
        out += (out.isEmpty() ? QString() : u"\n\n"_s) + (ru ? u"Задачи:\n"_s : u"Tasks:\n"_s) + todo.mid(0, 15).join(u'\n');
    return out;
}

QString TaskStore::promptSummary(qint64 now, int maxItems) const
{
    QStringList lines;
    for (const auto &v : tasks()) {
        const QJsonObject o = v.toObject();
        if (ms(o, u"done"_s) > 0 || lines.size() >= maxItems)
            continue;
        const qint64 at = ms(o, u"due"_s);
        const QString repeat = o.value(u"repeat"_s).toString();
        lines.append(u"- "_s
                     + (at > 0 ? QLocale::c().toString(local(at), u"ddd yyyy-MM-dd HH:mm"_s) + (repeat.isEmpty() ? QString() : u" ("_s + repeat + u')')
                               : u"to-do"_s)
                     + u": "_s + o.value(u"text"_s).toString());
    }
    Q_UNUSED(now);
    return lines.isEmpty() ? QString() : lines.join(u'\n') + u'\n';
}

} // namespace jarvis
