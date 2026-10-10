#include "Texts.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTranslator>

namespace {

// Заполняется до создания окон и потоков, дальше только читается.
QHash<QString, QString> &overrides()
{
    static QHash<QString, QString> map;
    return map;
}

void readTexts(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject texts =
        QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("texts")).toObject();
    for (auto group = texts.constBegin(); group != texts.constEnd(); ++group) {
        const QJsonObject entries = group.value().toObject();
        for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
            const QString value = it.value().toString();
            if (!value.isEmpty() && value != it.key())
                overrides().insert(it.key(), value);
        }
    }
}

class TextsTranslator : public QTranslator
{
public:
    using QTranslator::QTranslator;

    QString translate(const char *, const char *sourceText, const char *, int) const override
    {
        if (!sourceText)
            return {};
        return overrides().value(QString::fromUtf8(sourceText));
    }

    bool isEmpty() const override { return false; }
};

} // namespace

namespace Texts {

void load(const QString &configPath)
{
    overrides().clear();
#ifdef Q_OS_ANDROID
    const QString bundled = QStringLiteral("assets:/config.json");
#else
    const QString bundled =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config.json"));
#endif
    readTexts(bundled);
    if (QFileInfo(configPath).absoluteFilePath() != QFileInfo(bundled).absoluteFilePath())
        readTexts(configPath);

    if (auto *app = QCoreApplication::instance())
        QCoreApplication::installTranslator(new TextsTranslator(app));
}

QString get(const char *source)
{
    const QString text = QString::fromUtf8(source);
    const auto it = overrides().constFind(text);
    return it == overrides().constEnd() ? text : it.value();
}

} // namespace Texts
