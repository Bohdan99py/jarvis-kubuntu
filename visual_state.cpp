#include "visual_state.h"

#include <QDateTime>
#include <QHash>
#include <QTime>

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

int priority(const QString &transient)
{
    return transient == u"error" ? 3 : transient == u"warning" ? 2 : transient == u"notification" ? 1 : 0;
}

// The demo's script: a state and the events that come with it.
struct DemoStep
{
    const char *state;
    const char *kind;
    const char *detail;
};
const DemoStep kDemo[] = {
    {"idle", "", ""},
    {"listening", "voice", "listen"},
    {"processing", "request", "0"},
    {"processing", "route", "recall"},
    {"processing", "route", "llm"},
    {"processing", "stream", "420"},
    {"speaking", "reply", "900"},
    {"speaking", "memory", ""},
    {"idle", "tool", "kicad-cli"},
    {"notification", "notice", "Demo"},
    {"warning", "warning", "Demo"},
    {"error", "error", "Demo"},
    {"idle", "", ""},
};

} // namespace

VisualState::VisualState(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
    m_transientTimer.setSingleShot(true);
    connect(&m_transientTimer, &QTimer::timeout, this, [this] {
        m_transient.clear();
        m_message.clear();
        update();
    });
    m_demoTimer.setInterval(1700);
    connect(&m_demoTimer, &QTimer::timeout, this, &VisualState::demoTick);
    for (const char *key : {"requests", "replies", "llm", "recall", "memory", "errors", "notices", "tools"})
        m_counters.insert(QString::fromLatin1(key), 0);
}

void VisualState::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    if (!busy && m_streaming) {
        m_streaming = false;
        emit activityChanged();
    }
    emit inputsChanged();
    update();
}

void VisualState::setSpeaking(bool speaking)
{
    if (m_speaking == speaking)
        return;
    m_speaking = speaking;
    append(u"voice"_s, speaking ? u"speak"_s : u"speak-end"_s, false);
    emit inputsChanged();
    update();
}

void VisualState::setListening(bool listening)
{
    if (m_listening == listening)
        return;
    m_listening = listening;
    append(u"voice"_s, listening ? u"listen"_s : u"listen-end"_s, false);
    emit inputsChanged();
    update();
}

void VisualState::setJobs(int jobs)
{
    jobs = std::max(0, jobs);
    if (m_jobs == jobs)
        return;
    m_jobs = jobs;
    emit inputsChanged();
}

void VisualState::setDemo(bool demo)
{
    if (m_demo == demo)
        return;
    m_demo = demo;
    m_demoStep = 0;
    if (demo) {
        m_demoTimer.start();
    } else {
        m_demoTimer.stop();
        if (m_transient.isEmpty())
            m_message.clear();
    }
    emit demoChanged();
    update();
}

void VisualState::setForcedState(const QString &state)
{
    static const QStringList known{u"idle"_s, u"listening"_s, u"processing"_s, u"speaking"_s, u"notification"_s,
                                   u"warning"_s, u"error"_s};
    const QString value = known.contains(state) ? state : QString();
    if (m_forced == value)
        return;
    m_forced = value;
    update();
}

void VisualState::setDurations(int errorMs, int warningMs, int noticeMs)
{
    m_errorMs = errorMs;
    m_warningMs = warningMs;
    m_noticeMs = noticeMs;
}

qint64 VisualState::requestAge() const
{
    return m_request.isValid() ? m_request.elapsed() : 0;
}

void VisualState::record(const QString &kind, const QString &detail)
{
    handle(kind.left(20), detail.left(300), false);
}

void VisualState::count(const QString &key)
{
    m_counters[key] = m_counters.value(key).toInt() + 1;
}

void VisualState::handle(const QString &kind, const QString &detail, bool demo)
{
    const qint64 now = m_clock.elapsed();
    if (kind == u"memory") {
        // Learning writes in bursts: one entry and one effect per two seconds.
        if (!demo && m_lastMemoryAt >= 0 && now - m_lastMemoryAt < 2000)
            return;
        if (!demo) {
            m_lastMemoryAt = now;
            count(u"memory"_s);
        }
    }
    if (demo) {
        // Made-up events only reach the log and the effects, never the real numbers.
        append(kind, detail, true);
        emit pulse(kind, detail, true);
        return;
    }

    if (kind == u"request") {
        count(u"requests"_s);
        m_request.start();
        m_route.clear();
        m_routes.clear();
        m_streaming = false;
        m_streamChars = 0;
        m_streamRate = 0;
        m_lastStreamAt = -1;
        emit activityChanged();
    } else if (kind == u"route") {
        m_route = detail;
        if (!m_routes.contains(detail))
            m_routes.append(detail);
        if (detail == u"llm" || detail == u"recall")
            count(detail);
        emit activityChanged();
    } else if (kind == u"stream") {
        const int chars = detail.toInt();
        if (!m_streaming) {
            m_streaming = true;
            append(kind, detail, false); // only the start of the stream is logged
        }
        if (m_lastStreamAt >= 0 && now > m_lastStreamAt) {
            const double rate = double(std::max(0, chars - m_streamChars)) * 1000.0 / double(now - m_lastStreamAt);
            m_streamRate = m_streamRate <= 0 ? rate : m_streamRate * 0.7 + rate * 0.3;
        }
        m_lastStreamAt = now;
        m_streamChars = chars;
        emit activityChanged();
        emit pulse(kind, detail, false);
        return;
    } else if (kind == u"reply") {
        count(u"replies"_s);
        m_lastLatency = m_request.isValid() ? m_request.elapsed() : 0;
        m_request.invalidate();
        m_streaming = false;
        m_streamRate = 0;
        emit activityChanged();
    } else if (kind == u"error") {
        count(u"errors"_s);
        transient(u"error"_s, detail, m_errorMs);
    } else if (kind == u"fallback" || kind == u"warning") {
        transient(u"warning"_s, detail, m_warningMs);
    } else if (kind == u"daemon" && detail == u"off") {
        transient(u"warning"_s, detail, m_warningMs);
    } else if (kind == u"notice") {
        count(u"notices"_s);
        transient(u"notification"_s, detail, m_noticeMs);
    } else if (kind == u"tool") {
        count(u"tools"_s);
    }
    append(kind, detail, false);
    emit pulse(kind, detail, false);
}

void VisualState::append(const QString &kind, const QString &detail, bool demo)
{
    m_log.prepend(QVariantMap{{u"time"_s, QTime::currentTime().toString(u"HH:mm:ss"_s)},
                              {u"kind"_s, kind},
                              {u"detail"_s, detail},
                              {u"demo"_s, demo}});
    while (m_log.size() > kMaxLog)
        m_log.removeLast();
    emit logChanged();
}

void VisualState::transient(const QString &state, const QString &message, int ms)
{
    // A warning never hides an error that is still showing.
    if (!m_transient.isEmpty() && priority(m_transient) > priority(state) && m_transientTimer.isActive())
        return;
    m_transient = state;
    m_message = message;
    m_transientTimer.start(ms);
    update();
}

void VisualState::demoTick()
{
    const DemoStep &step = kDemo[m_demoStep % int(std::size(kDemo))];
    ++m_demoStep;
    const QString kind = QString::fromLatin1(step.kind);
    if (!kind.isEmpty())
        handle(kind, QString::fromLatin1(step.detail), true);
    m_message = QString::fromLatin1(step.state) == u"notification" || QString::fromLatin1(step.state) == u"warning"
                        || QString::fromLatin1(step.state) == u"error"
                    ? u"Demo"_s
                    : QString();
    update();
}

void VisualState::update()
{
    QString next;
    if (!m_forced.isEmpty())
        next = m_forced;
    else if (m_demo)
        next = QString::fromLatin1(kDemo[(m_demoStep + int(std::size(kDemo)) - 1) % int(std::size(kDemo))].state);
    else if (!m_transient.isEmpty())
        next = m_transient;
    else if (m_speaking)
        next = u"speaking"_s;
    else if (m_listening)
        next = u"listening"_s;
    else if (m_busy)
        next = u"processing"_s;
    else
        next = u"idle"_s;
    if (next == m_state && !m_demo)
        return;
    m_state = next;
    emit stateChanged();
}

} // namespace jarvis
