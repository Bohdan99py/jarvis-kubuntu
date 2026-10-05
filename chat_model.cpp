#include "chat_model.h"

#include <QByteArray>
#include <QDebug>
#include <QTime>

#include <utility>

ChatModel::ChatModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(&m_engine, &jarvis::ChatEngine::replyReady, this,
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
    if (trimmed.isEmpty() || m_busy)
        return;

    append(trimmed, /*fromUser=*/true);
    setBusy(true);
    m_pendingText = trimmed;

    if (m_daemon.isAvailable())
        m_daemon.ask(trimmed);
    else
        m_engine.ask(trimmed);
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
    append(QStringLiteral("Привет! Я Jarvis. Напиши мне что-нибудь, например «привет, как дела»."),
           /*fromUser=*/false);
}

void ChatModel::onReply(const QString &reply)
{
    if (!m_busy)
        return; // stray answer, e.g. from a request we already gave up on
    append(reply, /*fromUser=*/false);
    m_pendingText.clear();
    setBusy(false);
}

void ChatModel::onDaemonFailed(const QString &reason)
{
    if (!m_busy)
        return;
    qWarning().noquote() << "jarvisd failed (" << reason << "), answering locally";
    m_engine.ask(m_pendingText); // graceful fallback: the user still gets an answer
}
