#include "learning_store.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <cmath>
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
QString LearningStore::save(const QJsonArray &items) const {
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
QString LearningStore::teach(const QString &question, const QString &answer) {
    const QString key = normalize(question);
    if (key.isEmpty() || answer.trimmed().isEmpty() || question.size() > 4096 || answer.size() > 4096)
        return QCoreApplication::translate("jarvis", "A question and an answer are required, up to 4096 characters each.");
    QJsonArray items = load();
    int found = -1;
    for (int i=0; i<items.size(); ++i)
        if (normalize(items[i].toObject()["question"].toString()) == key) { found=i; break; }
    const double now = double(QDateTime::currentMSecsSinceEpoch());
    if (found < 0 && items.size() >= kMaxItems) {
        // Full: forget the least used, longest unused example.
        int victim = 0;
        auto rank = [](const QJsonObject &o) {
            return std::pair(o["hits"].toInt(), std::max(o["used"].toDouble(), o["created"].toDouble()));
        };
        for (int i=1; i<items.size(); ++i)
            if (rank(items[i].toObject()) < rank(items[victim].toObject())) victim = i;
        items.removeAt(victim);
    }
    QJsonObject entry{{"question", question.trimmed()}, {"answer", answer.trimmed()}, {"created", now}, {"hits", 0}};
    if (found >= 0) {
        // A corrected answer keeps its history: it is the same question.
        entry["created"] = items[found].toObject()["created"].toDouble(now);
        entry["hits"] = items[found].toObject()["hits"].toInt();
        items[found] = entry;
    } else {
        items.append(entry);
    }
    return save(items);
}
void LearningStore::touch(const QString &question) {
    const QString key = normalize(question);
    QJsonArray items = load();
    for (int i=0; i<items.size(); ++i) {
        QJsonObject o = items[i].toObject();
        if (normalize(o["question"].toString()) != key) continue;
        o["hits"] = o["hits"].toInt() + 1;
        o["used"] = double(QDateTime::currentMSecsSinceEpoch());
        items[i] = o;
        save(items);
        return;
    }
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
    const QJsonArray items = load();
    QList<QSet<QString>> stemsOf;
    QHash<QString, int> df;
    for (const auto &v : items) {
        stemsOf.append(text::stems(v.toObject()["question"].toString()));
        for (const QString &s : std::as_const(stemsOf.last())) ++df[s];
    }
    const double n = items.size();
    auto weight = [&](const QString &s) { return 1.0 + std::log((n + 1.0) / (df.value(s) + 1.0)); };
    for (int i = 0; i < items.size(); ++i) {
        const QSet<QString> &have = stemsOf.at(i);
        if (have.isEmpty()) continue;
        double common = 0, all = 0;
        for (const QString &s : wanted) { all += weight(s); if (have.contains(s)) common += weight(s); }
        for (const QString &s : have) if (!wanted.contains(s)) all += weight(s);
        const double score = all > 0 ? common / all : 0;
        const auto o = items[i].toObject();
        if (score >= minScore) out.append({o["question"].toString(), o["answer"].toString(), score});
    }
    std::stable_sort(out.begin(), out.end(), [](const Match &a, const Match &b) { return a.score > b.score; });
    return out.mid(0, limit);
}
}
