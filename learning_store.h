#pragma once
#include <QString>
#include <QJsonArray>
#include <QList>
namespace jarvis {
// Taught question → answer pairs. Exact (normalized) matches answer locally;
// similar ones are offered to Claude as context, or answer offline when close.
// Each pair counts how often it answered; when memory is full, the pair that
// was used least (and longest ago) makes room for the new one.
class LearningStore {
public:
    static constexpr int kMaxItems = 2000;
    struct Match {
        QString question;
        QString answer;
        double score = 0;
    };
    QString teach(const QString &question, const QString &answer);
    QString recall(const QString &question) const;
    // The pair for `question` answered again: count it.
    void touch(const QString &question);
    // Best matches by word overlap weighted by rarity (a word shared by many
    // examples says little), highest first, score >= minScore.
    QList<Match> similar(const QString &question, double minScore, int limit) const;
    QJsonArray items() const { return load(); }
private:
    static QString path();
    static QString normalize(const QString &text);
    QJsonArray load() const;
    QString save(const QJsonArray &items) const;
};
}
