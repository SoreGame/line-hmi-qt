#ifndef RUNHISTORYDIALOG_H
#define RUNHISTORYDIALOG_H

#include <QDialog>

// Последние запуски линии из RunHistory, новые сверху.
class RunHistoryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RunHistoryDialog(QWidget *parent = nullptr);
};

#endif // RUNHISTORYDIALOG_H
