#ifndef PLCCLIENT_H
#define PLCCLIENT_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <mutex>

// Raw TCP client for the line PLC: fixed 1024-byte request/response frames.
// Polls with HOLD (0xFF) on valve bytes so status reads do not change outputs.
class PlcClient : public QObject
{
    Q_OBJECT
public:
    static constexpr int kFrameSize = 1024;
    static constexpr int kValveCount = 12;
    static constexpr int kSensorCount = 12;
    static constexpr int kValveOffset = 0;
    static constexpr int kSensorOffset = 12;
    static constexpr quint8 kHoldByte = 0xFF;

    // How long without a successful exchange before PLC is considered down.
    static constexpr qint64 kTimeoutMs = 2000;

    explicit PlcClient(QObject *parent = nullptr);
    ~PlcClient() override;

    void start(const QString &host, quint16 port);
    void stop();

    bool isOk() const;
    qint64 lastOkMs() const { return m_lastOkMs.load(std::memory_order_relaxed); }

    // Snapshot of valve bytes K1..K12 (0/1). Empty / zeros if never received.
    QVector<quint8> valves() const;
    QVector<quint8> sensors() const;

signals:
    void stateChanged();

private:
    class Worker;

    Worker *m_worker = nullptr;

    mutable std::mutex m_dataMutex;
    QVector<quint8> m_valves;
    QVector<quint8> m_sensors;
    std::atomic<qint64> m_lastOkMs{0};
};

#endif // PLCCLIENT_H
