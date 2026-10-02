#include "ArduinoLink.h"

#include <QElapsedTimer>
#include <QTimer>

#if defined(LINE_HMI_HAS_SERIALPORT)
#include <QSerialPort>
#endif

ArduinoLink::ArduinoLink(QObject *parent)
    : QObject(parent)
{
#if defined(LINE_HMI_HAS_SERIALPORT)
    m_port = new QSerialPort(this);
    connect(m_port, &QSerialPort::readyRead, this, &ArduinoLink::onReadyRead);
    connect(m_port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError)
            return;
        onErrorOccurred();
    });
#endif

    m_watchdog = new QTimer(this);
    m_watchdog->setInterval(250);
    connect(m_watchdog, &QTimer::timeout, this, &ArduinoLink::onWatchdog);
    m_activity.invalidate();
}

ArduinoLink::~ArduinoLink()
{
    close();
}

qint32 ArduinoLink::clampValue(qint32 v)
{
    return qBound(kMinValue, v, kMaxValue);
}

bool ArduinoLink::open(const QString &portName)
{
    close();

    const QString name = portName.trimmed();
    if (name.isEmpty()) {
        setLinked(false, QStringLiteral("Тензодатчик не подключен"));
        return false;
    }

#if !defined(LINE_HMI_HAS_SERIALPORT)
    Q_UNUSED(name);
    setLinked(false, QStringLiteral("Тензодатчик не подключен"));
    return false;
#else
    m_port->setPortName(name);
    m_port->setBaudRate(kBaudRate);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port->open(QIODevice::ReadWrite)) {
        setLinked(false, QStringLiteral("Тензодатчик не подключен"));
        return false;
    }

    m_rx.clear();
    m_activity.invalidate();
    m_watchdog->start();
    setLinked(false, QStringLiteral("Ожидание данных…"));

    if (m_hasThreshold)
        sendThreshold(m_lastThreshold);

    return true;
#endif
}

void ArduinoLink::close()
{
    if (m_watchdog)
        m_watchdog->stop();

#if defined(LINE_HMI_HAS_SERIALPORT)
    if (m_port && m_port->isOpen())
        m_port->close();
#endif

    m_rx.clear();
    m_activity.invalidate();
    setLinked(false, QStringLiteral("Тензодатчик не подключен"));
}

bool ArduinoLink::isOpen() const
{
#if defined(LINE_HMI_HAS_SERIALPORT)
    return m_port && m_port->isOpen();
#else
    return false;
#endif
}

bool ArduinoLink::isLinked() const
{
    return m_linked;
}

QString ArduinoLink::portName() const
{
#if defined(LINE_HMI_HAS_SERIALPORT)
    return m_port ? m_port->portName() : QString();
#else
    return {};
#endif
}

QString ArduinoLink::statusText() const
{
    return m_status;
}

void ArduinoLink::sendThreshold(qint32 threshold)
{
    m_lastThreshold = clampValue(threshold);
    m_hasThreshold = true;

#if defined(LINE_HMI_HAS_SERIALPORT)
    if (!m_port || !m_port->isOpen())
        return;
    const QByteArray line = QByteArray("T ") + QByteArray::number(m_lastThreshold) + '\n';
    m_port->write(line);
    m_port->flush();
#else
    Q_UNUSED(threshold);
#endif
}

void ArduinoLink::onReadyRead()
{
#if defined(LINE_HMI_HAS_SERIALPORT)
    if (!m_port)
        return;
    feedBytes(m_port->readAll());
#endif
}

void ArduinoLink::feedBytes(const QByteArray &chunk)
{
    if (chunk.isEmpty())
        return;

    m_rx.append(chunk);
    while (true) {
        const int nl = m_rx.indexOf('\n');
        if (nl < 0)
            break;
        QByteArray line = m_rx.left(nl);
        m_rx.remove(0, nl + 1);
        if (!line.isEmpty() && line.endsWith('\r'))
            line.chop(1);
        processLine(line);
    }

    if (m_rx.size() > 256)
        m_rx.clear();
}

void ArduinoLink::processLine(const QByteArray &line)
{
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty())
        return;

    if (trimmed.startsWith("V ") || trimmed.startsWith("v ")) {
        bool ok = false;
        const qint64 raw = trimmed.mid(2).trimmed().toLongLong(&ok);
        if (!ok)
            return;
        const qint32 value = clampValue(static_cast<qint32>(
            qBound(static_cast<qint64>(kMinValue), raw, static_cast<qint64>(kMaxValue))));
        m_activity.restart();
        setLinked(true, QStringLiteral("Подключено"));
        emit valueChanged(value);
        return;
    }

    if (trimmed.startsWith("OK ")) {
        m_activity.restart();
        setLinked(true, QStringLiteral("Подключено"));
    }
}

void ArduinoLink::onWatchdog()
{
#if defined(LINE_HMI_HAS_SERIALPORT)
    if (!m_port || !m_port->isOpen()) {
        setLinked(false, QStringLiteral("Тензодатчик не подключен"));
        return;
    }

    if (!m_activity.isValid())
        return;

    if (m_activity.elapsed() > kStaleMs)
        setLinked(false, QStringLiteral("Тензодатчик не подключен"));
#else
    setLinked(false, QStringLiteral("Тензодатчик не подключен"));
#endif
}

void ArduinoLink::onErrorOccurred()
{
#if defined(LINE_HMI_HAS_SERIALPORT)
    if (!m_port)
        return;
    if (m_port->isOpen())
        m_port->close();
#endif
    m_watchdog->stop();
    m_rx.clear();
    m_activity.invalidate();
    setLinked(false, QStringLiteral("Тензодатчик не подключен"));
}

void ArduinoLink::setLinked(bool linked, const QString &message)
{
    const bool changed = (m_linked != linked) || (m_status != message);
    m_linked = linked;
    m_status = message;
    if (changed)
        emit linkChanged(m_linked, m_status);
}
