#pragma once
#include <QString>
#include <QJsonArray>
namespace jarvis {
class LearningStore {
public:
    QString teach(const QString &question, const QString &answer);
    QString recall(const QString &question) const;
    QString graph() const;
private:
    static QString path();
    static QString normalize(const QString &text);
    QJsonArray load() const;
};
}
