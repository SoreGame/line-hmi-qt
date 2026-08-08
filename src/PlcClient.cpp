#include "PlcClient.h"

#include <QDateTime>
#include <QTcpSocket>
#include <QThread>

namespace {

QByteArray makeHoldFrame()
{
    QByteArray frame(PlcClient::kFrameSize, '\0');
    for (int i = 0; i < PlcClient::kValveCount; ++i)
        frame[PlcClient::kValveOffset + i] = static_cast<char>(PlcClient::kHoldByte);
    return frame;
}

bool readExact(QTcpSocket *sock, char *dst, int n, int timeoutMs)
{
    int got = 0;
    while (got < n) {
        if (sock->bytesAvailable() < 1) {
            if (!sock->waitForReadyRead(timeoutMs))
                return false;
        }
        const qint64 nread = sock->read(dst + got, n - got);
        if (nread <= 0)
            return false;
        got += static_cast<int>(nread);
    }
    return true;
}

} // namespace

class PlcClient::Worker : public QThread
{
public:
    Worker(PlcClient *owner, QString host, quint16 port)
        : m_owner(owner)
        , m_host(std::move(host))
        , m_port(port)
    {
    }

protected:
    void run() override
    {
        QTcpSocket sock;
        while (!isInterruptionRequested()) {
            if (sock.state() != QAbstractSocket::ConnectedState) {
                sock.abort();
                sock.connectToHost(m_host, m_port);
                if (!sock.waitForConnected(500)) {
                    sock.abort();
                    msleep(500);
                    continue;
                }
            }

            const QByteArray frame = makeHoldFrame();
            if (sock.write(frame) != frame.size() || !sock.waitForBytesWritten(500)) {
                sock.abort();
                msleep(200);
                continue;
            }

            QByteArray reply(PlcClient::kFrameSize, Qt::Uninitialized);
            if (!readExact(&sock, reply.data(), PlcClient::kFrameSize, 1000)) {
                sock.abort();
                msleep(200);
                continue;
            }

            QVector<quint8> valves(PlcClient::kValveCount);
            QVector<quint8> sensors(PlcClient::kSensorCount);
            for (int i = 0; i < PlcClient::kValveCount; ++i)
                valves[i] = static_cast<quint8>(reply[PlcClient::kValveOffset + i]);
            for (int i = 0; i < PlcClient::kSensorCount; ++i)
                sensors[i] = static_cast<quint8>(reply[PlcClient::kSensorOffset + i]);

            {
                std::lock_guard<std::mutex> lock(m_owner->m_dataMutex);
                m_owner->m_valves = std::move(valves);
                m_owner->m_sensors = std::move(sensors);
            }
            m_owner->m_lastOkMs.store(QDateTime::currentMSecsSinceEpoch(),
                                      std::memory_order_relaxed);
            emit m_owner->stateChanged();

            msleep(500);
        }

        sock.abort();
    }

private:
    PlcClient *m_owner = nullptr;
    QString m_host;
    quint16 m_port = 0;
};

PlcClient::PlcClient(QObject *parent)
    : QObject(parent)
    , m_valves(kValveCount, 0)
    , m_sensors(kSensorCount, 0)
{
}

PlcClient::~PlcClient()
{
    stop();
}

void PlcClient::start(const QString &host, quint16 port)
{
    stop();
    m_worker = new Worker(this, host, port);
    m_worker->start();
}

void PlcClient::stop()
{
    if (!m_worker)
        return;
    m_worker->requestInterruption();
    m_worker->quit();
    if (!m_worker->wait(2000))
        m_worker->terminate();
    delete m_worker;
    m_worker = nullptr;
}

bool PlcClient::isOk() const
{
    const qint64 last = m_lastOkMs.load(std::memory_order_relaxed);
    if (last <= 0)
        return false;
    return (QDateTime::currentMSecsSinceEpoch() - last) < kTimeoutMs;
}

QVector<quint8> PlcClient::valves() const
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    return m_valves;
}

QVector<quint8> PlcClient::sensors() const
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    return m_sensors;
}
