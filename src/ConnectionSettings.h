#ifndef CONNECTIONSETTINGS_H
#define CONNECTIONSETTINGS_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

struct ConnectionSettings {
    QString natsHost = QStringLiteral("127.0.0.1");
    quint16 natsPort = 4222;
    QString plcHost = QStringLiteral("127.0.0.1");
    quint16 plcPort = 1502;
    // 120 байт: 60 int16 (старые конфиги на 100 байт тоже принимаются).
    // Маска 0x00 — байт не проверяется.
    QByteArray plcReadyMask;
    QByteArray plcReadyValue;
    QString plcPatternWarning;
    bool autostart = false;
    bool hmiAutostart = false;
    // Курсор мыши. По умолчанию включён; в админке можно выключить для киоска.
    bool showCursor = true;
    // Игнорирование тензодатчика. Вкл → старт [7,1,1,0], выкл → [7,1,1,1].
    bool ignoreLoadCell = false;

    QString natsUrl() const;
    QString plcEndpoint() const;

    static ConnectionSettings load();
    void save() const;
    static bool isConfigured();
    static QString configFilePath();

    /// Вкл/выкл системный курсор (BlankCursor при выключенном).
    static void applyShowCursor(bool show);
};

#endif // CONNECTIONSETTINGS_H
