#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>

// Desktop notifications through org.freedesktop.Notifications (Plasma shows
// them, keeps them in its history and honours "Do not disturb"). Each
// notification has a key: showing the same key again replaces it. Used by
// jarvisd (reminders, hints) and by the GUI (answers, updates, finished jobs).
// Without a notification server nothing happens.
class Notifier : public QObject
{
    Q_OBJECT

public:
    explicit Notifier(QObject *parent = nullptr);

    // actions: [[id, label], …]; the id "default" is a click on the notification.
    // timeoutMs: -1 = the server's default, 0 = stays until dismissed.
    Q_INVOKABLE void show(const QString &key, const QString &title, const QString &body,
                          const QVariantList &actions = {}, int timeoutMs = -1);
    // With a theme sound (reminders).
    void showWith(const QString &key, const QString &title, const QString &body, const QVariantList &actions,
                  int timeoutMs, bool sound);
    // {key, kind, title, body, actions, sound} as emitted by jarvis::Assistant::notice.
    void showNotice(const QJsonObject &notice);
    Q_INVOKABLE void close(const QString &key);

signals:
    void actionInvoked(const QString &key, const QString &action);

private slots:
    void onActionInvoked(uint id, const QString &action);
    void onClosed(uint id, uint reason);

private:
    QHash<QString, uint> m_ids;
    QHash<uint, QString> m_keys;
};
