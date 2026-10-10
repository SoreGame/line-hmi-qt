#include "NatsClient.h"
#include "Texts.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <algorithm>

namespace {
// Тот же префикс ключа, что и SCRIPT_DATA_PREFIX в веб-редакторе.
const char *kScriptDataPrefix = "data.";
const char *kScriptProgressKey = "script.progress";
const char *kExecSubject = "script.command";
const char *kBehaviourCommandSubject = "behaviour.command";
const char *kMotionCommandSubject = "motion.command";
const char *kMotionStatusSubject = "motion.status";
const char *kBehaviourStatusSubject = "behaviour.status";
const char *kScriptStatusSubject = "script.status";
const char *kRobotPointsBucket = "robot_points";

double jsonNumber(const QJsonObject &obj, const char *lower, const char *upper)
{
    const QJsonValue v = obj.contains(QLatin1String(lower)) ? obj.value(QLatin1String(lower))
                                                            : obj.value(QLatin1String(upper));
    if (v.isDouble())
        return v.toDouble();
    if (v.isString())
        return v.toString().toDouble();
    return 0;
}

QJsonObject poseSource(const QJsonObject &obj)
{
    if (obj.value(QStringLiteral("data")).isObject())
        return obj.value(QStringLiteral("data")).toObject();
    return obj;
}

void applyXyzr(QJsonObject &obj, double x, double y, double z, double r)
{
    const auto setPose = [&](QJsonObject &o) {
        o.insert(QStringLiteral("x"), x);
        o.insert(QStringLiteral("y"), y);
        o.insert(QStringLiteral("z"), z);
        o.insert(QStringLiteral("r"), r);
        if (o.contains(QStringLiteral("X")))
            o.insert(QStringLiteral("X"), x);
        if (o.contains(QStringLiteral("Y")))
            o.insert(QStringLiteral("Y"), y);
        if (o.contains(QStringLiteral("Z")))
            o.insert(QStringLiteral("Z"), z);
        if (o.contains(QStringLiteral("R")))
            o.insert(QStringLiteral("R"), r);
    };

    setPose(obj);
    if (obj.value(QStringLiteral("data")).isObject()) {
        QJsonObject data = obj.value(QStringLiteral("data")).toObject();
        setPose(data);
        obj.insert(QStringLiteral("data"), data);
    }
}

NatsClient::RobotPoint parseRobotPoint(const QString &kvKey, const QJsonObject &obj)
{
    const QJsonObject data = poseSource(obj);
    NatsClient::RobotPoint p;
    p.kvKey = kvKey;
    p.name = obj.value(QStringLiteral("name")).toString().trimmed();
    if (p.name.isEmpty())
        p.name = data.value(QStringLiteral("name")).toString().trimmed();
    if (p.name.isEmpty())
        p.name = kvKey;
    p.x = jsonNumber(data, "x", "X");
    p.y = jsonNumber(data, "y", "Y");
    p.z = jsonNumber(data, "z", "Z");
    p.r = jsonNumber(data, "r", "R");
    return p;
}

bool parseKvJson(kvEntry *entry, QJsonObject *objOut, QString *errorOut)
{
    const QByteArray json(reinterpret_cast<const char *>(kvEntry_Value(entry)),
                          kvEntry_ValueLen(entry));
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut)
            *errorOut = tx("Некорректный JSON точки: ") + parseErr.errorString();
        return false;
    }
    *objOut = doc.object();
    return true;
}
}

NatsClient::NatsClient(QObject *parent)
    : QObject(parent)
{
}

NatsClient::~NatsClient()
{
    disconnectFromServer();
}

bool NatsClient::connectToServer(const QString &url, QString *errorOut)
{
    disconnectFromServer();

    natsOptions *opts = nullptr;
    natsStatus s = natsOptions_Create(&opts);
    if (s != NATS_OK) {
        if (errorOut)
            *errorOut = QString::fromUtf8(natsStatus_GetText(s));
        return false;
    }

    const QByteArray urlBytes = url.toUtf8();
    s = natsOptions_SetURL(opts, urlBytes.constData());
    if (s == NATS_OK)
        s = natsOptions_SetTimeout(opts, 5000);
    if (s == NATS_OK)
        s = natsOptions_IPResolutionOrder(opts, 4); // IPv4 first (Android LAN)
    if (s == NATS_OK)
        s = natsConnection_Connect(&m_conn, opts);

    natsOptions_Destroy(opts);

    if (s != NATS_OK) {
        if (errorOut)
            *errorOut = QString::fromUtf8(natsStatus_GetText(s));
        m_conn = nullptr;
        return false;
    }
    return true;
}

void NatsClient::disconnectFromServer()
{
    stopModuleWatch();
    if (m_robotPointsKv) {
        kvStore_Destroy(m_robotPointsKv);
        m_robotPointsKv = nullptr;
    }
    if (m_kv) {
        kvStore_Destroy(m_kv);
        m_kv = nullptr;
    }
    if (m_conn) {
        natsConnection_Destroy(m_conn);
        m_conn = nullptr;
    }
}

bool NatsClient::isConnected() const
{
    return m_conn != nullptr && natsConnection_Status(m_conn) == NATS_CONN_STATUS_CONNECTED;
}

bool NatsClient::openKvBucket(const QString &bucketName, QString *errorOut)
{
    if (!m_conn) {
        if (errorOut)
            *errorOut = tx("Нет соединения с NATS");
        return false;
    }

    jsCtx *js = nullptr;
    natsStatus s = natsConnection_JetStream(&js, m_conn, nullptr);
    if (s != NATS_OK) {
        if (errorOut)
            *errorOut = QString::fromUtf8(natsStatus_GetText(s));
        return false;
    }

    if (m_kv) {
        kvStore_Destroy(m_kv);
        m_kv = nullptr;
    }

    s = js_KeyValue(&m_kv, js, bucketName.toUtf8().constData());
    jsCtx_Destroy(js);

    if (s != NATS_OK) {
        if (errorOut)
            *errorOut = QString::fromUtf8(natsStatus_GetText(s));
        m_kv = nullptr;
        return false;
    }
    return true;
}

bool NatsClient::fetchScriptCode(const QString &filename, QString *codeOut, QString *errorOut) const
{
    if (!m_kv) {
        if (errorOut)
            *errorOut = tx("KV bucket не открыт");
        return false;
    }

    const QString key = QString::fromUtf8(kScriptDataPrefix) + filename;

    kvEntry *entry = nullptr;
    const natsStatus s = kvStore_Get(&entry, m_kv, key.toUtf8().constData());
    if (s != NATS_OK) {
        if (errorOut)
            *errorOut = QString::fromUtf8(natsStatus_GetText(s));
        return false;
    }

    const QByteArray json(reinterpret_cast<const char *>(kvEntry_Value(entry)), kvEntry_ValueLen(entry));
    kvEntry_Destroy(entry);

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut)
            *errorOut = tx("Некорректный JSON записи скрипта: ") + parseErr.errorString();
        return false;
    }

    const QJsonObject obj = doc.object();
    if (!obj.contains(QStringLiteral("code")) || !obj.value(QStringLiteral("code")).isString()) {
        if (errorOut)
            *errorOut = tx("В записи скрипта нет строкового поля 'code'");
        return false;
    }

    *codeOut = obj.value(QStringLiteral("code")).toString();
    return true;
}

bool NatsClient::publishJson(const char *subject, const QJsonObject &obj, QString *errorOut) const
{
    if (!m_conn) {
        if (errorOut)
            *errorOut = tx("Нет соединения с NATS");
        return false;
    }

    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    const natsStatus s = natsConnection_Publish(m_conn, subject, payload.constData(), payload.size());
    if (s != NATS_OK) {
        if (errorOut)
            *errorOut = QString::fromUtf8(natsStatus_GetText(s));
        return false;
    }
    return true;
}

bool NatsClient::publishExecCommand(const QString &filename, const QString &code, int fromLine,
                                    QString *errorOut) const
{
    // Формат совпадает с executeScript() в веб-редакторе:
    // { command: 'exec', type: 'Script', code, from_line, filename }
    QJsonObject cmd;
    cmd.insert(QStringLiteral("command"), QStringLiteral("exec"));
    cmd.insert(QStringLiteral("type"), QStringLiteral("Script"));
    cmd.insert(QStringLiteral("code"), code);
    cmd.insert(QStringLiteral("from_line"), fromLine < 1 ? 1 : fromLine);
    cmd.insert(QStringLiteral("filename"), filename);

    if (!publishJson(kExecSubject, cmd, errorOut))
        return false;

    natsConnection_Flush(m_conn);
    return true;
}

bool NatsClient::publishStopCommand(QString *errorOut) const
{
    // Как stop() в robot-gui/src/lib/client/commands.ts:
    // останавливаем и движение, и скрипт/behaviour — иначе delay()/следующий
    // moveL снова поднимут run на контроллере.
    QJsonObject stop;
    stop.insert(QStringLiteral("command"), QStringLiteral("stop"));

    if (!publishJson(kMotionCommandSubject, stop, errorOut))
        return false;
    if (!publishJson(kBehaviourCommandSubject, stop, errorOut))
        return false;
    if (!publishJson(kExecSubject, stop, errorOut))
        return false;

    natsConnection_Flush(m_conn);
    return true;
}

bool NatsClient::publishMotionStart(QString *errorOut) const
{
    QJsonObject cmd;
    cmd.insert(QStringLiteral("command"), QStringLiteral("start"));
    if (!publishJson(kMotionCommandSubject, cmd, errorOut))
        return false;
    natsConnection_Flush(m_conn);
    return true;
}

bool NatsClient::publishMotionStop(QString *errorOut) const
{
    QJsonObject cmd;
    cmd.insert(QStringLiteral("command"), QStringLiteral("stop"));
    if (!publishJson(kMotionCommandSubject, cmd, errorOut))
        return false;
    natsConnection_Flush(m_conn);
    return true;
}

bool NatsClient::publishMoveOffset(double x, double y, double z, double r, QString *errorOut) const
{
    QJsonObject offset;
    offset.insert(QStringLiteral("x"), x);
    offset.insert(QStringLiteral("y"), y);
    offset.insert(QStringLiteral("z"), z);
    offset.insert(QStringLiteral("r"), r);

    QJsonObject cmd;
    cmd.insert(QStringLiteral("command"), QStringLiteral("moveOffset"));
    cmd.insert(QStringLiteral("duration"), 1);
    cmd.insert(QStringLiteral("offset"), offset);

    if (!publishJson(kMotionCommandSubject, cmd, errorOut))
        return false;
    natsConnection_Flush(m_conn);
    return true;
}

bool NatsClient::fetchScriptProgressLine(int *lineOut, QString *errorOut) const
{
    if (!m_kv) {
        if (errorOut)
            *errorOut = tx("KV bucket не открыт");
        return false;
    }
    if (!lineOut)
        return false;

    kvEntry *entry = nullptr;
    const natsStatus s = kvStore_Get(&entry, m_kv, kScriptProgressKey);
    if (s != NATS_OK) {
        if (errorOut)
            *errorOut = QString::fromUtf8(natsStatus_GetText(s));
        return false;
    }

    const QByteArray json(reinterpret_cast<const char *>(kvEntry_Value(entry)), kvEntry_ValueLen(entry));
    kvEntry_Destroy(entry);

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut)
            *errorOut = tx("Некорректный JSON script.progress: ") + parseErr.errorString();
        return false;
    }

    const QJsonValue lineVal = doc.object().value(QStringLiteral("line"));
    const int line = lineVal.toInt(0);
    if (line < 1) {
        if (errorOut)
            *errorOut = tx("В script.progress нет валидной строки");
        return false;
    }

    *lineOut = line;
    return true;
}

bool NatsClient::openRobotPointsKv(QString *errorOut)
{
    if (!m_conn) {
        if (errorOut)
            *errorOut = tx("Нет соединения с NATS");
        return false;
    }
    if (m_robotPointsKv)
        return true;

    jsCtx *js = nullptr;
    natsStatus s = natsConnection_JetStream(&js, m_conn, nullptr);
    if (s != NATS_OK) {
        if (errorOut)
            *errorOut = QString::fromUtf8(natsStatus_GetText(s));
        return false;
    }

    s = js_KeyValue(&m_robotPointsKv, js, kRobotPointsBucket);
    jsCtx_Destroy(js);

    if (s != NATS_OK) {
        if (errorOut)
            *errorOut = QString::fromUtf8(natsStatus_GetText(s));
        m_robotPointsKv = nullptr;
        return false;
    }
    return true;
}

bool NatsClient::fetchRobotPoints(QVector<RobotPoint> *out, QString *errorOut) const
{
    if (!out)
        return false;
    out->clear();

    if (!m_robotPointsKv) {
        if (errorOut)
            *errorOut = tx("KV bucket robot_points не открыт");
        return false;
    }

    kvKeysList keys;
    keys.Keys = nullptr;
    keys.Count = 0;
    natsStatus s = kvStore_Keys(&keys, m_robotPointsKv, nullptr);
    if (s == NATS_NOT_FOUND) {
        return true;
    }
    if (s != NATS_OK) {
        if (errorOut)
            *errorOut = QString::fromUtf8(natsStatus_GetText(s));
        return false;
    }

    QVector<RobotPoint> points;
    points.reserve(keys.Count);
    for (int i = 0; i < keys.Count; ++i) {
        const QString kvKey = QString::fromUtf8(keys.Keys[i]);
        kvEntry *entry = nullptr;
        const natsStatus gs = kvStore_Get(&entry, m_robotPointsKv, keys.Keys[i]);
        if (gs != NATS_OK || !entry)
            continue;

        QJsonObject obj;
        QString parseErr;
        const bool ok = parseKvJson(entry, &obj, &parseErr);
        kvEntry_Destroy(entry);
        if (!ok)
            continue;
        points.push_back(parseRobotPoint(kvKey, obj));
    }
    kvKeysList_Destroy(&keys);

    std::sort(points.begin(), points.end(), [](const RobotPoint &a, const RobotPoint &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    *out = std::move(points);
    return true;
}

bool NatsClient::saveRobotPointPose(const QString &kvKey, double x, double y, double z, double r,
                                    QString *errorOut) const
{
    if (!m_robotPointsKv) {
        if (errorOut)
            *errorOut = tx("KV bucket robot_points не открыт");
        return false;
    }
    if (kvKey.isEmpty()) {
        if (errorOut)
            *errorOut = tx("Пустой ключ точки");
        return false;
    }

    const QByteArray keyBytes = kvKey.toUtf8();
    kvEntry *entry = nullptr;
    natsStatus s = kvStore_Get(&entry, m_robotPointsKv, keyBytes.constData());
    if (s != NATS_OK) {
        if (errorOut)
            *errorOut = QString::fromUtf8(natsStatus_GetText(s));
        return false;
    }

    QJsonObject obj;
    if (!parseKvJson(entry, &obj, errorOut)) {
        kvEntry_Destroy(entry);
        return false;
    }
    kvEntry_Destroy(entry);

    applyXyzr(obj, x, y, z, r);

    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    s = kvStore_Put(nullptr, m_robotPointsKv, keyBytes.constData(), payload.constData(),
                    payload.size());
    if (s != NATS_OK) {
        if (errorOut)
            *errorOut = QString::fromUtf8(natsStatus_GetText(s));
        return false;
    }
    return true;
}

bool NatsClient::startModuleWatch(QString *errorOut)
{
    stopModuleWatch();

    if (!m_conn) {
        if (errorOut)
            *errorOut = tx("Нет соединения с NATS");
        return false;
    }

    natsStatus s = natsConnection_Subscribe(&m_motionSub, m_conn, kMotionStatusSubject,
                                            &NatsClient::onMotionStatus, this);
    if (s != NATS_OK) {
        if (errorOut)
            *errorOut = QString::fromUtf8(natsStatus_GetText(s));
        m_motionSub = nullptr;
        return false;
    }

    s = natsConnection_Subscribe(&m_behaviourSub, m_conn, kBehaviourStatusSubject,
                                 &NatsClient::onBehaviourStatus, this);
    if (s != NATS_OK) {
        if (errorOut)
            *errorOut = QString::fromUtf8(natsStatus_GetText(s));
        stopModuleWatch();
        return false;
    }

    s = natsConnection_Subscribe(&m_scriptStatusSub, m_conn, kScriptStatusSubject,
                                 &NatsClient::onScriptStatus, this);
    if (s != NATS_OK) {
        if (errorOut)
            *errorOut = QString::fromUtf8(natsStatus_GetText(s));
        stopModuleWatch();
        return false;
    }

    return true;
}

void NatsClient::stopModuleWatch()
{
    if (m_motionSub) {
        natsSubscription_Destroy(m_motionSub);
        m_motionSub = nullptr;
    }
    if (m_behaviourSub) {
        natsSubscription_Destroy(m_behaviourSub);
        m_behaviourSub = nullptr;
    }
    if (m_scriptStatusSub) {
        natsSubscription_Destroy(m_scriptStatusSub);
        m_scriptStatusSub = nullptr;
    }
    m_lastCtrlMs.store(0, std::memory_order_relaxed);
    m_lastBehaviourMs.store(0, std::memory_order_relaxed);
    m_lastScriptLine.store(0, std::memory_order_relaxed);
}

void NatsClient::onMotionStatus(natsConnection *, natsSubscription *, natsMsg *msg, void *closure)
{
    auto *self = static_cast<NatsClient *>(closure);
    if (!self) {
        natsMsg_Destroy(msg);
        return;
    }

    const QByteArray json(natsMsg_GetData(msg), natsMsg_GetDataLength(msg));
    natsMsg_Destroy(msg);

    self->m_lastCtrlMs.store(QDateTime::currentMSecsSinceEpoch(), std::memory_order_relaxed);

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return;

    const QJsonObject obj = doc.object();
    const QJsonObject pose = obj.value(QStringLiteral("pose")).toObject();
    const double x = pose.value(QStringLiteral("x")).toDouble();
    const double y = pose.value(QStringLiteral("y")).toDouble();
    const double z = pose.value(QStringLiteral("z")).toDouble();
    const double r = pose.value(QStringLiteral("r")).toDouble();
    const bool running = obj.value(QStringLiteral("run")).toBool(false);
    emit self->motionPoseReceived(x, y, z, r, running);
}

void NatsClient::onBehaviourStatus(natsConnection *, natsSubscription *, natsMsg *msg, void *closure)
{
    natsMsg_Destroy(msg);
    auto *self = static_cast<NatsClient *>(closure);
    if (self)
        self->m_lastBehaviourMs.store(QDateTime::currentMSecsSinceEpoch(), std::memory_order_relaxed);
}

void NatsClient::onScriptStatus(natsConnection *, natsSubscription *, natsMsg *msg, void *closure)
{
    auto *self = static_cast<NatsClient *>(closure);
    if (!self) {
        natsMsg_Destroy(msg);
        return;
    }

    const QByteArray json(natsMsg_GetData(msg), natsMsg_GetDataLength(msg));
    natsMsg_Destroy(msg);

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return;

    const QJsonObject obj = doc.object();
    const int line = obj.value(QStringLiteral("line")).toInt(0);
    if (line > 0)
        self->m_lastScriptLine.store(line, std::memory_order_relaxed);

    const bool running = obj.value(QStringLiteral("running")).toBool(false);
    const bool completed = obj.value(QStringLiteral("completed")).toBool(false);
    const QString filename = obj.value(QStringLiteral("filename")).toString();
    // Emit из потока NATS → QueuedConnection на GUI (объект живёт в main).
    emit self->scriptStatusReceived(running, completed, line, filename);
}
