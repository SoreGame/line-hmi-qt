#include "PlcClient.h"
#include "Texts.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QThread>

#include <chrono>

namespace {

constexpr int kPingIntervalMs = 1000;
constexpr int kPingTimeoutMs = 800;
constexpr int kConnectTimeoutMs = 3000;

bool readStatusFrame(QTcpSocket &sock, QByteArray *out, int size, int timeoutMs)
{
    out->clear();
    QElapsedTimer timer;
    timer.start();
    while (out->size() < size) {
        const int elapsed = static_cast<int>(timer.elapsed());
        if (elapsed > timeoutMs || sock.state() != QAbstractSocket::ConnectedState)
            return false;
        const int remain = timeoutMs - elapsed;
        if (sock.bytesAvailable() > 0 || sock.waitForReadyRead(qMin(50, qMax(1, remain))))
            out->append(sock.read(size - out->size()));
    }
    return true;
}

QByteArray bytes4(quint8 a, quint8 b, quint8 c, quint8 d)
{
    const char raw[] = {char(a), char(b), char(c), char(d)};
    return QByteArray(raw, sizeof(raw));
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
        QElapsedTimer pingTimer;
        pingTimer.start();

        while (!isInterruptionRequested()) {
            if (sock.state() != QAbstractSocket::ConnectedState) {
                sock.abort();
                failQueued(tx("Команда не отправлена: нет связи с ПЛК"));
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
                const auto ready = [this, &pingTimer] {
                    return isInterruptionRequested() || !m_owner->m_cmdQueue.empty() || pollPending()
                        || pingTimer.elapsed() >= kPingIntervalMs;
                };
                if (!ready())
                    m_owner->m_cmdCv.wait_for(lock, std::chrono::milliseconds(50), ready);
                if (!m_owner->m_cmdQueue.empty()) {
                    command = std::move(m_owner->m_cmdQueue.front());
                    m_owner->m_cmdQueue.pop_front();
                }
            }
            if (isInterruptionRequested())
                break;

            if (!command.isEmpty()) {
                if (!writeFrame(sock, command)) {
                    emit m_owner->commandFinished(false, tx("Не удалось отправить кадр на ПЛК"));
                    sock.abort();
                    continue;
                }
                emit m_owner->commandFinished(true, QString());
                continue;
            }

            if (!pollPending() && pingTimer.elapsed() < kPingIntervalMs)
                continue;
            pingTimer.restart();

            if (!pingPong(sock))
                sock.abort();
        }

        sock.abort();
    }

private:
    bool pollPending() const
    {
        return m_owner->m_pollRequested.load(std::memory_order_acquire)
            > m_owner->m_pollServed.load(std::memory_order_acquire);
    }

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
        const quint64 ticket = m_owner->m_pollRequested.load(std::memory_order_acquire);
        // ПЛК говорит только в ответ на опрос: всё, что лежит в сокете сейчас, — хвост
        // прошлого кадра, если ПЛК шлёт больше, чем задано в настройках.
        const qint64 extra = sock.bytesAvailable();
        if (extra > 0)
            sock.read(extra);
        m_owner->m_extraBytes.store(int(extra), std::memory_order_relaxed);
        if (!writeFrame(sock, m_owner->pollFrame()))
            return false;

        QByteArray reply;
        if (!readStatusFrame(sock, &reply, m_owner->statusSize(), kPingTimeoutMs))
            return false;

        {
            std::lock_guard<std::mutex> lock(m_owner->m_dataMutex);
            m_owner->m_status = reply;
        }
        m_owner->m_lastOkMs.store(QDateTime::currentMSecsSinceEpoch(), std::memory_order_relaxed);
        if (ticket > m_owner->m_pollServed.load(std::memory_order_acquire))
            m_owner->m_pollServed.store(ticket, std::memory_order_release);
        emit m_owner->stateChanged();
        return true;
    }

    PlcClient *m_owner = nullptr;
    QString m_host;
    quint16 m_port = 0;
};

PlcClient::PlcClient(QObject *parent)
    : QObject(parent)
    , m_status(kDefaultStatusSize, 0)
{
}

int PlcClient::normalizeStatusSize(int bytes)
{
    return qBound(kMinStatusSize, bytes, kMaxStatusSize) / kStatusIntBytes * kStatusIntBytes;
}

PlcClient::~PlcClient()
{
    stop();
}

QByteArray PlcClient::controlCommand(Command code)
{
    return bytes4(7, code, 0, 0);
}

QByteArray PlcClient::selectCommand(int program, bool useLoadCell)
{
    return bytes4(7, 1, quint8(program), useLoadCell ? 1 : 0);
}

QByteArray PlcClient::scriptReportCommand(ScriptRole role, bool completed)
{
    return bytes4(7, 7, role, completed ? 1 : 0);
}

QByteArray PlcClient::serviceModeCommand(bool on)
{
    const char bytes[] = {99, static_cast<char>(on ? 1 : 0)};
    return QByteArray(bytes, sizeof(bytes));
}

QByteArray PlcClient::doCommand(int n, bool on)
{
    const char bytes[] = {2, static_cast<char>(n), static_cast<char>(on ? 1 : 0)};
    return QByteArray(bytes, sizeof(bytes));
}

QString PlcClient::modeText(Mode mode)
{
    switch (mode) {
    case Mode::Boot: return tx("ПЛК загружается");
    case Mode::Ready: return tx("Готов");
    case Mode::Stopping: return tx("Остановка");
    case Mode::Run: return tx("В работе");
    case Mode::Estop: return tx("Авария");
    case Mode::Service: return tx("Сервис");
    }
    return tx("Режим %1").arg(int(mode));
}

void PlcClient::start(const QString &host, quint16 port, int statusBytes)
{
    stop();
    m_lastOkMs.store(0, std::memory_order_relaxed);
    const int size = normalizeStatusSize(statusBytes);
    m_statusSize.store(size, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        m_status = QByteArray(size, 0);
    }
    m_worker = new Worker(this, host, port);
    m_worker->start();
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

void PlcClient::sendCommand(const QByteArray &frame)
{
    if (!m_worker) {
        emit commandFinished(false, tx("ПЛК не подключён"));
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_cmdMutex);
        m_cmdQueue.push_back(frame);
    }
    m_cmdCv.notify_one();
}

quint64 PlcClient::pollNow()
{
    quint64 ticket;
    {
        std::lock_guard<std::mutex> lock(m_cmdMutex);
        ticket = m_pollRequested.fetch_add(1, std::memory_order_acq_rel) + 1;
    }
    m_cmdCv.notify_one();
    return ticket;
}

void PlcClient::setRobotReport(const RobotReport &report)
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    m_report = report;
}

QByteArray PlcClient::pollFrame() const
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    const char raw[] = {
        char(203),
        char(m_report.natsConnected ? 1 : 0),
        char(m_report.ctrlAlive ? 1 : 0),
        char(m_report.behaviourAlive ? 1 : 0),
        char(m_report.scriptRunning ? 1 : 0),
        char(qBound(0, m_report.zone, 255)),
    };
    return QByteArray(raw, sizeof(raw));
}

bool PlcClient::isOk() const
{
    const qint64 last = m_lastOkMs.load(std::memory_order_relaxed);
    return last > 0 && (QDateTime::currentMSecsSinceEpoch() - last) < kTimeoutMs;
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

bool PlcClient::weldValvesSafe() const
{
    if (!isOk())
        return false;
    for (int index : kWeldValveInts) {
        if (statusInt16(index) != 0)
            return false;
    }
    return true;
}

bool PlcClient::holdersPresent(int program) const
{
    if (!isOk())
        return false;
    switch (program) {
    case 1: return statusInt16(kBigHoldersSensorIndex) == 1;
    case 2: return statusInt16(kSmallHoldersSensorIndex) == 1;
    default: return false;
    }
}
