#ifndef PLACEOVERLAY_H
#define PLACEOVERLAY_H

#include "OverlayCard.h"

class QLabel;

// Робот вне стартовых зон, ПЛК готов: картинка стартового положения. Закрывает его пульт,
// когда робот попадает в зону.
class PlaceOverlay : public OverlayCard
{
    Q_OBJECT
public:
    PlaceOverlay(const QString &imagePath, QWidget *parent);

    void refresh(int zone);

private:
    QLabel *m_zone = nullptr;
};

#endif // PLACEOVERLAY_H
