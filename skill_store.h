#pragma once
#include <QJsonArray>
#include <QString>
namespace jarvis {
// Declarative skills contain prompts and optional exact-match examples, never executable code.
class SkillStore {
public:
    SkillStore();
    QJsonArray list() const;
    QString setEnabled(const QString &id, bool enabled);
    QString install(const QByteArray &json);
    QString prompt() const;
    QString recall(const QString &question) const;
    static QString validate(const QByteArray &json);
private:
    static QString directory();
};
}
