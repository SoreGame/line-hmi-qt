#include "PlcClient.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QThread>

#include <chrono>

namespace {

constexpr int kPingIntervalMs = 1000;
constexpr int kPingTimeoutMs = 800;
constexpr int kConnectTimeoutMs = 3000;
constexpr int kLegacyStatusSize = 100; // прежний кадр 50 int16
constexpr char kPingFrame[] = {static_cast<char>(203), '\0', '\0', '\0'};

bool readExact(QTcpSocket &sock, QByteArray *out, int nbytes, int timeoutMs)
{
    out->clear();
    QElapsedTimer timer;
    timer.start();
    while (out->size() < nbytes) {
        if (timer.elapsed() > timeoutMs)
            return false;
        if (sock.state() != QAbstractSocket::ConnectedState)
            return false;
        if (sock.bytesAvailable() > 0 || sock.waitForReadyRead(qMin(100, timeoutMs))) {
            out->append(sock.read(nbytes - out->size()));
        }
    }
    return out->size() >= nbytes;
}

// ПЛК отдаёт statusBytes (100..kStatusSize).
bool readStatusFrame(QTcpSocket &sock, QByteArray *out, int timeoutMs, int statusBytes)
{
    const int expected = qBound(kLegacyStatusSize, statusBytes, PlcClient::kStatusSize);
    out->clear();
    QElapsedTimer timer;
    timer.start();
    while (out->size() < expected) {
        const int elapsed = static_cast<int>(timer.elapsed());
        if (elapsed > timeoutMs)
            break;
        if (sock.state() != QAbstractSocket::ConnectedState)
            break;
        const int remain = timeoutMs - elapsed;
        if (sock.bytesAvailable() > 0 || sock.waitForReadyRead(qMin(50, qMax(1, remain)))) {
            out->append(sock.read(expected - out->size()));
            continue;
        }
    }
    if (out->size() < expected)
        return false;
    if (out->size() < PlcClient::kStatusSize)
        out->append(QByteArray(PlcClient::kStatusSize - out->size(), '\0'));
    return true;
}

} // namespace

class PlcClient::Worker : public QThread
{
public:
    Worker(PlcClient *owner, QString host, quint16 port,
           QByteArray readyValue, QByteArray readyMask, int statusBytes)
        : m_owner(owner)
        , m_host(std::move(host))
        , m_port(port)
        , m_readyValue(std::move(readyValue))
        , m_readyMask(std::move(readyMask))
        , m_statusBytes(qBound(PlcClient::kMinStatusSize, statusBytes, PlcClient::kStatusSize))
    {
    }

protected:
    void run() override
    {
        QTcpSocket sock;
        QElapsedTimer pingTimer;
        pingTimer.start();

        while (!isInterruptionRequested()) {
            if (sock.state() != QAbstractSocket::ConnectedState) {
                sock.abort();
                failQueued(QStringLiteral("Нет связи с ПЛК"));
                sock.connectToHost(m_host, m_port);
                if (!sock.waitForConnected(kConnectTimeoutMs)) {
                    sock.abort();
                    msleep(500);
                    continue;
                }
                sock.setSocketOption(QAbstractSocket::LowDelayOption, 1);
            }

            QByteArray command;
            {
                std::unique_lock<std::mutex> lock(m_owner->m_cmdMutex);
                if (m_owner->m_cmdQueue.empty() && pingTimer.elapsed() < kPingIntervalMs) {
                    m_owner->m_cmdCv.wait_for(lock, std::chrono::milliseconds(50), [this] {
                        return isInterruptionRequested() || !m_owner->m_cmdQueue.empty();
                    });
                }
                if (!m_owner->m_cmdQueue.empty()) {
                    command = std::move(m_owner->m_cmdQueue.front());
                    m_owner->m_cmdQueue.pop_front();
                }
            }
            if (isInterruptionRequested())
                break;

            if (!command.isEmpty()) {
                if (!writeFrame(sock, command)) {
                    emit m_owner->commandFinished(
                        false, QStringLiteral("Не удалось отправить кадр на ПЛК"));
                    sock.abort();
                    continue;
                }
                // Следующий опрос не раньше чем через интервал, чтобы 10 байт
                // и ping не склеились в один TCP-сегмент.
                pingTimer.restart();
                emit m_owner->commandFinished(true, QString());
                continue;
            }

            if (pingTimer.elapsed() < kPingIntervalMs)
                continue;
            pingTimer.restart();

            if (!pingPong(sock))
                sock.abort();
        }

        sock.abort();
    }

private:
    void failQueued(const QString &error)
    {
        std::deque<QByteArray> dropped;
        {
            std::lock_guard<std::mutex> lock(m_owner->m_cmdMutex);
            dropped.swap(m_owner->m_cmdQueue);
        }
        for (std::size_t i = 0; i < dropped.size(); ++i)
            emit m_owner->commandFinished(false, error);
    }

    bool writeFrame(QTcpSocket &sock, const QByteArray &frame)
    {
        const qint64 written = sock.write(frame);
        if (written != frame.size() || !sock.waitForBytesWritten(kPingTimeoutMs))
            return false;
        // flush() returns false when the buffer is already empty after
        // waitForBytesWritten, so it is not a send failure.
        return sock.bytesToWrite() == 0;
    }

    bool pingPong(QTcpSocket &sock)
    {
        const QByteArray ping(kPingFrame, sizeof(kPingFrame));
        if (sock.write(ping) != ping.size() || !sock.waitForBytesWritten(kPingTimeoutMs))
            return false;

        QByteArray reply;
        if (!readStatusFrame(sock, &reply, kPingTimeoutMs, m_statusBytes))
            return false;

        applyIncoming(reply.left(PlcClient::kStatusSize));
        m_owner->m_lastOkMs.store(QDateTime::currentMSecsSinceEpoch(),
                                  std::memory_order_relaxed);
        emit m_owner->stateChanged();
        return true;
    }

    void applyIncoming(const QByteArray &buf)
    {
        if (buf.isEmpty())
            return;

        QVector<quint8> valves(PlcClient::kValveCount, 0);
        QVector<quint8> sensors(PlcClient::kSensorCount, 0);
        {
            std::lock_guard<std::mutex> lock(m_owner->m_dataMutex);
            valves = m_owner->m_valves;
            sensors = m_owner->m_sensors;
        }

        QByteArray frame = buf;
        if (frame.size() < PlcClient::kStatusSize)
            frame.append(QByteArray(PlcClient::kStatusSize - frame.size(), '\0'));

        if (frame.size() < kLegacyStatusSize || (frame.size() % PlcClient::kStatusIntBytes) != 0)
            return;

        for (int i = 0; i < PlcClient::kValveCount; ++i)
            valves[i] = static_cast<quint8>(buf[PlcClient::kValveOffset + i]);
        for (int i = 0; i < PlcClient::kSensorCount; ++i)
            sensors[i] = static_cast<quint8>(buf[PlcClient::kSensorOffset + i]);

        int mismatch = -1;
        quint8 actual = 0;
        quint8 expectedBits = 0;
        for (int i = 0; i < PlcClient::kStatusSize; ++i) {
            const quint8 b = static_cast<quint8>(buf[i]);
            const quint8 m = static_cast<quint8>(m_readyMask[i]);
            const quint8 e = static_cast<quint8>(m_readyValue[i]);
            if ((b & m) != (e & m)) {
                mismatch = i;
                actual = b;
                expectedBits = e & m;
                break;
            }
        }
        m_owner->m_maskOk.store(mismatch < 0, std::memory_order_relaxed);
        m_owner->m_mismatchIndex.store(mismatch, std::memory_order_relaxed);
        m_owner->m_mismatchActual.store(actual, std::memory_order_relaxed);
        m_owner->m_mismatchExpected.store(expectedBits, std::memory_order_relaxed);

        const auto readI16 = [&buf](int index) -> qint16 {
            const int off = index * PlcClient::kStatusIntBytes;
            if (off < 0 || off + 1 >= buf.size())
                return 0;
            const auto lo = static_cast<quint8>(buf[off]);
            const auto hi = static_cast<quint8>(buf[off + 1]);
            return static_cast<qint16>(lo | (hi << 8));
        };
        const quint8 v1 =
            readI16(PlcClient::kVision1IntIndex) == PlcClient::kVisionReadyValue ? 1 : 0;
        const quint8 v2 =
            readI16(PlcClient::kVision2IntIndex) == PlcClient::kVisionReadyValue ? 1 : 0;
        m_owner->m_vision1.store(v1, std::memory_order_relaxed);
        m_owner->m_vision2.store(v2, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(m_owner->m_dataMutex);
            m_owner->m_status = buf.left(PlcClient::kStatusSize);
            m_owner->m_valves = std::move(valves);
            m_owner->m_sensors = std::move(sensors);
        }
    }

    PlcClient *m_owner = nullptr;
    QString m_host;
    quint16 m_port = 0;
    QByteArray m_readyValue;
    QByteArray m_readyMask;
    int m_statusBytes = PlcClient::kStatusSize;
};

PlcClient::PlcClient(QObject *parent)
    : QObject(parent)
    , m_status(kStatusSize, 0)
    , m_valves(kValveCount, 0)
    , m_sensors(kSensorCount, 0)
{
}

QByteArray PlcClient::controlCommand(int code)
{
    const char bytes[] = {
        7,
        static_cast<char>(code),
        0,
        0,
    };
    return QByteArray(bytes, sizeof(bytes));
}

QByteArray PlcClient::startCommand(bool ignoreLoadCell)
{
    const char bytes[] = {
        7,
        1,
        1,
        static_cast<char>(ignoreLoadCell ? 0 : 1),
    };
    return QByteArray(bytes, sizeof(bytes));
}

QByteArray PlcClient::programSelectCommand(int program)
{
    const char bytes[] = {
        7,
        1,
        static_cast<char>(program),
        0,
    };
    return QByteArray(bytes, sizeof(bytes));
}

QByteArray PlcClient::serviceModeCommand(bool on)
{
    const char bytes[] = {99, static_cast<char>(on ? 1 : 0)};
    return QByteArray(bytes, sizeof(bytes));
}

QByteArray PlcClient::doCommand(int n, bool on)
{
    const char bytes[] = {
        2,
        static_cast<char>(n),
        static_cast<char>(on ? 1 : 0),
    };
    return QByteArray(bytes, sizeof(bytes));
}

void PlcClient::excludeLiveSignals(QByteArray *mask)
{
    if (!mask || mask->size() < kStatusSize)
        return;
    const auto clear = [mask](int index) {
        if (index >= 0 && index < mask->size())
            (*mask)[index] = 0;
    };
    clear(kVision1Offset);
    clear(kVision1Offset + 1);
    clear(kVision2Offset);
    clear(kVision2Offset + 1);
    clear(kLamellaeOffset);
    clear(kGeeseOffset);
    for (int i = 0; i < kWeldSensorCount; ++i)
        clear(kWeldOffset + i);
    // Кнопки корпуса и подозрения (int16 50–59) меняются в работе — не входят в маску.
    for (int i = kPanelIntIndex; i <= kSuspicionLastIntIndex; ++i) {
        clear(i * kStatusIntBytes);
        clear(i * kStatusIntBytes + 1);
    }
}

PlcClient::~PlcClient()
{
    stop();
}

void PlcClient::start(const QString &host, quint16 port,
                      const QByteArray &readyValue, const QByteArray &readyMask,
                      int statusBytes)
{
    stop();
    QByteArray value = readyValue;
    QByteArray mask = readyMask;
    if (value.size() != kStatusSize)
        value = QByteArray(kStatusSize, 0);
    if (mask.size() != kStatusSize)
        mask = QByteArray(kStatusSize, char(0xFF));
    // Камеры и сигналы предподготовки проверяются отдельно, не маской «ПЛК готов».
    excludeLiveSignals(&mask);

    m_lastOkMs.store(0, std::memory_order_relaxed);
    m_maskOk.store(false, std::memory_order_relaxed);
    m_mismatchIndex.store(-1, std::memory_order_relaxed);
    m_vision1.store(0, std::memory_order_relaxed);
    m_vision2.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        m_status.fill(0);
    }
    m_statusBytes.store(qBound(kMinStatusSize, statusBytes, kStatusSize), std::memory_order_relaxed);
    m_worker = new Worker(this, host, port, std::move(value), std::move(mask), statusBytes);
    m_worker->start();
}

void PlcClient::sendCommand(const QByteArray &frame)
{
    if (!m_worker) {
        emit commandFinished(false, QStringLiteral("ПЛК не подключён"));
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_cmdMutex);
        m_cmdQueue.push_back(frame);
    }
    m_cmdCv.notify_one();
}

void PlcClient::stop()
{
    if (!m_worker)
        return;
    {
        std::lock_guard<std::mutex> lock(m_cmdMutex);
        m_cmdQueue.clear();
    }
    m_worker->requestInterruption();
    m_cmdCv.notify_all();
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

bool PlcClient::matchesMask() const
{
    return isOk();
}

bool PlcClient::vision1Ok() const
{
    return isOk() && m_vision1.load(std::memory_order_relaxed) == 1;
}

bool PlcClient::vision2Ok() const
{
    return isOk() && m_vision2.load(std::memory_order_relaxed) == 1;
}

bool PlcClient::bytesAreOne(int offset, int count) const
{
    if (!isOk() || offset < 0 || count <= 0)
        return false;
    std::lock_guard<std::mutex> lock(m_dataMutex);
    if (offset + count > m_status.size())
        return false;
    for (int i = 0; i < count; ++i) {
        if (static_cast<quint8>(m_status.at(offset + i)) != 1)
            return false;
    }
    return true;
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

quint8 PlcClient::statusByte(int index) const
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    if (index < 0 || index >= m_status.size())
        return 0;
    return static_cast<quint8>(m_status.at(index));
}

qint16 PlcClient::statusInt16(int index) const
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    const int byte = index * kStatusIntBytes;
    if (index < 0 || byte + 1 >= m_status.size())
        return 0;
    const quint8 lo = static_cast<quint8>(m_status.at(byte));
    const quint8 hi = static_cast<quint8>(m_status.at(byte + 1));
    return static_cast<qint16>(lo | (hi << 8));
}

bool PlcClient::weldingReady() const
{
    if (!isOk())
        return false;
    for (int index : kWeldingReadyInts) {
        if (statusInt16(index) != kWeldingReadyValue)
            return false;
    }
    return true;
}
