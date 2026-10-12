#include "RunHistoryDialog.h"
#include "RunHistory.h"
#include "Texts.h"

#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

constexpr int kShown = 50;

QString seconds(qint64 ms)
{
    if (ms < 0)
        return QStringLiteral("—");
    const double sec = ms / 1000.0;
    return tx("%1 с").arg(sec, 0, 'f', sec < 10.0 ? 1 : 0);
}

} // namespace

RunHistoryDialog::RunHistoryDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tx("История запусков"));
    resize(1500, 760);
    setStyleSheet(QStringLiteral(
        "QDialog{background:#FFFFFF; border:2px solid #6B7380;}"
        " QTableWidget{background:#FFFFFF; alternate-background-color:#F5F6F8; color:#1F2126;"
        " gridline-color:#E3E5E9; border:1px solid #A9B0B8;"
        " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:14px;}"
        " QHeaderView::section{background:#F0F2F5; color:#4A4F57; border:none;"
        " border-right:1px solid #DBDEE3; border-bottom:1px solid #DBDEE3;"
        " padding:6px; font-size:13px; font-weight:600;}"
        " QTableCornerButton::section{background:#F0F2F5; border:none;}"
        " QPushButton{background:#FFFFFF; color:#1F2126; border:1px solid #DBDEE3; border-radius:6px;"
        " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:14px; font-weight:600;}"
        " QPushButton:pressed{background:#F0F2F5;}"));

    const QStringList headers = {
        tx("Деталь"), tx("Начало"), tx("Конец"), tx("Годные"), tx("Брак"),
        tx("Циклов"), tx("Среднее время цикла"), tx("Последний цикл"), tx("Причина завершения"),
    };

    auto *table = new QTableWidget(this);
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    table->setAlternatingRowColors(true);

    const QVector<RunRecord> all = RunHistory::load();
    const int count = qMin<int>(kShown, all.size());
    table->setRowCount(count);
    const QString timeFormat = QStringLiteral("dd.MM.yyyy HH:mm:ss");
    for (int row = 0; row < count; ++row) {
        const RunRecord &r = all.at(all.size() - 1 - row);
        const QStringList cells = {
            r.program > 0 ? tx("Деталь %1").arg(r.program) : QStringLiteral("—"),
            r.started.toString(timeFormat),
            r.finished.toString(timeFormat),
            QString::number(r.good),
            QString::number(r.scrap),
            QString::number(r.cycles),
            seconds(r.averageCycleMs()),
            seconds(r.lastCycleMs),
            r.reason,
        };
        for (int col = 0; col < cells.size(); ++col)
            table->setItem(row, col, new QTableWidgetItem(cells.at(col)));
    }
    table->resizeColumnsToContents();

    auto *close = new QPushButton(tx("Закрыть"), this);
    close->setFixedSize(200, 48);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(table, 1);
    layout->addWidget(close, 0, Qt::AlignRight);
}
