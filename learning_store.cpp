#include "learning_store.h"
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QMap>
#include <QSet>
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
        return QStringLiteral("Нужны вопрос и ответ, до 4096 символов каждый.");
    QJsonArray items = load();
    int found = -1;
    for (int i=0; i<items.size(); ++i)
        if (normalize(items[i].toObject()["question"].toString()) == key) { found=i; break; }
    if (found < 0 && items.size() >= 500) return QStringLiteral("Лимит памяти: 500 примеров.");
    QJsonObject entry{{"question", question.trimmed()}, {"answer", answer.trimmed()}};
    if (found >= 0) items[found] = entry; else items.append(entry);
    QDir dir(QFileInfo(path()).absolutePath());
    if (!dir.mkpath(".")) return QStringLiteral("Не удалось создать папку памяти.");
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
QString LearningStore::graph() const {
    QMap<QString,int> counts;
    QMap<QString,int> links;
    const auto items=load();
    for (const auto &v : items) {
        const auto o=v.toObject();
        const auto words=normalize(o["question"].toString()+" "+o["answer"].toString()).split(' ', Qt::SkipEmptyParts);
        QSet<QString> seen;
        QStringList tokens;
        for (const auto &w : words) if (w.size()>2 && !seen.contains(w) && tokens.size()<20) {
            seen.insert(w); tokens.append(w); counts[w]++;
        }
        tokens.sort();
        for (int i=0;i<tokens.size();++i) for (int j=i+1;j<tokens.size();++j)
            links[tokens[i]+QChar(0x1f)+tokens[j]]++;
    }
    QStringList names=counts.keys();
    std::sort(names.begin(),names.end(),[&](const QString &a,const QString &b){return counts[a]==counts[b] ? a<b : counts[a]>counts[b];});
    names=names.mid(0,48);
    QJsonArray nodes, edges;
    for (const auto &name:names) nodes.append(QJsonObject{{"label",name},{"weight",counts[name]}});
    for (auto it=links.cbegin();it!=links.cend();++it) {
        const auto pair=it.key().split(QChar(0x1f));
        int a=names.indexOf(pair[0]), b=names.indexOf(pair[1]);
        if(a>=0 && b>=0) edges.append(QJsonObject{{"source",a},{"target",b},{"weight",it.value()}});
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"nodes",nodes},{"edges",edges},{"examples",items.size()}}).toJson(QJsonDocument::Compact));
}
}
