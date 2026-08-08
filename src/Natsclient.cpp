#include "NatsClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace {
// Тот же префикс ключа, что и SCRIPT_DATA_PREFIX в веб-редакторе.
const char *kScriptDataPrefix = "data.";
const char *kExecSubject = "script.command";
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

bool NatsClient::publishExecCommand(const QString &filename, const QString &code, int fromLine,
                                    QString *errorOut) const
{
    if (!m_conn) {
        if (errorOut)
            *errorOut = QStringLiteral("Нет соединения с NATS");
        return false;
    }

    // Формат совпадает с executeScript() в веб-редакторе:
    // { command: 'exec', type: 'Script', code, from_line, filename }
    QJsonObject cmd;
    cmd.insert(QStringLiteral("command"), QStringLiteral("exec"));
    cmd.insert(QStringLiteral("type"), QStringLiteral("Script"));
    cmd.insert(QStringLiteral("code"), code);
    cmd.insert(QStringLiteral("from_line"), fromLine < 1 ? 1 : fromLine);
    cmd.insert(QStringLiteral("filename"), filename);

    const QByteArray payload = QJsonDocument(cmd).toJson(QJsonDocument::Compact);

    const natsStatus s = natsConnection_Publish(m_conn, kExecSubject, payload.constData(), payload.size());
    if (s != NATS_OK) {
        if (errorOut)
            *errorOut = QString::fromUtf8(natsStatus_GetText(s));
        return false;
    }

    // Гарантируем, что сообщение реально ушло в сокет, прежде чем
    // сообщать вызывающей стороне об успехе.
    natsConnection_Flush(m_conn);
    return true;
}