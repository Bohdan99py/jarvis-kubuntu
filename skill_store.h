#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
namespace jarvis {
// Declarative skills contain prompts and optional exact-match examples, never executable code.
// Optional fields: "image" (bundled picture id), "color" (#rrggbb), "tools" (ids of
// built-in calculators the skill brings), "formulas" ([{title, formula, note}],
// with *_en variants) and per-example "aliases" (other wordings of the question).
class SkillStore {
public:
    static constexpr int kMaxBytes = 160 * 1024;
    static constexpr int kMaxExamples = 150;
    static constexpr int kMaxPrompt = 24000;
    SkillStore();
    QJsonArray list() const;
    QString setEnabled(const QString &id, bool enabled);
    QString install(const QByteArray &json);
    QString prompt() const;
    QString recall(const QString &question) const;
    // Offline fallback: the example of an enabled skill closest to `question`
    // by words (overlap ≥ `threshold`), as {question, answer, skill}; {} when none.
    QJsonObject similar(const QString &question, double threshold = 0.6) const;
    static QString validate(const QByteArray &json);
private:
    static QString directory();
};
}
