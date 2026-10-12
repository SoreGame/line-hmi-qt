#ifndef ESTOPOVERLAY_H
#define ESTOPOVERLAY_H

#include "OverlayCard.h"

#include <QStringList>

class ManualCheck;
class QLabel;
class QPushButton;

// Режим ПЛК Estop: причины аварии из кадра, чек-лист оператора и запрос «Выйти из E-STOP».
// Кнопка выхода активна, когда грибок отжат (ячейка 51) и оператор отметил все пункты.
class EstopOverlay : public OverlayCard
{
    Q_OBJECT
public:
    explicit EstopOverlay(QWidget *parent);

    void refresh(const QStringList &causes, bool mushroomReleased, int zone);

signals:
    void exitRequested();

private:
    void updateItems();

    static constexpr int kItemCount = 3;
    QLabel *m_causes = nullptr;
    QLabel *m_zone = nullptr;
    ManualCheck *m_items[kItemCount] = {};
    bool m_mushroomReleased = false;
    QPushButton *m_btnExit = nullptr;
};

#endif // ESTOPOVERLAY_H
