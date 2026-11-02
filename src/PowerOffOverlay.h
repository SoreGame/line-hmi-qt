#ifndef POWEROFFOVERLAY_H
#define POWEROFFOVERLAY_H

#include "OverlayCard.h"

class ManualCheck;
class QPushButton;

// Чек-лист выключения питания. Без кнопки отмены: закрывается только «Выключить».
class PowerOffOverlay : public OverlayCard
{
    Q_OBJECT
public:
    explicit PowerOffOverlay(QWidget *parent);

signals:
    void weldSafeRequested();
    void welderOffRequested();
    void powerOffConfirmed();

private:
    void updateGates();
    void onWeldSafeClicked();
    void onWelderOffClicked();
    void restyleCommandButton(QPushButton *btn, const QString &idle, const QString &done,
                              bool sent, bool enabled);

    ManualCheck *m_robotSafe = nullptr;
    ManualCheck *m_noObjects = nullptr;
    ManualCheck *m_airOff = nullptr;
    QPushButton *m_btnWeldSafe = nullptr;
    QPushButton *m_btnWelderOff = nullptr;
    QPushButton *m_btnPowerOff = nullptr;
    bool m_weldSafeSent = false;
    bool m_welderOffSent = false;
};

#endif // POWEROFFOVERLAY_H
