#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "language.h"

namespace jarvis {

// Progress in the French theory test (Code de la route, category B): which of
// the ten official themes of the ETG the user practised, how often their own
// answer was right or wrong, and which themes keep going wrong. Results come
// from <code-route theme="R" result="wrong"/> tags Claude adds when it checks
// an answer the user gave, or "seen" when it only explained a question.
// Nothing about the question itself is kept: no text, no screenshot.
// ${XDG_DATA_HOME}/jarvis/driving.json, file 0600; entries older than
// kKeepDays leave the history (the per-theme totals stay).
class DrivingStore
{
public:
    static constexpr int kMaxHistory = 400;
    static constexpr int kKeepDays = 180;

    DrivingStore();

    // The ten ETG themes: L C R U D A P M S E.
    static QStringList themes();
    static QString themeName(const QString &code, Lang lang);
    // "R", "route", "priorité", "signalisation"… to a theme code; "" when unknown.
    static QString themeOf(const QString &text);

    // result: right, wrong or seen. False for an unknown theme or result.
    bool record(const QString &theme, const QString &result, qint64 nowMs);
    void clear();

    // {themes: [{code, name, seen, right, wrong, accuracy}], answered, right, weak: [codes], streak}
    QJsonObject stats(Lang lang, qint64 nowMs) const;
    // Themes answered wrong at least twice with accuracy under 70 %, worst first.
    QStringList weakThemes(qint64 nowMs) const;
    // English lines for Claude; "" when nothing was practised.
    QString promptSummary(qint64 nowMs) const;
    // The chat answer to "my driving test progress".
    QString progressText(Lang lang, qint64 nowMs) const;

private:
    static QString path();
    void load();
    bool save() const;
    void prune(qint64 nowMs);

    QJsonObject m_root;
};

} // namespace jarvis
