#pragma once
#include <QString>
#include <QJsonArray>
#include <QList>
namespace jarvis {
// Taught question → answer pairs. Exact (normalized) matches answer locally;
// similar ones are offered to Claude as context, or answer offline when close.
class LearningStore {
public:
    struct Match {
        QString question;
        QString answer;
        double score = 0;
    };
    QString teach(const QString &question, const QString &answer);
    QString recall(const QString &question) const;
    // Best matches by word overlap, highest first, score >= minScore.
    QList<Match> similar(const QString &question, double minScore, int limit) const;
    QJsonArray items() const { return load(); }
private:
    static QString path();
    static QString normalize(const QString &text);
    QJsonArray load() const;
};
}
