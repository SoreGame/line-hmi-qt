#ifndef PREPOVERLAY_H
#define PREPOVERLAY_H

#include "LineController.h"
#include "OverlayCard.h"

class PlcClient;
class QLabel;
class QPushButton;

// Режим ПЛК Prep: задачи предподготовки из ячеек 65..67 и выполнение init-скрипта.
class PrepOverlay : public OverlayCard
{
    Q_OBJECT
public:
    explicit PrepOverlay(QWidget *parent);

    void refresh(const PlcClient &plc, LineController::InitState init, int mainDelaySec = 0);

signals:
    void stopRequested();

private:
    QLabel *m_rows[4] = {};
    QLabel *m_countdown = nullptr;
    QPushButton *m_btnStop = nullptr;
};

#endif // PREPOVERLAY_H
