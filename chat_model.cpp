#include "chat_model.h"

#include <QByteArray>
#include <QDebug>
#include <QJsonObject>
#include <QTime>
#include <QTimer>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include "dbus_names.h"

#include <utility>

ChatModel::ChatModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(&m_assistant, &jarvis::Assistant::replyReady, this,
            [this](quint64, const QString &reply) { onReply(reply); });
    connect(&m_daemon, &DaemonClient::replyReady, this, &ChatModel::onReply);
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
    };
}

void ChatModel::send(const QString &text)
{
    const QString trimmed = text.trimmed();
    // One request at a time keeps replies in order.
    if (trimmed.isEmpty() || trimmed.size() > 4096 || m_busy)
        return;

    append(trimmed, /*fromUser=*/true);
    setBusy(true);
    m_pendingText = trimmed;

    if (m_daemon.isAvailable())
        m_daemon.ask(trimmed);
    else
        m_assistant.ask(trimmed);
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
    beginResetModel();
    m_messages.clear();
    endResetModel();
    addGreeting();
}

void ChatModel::append(QString text, bool fromUser)
{
    // Bounded history: the oldest message leaves the model when the cap is hit.
    if (m_messages.size() >= kMaxMessages) {
        beginRemoveRows({}, 0, 0);
        m_messages.removeFirst();
        endRemoveRows();
    }

    const int row = int(m_messages.size());
    beginInsertRows({}, row, row);
    m_messages.append(Message{std::move(text), QTime::currentTime().toString(u"HH:mm"), fromUser});
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

void ChatModel::onReply(const QString &reply)
{
    if (!m_busy)
        return; // stray answer, e.g. from a request we already gave up on
    append(reply, /*fromUser=*/false);
    m_lastReply=reply;emit lastReplyChanged();emit assistantReply(reply);
    m_pendingText.clear();
    setBusy(false);
}

void ChatModel::onDaemonFailed(const QString &reason)
{
    if (!m_busy)
        return;
    qWarning().noquote() << "jarvisd failed (" << reason << "), answering locally";
    m_assistant.ask(m_pendingText); // graceful fallback: the user still gets an answer
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
