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
//  * episodes — one-paragraph summaries of finished conversations, so Jarvis
//    remembers what was discussed, not only who the user is;
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
    static constexpr int kMaxEpisodes = 150;
    static constexpr int kMaxEpisodeChars = 500;

    struct Learned
    {
        QString slot;
        QString value;
    };

    // Extracts facts from one user message (when `facts` is set) and updates
    // topic statistics. Questions and sensitive text teach nothing.
    QList<Learned> observe(const QString &message, bool facts);

    // Stores a fact in `slot`. Sources: "dialog", "manual", "claude",
    // "reflection", "curiosity", "activity", "vscode", "study". Liking what was
    // disliked (and vice versa) replaces the opposite fact. Returns "" or an error code:
    // "short", "long", "sensitive", "unsafe", "slot", "io".
    QString learn(const QString &slot, const QString &value, const QString &source, double confidence);
    // learn("note", …) with the confidence of an explicit request.
    QString remember(const QString &text, const QString &source);
    bool hasSlot(const QString &slot) const;
    static bool knownSlot(const QString &slot);
    // "" when the value may be stored, otherwise an error code (see learn).
    static QString check(const QString &value);
    static QString checkText(const QString &value, int maxChars);

    // State of the curiosity engine, kept with the facts it learns.
    QJsonObject curiosityState() const;
    void setCuriosityState(const QJsonObject &state);
    // State of the autonomous side (hints shown, last memory review).
    QJsonObject autonomyState() const;
    void setAutonomyState(const QJsonObject &state);
    // Removes facts matching `query`: an id, whole words of the value, or a
    // large word overlap. When more than `maxMatches` facts match by text,
    // nothing is removed and the negated count is returned.
    int forget(const QString &query, int maxMatches = 3);
    void clear();

    QJsonArray facts() const;
    QJsonObject topics() const;
    // "name: Bohdan\nuses: Kate\n…" for the system prompt; capped. With a
    // question, the facts that matter for it come first (core facts, word
    // overlap, confidence, recency), so a long memory still fits.
    QString profileForPrompt(int maxChars = 1800, const QString &question = {}) const;

    // Conversation summaries ("episodes"), newest first.
    QString addEpisode(const QString &summary, qint64 when);
    QJsonArray episodes() const;
    bool forgetEpisode(const QString &id);
    // Summaries that state what the assistant cannot do ("Ассистент не может управлять
    // компьютером"): such claims go stale with every version and, fed back into the
    // prompt, made Claude refuse laptop tasks (0.22.1). They are kept — they are the
    // user's history — but marked "excluded" and never put into a prompt again.
    // Returns how many were marked now.
    int excludeCapabilityClaims();
    static bool claimsIncapable(const QString &summary);
    // Summaries related to `question` plus the latest one if it is recent;
    // "" when there is nothing worth adding to the prompt.
    QString episodesForPrompt(const QString &question, qint64 now, int maxChars = 900) const;
    // Human answer to "what did we talk about?".
    QString describeEpisodes(Lang lang, int max = 5) const;
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
