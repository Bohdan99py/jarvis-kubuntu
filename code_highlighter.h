#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QString>
#include <QtQml/qqmlregistration.h>

// Syntax colours for code blocks in chat answers: keywords, types, strings,
// numbers, comments, preprocessor lines and calls, for C/C++/Arduino, Python,
// JavaScript/QML, Rust, Go, Java, shell, CMake, SQL, assembly, JSON/YAML/INI.
//   TextEdit { id: code } CodeHighlighter { document: code.textDocument; language: "cpp" }
class CodeHighlighter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY changed FINAL)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY changed FINAL)

public:
    explicit CodeHighlighter(QObject *parent = nullptr);
    ~CodeHighlighter() override;

    QQuickTextDocument *document() const { return m_document; }
    void setDocument(QQuickTextDocument *document);
    QString language() const { return m_language; }
    void setLanguage(const QString &language);

signals:
    void changed();

private:
    void attach();

    QPointer<QQuickTextDocument> m_document;
    QString m_language;
    // Owned by the document, which may go first.
    QPointer<QObject> m_highlighter;
};
