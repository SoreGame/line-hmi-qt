#ifndef CONNECTIONSETTINGS_H
#define CONNECTIONSETTINGS_H

#include <QString>
#include <QtGlobal>

#include <array>

// Прямоугольная область по x, y, z, r, из которой init-скрипт сам выводит робота в старт.
struct StartZone {
    bool enabled = false;
    double xMin = 0, xMax = 0;
    double yMin = 0, yMax = 0;
    double zMin = 0, zMax = 0;
    double rMin = 0, rMax = 0;

    bool contains(double x, double y, double z, double r) const;
};

struct ConnectionSettings {
    static constexpr int kZoneCount = 4;

    QString natsHost = QStringLiteral("192.168.11.170");
    quint16 natsPort = 4222;
    QString plcHost = QStringLiteral("192.168.58.88");
    quint16 plcPort = 2025;
    // Размер кадра от ПЛК в байтах, чётный: int16 на ячейку.
    int plcStatusBytes = 160;
    bool autostart = false;
    bool hmiAutostart = false;
    // Курсор мыши. По умолчанию включён; в админке можно выключить для киоска.
    bool showCursor = true;
    // Уходит на ПЛК в 7 1 N X: X = 0, если тензодатчик игнорируется.
    bool ignoreLoadCell = false;
    // COM-порт Arduino (пусто — не открывать). Порог уходит командой T <int>.
    QString arduinoPort = QStringLiteral("/dev/ttyUSB0");
    qint32 arduinoThreshold = 1100000;
    // кг = сырое / arduinoUnitsPerKg (после тарирования датчика).
    double arduinoUnitsPerKg = 100000.0;
    // Имена .chai в KV (как сохранены редактором). Пустое → значение по умолчанию.
    QString initScriptDetail1 = QStringLiteral("preProg1.chai");
    QString initScriptDetail2 = QStringLiteral("preProg2.chai");
    QString mainScriptDetail1 = QStringLiteral("ai.chai");
    QString mainScriptDetail2 = QStringLiteral("ai2.chai");
    // Стоп в цикле: сколько ждать конца цикла, потом полный стоп роботу. 0 — ждать без ограничения.
    int stopTimeoutSec = 30;
    std::array<StartZone, kZoneCount> zones{};
    // Картинка для окна ручной установки робота. Пусто — окно без картинки.
    QString placementImage;

    QString natsUrl() const;
    QString plcEndpoint() const;
    QString initScript(int program) const;
    QString mainScript(int program) const;
    // 0 — ни в одной включённой зоне, иначе 1..kZoneCount.
    int zoneOf(double x, double y, double z, double r) const;

    static ConnectionSettings load();
    void save() const;
    static bool isConfigured();
    static QString configFilePath();

    /// Вкл/выкл системный курсор (BlankCursor при выключенном).
    static void applyShowCursor(bool show);
};

#endif // CONNECTIONSETTINGS_H
