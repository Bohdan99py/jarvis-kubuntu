#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "config.h"
#include "language.h"

namespace jarvis {

// Cognitive awareness: what the user is doing right now, how sure Jarvis is,
// and whether one carefully chosen offer of help is worth an interruption.
//
// Evidence, strongest first: text Jarvis read on the screen (only with the
// screen permission, recognised on this computer), errors reported by the VS
// Code extension, the window title, the application. An activity inferred from
// the application alone keeps a low confidence.
//
// Everything observed lives in memory only and fades: titles and screen text
// are never written to disk. What persists (awareness.json, 0600) is the
// suggestion history — what was offered, accepted or declined — for 30 days,
// and a pause the user asked for.
//
// It is pure logic with the time passed in, so it is unit-tested; the
// assistant feeds it and turns its suggestions into notifications.
class Awareness
{
public:
    static constexpr qint64 kErrorWindowMs = 30 * 60 * 1000;
    static constexpr qint64 kRepeatGapMs = 20 * 1000; // the same error again sooner is the same build
    static constexpr qint64 kScreenFreshMs = 10 * 60 * 1000;
    static constexpr qint64 kKeepHistoryMs = 30LL * 24 * 3600 * 1000;
    static constexpr int kMaxErrors = 40;

    struct Window
    {
        QString caption;
        QString appClass;
        QString desktopId;
        QString handle;   // KWin's internal window id, for a screenshot of exactly this window
        QString output;   // the screen it is on
        bool fullscreen = false;
    };

    struct Suggestion
    {
        QString category; // build, electronics, driving, docs, study
        QString title;
        QString body;
        QString prompt;   // what Jarvis is asked when the user accepts
        QString open;     // instead of asking: a part of Jarvis to open ("skill:electronics:calculators")
        double score = 0;
        QStringList evidence;
        bool isValid() const { return !category.isEmpty(); }
    };

    Awareness();

    void configure(const ConfigData &config);
    // The workspace profile (0.19.2): suggestions in its focus categories count
    // more, the others less; the profile adds to the context, never observes more.
    void setWorkspace(const QString &profile, const QStringList &focus);
    QString workspace() const { return m_workspace; }
    QString level() const { return m_level; }
    bool windowAllowed() const { return m_level != u"off" && m_windowAllowed; }

    // ---- Observations ----
    // A focus change reported by KWin. Jarvis's own windows are ignored, so the
    // context survives opening the overlay.
    void windowFocused(const Window &window, qint64 nowMs);
    // An error from a tool (the VS Code extension's diagnostics, OCR of a build log).
    void error(const QString &source, const QString &message, qint64 nowMs);
    // Text recognised on the screen while `appKey` was focused. Kept in memory, briefly.
    void screenObserved(const QString &appKey, const QString &text, qint64 nowMs);
    // The user talked to Jarvis: no unsolicited offer right now.
    void userInteracted(qint64 nowMs) { m_lastInteraction = nowMs; }
    void setLocked(bool locked) { m_locked = locked; }
    void forget();

    // ---- Context ----
    // {app, desktopId, category, activity, label, confidence, evidence[], since, duration,
    //  switches, project, handle, output, fullscreen, excluded, private, errors[{text, count}],
    //  screen: {seen, at}, title (only with the window permission and not private)}
    QJsonObject context(qint64 nowMs, Lang lang) const;
    // English block for Claude, honest about what Jarvis actually saw.
    // screen: "none", "text" (OCR text goes with the request) or "image" (a screenshot does).
    QString promptContext(qint64 nowMs, const QString &screen) const;
    // True when the focused application must not be observed (excluded, a
    // password manager, a private window).
    bool excludedNow() const;

    // ---- Suggestions (the importance filter) ----
    // The best offer that passes every gate, or an invalid one. `state` is the
    // persisted suggestion history (see load/save). `dnd`: the desktop's "Do not
    // disturb". Gates: level, pause, DND, lock, night, fullscreen, excluded app,
    // recent conversation, global cooldown, daily cap, muted category, the
    // category's own back-off after a "no", and a usefulness score.
    Suggestion nextSuggestion(const QJsonObject &state, qint64 nowMs, bool dnd, Lang lang, QString *why = nullptr) const;
    static void markShown(QJsonObject &state, const Suggestion &s, qint64 nowMs);
    // accepted, later (a soft no) or never (mute the category for 30 days).
    static void feedback(QJsonObject &state, const QString &category, const QString &answer, qint64 nowMs);
    static void pause(QJsonObject &state, qint64 untilMs);
    static qint64 pausedUntil(const QJsonObject &state, qint64 nowMs);
    // {shown, accepted, declined, categories: {cat: {...}}} for the settings page.
    static QJsonObject summary(const QJsonObject &state, qint64 nowMs);
    static void prune(QJsonObject &state, qint64 nowMs);

    static QJsonObject load();
    static bool save(const QJsonObject &state);
    static QString path();

    // ---- Helpers, public for tests ----
    // "error: 'foo' was not declared in this scope" -> a signature without names,
    // numbers and paths, so the same mistake in another line still matches.
    static QString errorSignature(const QString &message);
    // Error lines in a block of text (a build log on the screen).
    static QStringList errorLines(const QString &text, int max = 6);
    static bool isOwnWindow(const QString &appClass, const QString &desktopId);
    static QString appKey(const QString &appClass, const QString &desktopId);

private:
    struct Activity
    {
        QString id;    // coding, build-errors, electronics, driving-test, documentation, study-cpp, …
        double confidence = 0;
        QStringList evidence;
    };
    struct ErrorSeen
    {
        QString signature;
        QString sample;
        int count = 0;
        qint64 first = 0;
        qint64 last = 0;
    };

    Activity infer(qint64 nowMs) const;
    void updateTask(qint64 nowMs);
    QList<ErrorSeen> recentErrors(qint64 nowMs) const;
    QString activityLabel(const QString &id, Lang lang) const;
    QString projectFromTitle() const;
    QString languageFromTitle() const;
    bool screenFresh(qint64 nowMs) const;
    int switchesSince(qint64 sinceMs) const;
    int returnsTo(const QString &key, qint64 sinceMs) const;

    QString m_level = QStringLiteral("manual");
    bool m_windowAllowed = false;
    bool m_screenAllowed = false;
    bool m_fullscreenAllowed = false;
    QStringList m_muted;
    QString m_workspace;
    QStringList m_workspaceFocus;
    QStringList m_excluded;
    qint64 m_cooldownMs = 45 * 60 * 1000;
    int m_perDay = 4;

    Window m_window;
    QString m_key;           // appKey of the focused (non-Jarvis) window
    QString m_category;      // ActivityStore category of that application
    qint64 m_focusSince = 0; // when this application got the focus
    QString m_taskId;        // the activity inferred last, and since when
    qint64 m_taskSince = 0;
    QList<std::pair<qint64, QString>> m_focusLog; // focus changes, newest last (1 h)
    QList<ErrorSeen> m_errors;
    QString m_screenText;
    QString m_screenKey;
    qint64 m_screenAt = 0;
    qint64 m_lastInteraction = 0;
    bool m_locked = false;
    bool m_private = false;  // the focused window's title is private (passwords, private browsing)
    QStringList m_screenErrors; // error signatures on the previous screen, so a log left open counts once
    QString m_prevTaskId;    // a short detour keeps the task's start time
    qint64 m_prevTaskSince = 0;
    qint64 m_prevTaskEnd = 0;
};

} // namespace jarvis
