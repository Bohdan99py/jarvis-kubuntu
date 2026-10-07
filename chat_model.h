#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <functional>

#include "assistant.h"
#include "daemon_client.h"

// List model for the chat view + bridge between QML and the brain.
// If jarvisd is on the session bus, requests go to it; otherwise (or if the
// daemon fails mid-request) the in-process engine answers, so the UI never hangs.
class ChatModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString lastReply READ lastReply NOTIFY lastReplyChanged FINAL)
    Q_PROPERTY(QString graph READ graph NOTIFY graphChanged FINAL)
    Q_PROPERTY(QString learningStatus READ learningStatus NOTIFY graphChanged FINAL)
    Q_PROPERTY(bool learningBusy READ learningBusy NOTIFY graphChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(bool daemonConnected READ daemonConnected NOTIFY daemonConnectedChanged FINAL)
    Q_PROPERTY(QString memory READ memory NOTIFY memoryChanged FINAL)
    Q_PROPERTY(QString memoryStatus READ memoryStatus NOTIFY memoryChanged FINAL)

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
    Q_INVOKABLE void refreshGraph();
    Q_INVOKABLE void teach(const QString &question, const QString &answer);
    // Long-term memory: facts, topics and activity as JSON (see Assistant::memoryJson).
    Q_INVOKABLE void refreshMemory();
    Q_INVOKABLE void remember(const QString &text);
    // A fact id, "facts" or "activity".
    Q_INVOKABLE void forget(const QString &what);
    // A Jarvis action the user triggered; feeds suggestions.
    Q_INVOKABLE void recordAction(const QString &id, const QString &label);
    // Jarvis asks one of its curious questions in the chat.
    Q_INVOKABLE void askMeSomething();
    // After a language switch: re-greet if the chat is still untouched.
    Q_INVOKABLE void retranslate();
    QString memory() const { return m_memory; }
    QString memoryStatus() const { return m_memoryStatus; }
    QString lastReply() const { return m_lastReply; }
    QString graph() const { return m_graph; }
    QString learningStatus() const { return m_learningStatus; }
    bool learningBusy() const { return m_learningBusy; }
    // Re-read the config file here and in the daemon (after the settings dialog saved).
    Q_INVOKABLE void reloadConfig();
    // Stop the background daemon (the window keeps working in local mode).
    Q_INVOKABLE void quitDaemon();

signals:
    void lastReplyChanged();
    void assistantReply(const QString &text);
    void graphChanged();
    void busyChanged();
    void daemonConnectedChanged();
    void memoryChanged();

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
    void callDaemon(const QString &method, const QVariantList &args,
                    const std::function<void(const QString &result, const QString &error)> &done);

private slots:
    void onDaemonMemoryChanged();
    void onDaemonActivityChanged();

private:
    QString m_lastReply;
    QString m_graph = QStringLiteral("{\"nodes\":[],\"edges\":[],\"examples\":0}");
    QString m_learningStatus;
    QString m_memory = QStringLiteral("{}");
    QString m_memoryStatus;
    bool m_learningBusy = false;
    QList<Message> m_messages;
    DaemonClient m_daemon;
    jarvis::Assistant m_assistant;
    QString m_pendingText;
    bool m_busy = false;
};
