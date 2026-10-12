#ifndef RUNHISTORY_H
#define RUNHISTORY_H

#include <QDateTime>
#include <QString>
#include <QVector>

// Запуск линии: от Старта до Стопа или аварии.
struct RunRecord {
    int program = 0;
    QDateTime started;
    QDateTime finished;
    int good = 0;
    int scrap = 0;
    int cycles = 0;
    qint64 totalCycleMs = 0;
    qint64 lastCycleMs = -1;
    QString reason;

    qint64 averageCycleMs() const { return cycles > 0 ? totalCycleMs / cycles : -1; }
};

// История запусков в JSON-файле пульта, новые в конце.
namespace RunHistory {
QVector<RunRecord> load();
void append(const RunRecord &record);
QString filePath();
}

#endif // RUNHISTORY_H
