#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <functional>

#include "assistant.h"
#include "daemon_client.h"
#include "notifier.h"
#include "screen_awareness.h"

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
    // False while no window shows memory: background changes (activity, daemon
    // updates) are then only noted and fetched once a window shows up again.
    Q_PROPERTY(bool liveUpdates READ liveUpdates WRITE setLiveUpdates NOTIFY liveUpdatesChanged FINAL)
    // Screen awareness (set from QML): pictures for requests about the screen.
    Q_PROPERTY(ScreenAwareness *screen READ screen WRITE setScreen NOTIFY screenChanged FINAL)
    // What the user is doing (awareness context JSON from jarvisd); "{}" when unknown.
    Q_PROPERTY(QString context READ context NOTIFY contextChanged FINAL)
    // The answer being written, or the last one (the overlay shows it).
    Q_PROPERTY(QString liveReply READ liveReply NOTIFY liveReplyChanged FINAL)
    // A request is being stopped.
    Q_PROPERTY(bool stopping READ stopping NOTIFY busyChanged FINAL)
    // The last request failed and can be sent again.
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY busyChanged FINAL)

public:
    enum Role {
        TextRole = Qt::UserRole + 1,
        FromUserRole,
        TimeRole,
        // Jarvis's text as Markdown that cannot load anything (images, HTML).
        MarkdownRole,
        // Jarvis's answer split into [{kind: "text", markdown} | {kind: "code", lang, file, code}].
        SegmentsRole,
        // Names of the files attached to a user message.
        FilesRole,
        // user, jarvis, system (a note from Jarvis about what it did) or error.
        KindRole,
        // The answer being written right now.
        StreamingRole,
    };
    Q_ENUM(Role)

    explicit ChatModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool busy() const noexcept { return m_busy; }
    bool daemonConnected() const noexcept { return m_daemon.isAvailable(); }

    Q_INVOKABLE void send(const QString &text);
    // With attachments: file URLs or paths (images, PDF, code, KiCad and SimulIDE files).
    Q_INVOKABLE void sendWithFiles(const QString &text, const QVariantList &files);
    // With what the user is doing: the focused application and its title
    // (`useWindow`) and, when `useScreen` and allowed, the window's text (OCR) or picture.
    Q_INVOKABLE void sendWithContext(const QString &text, const QVariantList &files, bool useWindow, bool useScreen);
    // Stops the answer being written; what arrived stays.
    Q_INVOKABLE void stop();
    // Sends the last request again (after an error).
    Q_INVOKABLE void retry();
    // A short note from Jarvis that is not an answer ("screen awareness is off").
    Q_INVOKABLE void note(const QString &text);
    // Re-reads the awareness context from jarvisd.
    Q_INVOKABLE void refreshContext();
    // Pause suggestions and observation: minutes (0 resumes, -1 until tomorrow).
    Q_INVOKABLE void pauseAwareness(int minutes);
    // Saves code from an answer into ~/Documents/Jarvis/Files; returns the path or "".
    Q_INVOKABLE QString saveFile(const QString &name, const QString &content, const QString &language) const;
    Q_INVOKABLE void showInFolder(const QString &path) const;
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
    // 👍 / 👎 on Jarvis's last answer: Jarvis learns from it and replies.
    Q_INVOKABLE void feedback(bool good);
    // A message from Jarvis that is not an answer (a question from a notification).
    Q_INVOKABLE void say(const QString &text);
    // Tasks panel: "tomorrow at 9 call mum" or a plain to-do; the confirmation lands in memoryStatus.
    Q_INVOKABLE void addTask(const QString &text);
    // done, remove, snooze, reopen
    Q_INVOKABLE void taskAction(const QString &id, const QString &action);
    // when: "tomorrow at 10" ("" keeps the time, "-" none); repeat: "keep" or a code. Result in memoryStatus.
    Q_INVOKABLE void editTask(const QString &id, const QString &text, const QString &when, const QString &repeat);
    // Plain text to the clipboard (code from an answer).
    Q_INVOKABLE void copyText(const QString &text) const;
    QString memory() const { return m_memory; }
    QString memoryStatus() const { return m_memoryStatus; }
    QString lastReply() const { return m_lastReply; }
    QString graph() const { return m_graph; }
    QString learningStatus() const { return m_learningStatus; }
    bool learningBusy() const { return m_learningBusy; }
    bool liveUpdates() const { return m_liveUpdates; }
    void setLiveUpdates(bool live);
    ScreenAwareness *screen() const { return m_screen; }
    void setScreen(ScreenAwareness *screen);
    QString context() const { return m_context; }
    QString liveReply() const { return m_liveReply; }
    bool stopping() const { return m_stopping; }
    bool canRetry() const { return !m_busy && m_lastFailed && !m_last.text.isEmpty(); }
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
    void liveUpdatesChanged();
    void screenChanged();
    void liveReplyChanged();
    void contextChanged();
    // An offer of help from jarvisd's awareness (JSON: category, title, body, score).
    void suggested(const QString &json);
    // Without a daemon this window delivers Jarvis's own notifications; a click
    // asks the window to show "main", "tasks", "study", "curious" or "memory".
    void openRequested(const QString &target, const QString &payload);
    // What Jarvis is doing, for the HUD — real events only: request (detail: files
    // attached), route (llm, recall, calculator…; route-ext when another client such
    // as VS Code asked), stream (characters so far), reply (characters), error
    // (Claude failed), fallback (the daemon failed, answering locally), notice
    // (title), memory (memory changed), daemon (on/off).
    void activity(const QString &kind, const QString &detail);

private:
    struct Message
    {
        QString text;
        QString time;
        bool fromUser;
        QStringList files = {};
        QString kind = {};
    };
    struct Sent
    {
        QString text;
        QStringList paths;
        QStringList names;
        bool context = false;
        bool screen = false;
    };

    static constexpr int kMaxMessages = 500;

    void append(QString text, bool fromUser, QStringList files = {}, QString kind = {});
    // Hands a prepared request to jarvisd (or the local engine).
    void dispatch(const QString &text, const QStringList &paths, const QJsonObject &desktop);
    void start(const Sent &request, bool showMessage);
    void finishStop();
    void setBusy(bool busy);
    void addGreeting();
    void onReply(const QString &reply);
    void onProgress(const QString &textSoFar);
    void onDaemonFailed(const QString &reason);
    // Background change: refresh now, or later if no window shows memory.
    void backgroundChange(bool graph);
    // The local engine runs reminders and hints only while no daemon does.
    void updateLocalAutonomy();
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
    bool m_liveUpdates = true;
    bool m_memoryStale = false;
    bool m_graphStale = false;
    QList<Message> m_messages;
    DaemonClient m_daemon;
    jarvis::Assistant m_assistant;
    Notifier m_notifier;
    QHash<QString, QString> m_noticeBodies;
    QString m_pendingText;
    QStringList m_pendingFiles;
    int m_streamRow = -1; // the answer being written, shown as it grows
    bool m_busy = false;
    bool m_stopping = false;
    bool m_failed = false;     // Claude failed this request: the answer is an error message
    bool m_lastFailed = false;
    Sent m_last;
    ScreenAwareness *m_screen = nullptr;
    int m_captureId = 0;
    QString m_screenImage;     // a picture prepared for this request, removed after the answer
    QString m_context = QStringLiteral("{}");
    QString m_liveReply;
    void setLiveReply(const QString &text);
};
