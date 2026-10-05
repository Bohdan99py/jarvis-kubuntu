#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "chat_engine.h"
#include "daemon_client.h"

// List model for the chat view + bridge between QML and the brain.
// If jarvisd is on the session bus, requests go to it; otherwise (or if the
// daemon fails mid-request) the in-process engine answers, so the UI never hangs.
class ChatModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(bool daemonConnected READ daemonConnected NOTIFY daemonConnectedChanged FINAL)

public:
    enum Role {
        TextRole = Qt::UserRole + 1,
        FromUserRole,
        TimeRole,
    };
    Q_ENUM(Role)

    explicit ChatModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool busy() const noexcept { return m_busy; }
    bool daemonConnected() const noexcept { return m_daemon.isAvailable(); }

    Q_INVOKABLE void send(const QString &text);
    Q_INVOKABLE void clear();

signals:
    void busyChanged();
    void daemonConnectedChanged();

private:
    struct Message
    {
        QString text;
        QString time;
        bool fromUser;
    };

    static constexpr int kMaxMessages = 500;

    void append(QString text, bool fromUser);
    void setBusy(bool busy);
    void addGreeting();
    void onReply(const QString &reply);
    void onDaemonFailed(const QString &reason);

    QList<Message> m_messages;
    DaemonClient m_daemon;
    jarvis::ChatEngine m_engine;
    QString m_pendingText;
    bool m_busy = false;
};
