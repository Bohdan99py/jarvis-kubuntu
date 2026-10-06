#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// What the settings dialog sees: whether a key is stored, a masked hint of it,
// and the model name. Reads and writes ~/.config/jarvis/config.json.
class AppSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool hasKey READ hasKey NOTIFY changed FINAL)
    Q_PROPERTY(QString keyHint READ keyHint NOTIFY changed FINAL)
    Q_PROPERTY(QString model READ model NOTIFY changed FINAL)
    Q_PROPERTY(QString configPath READ configPath CONSTANT FINAL)

public:
    explicit AppSettings(QObject *parent = nullptr);

    bool hasKey() const noexcept { return m_hasKey; }
    QString keyHint() const { return m_keyHint; }
    QString model() const { return m_model; }
    QString configPath() const;

    // An empty `apiKey` keeps the stored key. Both return "" on success,
    // otherwise a message for the user.
    Q_INVOKABLE QString save(const QString &apiKey, const QString &model);
    Q_INVOKABLE QString removeKey();

signals:
    void changed();
    void saved();

private:
    void refresh(bool notify);

    bool m_hasKey = false;
    QString m_keyHint;
    QString m_model;
};
