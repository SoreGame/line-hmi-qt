#ifndef RUNTIMESTATE_H
#define RUNTIMESTATE_H

#include <QString>

// Локальное состояние пульта между перезапусками (не путать с config.json).
struct RuntimeState {
    // true — незавершённый аварийный стоп; при старте снова шлём e-stop на ПЛК
    // и открываем recovery.
    bool recoveryPending = false;
    // true — прошлую сессию можно было обесточить (программа не шла).
    // Нет файла / первое включение: считаем штатным (true).
    bool cleanShutdown = true;

    static RuntimeState load();
    void save() const;

    static QString stateFilePath();
    static void setRecoveryPending(bool pending);
    static void setCleanShutdown(bool clean);
};

#endif // RUNTIMESTATE_H
