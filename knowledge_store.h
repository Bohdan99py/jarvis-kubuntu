#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "language.h"

namespace jarvis {

// Long-term memory about the user, learned from the conversation:
//  * facts — "name", "uses", "likes", explicit "remember that …" notes and
//    notes Claude flagged with <memory> tags;
//  * topics — content words of the user's messages and how often they occur
//    together (no raw messages are stored).
// ${XDG_DATA_HOME}/jarvis/knowledge.json, file 0600, atomic writes. Every call
// re-reads the file so the daemon and a local-mode GUI never overwrite each other.
class KnowledgeStore
{
public:
    static constexpr int kMaxFacts = 300;
    static constexpr int kMaxFactChars = 300;
    static constexpr int kMaxTopics = 400;
    static constexpr int kMaxLinks = 1200;

    struct Learned
    {
        QString slot;
        QString value;
    };

    // Extracts facts from one user message (when `facts` is set) and updates
    // topic statistics. Questions and sensitive text teach nothing.
    QList<Learned> observe(const QString &message, bool facts);

    // Stores a note. `source` is "manual" or "claude". Returns an error text.
    QString remember(const QString &text, const QString &source);
    // Removes facts matching `query`: an id, whole words of the value, or a
    // large word overlap. When more than `maxMatches` facts match by text,
    // nothing is removed and the negated count is returned.
    int forget(const QString &query, int maxMatches = 3);
    void clear();

    QJsonArray facts() const;
    QJsonObject topics() const;
    // "name: Bohdan\nuses: Kate\n…" for the system prompt; capped.
    QString profileForPrompt(int maxChars = 1800) const;
    // Human answer to "what do you know about me?".
    QString describe(Lang lang, int max = 15) const;

    // Memory graph from taught examples, facts and dialogue topics.
    QString graph(const QJsonArray &examples) const;

    static QString slotLabel(const QString &slot, Lang lang);

private:
    static QString path();
    QJsonObject load() const;
    bool save(const QJsonObject &root) const;
    static void upsertFact(QJsonArray &facts, const QString &slot, const QString &value,
                           const QString &source, double confidence);
};

} // namespace jarvis
