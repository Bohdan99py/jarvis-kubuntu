#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "language.h"

namespace jarvis {

// What the user does on the desktop, aggregated:
//  * time per application (total, per hour of day, per day for 14 days);
//  * switches between applications;
//  * Jarvis actions the user triggers (quick-bar buttons, launches);
//  * a short list of recent focus spans (window titles only when allowed).
// Events arrive from the KWin bridge in jarvisd. Time accrues while focus
// events keep coming; after kIdleCapMs of silence or while the screen is
// locked nothing is counted, so a forgotten window does not collect hours.
// ${XDG_DATA_HOME}/jarvis/activity.json, file 0600, saved by flush().
class ActivityStore
{
public:
    static constexpr qint64 kIdleCapMs = 15 * 60 * 1000;
    static constexpr int kKeepDays = 14;
    static constexpr int kMaxApps = 200;
    static constexpr int kMaxRecent = 40;
    static constexpr int kMaxActions = 100;
    static constexpr int kMaxTransitions = 300;

    ActivityStore();
    ~ActivityStore();

    void setKeepTitles(bool keep);
    void windowActivated(const QString &caption, const QString &appClass, const QString &desktopId, qint64 nowMs);
    void tick(qint64 nowMs);
    void setLocked(bool locked, qint64 nowMs);
    // Tracking switched off: count the current span and forget the window.
    void stop(qint64 nowMs);
    void recordAction(const QString &id, const QString &label, qint64 nowMs);
    void clear();
    bool flush();
    // Another process may have written the file: re-read it when nothing here
    // is pending (no unsaved data, no window being tracked).
    void reloadIfIdle();

    bool hasCurrent() const { return !m_current.key.isEmpty(); }
    QJsonObject snapshot(qint64 nowMs) const;
    QJsonArray suggestions(qint64 nowMs, int max = 6) const;

    // Chat answers ("what am I doing", "what did I do today").
    QString currentText(Lang lang, qint64 nowMs, bool tracking) const;
    QString todayText(Lang lang, qint64 nowMs, bool tracking) const;
    // English context block for Claude.
    QString promptContext(qint64 nowMs) const;

    static QString category(const QString &appClass, const QString &desktopId);
    static bool privateTitle(const QString &caption);
    static QString formatDuration(double seconds, Lang lang);

private:
    struct Current
    {
        QString key;
        QString name;
        QString desktopId;
        QString category;
        QString title;
        qint64 since = 0;
        qint64 lastEvent = 0;
        qint64 lastAccrued = 0;
    };

    static QString path();
    void load();
    void accrue(qint64 nowMs);
    void closeSpan(qint64 nowMs);
    void pruneDays(qint64 nowMs);
    QString displayName(const QString &appClass, const QString &desktopId);
    static QString categoryName(const QString &id, Lang lang);
    QList<std::pair<QString, double>> todayApps(qint64 nowMs) const;

    QJsonObject m_root;
    Current m_current;
    QHash<QString, QString> m_names;
    bool m_keepTitles = false;
    bool m_locked = false;
    bool m_dirty = false;
};

} // namespace jarvis
