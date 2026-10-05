#include "ConnectionSettings.h"
#include "ArduinoLink.h"
#include "PlcClient.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <utility>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {

QString bundledConfigPath()
{
    return QStringLiteral("assets:/config.json");
}

QString writableConfigPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(dir).filePath(QStringLiteral("config.json"));
}

QStringList configSearchPaths()
{
    QStringList paths;
#ifdef Q_OS_ANDROID
    paths << bundledConfigPath();
#else
    // Сначала локальные настройки — cmake copy config.json их не затирает.
    paths << writableConfigPath();
    paths << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config.json"));
    paths << QDir::current().filePath(QStringLiteral("config.json"));
#endif
    paths.removeDuplicates();
    return paths;
}

constexpr int kPlcStatusSize = PlcClient::kStatusSize;
constexpr int kLegacyStatusSize = 100; // прежний кадр 50 int16

QByteArray defaultPlcMask()
{
    // Первые 100 байт — прежняя проверка. Байты int16 49–59 (кнопки корпуса/подозрения)
    // в маску не входят: excludeLiveSignals обнуляет их.
    QByteArray mask(kPlcStatusSize, 0);
    const int checked = qMin(kLegacyStatusSize, kPlcStatusSize);
    for (int i = 0; i < checked; ++i)
        mask[i] = char(0xFF);
    PlcClient::excludeLiveSignals(&mask);
    return mask;
}

QByteArray defaultPlcValue()
{
    return QByteArray(kPlcStatusSize, 0);
}

QByteArray decodeHex100(const QString &hex, bool *ok)
{
    QString s = hex;
    s.remove(QLatin1Char(' '));
    s.remove(QLatin1Char('\n'));
    s.remove(QLatin1Char('\r'));
    const int nbytes = s.size() / 2;
    const bool full = nbytes == kPlcStatusSize;
    const bool legacy = nbytes == kLegacyStatusSize;
    if (s.size() % 2 != 0 || !(full || legacy)) {
        *ok = false;
        return {};
    }
    QByteArray out(kPlcStatusSize, 0);
    for (int i = 0; i < nbytes; ++i) {
        bool byteOk = false;
        const int v = s.mid(i * 2, 2).toInt(&byteOk, 16);
        if (!byteOk || v < 0 || v > 255) {
            *ok = false;
            return {};
        }
        out[i] = static_cast<char>(v);
    }
    *ok = true;
    return out;
}

QString encodeHex100(const QByteArray &bytes)
{
    QString out;
    out.reserve(bytes.size() * 2);
    for (unsigned char b : bytes)
        out += QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0'));
    return out;
}

void takePlcPattern(const QJsonObject &plc, const char *key, const QByteArray &fallback,
                    QByteArray *dst, QString *warning)
{
    if (!plc.contains(QLatin1String(key))) {
        *dst = fallback;
        return;
    }
    bool ok = false;
    const QByteArray decoded = decodeHex100(plc.value(QLatin1String(key)).toString(), &ok);
    if (!ok) {
        *dst = fallback;
        if (warning->isEmpty()) {
            *warning = QStringLiteral(
                "plc.%1 в config.json должен быть hex из 100 или 120 байт "
                "(50 или 60 int16) — взята маска по умолчанию")
                           .arg(QLatin1String(key));
        }
        return;
    }
    *dst = decoded;
}

ConnectionSettings normalize(ConnectionSettings c)
{
    if (c.natsHost.isEmpty())
        c.natsHost = QStringLiteral("192.168.11.170");
    if (c.natsPort == 0)
        c.natsPort = 4222;
    if (c.plcHost.isEmpty())
        c.plcHost = QStringLiteral("192.168.58.88");
    if (c.plcPort == 0)
        c.plcPort = 2025;
    if (c.plcReadyMask.size() != kPlcStatusSize)
        c.plcReadyMask = defaultPlcMask();
    if (c.plcReadyValue.size() != kPlcStatusSize)
        c.plcReadyValue = defaultPlcValue();
    if (c.plcStatusBytes < kLegacyStatusSize)
        c.plcStatusBytes = kLegacyStatusSize;
    if (c.plcStatusBytes > kPlcStatusSize)
        c.plcStatusBytes = kPlcStatusSize;
    if (c.plcStatusBytes % 2 != 0)
        c.plcStatusBytes += 1;
    if (c.plcStatusBytes > kPlcStatusSize)
        c.plcStatusBytes = kPlcStatusSize;

    const ConnectionSettings defaults;
    for (auto [field, fallback] : {
             std::pair{&c.initScriptDetail1, &defaults.initScriptDetail1},
             std::pair{&c.initScriptDetail2, &defaults.initScriptDetail2},
             std::pair{&c.mainScriptDetail1, &defaults.mainScriptDetail1},
             std::pair{&c.mainScriptDetail2, &defaults.mainScriptDetail2},
             std::pair{&c.estopScript, &defaults.estopScript},
         }) {
        *field = field->trimmed();
        if (field->isEmpty())
            *field = *fallback;
    }
    c.startCountdownSec = qBound(0, c.startCountdownSec, 60);
    return c;
}

bool readConfigFile(const QString &path, ConnectionSettings *out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject root = doc.object();

    const QJsonObject server = root.value(QStringLiteral("server")).toObject();
    if (!server.isEmpty()) {
        out->natsHost = server.value(QStringLiteral("ip")).toString(out->natsHost).trimmed();
        out->natsPort = static_cast<quint16>(server.value(QStringLiteral("port")).toInt(out->natsPort));
    }

    const QJsonObject plc = root.value(QStringLiteral("plc")).toObject();
    if (!plc.isEmpty()) {
        out->plcHost = plc.value(QStringLiteral("ip")).toString(out->plcHost).trimmed();
        out->plcPort = static_cast<quint16>(plc.value(QStringLiteral("port")).toInt(out->plcPort));
        takePlcPattern(plc, "ready_mask", defaultPlcMask(), &out->plcReadyMask, &out->plcPatternWarning);
        takePlcPattern(plc, "ready_value", defaultPlcValue(), &out->plcReadyValue, &out->plcPatternWarning);
        if (plc.contains(QStringLiteral("status_bytes")))
            out->plcStatusBytes = plc.value(QStringLiteral("status_bytes")).toInt(out->plcStatusBytes);
    }

    const QJsonObject system = root.value(QStringLiteral("system")).toObject();
    if (system.contains(QStringLiteral("autostart")))
        out->autostart = system.value(QStringLiteral("autostart")).toBool(false);
    if (system.contains(QStringLiteral("hmiAutostart")))
        out->hmiAutostart = system.value(QStringLiteral("hmiAutostart")).toBool(false);
    if (system.contains(QStringLiteral("showCursor")))
        out->showCursor = system.value(QStringLiteral("showCursor")).toBool(true);
    if (system.contains(QStringLiteral("ignoreLoadCell")))
        out->ignoreLoadCell = system.value(QStringLiteral("ignoreLoadCell")).toBool(false);
    if (system.contains(QStringLiteral("ignoreLaunchLocks")))
        out->ignoreLaunchLocks = system.value(QStringLiteral("ignoreLaunchLocks")).toBool(false);

    const QJsonObject arduino = root.value(QStringLiteral("arduino")).toObject();
    if (!arduino.isEmpty()) {
        if (arduino.contains(QStringLiteral("port")))
            out->arduinoPort = arduino.value(QStringLiteral("port")).toString(out->arduinoPort).trimmed();
        if (arduino.contains(QStringLiteral("threshold"))) {
            const int raw = arduino.value(QStringLiteral("threshold")).toInt(out->arduinoThreshold);
            out->arduinoThreshold = ArduinoLink::clampValue(raw);
        }
        if (arduino.contains(QStringLiteral("unitsPerKg"))) {
            const double scale = arduino.value(QStringLiteral("unitsPerKg")).toDouble(out->arduinoUnitsPerKg);
            if (scale > 0.0)
                out->arduinoUnitsPerKg = scale;
        }
    }

    const QJsonObject programs = root.value(QStringLiteral("programs")).toObject();
    if (!programs.isEmpty()) {
        out->initScriptDetail1 = programs.value(QStringLiteral("initDetail1")).toString(out->initScriptDetail1);
        out->initScriptDetail2 = programs.value(QStringLiteral("initDetail2")).toString(out->initScriptDetail2);
        out->mainScriptDetail1 = programs.value(QStringLiteral("mainDetail1")).toString(out->mainScriptDetail1);
        out->mainScriptDetail2 = programs.value(QStringLiteral("mainDetail2")).toString(out->mainScriptDetail2);
        out->estopScript = programs.value(QStringLiteral("estop")).toString(out->estopScript);
        out->startCountdownSec = programs.value(QStringLiteral("countdownSec")).toInt(out->startCountdownSec);
    }

    return true;
}

bool writeConfigFile(const QString &path, const ConnectionSettings &cfg)
{
    QJsonObject root;
    {
        QFile existing(path);
        if (existing.open(QIODevice::ReadOnly)) {
            const QJsonDocument prev = QJsonDocument::fromJson(existing.readAll());
            if (prev.isObject())
                root = prev.object();
        }
    }

    QJsonObject server = root.value(QStringLiteral("server")).toObject();
    server.insert(QStringLiteral("ip"), cfg.natsHost);
    server.insert(QStringLiteral("port"), cfg.natsPort);

    QJsonObject plc = root.value(QStringLiteral("plc")).toObject();
    plc.insert(QStringLiteral("ip"), cfg.plcHost);
    plc.insert(QStringLiteral("port"), cfg.plcPort);
    if (cfg.plcReadyMask.size() == kPlcStatusSize)
        plc.insert(QStringLiteral("ready_mask"), encodeHex100(cfg.plcReadyMask));
    if (cfg.plcReadyValue.size() == kPlcStatusSize)
        plc.insert(QStringLiteral("ready_value"), encodeHex100(cfg.plcReadyValue));
    plc.insert(QStringLiteral("status_bytes"), cfg.plcStatusBytes);

    root.insert(QStringLiteral("server"), server);
    root.insert(QStringLiteral("plc"), plc);

    QJsonObject system = root.value(QStringLiteral("system")).toObject();
    system.insert(QStringLiteral("autostart"), cfg.autostart);
    system.insert(QStringLiteral("hmiAutostart"), cfg.hmiAutostart);
    system.insert(QStringLiteral("showCursor"), cfg.showCursor);
    system.insert(QStringLiteral("ignoreLoadCell"), cfg.ignoreLoadCell);
    system.insert(QStringLiteral("ignoreLaunchLocks"), cfg.ignoreLaunchLocks);
    root.insert(QStringLiteral("system"), system);

    QJsonObject arduino = root.value(QStringLiteral("arduino")).toObject();
    arduino.insert(QStringLiteral("port"), cfg.arduinoPort);
    arduino.insert(QStringLiteral("threshold"), cfg.arduinoThreshold);
    arduino.insert(QStringLiteral("unitsPerKg"), cfg.arduinoUnitsPerKg);
    root.insert(QStringLiteral("arduino"), arduino);

    QJsonObject programs = root.value(QStringLiteral("programs")).toObject();
    programs.insert(QStringLiteral("initDetail1"), cfg.initScriptDetail1);
    programs.insert(QStringLiteral("initDetail2"), cfg.initScriptDetail2);
    programs.insert(QStringLiteral("mainDetail1"), cfg.mainScriptDetail1);
    programs.insert(QStringLiteral("mainDetail2"), cfg.mainScriptDetail2);
    programs.insert(QStringLiteral("estop"), cfg.estopScript);
    programs.insert(QStringLiteral("countdownSec"), cfg.startCountdownSec);
    root.insert(QStringLiteral("programs"), programs);

    const QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath()))
        return false;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.flush();
#ifdef Q_OS_UNIX
    const int fd = file.handle();
    if (fd >= 0)
        ::fsync(fd);
#endif
    return true;
}

QString &loadedConfigPath()
{
    static QString path;
    return path;
}

} // namespace

QString ConnectionSettings::natsUrl() const
{
    return QStringLiteral("nats://%1:%2").arg(natsHost).arg(natsPort);
}

QString ConnectionSettings::plcEndpoint() const
{
    return QStringLiteral("%1:%2").arg(plcHost).arg(plcPort);
}

void ConnectionSettings::applyShowCursor(bool show)
{
    while (QApplication::overrideCursor())
        QApplication::restoreOverrideCursor();
    if (!show)
        QApplication::setOverrideCursor(Qt::BlankCursor);
}

QString ConnectionSettings::configFilePath()
{
    if (!loadedConfigPath().isEmpty())
        return loadedConfigPath();
    return writableConfigPath();
}

ConnectionSettings ConnectionSettings::load()
{
    ConnectionSettings cfg;
    loadedConfigPath().clear();

    for (const QString &path : configSearchPaths()) {
        if (!QFile::exists(path))
            continue;
        ConnectionSettings parsed;
        if (!readConfigFile(path, &parsed))
            continue;
        parsed = normalize(parsed);
#ifndef Q_OS_ANDROID
        const QString persist = writableConfigPath();
        if (path != persist) {
            QDir().mkpath(QFileInfo(persist).absolutePath());
            writeConfigFile(persist, parsed);
        }
        loadedConfigPath() = persist;
#else
        loadedConfigPath() = path;
#endif
        return parsed;
    }

    return normalize(cfg);
}

void ConnectionSettings::save() const
{
#ifdef Q_OS_ANDROID
    return;
#else
    const QString path = writableConfigPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (writeConfigFile(path, *this))
        loadedConfigPath() = path;
#endif
}

bool ConnectionSettings::isConfigured()
{
#ifdef Q_OS_ANDROID
    ConnectionSettings cfg;
    if (!readConfigFile(bundledConfigPath(), &cfg))
        return false;
    cfg = normalize(cfg);
    return !cfg.natsHost.isEmpty() && !cfg.plcHost.isEmpty();
#else
    return true;
#endif
}
