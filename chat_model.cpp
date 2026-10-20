#include "chat_model.h"

#include <QByteArray>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QDebug>
#include <QJsonObject>
#include <QTime>
#include <QTimer>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include "dbus_names.h"

using namespace Qt::StringLiterals;

#include <utility>

namespace {

// Answers come from Claude and may quote anything: as Markdown they must not
// load images or run HTML. Outside code, "<" becomes an entity (it still reads
// as "<"); code blocks and `inline code` show it literally anyway.
QString safeMarkdown(const QString &text)
{
    static const QRegularExpression image(QStringLiteral("!\\[([^\\]]*)\\]\\([^)]*\\)"));
    QStringList lines = text.split(u'\n');
    bool fence = false;
    for (QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~"))) {
            fence = !fence;
            continue;
        }
        if (fence)
            continue;
        line.replace(image, QStringLiteral("[\\1]"));
        QStringList parts = line.split(u'`');
        for (int i = 0; i < parts.size(); i += 2) // even parts are outside `code`
            parts[i].replace(u'<', QLatin1String("&lt;"));
        line = parts.join(u'`');
    }
    return lines.join(u'\n');
}

// Text and fenced code apart: code gets its own block with Copy and Save.
// "```cpp main.cpp" names the file; an unclosed fence (still streaming) runs to the end.
QVariantList segments(const QString &text)
{
    static const QRegularExpression fence(QStringLiteral("^\\s*(```+|~~~+)\\s*([\\w+#.-]*)\\s*([^\\s`]*)\\s*$"));
    QVariantList out;
    QStringList prose, code;
    QString lang, file, marker;
    bool inCode = false;
    auto flushProse = [&] {
        const QString t = prose.join(u'\n').trimmed();
        if (!t.isEmpty())
            out.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("text")}, {QStringLiteral("markdown"), safeMarkdown(t)}});
        prose.clear();
    };
    auto flushCode = [&] {
        out.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("code")}, {QStringLiteral("lang"), lang},
                               {QStringLiteral("file"), file}, {QStringLiteral("code"), code.join(u'\n')}});
        code.clear();
    };
    for (const QString &line : text.split(u'\n')) {
        const auto m = fence.match(line);
        if (!inCode && m.hasMatch()) {
            flushProse();
            inCode = true;
            marker = m.captured(1);
            lang = m.captured(2).toLower();
            file = m.captured(3);
            continue;
        }
        if (inCode && m.hasMatch() && m.captured(1).startsWith(marker.left(3)) && m.captured(2).isEmpty()) {
            flushCode();
            inCode = false;
            continue;
        }
        (inCode ? code : prose).append(line);
    }
    if (inCode)
        flushCode();
    flushProse();
    return out;
}

} // namespace

ChatModel::ChatModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(&m_assistant, &jarvis::Assistant::replyReady, this,
            [this](quint64, const QString &reply) { onReply(reply); });
    connect(&m_daemon, &DaemonClient::replyReady, this, &ChatModel::onReply);
    connect(&m_daemon, &DaemonClient::replyProgress, this, &ChatModel::onProgress);
    connect(&m_assistant, &jarvis::Assistant::replyProgress, this,
            [this](quint64, const QString &text) { onProgress(text); });
    connect(&m_daemon, &DaemonClient::failed, this, &ChatModel::onDaemonFailed);
    connect(&m_daemon, &DaemonClient::availabilityChanged, this,
            &ChatModel::daemonConnectedChanged);

    m_messages.reserve(kMaxMessages + 1);
    addGreeting();

    // If the daemon is installed as a user service, this wakes it up via D-Bus
    // activation; if it is not installed, nothing happens and we stay local.
    m_daemon.tryStartDaemon();
    connect(&m_daemon, &DaemonClient::availabilityChanged, this, [this] { refreshGraph(); refreshMemory(); updateLocalAutonomy(); });
    connect(&m_assistant, &jarvis::Assistant::notice, this, [this](const QJsonObject &notice) {
        m_noticeBodies.insert(notice.value(QStringLiteral("key")).toString(), notice.value(QStringLiteral("body")).toString());
        m_notifier.showNotice(notice);
    });
    connect(&m_notifier, &Notifier::actionInvoked, this, [this](const QString &key, const QString &action) {
        const QString target = m_assistant.noticeAction(key, action);
        if (!target.isEmpty())
            emit openRequested(target, target == QLatin1String("curious") ? m_noticeBodies.value(key) : QString());
    });
    // Give D-Bus activation time to bring the daemon up before deciding to stand in for it.
    QTimer::singleShot(10000, this, &ChatModel::updateLocalAutonomy);
    connect(&m_assistant, &jarvis::Assistant::memoryChanged, this, [this] {
        if (!daemonConnected()) backgroundChange(true);
    });
    connect(&m_assistant, &jarvis::Assistant::activityChanged, this, [this] {
        if (!daemonConnected()) backgroundChange(false);
    });
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(QString(jarvis::dbus::kService), QString(jarvis::dbus::kPath), QString(jarvis::dbus::kInterface),
                QStringLiteral("MemoryChanged"), this, SLOT(onDaemonMemoryChanged()));
    bus.connect(QString(jarvis::dbus::kService), QString(jarvis::dbus::kPath), QString(jarvis::dbus::kInterface),
                QStringLiteral("ActivityChanged"), this, SLOT(onDaemonActivityChanged()));
    refreshGraph();
    refreshMemory();
}

int ChatModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_messages.size());
}

QVariant ChatModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_messages.size())
        return {};

    const Message &m = m_messages.at(index.row());
    switch (role) {
    case TextRole:
        return m.text;
    case FromUserRole:
        return m.fromUser;
    case TimeRole:
        return m.time;
    case MarkdownRole:
        return m.fromUser ? QString() : safeMarkdown(m.text);
    case SegmentsRole:
        return m.fromUser ? QVariantList() : segments(m.text);
    case FilesRole:
        return m.files;
    default:
        return {};
    }
}

QHash<int, QByteArray> ChatModel::roleNames() const
{
    return {
        {TextRole, "text"},
        {FromUserRole, "fromUser"},
        {TimeRole, "time"},
        {MarkdownRole, "markdown"},
        {SegmentsRole, "segments"},
        {FilesRole, "files"},
    };
}

void ChatModel::send(const QString &text)
{
    sendWithFiles(text, {});
}

void ChatModel::sendWithFiles(const QString &text, const QVariantList &files)
{
    const QString trimmed = text.trimmed();
    // One request at a time keeps replies in order.
    if ((trimmed.isEmpty() && files.isEmpty()) || trimmed.size() > 4096 || m_busy || files.size() > 8)
        return;

    QStringList paths, names;
    const QString inbox = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/jarvis/attachments"_s;
    for (const QVariant &v : files) {
        const QUrl url = v.toUrl();
        QString path = url.isLocalFile() ? url.toLocalFile() : v.toString();
        if (path.isEmpty())
            continue;
        // The daemon has a private /tmp: files from there are handed over through its data folder.
        if (path.startsWith(u"/tmp/") || path.startsWith(u"/var/tmp/")) {
            QDir().mkpath(inbox);
            const QString copy = inbox + u'/' + QString::number(QDateTime::currentMSecsSinceEpoch()) + u'-' + QFileInfo(path).fileName();
            if (QFile::copy(path, copy))
                path = copy;
        }
        paths.append(path);
        names.append(QFileInfo(path).fileName().section(u'-', QFileInfo(path).path() == inbox ? 1 : 0));
    }
    if (trimmed.isEmpty() && paths.isEmpty())
        return;
    // Old hand-overs are not kept.
    for (const QFileInfo &old : QDir(inbox).entryInfoList(QDir::Files))
        if (old.lastModified().secsTo(QDateTime::currentDateTime()) > 24 * 3600)
            QFile::remove(old.filePath());

    append(trimmed, /*fromUser=*/true, names);
    m_streamRow = -1;
    setBusy(true);
    m_pendingText = trimmed;
    m_pendingFiles = paths;

    if (m_daemon.isAvailable())
        m_daemon.ask(trimmed, paths);
    else
        m_assistant.ask(trimmed, paths);
}

QString ChatModel::saveFile(const QString &name, const QString &content, const QString &language) const
{
    static const QHash<QString, QString> suffixes = {
        {u"c"_s, u"c"_s}, {u"cpp"_s, u"cpp"_s}, {u"c++"_s, u"cpp"_s}, {u"arduino"_s, u"ino"_s}, {u"ino"_s, u"ino"_s},
        {u"python"_s, u"py"_s}, {u"py"_s, u"py"_s}, {u"javascript"_s, u"js"_s}, {u"js"_s, u"js"_s}, {u"qml"_s, u"qml"_s},
        {u"bash"_s, u"sh"_s}, {u"sh"_s, u"sh"_s}, {u"json"_s, u"json"_s}, {u"cmake"_s, u"cmake"_s}, {u"html"_s, u"html"_s},
        {u"rust"_s, u"rs"_s}, {u"go"_s, u"go"_s}, {u"java"_s, u"java"_s}, {u"yaml"_s, u"yaml"_s}, {u"xml"_s, u"xml"_s},
        {u"verilog"_s, u"v"_s}, {u"vhdl"_s, u"vhd"_s}, {u"asm"_s, u"s"_s}, {u"spice"_s, u"cir"_s}, {u"markdown"_s, u"md"_s},
        {u"csv"_s, u"csv"_s}, {u"sql"_s, u"sql"_s}, {u"ini"_s, u"ini"_s}, {u"toml"_s, u"toml"_s}, {u"css"_s, u"css"_s}};
    QString fileName = QFileInfo(name).fileName(); // never a path from the answer
    static const QRegularExpression unsafe(u"[^\\w.+-]"_s);
    fileName.replace(unsafe, u"_"_s);
    if (fileName.isEmpty() || fileName.startsWith(u'.'))
        fileName = u"code."_s + suffixes.value(language.toLower(), u"txt"_s);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + u"/Jarvis/Files"_s;
    if (!QDir().mkpath(dir))
        return {};
    QString path = dir + u'/' + fileName;
    const QFileInfo info(fileName);
    for (int i = 2; QFileInfo::exists(path) && i < 1000; ++i)
        path = dir + u'/' + info.completeBaseName() + u" ("_s + QString::number(i) + u")"_s
               + (info.suffix().isEmpty() ? QString() : u'.' + info.suffix());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    const QByteArray bytes = content.toUtf8() + (content.endsWith(u'\n') ? QByteArray() : QByteArray("\n"));
    if (f.write(bytes) != bytes.size() || !f.commit())
        return {};
    return path;
}

void ChatModel::showInFolder(const QString &path) const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

void ChatModel::reloadConfig()
{
    m_assistant.reloadConfig();
    m_daemon.reloadConfig();
}

void ChatModel::quitDaemon()
{
    m_daemon.quitDaemon();
}

void ChatModel::clear()
{
    m_streamRow = -1;
    beginResetModel();
    m_messages.clear();
    endResetModel();
    addGreeting();
}

void ChatModel::append(QString text, bool fromUser, QStringList files)
{
    // Bounded history: the oldest message leaves the model when the cap is hit.
    if (m_messages.size() >= kMaxMessages) {
        beginRemoveRows({}, 0, 0);
        m_messages.removeFirst();
        endRemoveRows();
        if (m_streamRow >= 0)
            --m_streamRow;
    }

    const int row = int(m_messages.size());
    beginInsertRows({}, row, row);
    m_messages.append(Message{std::move(text), QTime::currentTime().toString(u"HH:mm"), fromUser, std::move(files)});
    endInsertRows();
}

void ChatModel::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void ChatModel::addGreeting()
{
    append(tr("Hi! I'm Jarvis. Write me anything, for example \"hi, how are you\". "
              "I learn as we talk: tell me about yourself or say \"remember that …\"."),
           /*fromUser=*/false);
}

void ChatModel::onProgress(const QString &textSoFar)
{
    if (!m_busy)
        return;
    if (m_streamRow < 0 || m_streamRow >= m_messages.size()) {
        append(textSoFar, /*fromUser=*/false);
        m_streamRow = int(m_messages.size()) - 1;
        return;
    }
    m_messages[m_streamRow].text = textSoFar;
    const QModelIndex at = index(m_streamRow);
    emit dataChanged(at, at, {TextRole, MarkdownRole, SegmentsRole});
}

void ChatModel::copyText(const QString &text) const
{
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}

void ChatModel::onReply(const QString &reply)
{
    if (!m_busy)
        return; // stray answer, e.g. from a request we already gave up on
    // The answer was shown while it was written: the final text replaces it.
    if (const int row = std::exchange(m_streamRow, -1); row >= 0 && row < m_messages.size() && !m_messages.at(row).fromUser) {
        m_messages[row].text = reply;
        const QModelIndex at = index(row);
        emit dataChanged(at, at, {TextRole, MarkdownRole, SegmentsRole});
    } else {
        append(reply, /*fromUser=*/false);
    }
    m_lastReply=reply;emit lastReplyChanged();emit assistantReply(reply);
    m_pendingText.clear();
    m_pendingFiles.clear();
    setBusy(false);
}

void ChatModel::onDaemonFailed(const QString &reason)
{
    if (!m_busy)
        return;
    qWarning().noquote() << "jarvisd failed (" << reason << "), answering locally";
    m_assistant.ask(m_pendingText, m_pendingFiles); // graceful fallback: the user still gets an answer
}

void ChatModel::refreshGraph()
{
    if (!daemonConnected()) { m_graph = m_assistant.graph(); emit graphChanged(); return; }
    auto msg=QDBusMessage::createMethodCall(jarvis::dbus::kService, jarvis::dbus::kPath, jarvis::dbus::kInterface, "Graph");
    auto *w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg,3000),this);
    connect(w,&QDBusPendingCallWatcher::finished,this,[this](QDBusPendingCallWatcher *w){
        QDBusPendingReply<QString> result=*w;
        w->deleteLater();
        if (result.isError()) m_learningStatus=result.error().message();
        else if (m_graph == result.value()) return;
        else m_graph=result.value();
        emit graphChanged();
    });
}
void ChatModel::teach(const QString &question, const QString &answer)
{
    if (m_learningBusy) return;
    if (!daemonConnected()) {
        const auto error=m_assistant.teach(question,answer);
        m_learningStatus=error.isEmpty() ? tr("Example saved to memory.") : error;
        refreshGraph(); return;
    }
    m_learningBusy=true; emit graphChanged();
    auto msg=QDBusMessage::createMethodCall(jarvis::dbus::kService,jarvis::dbus::kPath,jarvis::dbus::kInterface,"Teach");
    msg << question << answer;
    auto *w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg,3000),this);
    connect(w,&QDBusPendingCallWatcher::finished,this,[this](QDBusPendingCallWatcher *w){
        QDBusPendingReply<QString> result=*w; w->deleteLater(); m_learningBusy=false;
        m_learningStatus=result.isError() ? result.error().message() : (result.value().isEmpty() ? tr("Example saved by the daemon.") : result.value());
        emit graphChanged(); refreshGraph();
    });
}

void ChatModel::callDaemon(const QString &method, const QVariantList &args,
                           const std::function<void(const QString &, const QString &)> &done)
{
    auto msg = QDBusMessage::createMethodCall(jarvis::dbus::kService, jarvis::dbus::kPath, jarvis::dbus::kInterface, method);
    msg.setArguments(args);
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, 3000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [done](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QString> result = *w;
        w->deleteLater();
        if (done)
            done(result.isError() ? QString() : result.value(), result.isError() ? result.error().message() : QString());
    });
}

void ChatModel::refreshMemory()
{
    // Every window re-reads the whole memory on memoryChanged: skip identical copies.
    if (!daemonConnected()) {
        QString memory = m_assistant.memoryJson();
        if (memory != m_memory) {
            m_memory = std::move(memory);
            emit memoryChanged();
        }
        return;
    }
    callDaemon(QStringLiteral("Memory"), {}, [this](const QString &result, const QString &error) {
        if (error.isEmpty()) {
            if (result == m_memory)
                return;
            m_memory = result;
        } else {
            m_memoryStatus = error;
        }
        emit memoryChanged();
    });
}

void ChatModel::remember(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;
    auto report = [this](const QString &result, const QString &error) {
        m_memoryStatus = !error.isEmpty() ? error : (result.isEmpty() ? tr("Saved to memory.") : result);
        emit memoryChanged();
        refreshMemory();
        refreshGraph();
    };
    if (!daemonConnected())
        report(m_assistant.remember(trimmed), {});
    else
        callDaemon(QStringLiteral("Remember"), {trimmed}, report);
}

void ChatModel::forget(const QString &what)
{
    auto report = [this, what](const QString &result, const QString &error) {
        m_memoryStatus = !error.isEmpty() ? error
                         : !result.isEmpty() ? result
                         : what == QLatin1String("activity") ? tr("Activity history cleared.")
                         : what == QLatin1String("facts") ? tr("Everything learned about you was forgotten.")
                                                          : tr("Forgotten.");
        emit memoryChanged();
        refreshMemory();
        refreshGraph();
    };
    if (!daemonConnected())
        report(m_assistant.forget(what), {});
    else
        callDaemon(QStringLiteral("Forget"), {what}, report);
}

void ChatModel::recordAction(const QString &id, const QString &label)
{
    if (!daemonConnected()) {
        m_assistant.recordAction(id, label);
        return;
    }
    auto msg = QDBusMessage::createMethodCall(jarvis::dbus::kService, jarvis::dbus::kPath, jarvis::dbus::kInterface,
                                              QStringLiteral("RecordAction"));
    msg << id << label;
    QDBusConnection::sessionBus().asyncCall(msg, 3000);
}

void ChatModel::feedback(bool good)
{
    if (m_busy)
        return;
    auto show = [this](const QString &text, const QString &error) {
        const QString reply = error.isEmpty() ? text : error;
        if (reply.isEmpty())
            return;
        append(reply, /*fromUser=*/false);
        refreshMemory();
        refreshGraph();
    };
    if (!daemonConnected())
        show(m_assistant.feedback(good), {});
    else
        callDaemon(QStringLiteral("Feedback"), {good}, show);
}

void ChatModel::retranslate()
{
    if (m_messages.size() == 1 && !m_messages.first().fromUser) {
        beginResetModel();
        m_messages.clear();
        endResetModel();
        addGreeting();
    }
}

void ChatModel::askMeSomething()
{
    if (m_busy)
        return;
    auto show = [this](const QString &question, const QString &error) {
        const QString text = !error.isEmpty() ? error
                             : question.isEmpty() ? tr("I know the basics about you already. Tell me something new!")
                                                  : question;
        append(text, /*fromUser=*/false);
        m_lastReply = text;
        emit lastReplyChanged();
        emit assistantReply(text);
        refreshMemory();
    };
    if (!daemonConnected())
        show(m_assistant.curiousQuestion(), {});
    else
        callDaemon(QStringLiteral("Curious"), {}, show);
}

void ChatModel::onDaemonMemoryChanged()
{
    backgroundChange(true);
}

void ChatModel::onDaemonActivityChanged()
{
    backgroundChange(false);
}

void ChatModel::updateLocalAutonomy()
{
    m_assistant.setAutonomous(!daemonConnected());
}

void ChatModel::say(const QString &text)
{
    const QString trimmed = text.trimmed().left(1000);
    if (!trimmed.isEmpty())
        append(trimmed, /*fromUser=*/false);
}

void ChatModel::addTask(const QString &text)
{
    auto report = [this](const QString &result, const QString &error) {
        m_memoryStatus = error.isEmpty() ? result : error;
        emit memoryChanged();
        refreshMemory();
    };
    if (!daemonConnected())
        report(m_assistant.addTask(text), {});
    else
        callDaemon(QStringLiteral("AddTask"), {text.left(1000)}, report);
}

void ChatModel::taskAction(const QString &id, const QString &action)
{
    auto report = [this](const QString &result, const QString &error) {
        if (!result.isEmpty() || !error.isEmpty()) {
            m_memoryStatus = error.isEmpty() ? result : error;
            emit memoryChanged();
        }
        refreshMemory();
    };
    if (!daemonConnected())
        report(m_assistant.taskAction(id, action), {});
    else
        callDaemon(QStringLiteral("TaskAction"), {id, action}, report);
}

void ChatModel::editTask(const QString &id, const QString &text, const QString &when, const QString &repeat)
{
    auto report = [this](const QString &result, const QString &error) {
        m_memoryStatus = error.isEmpty() ? (result.isEmpty() ? tr("Saved.") : result) : error;
        emit memoryChanged();
        refreshMemory();
    };
    if (!daemonConnected())
        report(m_assistant.editTask(id, text, when, repeat), {});
    else
        callDaemon(QStringLiteral("EditTask"), {id, text.left(1000), when.left(300), repeat.left(40)}, report);
}

void ChatModel::backgroundChange(bool graph)
{
    m_memoryStale = true;
    m_graphStale = m_graphStale || graph;
    if (!m_liveUpdates)
        return;
    m_memoryStale = false;
    refreshMemory();
    if (std::exchange(m_graphStale, false))
        refreshGraph();
}

void ChatModel::setLiveUpdates(bool live)
{
    if (m_liveUpdates == live)
        return;
    m_liveUpdates = live;
    emit liveUpdatesChanged();
    if (live && m_memoryStale)
        backgroundChange(false);
}
