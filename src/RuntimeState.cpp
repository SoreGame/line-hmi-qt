#include "RuntimeState.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

QString preferredStatePath()
{
#ifdef Q_OS_ANDROID
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("state.json"));
#else
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("state.json"));
#endif
}

void syncFile(QFile &file)
{
    file.flush();
#ifdef Q_OS_UNIX
    const int fd = file.handle();
    if (fd >= 0)
        ::fsync(fd);
#endif
}

} // namespace

QString RuntimeState::stateFilePath()
{
    return preferredStatePath();
}

RuntimeState RuntimeState::load()
{
    RuntimeState state;
    const QString path = stateFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return state;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return state;

    const QJsonObject root = doc.object();
    state.recoveryPending = root.value(QStringLiteral("recoveryPending")).toBool(false);
    if (root.contains(QStringLiteral("cleanShutdown")))
        state.cleanShutdown = root.value(QStringLiteral("cleanShutdown")).toBool(true);
    return state;
}

void RuntimeState::save() const
{
    const QString path = stateFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject root;
    {
        QFile existing(path);
        if (existing.open(QIODevice::ReadOnly)) {
            const QJsonDocument prev = QJsonDocument::fromJson(existing.readAll());
            if (prev.isObject())
                root = prev.object();
        }
    }

    root.insert(QStringLiteral("recoveryPending"), recoveryPending);
    root.insert(QStringLiteral("cleanShutdown"), cleanShutdown);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    syncFile(file);
}

void RuntimeState::setRecoveryPending(bool pending)
{
    RuntimeState state = load();
    if (state.recoveryPending == pending)
        return;
    state.recoveryPending = pending;
    state.save();
}

void RuntimeState::setCleanShutdown(bool clean)
{
    RuntimeState state = load();
    if (state.cleanShutdown == clean)
        return;
    state.cleanShutdown = clean;
    state.save();
}
