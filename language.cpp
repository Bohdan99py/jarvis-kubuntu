#include "language.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

#include <memory>

static void initTranslations() { Q_INIT_RESOURCE(translations); }

namespace jarvis {

bool detectLanguage(const QString &text, Lang *out)
{
    int cyrillic = 0;
    int latin = 0;
    for (const QChar c : text) {
        const ushort u = c.unicode();
        if (u >= 0x0400 && u <= 0x04FF)
            ++cyrillic;
        else if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z'))
            ++latin;
    }
    if (cyrillic >= 2 || (cyrillic > 0 && latin < 2)) {
        *out = Lang::Ru;
        return true;
    }
    if (latin >= 2) {
        *out = Lang::En;
        return true;
    }
    return false;
}

Lang resolveLanguage(const QString &preference)
{
    if (preference == QLatin1String("ru"))
        return Lang::Ru;
    if (preference == QLatin1String("en"))
        return Lang::En;
    const QLocale::Language system = QLocale::system().language();
    // Russian-speaking users of Ukrainian/Belarusian locales get the Russian UI
    // rather than falling back to English.
    return (system == QLocale::Russian || system == QLocale::Ukrainian || system == QLocale::Belarusian)
               ? Lang::Ru : Lang::En;
}

QString languageCode(Lang lang)
{
    return lang == Lang::Ru ? QStringLiteral("ru") : QStringLiteral("en");
}

Lang applyUiLanguage(const QString &preference)
{
    static std::unique_ptr<QTranslator> app;
    static std::unique_ptr<QTranslator> qt;
    initTranslations();

    const Lang lang = resolveLanguage(preference);
    for (auto *t : {&app, &qt}) {
        if (*t) {
            QCoreApplication::removeTranslator(t->get());
            t->reset();
        }
    }
    if (lang == Lang::Ru) {
        app = std::make_unique<QTranslator>();
        if (app->load(QStringLiteral(":/i18n/jarvis_ru.qm")))
            QCoreApplication::installTranslator(app.get());
        qt = std::make_unique<QTranslator>();
        if (qt->load(QStringLiteral("qtbase_ru"), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            QCoreApplication::installTranslator(qt.get());
    }
    return lang;
}

} // namespace jarvis
