#ifndef ARDUINOLINK_H
#define ARDUINOLINK_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QtGlobal>

class QSerialPort;
class QTimer;

// Текстовый протокол по UART (115200 8N1):
//   Arduino → пульт: V <int>
//   Пульт → Arduino: T <int>
//   Arduino → пульт: OK T <int>
class ArduinoLink : public QObject
{
    Q_OBJECT

public:
    static constexpr qint32 kMinValue = -10000;
    static constexpr qint32 kMaxValue = 3000000;
    static constexpr int kBaudRate = 115200;
    static constexpr int kStaleMs = 1500;

    explicit ArduinoLink(QObject *parent = nullptr);
    ~ArduinoLink() override;

    bool open(const QString &portName);
    void close();
    bool isOpen() const;
    bool isLinked() const;

    QString portName() const;
    QString statusText() const;

    static qint32 clampValue(qint32 v);

public slots:
    void sendThreshold(qint32 threshold);

signals:
    void valueChanged(qint32 value);
    void linkChanged(bool linked, const QString &message);

private slots:
    void onReadyRead();
    void onWatchdog();
    void onErrorOccurred();

private:
    void setLinked(bool linked, const QString &message);
    void processLine(const QByteArray &line);
    void feedBytes(const QByteArray &chunk);

    QSerialPort *m_port = nullptr;
    QTimer *m_watchdog = nullptr;
    QByteArray m_rx;
    QElapsedTimer m_activity;
    bool m_linked = false;
    QString m_status = QStringLiteral("Тензодатчик не подключен");
    qint32 m_lastThreshold = 0;
    bool m_hasThreshold = false;
};

#endif // ARDUINOLINK_H
