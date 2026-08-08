#include "NatsClient.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

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

    const natsStatus s = natsConnection_ConnectTo(&m_conn, url.toUtf8().constData());
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
            *errorOut = QStringLiteral("Нет соединения с NATS");
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
            *errorOut = QStringLiteral("KV bucket не открыт");
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
            *errorOut = QStringLiteral("Некорректный JSON записи скрипта: ") + parseErr.errorString();
        return false;
    }

    const QJsonObject obj = doc.object();
    if (!obj.contains(QStringLiteral("code")) || !obj.value(QStringLiteral("code")).isString()) {
        if (errorOut)
            *errorOut = QStringLiteral("В записи скрипта нет строкового поля 'code'");
        return false;
    }

    *codeOut = obj.value(QStringLiteral("code")).toString();
    return true;
}

bool NatsClient::publishJson(const char *subject, const QJsonObject &obj, QString *errorOut) const
{
    if (!m_conn) {
        if (errorOut)
            *errorOut = QStringLiteral("Нет соединения с NATS");
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

bool NatsClient::fetchScriptProgressLine(int *lineOut, QString *errorOut) const
{
    if (!m_kv) {
        if (errorOut)
            *errorOut = QStringLiteral("KV bucket не открыт");
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
            *errorOut = QStringLiteral("Некорректный JSON script.progress: ") + parseErr.errorString();
        return false;
    }

    const QJsonValue lineVal = doc.object().value(QStringLiteral("line"));
    const int line = lineVal.toInt(0);
    if (line < 1) {
        if (errorOut)
            *errorOut = QStringLiteral("В script.progress нет валидной строки");
        return false;
    }

    *lineOut = line;
    return true;
}

bool NatsClient::startModuleWatch(QString *errorOut)
{
    stopModuleWatch();

    if (!m_conn) {
        if (errorOut)
            *errorOut = QStringLiteral("Нет соединения с NATS");
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
    natsMsg_Destroy(msg);
    auto *self = static_cast<NatsClient *>(closure);
    if (self)
        self->m_lastCtrlMs.store(QDateTime::currentMSecsSinceEpoch(), std::memory_order_relaxed);
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
    // Emit из потока NATS → QueuedConnection на GUI (объект живёт в main).
    emit self->scriptStatusReceived(running, completed, line);
}
