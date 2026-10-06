#include "learning_store.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include "memory_text.h"
namespace jarvis {
QString LearningStore::path() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/jarvis/learning.json";
}
QString LearningStore::normalize(const QString &text) {
    QString s = text.toLower().simplified();
    s.replace(QRegularExpression("[^\\p{L}\\p{N} ]"), " ");
    return s.simplified();
}
QJsonArray LearningStore::load() const {
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly) || f.size() > 8 * 1024 * 1024) return {};
    return QJsonDocument::fromJson(f.readAll()).array();
}
QString LearningStore::teach(const QString &question, const QString &answer) {
    const QString key = normalize(question);
    if (key.isEmpty() || answer.trimmed().isEmpty() || question.size() > 4096 || answer.size() > 4096)
        return QCoreApplication::translate("jarvis", "A question and an answer are required, up to 4096 characters each.");
    QJsonArray items = load();
    int found = -1;
    for (int i=0; i<items.size(); ++i)
        if (normalize(items[i].toObject()["question"].toString()) == key) { found=i; break; }
    if (found < 0 && items.size() >= 500) return QCoreApplication::translate("jarvis", "Memory limit reached: 500 examples.");
    QJsonObject entry{{"question", question.trimmed()}, {"answer", answer.trimmed()}};
    if (found >= 0) items[found] = entry; else items.append(entry);
    QDir dir(QFileInfo(path()).absolutePath());
    if (!dir.mkpath(".")) return QCoreApplication::translate("jarvis", "Cannot create the memory folder.");
    QFile::setPermissions(dir.absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile f(path());
    if (!f.open(QIODevice::WriteOnly)) return f.errorString();
    if (!f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) return f.errorString();
    const QByteArray data = QJsonDocument(items).toJson();
    if (f.write(data) != data.size() || !f.commit()) return f.errorString();
    return {};
}
QString LearningStore::recall(const QString &question) const {
    const QString key = normalize(question);
    for (const auto &v : load()) {
        const auto o=v.toObject();
        if (normalize(o["question"].toString()) == key) return o["answer"].toString();
    }
    return {};
}
QList<LearningStore::Match> LearningStore::similar(const QString &question, double minScore, int limit) const {
    const auto wanted = text::stems(question);
    QList<Match> out;
    if (wanted.isEmpty()) return out;
    for (const auto &v : load()) {
        const auto o = v.toObject();
        const double score = text::overlap(wanted, text::stems(o["question"].toString()));
        if (score >= minScore) out.append({o["question"].toString(), o["answer"].toString(), score});
    }
    std::sort(out.begin(), out.end(), [](const Match &a, const Match &b) { return a.score > b.score; });
    return out.mid(0, limit);
}
}
