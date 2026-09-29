#ifndef PLCCLIENT_H
#define PLCCLIENT_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>

// TCP-клиент: ПЛК — сервер, пульт к нему подключается.
// Раз в секунду шлём [203,0,0,0] и ждём 60 int16 (little-endian), это 120 байт.
// ПЛК готов, если этот пакет пришёл. Содержимое для готовности не сравнивается.
class PlcClient : public QObject
{
    Q_OBJECT
public:
    static constexpr int kStatusIntCount = 60;
    static constexpr int kStatusIntBytes = 2; // int16, little-endian
    static constexpr int kStatusSize = kStatusIntCount * kStatusIntBytes;
    static constexpr int kValveCount = 12;
    static constexpr int kSensorCount = 12;
    static constexpr int kValveOffset = 0;
    static constexpr int kSensorOffset = 12;
    // Vision module 1/2: int16 в кадре. 1 = модуль включён.
    static constexpr int kVision1IntIndex = 45;
    static constexpr int kVision2IntIndex = 46;
    static constexpr qint16 kVisionReadyValue = 1;
    // Байтовые смещения тех же int16 (для маски «ПЛК готов»).
    static constexpr int kVision1Offset = kVision1IntIndex * kStatusIntBytes; // 90..91
    static constexpr int kVision2Offset = kVision2IntIndex * kStatusIntBytes; // 92..93
    // Предподготовка. Пока произвольные свободные байты кадра (после датчиков
    // 12..23, до камер 90..93). 1 = готово. В маску «ПЛК готов» не входят:
    // во время набора они как раз меняются с 0 на 1.
    static constexpr int kLamellaeOffset = 24;
    static constexpr int kGeeseOffset = 25;
    static constexpr int kWeldOffset = 26;
    static constexpr int kWeldSensorCount = 20;
    // Стейты станций перед стартом: int16 в кадре.
    static constexpr int kLamelStateOffset = 40;
    static constexpr int kGooseStateOffset = 41;
    static constexpr int kBigGooseStateOffset = 42;
    static constexpr int kWeldingStateOffset = 43;
    static constexpr qint16 kLamelStateReady = 0;
    static constexpr qint16 kGooseStateReady = 0;
    static constexpr qint16 kBigGooseStateReady = 0;
    static constexpr quint8 kWeldingStateReady = 0;
    // Готовность сварочного модуля: эти int16 кадра равны 1. Индекс 15 не входит.
    static constexpr int kWeldingReadyInts[] = {10, 11, 12, 13, 14, 16};
    static constexpr qint16 kWeldingReadyValue = 1;
    // Новые ячейки кадра (индексы 50–59). В маску готовности не входят.
    // 50: грибок, 1 = нажат. 51–59: подозрения, 1 = показать на левой панели.
    static constexpr int kEstopIntIndex = 50;
    static constexpr qint16 kEstopPressedValue = 1;
    static constexpr int kSuspicionFirstIntIndex = 51;
    static constexpr int kSuspicionLastIntIndex = 59;
    static constexpr qint16 kSuspicionActiveValue = 1;

    // Обнуляет в маске байты, которые проверяются отдельно (камеры, предподготовка).
    static void excludeLiveSignals(QByteArray *mask);

    // Кадр, который пульт шлёт на ПЛК по «Старт», до окна предподготовки.
    // Ровно 10 байт. Содержимое пока нулевое — подставим, когда будет известно.
    static constexpr int kPrepCommandSize = 10;
    static QByteArray prepStartCommand();

    // No successful ping-pong for this long ⇒ PLC is down.
    static constexpr qint64 kTimeoutMs = 3000;

    explicit PlcClient(QObject *parent = nullptr);
    ~PlcClient() override;

    // readyValue/readyMask длиной kStatusSize. Пустые — value нули, mask 0xFF.
    void start(const QString &host, quint16 port,
               const QByteArray &readyValue = {},
               const QByteArray &readyMask = {});
    void stop();

    // 60 int16 пришли недавно — это и есть готовность ПЛК.
    bool isOk() const;
    bool matchesMask() const;
    // Кадр свежий и в байте камеры пришла 1.
    bool vision1Ok() const;
    bool vision2Ok() const;
    // Свежий кадр и в диапазоне все байты равны 1.
    bool bytesAreOne(int offset, int count) const;
    int mismatchIndex() const { return m_mismatchIndex.load(std::memory_order_relaxed); }
    quint8 mismatchActual() const { return m_mismatchActual.load(std::memory_order_relaxed); }
    quint8 mismatchExpected() const { return m_mismatchExpected.load(std::memory_order_relaxed); }
    qint64 lastOkMs() const { return m_lastOkMs.load(std::memory_order_relaxed); }

    QVector<quint8> valves() const;
    QVector<quint8> sensors() const;
    quint8 statusByte(int index) const;
    qint16 statusInt16(int index) const;
    bool weldingReady() const;

    // Пишет кадр в текущее соединение с ПЛК (тот же сокет, что и опрос статуса).
    // Ответ не читается. commandFinished приходит в поток GUI.
    void sendCommand(const QByteArray &frame);

signals:
    void stateChanged();
    void commandFinished(bool ok, const QString &error);

private:
    class Worker;

    Worker *m_worker = nullptr;

    mutable std::mutex m_dataMutex;
    std::mutex m_cmdMutex;
    std::condition_variable m_cmdCv;
    std::deque<QByteArray> m_cmdQueue;
    QByteArray m_status;
    QVector<quint8> m_valves;
    QVector<quint8> m_sensors;
    std::atomic<qint64> m_lastOkMs{0};
    std::atomic<bool> m_maskOk{false};
    std::atomic<int> m_mismatchIndex{-1};
    std::atomic<quint8> m_mismatchActual{0};
    std::atomic<quint8> m_mismatchExpected{0};
    std::atomic<quint8> m_vision1{0};
    std::atomic<quint8> m_vision2{0};
};

#endif // PLCCLIENT_H
