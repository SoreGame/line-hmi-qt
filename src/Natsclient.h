#ifndef NATSCLIENT_H
#define NATSCLIENT_H

#include <QObject>
#include <QString>

#include <nats.h>

// Тонкая обёртка над nats.c: соединение, чтение сохранённых скриптов
// из NATS KV bucket "behaviour" (тот же bucket, что использует
// веб-редактор ChaiScript) и публикация команды запуска на
// "script.command" в формате, совместимом с редактором.
//
// Методы этого класса блокирующие (используют синхронный API nats.c) —
// вызывайте их из фонового потока (см. MainWindow::startSelectedProgramScript,
// который использует QtConcurrent::run), а не напрямую из UI-слотов.
class NatsClient : public QObject
{
    Q_OBJECT
public:
    explicit NatsClient(QObject *parent = nullptr);
    ~NatsClient() override;

    // url, например: "nats://192.168.11.112:4222"
    bool connectToServer(const QString &url, QString *errorOut = nullptr);
    void disconnectFromServer();
    bool isConnected() const;

    // Открывает KV bucket (JetStream KV), созданный редактором ("behaviour").
    bool openKvBucket(const QString &bucketName, QString *errorOut = nullptr);

    // Читает код сохранённого скрипта по имени файла.
    // В KV редактор хранит запись под ключом "data.<filename>" в виде
    // JSON {id, name, code, timestamp} — здесь достаём поле "code".
    bool fetchScriptCode(const QString &filename, QString *codeOut, QString *errorOut = nullptr) const;

    // Публикует команду запуска скрипта на subject "script.command",
    // в том же формате, что и веб-редактор (executeScript()).
    bool publishExecCommand(const QString &filename, const QString &code, int fromLine,
                            QString *errorOut = nullptr) const;

private:
    natsConnection *m_conn = nullptr;
    kvStore *m_kv = nullptr;
};

#endif // NATSCLIENT_H