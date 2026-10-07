#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "language.h"

namespace jarvis {

// What Jarvis learns about the user's programming, mostly from the VS Code
// extension:
//  * time per language (editor heartbeats) and per project;
//  * frameworks found in project files (CMakeLists.txt, package.json, …);
//  * compiler/linter errors the user runs into, as normalized signatures;
//  * lessons — problems with the solution that worked (Claude's answers to
//    "fix"/"error" requests, snippets the user teaches, corrections after 👎).
//    Similar problems later get the lesson as context, or offline as the answer.
// Code itself is never stored except in lessons the user asked for or rated.
// ${XDG_DATA_HOME}/jarvis/code.json, 0600, re-read on every change.
class CodeStore
{
public:
    static constexpr int kMaxLessons = 300;
    static constexpr int kMaxErrors = 200;
    static constexpr int kMaxProjects = 50;
    static constexpr int kMaxLanguages = 60;
    static constexpr qint64 kHeartbeatGapMs = 2 * 60 * 1000;

    struct Lesson
    {
        QString id;
        QString language;
        QString problem;
        QString solution;
        QString source;
        double score = 0;
    };

    void activity(const QString &language, const QString &project, qint64 nowMs);
    void workspace(const QString &project, const QStringList &languages, const QStringList &frameworks, qint64 nowMs);
    void diagnostic(const QString &language, const QString &project, const QString &message, qint64 nowMs);

    // Returns the lesson id, or "" when the lesson was refused (sensitive, empty).
    QString addLesson(const QString &language, const QString &problem, const QString &solution,
                      const QString &source, qint64 nowMs);
    bool removeLesson(const QString &id);
    void rateLesson(const QString &id, bool good);
    QList<Lesson> similar(const QString &problem, const QString &language, double minScore, int limit) const;

    // "Languages: C++ (12 h), Python (3 h). Frameworks: Qt, CMake. Project: jarvis."
    QString profileForPrompt() const;
    QStringList languagesUsed(double minSeconds) const;
    QStringList frameworks(const QString &project) const;
    QString currentProject() const;
    int lessonsSince(qint64 sinceMs) const;
    QJsonObject snapshot() const;
    void clear();

    static QString languageName(const QString &id);
    static QString errorSignature(const QString &message);

private:
    static QString path();
    QJsonObject load() const;
    bool save(const QJsonObject &root) const;

    // Heartbeat accounting lives in memory: only the daemon receives them.
    QString m_lastLanguage;
    QString m_lastProject;
    qint64 m_lastBeat = 0;
};

} // namespace jarvis
