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
#include "markdown_safe.h"
#include "screen_awareness.h"
#include <QJsonDocument>

using namespace Qt::StringLiterals;

#include <utility>

namespace {

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
            out.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("text")}, {QStringLiteral("markdown"), jarvis::text::safeMarkdown(t)}});
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
    connect(&m_daemon, &DaemonClient::availabilityChanged, this,
            [this](bool on) { emit activity(QStringLiteral("daemon"), on ? QStringLiteral("on") : QStringLiteral("off")); });
    connect(&m_daemon, &DaemonClient::routed, this,
            [this](const QString &route, bool own) { emit activity(own || route == QLatin1String("reflect") ? QStringLiteral("route") : QStringLiteral("route-ext"), route); });
    connect(&m_daemon, &DaemonClient::claudeFailed, this, [this](const QString &reason, bool own) {
        if (own)
            m_failed = true;
        emit activity(QStringLiteral("error"), reason);
    });
    connect(&m_daemon, &DaemonClient::contextChanged, this, &ChatModel::refreshContext);
    connect(&m_daemon, &DaemonClient::suggestion, this, [this](const QString &json) {
        emit suggested(json.left(4096));
        emit activity(QStringLiteral("suggestion"),
                      QJsonDocument::fromJson(json.left(4096).toUtf8()).object().value(QStringLiteral("title")).toString().left(120));
    });
    connect(&m_daemon, &DaemonClient::notice, this, [this](const QString &, const QString &title) { emit activity(QStringLiteral("notice"), title); });
    // The local engine answers when there is no daemon (or as a fallback).
    connect(&m_assistant, &jarvis::Assistant::routed, this, [this](quint64, const QString &route) { emit activity(QStringLiteral("route"), route); });
    connect(&m_assistant, &jarvis::Assistant::failed, this, [this](quint64, const QString &reason) {
        m_failed = true;
        emit activity(QStringLiteral("error"), reason.left(300));
    });

    m_messages.reserve(kMaxMessages + 1);
    addGreeting();

    // If the daemon is installed as a user service, this wakes it up via D-Bus
    // activation; if it is not installed, nothing happens and we stay local.
    m_daemon.tryStartDaemon();
    connect(&m_daemon, &DaemonClient::availabilityChanged, this, [this] { refreshGraph(); refreshMemory(); updateLocalAutonomy(); });
    connect(&m_assistant, &jarvis::Assistant::notice, this, [this](const QJsonObject &notice) {
        m_noticeBodies.insert(notice.value(QStringLiteral("key")).toString(), notice.value(QStringLiteral("body")).toString());
        m_notifier.showNotice(notice);
        emit activity(QStringLiteral("notice"), notice.value(QStringLiteral("title")).toString().left(200));
    });
    connect(&m_notifier, &Notifier::actionInvoked, this, [this](const QString &key, const QString &action) {
        const QString target = m_assistant.noticeAction(key, action);
        if (!target.isEmpty())
            emit openRequested(target, target == QLatin1String("curious") ? m_noticeBodies.value(key) : QString());
    });
    // Give D-Bus activation time to bring the daemon up before deciding to stand in for it.
    QTimer::singleShot(10000, this, &ChatModel::updateLocalAutonomy);
    connect(&m_assistant, &jarvis::Assistant::memoryChanged, this, [this] {
        if (!daemonConnected()) {
            emit activity(QStringLiteral("memory"), {});
            backgroundChange(true);
        }
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
        return m.fromUser ? QString() : jarvis::text::safeMarkdown(m.text);
    case SegmentsRole:
        return m.fromUser ? QVariantList() : segments(m.text);
    case FilesRole:
        return m.files;
    case KindRole:
        return !m.kind.isEmpty() ? m.kind : m.fromUser ? QStringLiteral("user") : QStringLiteral("jarvis");
    case StreamingRole:
        return m_busy && index.row() == m_streamRow;
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
        {KindRole, "kind"},
        {StreamingRole, "streaming"},
    };
}

void ChatModel::send(const QString &text)
{
    sendWithFiles(text, {});
}

namespace {

// "что у меня на экране", "посмотри на экран", "what's on my screen"…
bool asksAboutScreen(const QString &text)
{
    static const QRegularExpression screen(
        u"(?:что|чо|что-то|что там)\\s+(?:у\\s+меня\\s+|сейчас\\s+|ты\\s+видишь\\s+)*на\\s+(?:мо[её]м\\s+|этом\\s+)?экране|"
        u"посмотри\\s+на\\s+(?:мой\\s+|этот\\s+)?экран|прочитай\\s+(?:с\\s+)?экран|что\\s+на\\s+экране|"
        u"what(?:'s|\\s+is)\\s+on\\s+(?:my|the)\\s+screen|look\\s+at\\s+my\\s+screen|read\\s+my\\s+screen|"
        u"qu'?est-ce\\s+qu'?il\\s+y\\s+a\\s+sur\\s+(?:mon|l')\\s*[ée]cran"_s,
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    return screen.match(text).hasMatch();
}

} // namespace

void ChatModel::sendWithFiles(const QString &text, const QVariantList &files)
{
    sendWithContext(text, files, false, false);
}

void ChatModel::sendWithContext(const QString &text, const QVariantList &files, bool useWindow, bool useScreen)
{
    const QString trimmed = text.trimmed();
    // One request at a time keeps replies in order.
    if ((trimmed.isEmpty() && files.isEmpty() && !useScreen) || trimmed.size() > 4096 || m_busy || files.size() > 8)
        return;

    Sent request;
    request.text = trimmed;
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
        request.paths.append(path);
        request.names.append(QFileInfo(path).fileName().section(u'-', QFileInfo(path).path() == inbox ? 1 : 0));
    }
    if (trimmed.isEmpty() && request.paths.isEmpty() && !useScreen)
        return;
    // Old hand-overs are not kept.
    for (const QFileInfo &old : QDir(inbox).entryInfoList(QDir::Files))
        if (old.lastModified().secsTo(QDateTime::currentDateTime()) > 24 * 3600)
            QFile::remove(old.filePath());

    // A question about the screen carries the desktop context by itself.
    const bool aboutScreen = asksAboutScreen(trimmed);
    request.context = useWindow || useScreen || aboutScreen;
    request.screen = useScreen || aboutScreen;
    start(request, true);
}

void ChatModel::start(const Sent &request, bool showMessage)
{
    if (showMessage) {
        QStringList names = request.names;
        if (request.screen)
            names.append(tr("screen"));
        append(request.text.isEmpty() && request.screen ? tr("What's on my screen?") : request.text, /*fromUser=*/true, names);
    }
    m_streamRow = -1;
    m_failed = false;
    m_lastFailed = false;
    m_last = request;
    setLiveReply({});
    setBusy(true);
    emit activity(QStringLiteral("request"), QString::number(request.paths.size()));

    if (!request.context) {
        dispatch(request.text, request.paths, {});
        return;
    }
    const QJsonObject none{{QStringLiteral("screen"), QStringLiteral("none")}};
    if (!request.screen) {
        dispatch(request.text, request.paths, none);
        return;
    }
    if (!m_screen || !m_screen->allowed()) {
        note(tr("Screen awareness is off, so I only know the application and its window title. "
                "Turn it on in Control center → Awareness to let me read the window."));
        dispatch(request.text, request.paths, none);
        return;
    }
    // The window the user was in before opening Jarvis, by KWin's id — never a private or excluded one.
    const QJsonObject context = QJsonDocument::fromJson(m_context.toUtf8()).object();
    if (context.value(QStringLiteral("excluded")).toBool() || context.value(QStringLiteral("private")).toBool()) {
        note(tr("The window you came from is private or excluded from observation, so I did not look at it."));
        dispatch(request.text, request.paths, none);
        return;
    }
    const QString handle = context.value(QStringLiteral("handle")).toString();
    emit activity(QStringLiteral("tool"), tr("reading the screen"));
    m_captureId = m_screen->capture(handle, true);
}

void ChatModel::dispatch(const QString &text, const QStringList &paths, const QJsonObject &desktop)
{
    m_pendingText = text;
    m_pendingFiles = paths;
    if (m_daemon.isAvailable())
        m_daemon.ask(text, paths, desktop.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(desktop).toJson(QJsonDocument::Compact)));
    else
        m_assistant.ask(text, paths, desktop);
}

void ChatModel::setScreen(ScreenAwareness *screen)
{
    if (m_screen == screen)
        return;
    if (m_screen)
        disconnect(m_screen, nullptr, this, nullptr);
    m_screen = screen;
    if (m_screen)
        connect(m_screen, &ScreenAwareness::captured, this, [this](int id, const QString &path, const QString &text, const QString &error) {
            if (id != m_captureId || !m_busy) {
                m_screen->discard(path);
                return;
            }
            m_captureId = 0;
            if (m_stopping) {
                m_screen->discard(path);
                finishStop();
                return;
            }
            QStringList paths = m_last.paths;
            QJsonObject desktop{{QStringLiteral("screen"), QStringLiteral("none")}};
            if (!path.isEmpty() && paths.size() < 8) {
                m_screenImage = path;
                paths.append(path);
                desktop[QStringLiteral("screen")] = QStringLiteral("image");
            } else {
                m_screen->discard(path);
            }
            if (!text.isEmpty()) {
                desktop[QStringLiteral("screenText")] = text.left(6000);
                if (path.isEmpty())
                    desktop[QStringLiteral("screen")] = QStringLiteral("text");
            }
            if (!error.isEmpty() && desktop.value(QStringLiteral("screen")).toString() == QLatin1String("none"))
                note(error);
            dispatch(m_last.text.isEmpty() ? tr("What's on my screen?") : m_last.text, paths, desktop);
        });
    emit screenChanged();
}

void ChatModel::stop()
{
    if (!m_busy || m_stopping)
        return;
    m_stopping = true;
    emit busyChanged();
    if (m_captureId != 0)
        return; // the picture being taken is dropped when it arrives
    if (daemonConnected() && m_daemon.waiting())
        m_daemon.cancel();
    else
        m_assistant.cancel();
    // The answer normally ends at once; never leave the chat waiting.
    QTimer::singleShot(3000, this, [this] {
        if (m_busy && m_stopping) {
            m_daemon.abandon();
            finishStop();
        }
    });
}

void ChatModel::finishStop()
{
    if (const int row = std::exchange(m_streamRow, -1); row >= 0 && row < m_messages.size() && !m_messages.at(row).fromUser) {
        m_messages[row].text += tr("\n\n…(stopped)");
        const QModelIndex at = index(row);
        emit dataChanged(at, at, {TextRole, MarkdownRole, SegmentsRole, StreamingRole});
    } else {
        note(tr("Stopped."));
    }
    if (m_screen && !m_screenImage.isEmpty())
        m_screen->discard(std::exchange(m_screenImage, QString()));
    m_captureId = 0;
    m_stopping = false;
    m_pendingText.clear();
    m_pendingFiles.clear();
    emit activity(QStringLiteral("reply"), QStringLiteral("0"));
    setBusy(false);
}

void ChatModel::setLiveReply(const QString &text)
{
    if (m_liveReply == text)
        return;
    m_liveReply = text;
    emit liveReplyChanged();
}

void ChatModel::retry()
{
    if (!canRetry())
        return;
    start(m_last, false);
}

void ChatModel::note(const QString &text)
{
    const QString trimmed = text.trimmed().left(1000);
    if (!trimmed.isEmpty())
        append(trimmed, /*fromUser=*/false, {}, QStringLiteral("system"));
}

void ChatModel::refreshContext()
{
    auto take = [this](const QString &json) {
        if (json.isEmpty() || json == m_context)
            return;
        m_context = json;
        emit contextChanged();
    };
    if (!daemonConnected()) {
        take(m_assistant.contextJson());
        return;
    }
    callDaemon(QStringLiteral("Context"), {}, [take](const QString &result, const QString &error) {
        if (error.isEmpty())
            take(result);
    });
}

void ChatModel::pauseAwareness(int minutes)
{
    if (!daemonConnected()) {
        m_assistant.pauseAwareness(minutes);
        refreshContext();
        return;
    }
    callDaemon(QStringLiteral("PauseAwareness"), {minutes}, [this](const QString &, const QString &) { refreshContext(); });
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

void ChatModel::append(QString text, bool fromUser, QStringList files, QString kind)
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
    m_messages.append(Message{std::move(text), QTime::currentTime().toString(u"HH:mm"), fromUser, std::move(files), std::move(kind)});
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
    emit activity(QStringLiteral("stream"), QString::number(textSoFar.size()));
    setLiveReply(textSoFar);
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
    const QString kind = m_failed ? QStringLiteral("error") : QString();
    // The answer was shown while it was written: the final text replaces it.
    if (const int row = std::exchange(m_streamRow, -1); row >= 0 && row < m_messages.size() && !m_messages.at(row).fromUser) {
        m_messages[row].text = reply;
        m_messages[row].kind = kind;
        const QModelIndex at = index(row);
        emit dataChanged(at, at, {TextRole, MarkdownRole, SegmentsRole, KindRole, StreamingRole});
    } else {
        append(reply, /*fromUser=*/false, {}, kind);
    }
    m_lastFailed = m_failed;
    m_failed = false;
    m_stopping = false;
    setLiveReply(reply);
    if (m_screen && !m_screenImage.isEmpty())
        m_screen->discard(std::exchange(m_screenImage, QString()));
    m_lastReply=reply;emit lastReplyChanged();emit assistantReply(reply);
    emit activity(QStringLiteral("reply"), QString::number(reply.size()));
    m_pendingText.clear();
    m_pendingFiles.clear();
    setBusy(false);
}

void ChatModel::onDaemonFailed(const QString &reason)
{
    if (!m_busy)
        return;
    qWarning().noquote() << "jarvisd failed (" << reason << "), answering locally";
    emit activity(QStringLiteral("fallback"), reason.left(200));
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
    // A quick command the user ran (open an app, search, a tool): the HUD shows it.
    emit activity(QStringLiteral("tool"), label.isEmpty() ? id.left(60) : label.left(60));
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
    emit activity(QStringLiteral("memory"), {});
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
