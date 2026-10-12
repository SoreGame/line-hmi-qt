#include "RunHistory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

constexpr int kMaxStored = 1000;

QJsonObject toJson(const RunRecord &r)
{
    QJsonObject o;
    o.insert(QStringLiteral("program"), r.program);
    o.insert(QStringLiteral("started"), r.started.toString(Qt::ISODate));
    o.insert(QStringLiteral("finished"), r.finished.toString(Qt::ISODate));
    o.insert(QStringLiteral("good"), r.good);
    o.insert(QStringLiteral("scrap"), r.scrap);
    o.insert(QStringLiteral("cycles"), r.cycles);
    o.insert(QStringLiteral("totalCycleMs"), double(r.totalCycleMs));
    o.insert(QStringLiteral("lastCycleMs"), double(r.lastCycleMs));
    o.insert(QStringLiteral("reason"), r.reason);
    return o;
}

RunRecord fromJson(const QJsonObject &o)
{
    RunRecord r;
    r.program = o.value(QStringLiteral("program")).toInt();
    r.started = QDateTime::fromString(o.value(QStringLiteral("started")).toString(), Qt::ISODate);
    r.finished = QDateTime::fromString(o.value(QStringLiteral("finished")).toString(), Qt::ISODate);
    r.good = o.value(QStringLiteral("good")).toInt();
    r.scrap = o.value(QStringLiteral("scrap")).toInt();
    r.cycles = o.value(QStringLiteral("cycles")).toInt();
    r.totalCycleMs = qint64(o.value(QStringLiteral("totalCycleMs")).toDouble());
    r.lastCycleMs = qint64(o.value(QStringLiteral("lastCycleMs")).toDouble(-1));
    r.reason = o.value(QStringLiteral("reason")).toString();
    return r;
}

} // namespace

QString RunHistory::filePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dir).filePath(QStringLiteral("run-history.json"));
}

QVector<RunRecord> RunHistory::load()
{
    QVector<RunRecord> out;
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
        return out;
    const QJsonArray arr = QJsonDocument::fromJson(file.readAll()).array();
    out.reserve(arr.size());
    for (const QJsonValue &v : arr)
        out.push_back(fromJson(v.toObject()));
    return out;
}

void RunHistory::append(const RunRecord &record)
{
    QVector<RunRecord> all = load();
    all.push_back(record);
    if (all.size() > kMaxStored)
        all.remove(0, all.size() - kMaxStored);

    QJsonArray arr;
    for (const RunRecord &r : all)
        arr.append(toJson(r));

    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    file.commit();
}
