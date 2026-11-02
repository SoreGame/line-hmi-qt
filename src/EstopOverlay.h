#ifndef ESTOPOVERLAY_H
#define ESTOPOVERLAY_H

#include "OverlayCard.h"

#include <QStringList>

class ManualCheck;
class QLabel;
class QPushButton;

// Режим ПЛК Estop: причины аварии из кадра, чек-лист оператора и запрос «Выйти из E-STOP».
// Кнопка выхода активна, когда грибок отжат (ячейка 51), оператор отметил пункты
// и подтвердил возврат сварки в безопасное положение.
class EstopOverlay : public OverlayCard
{
    Q_OBJECT
public:
    explicit EstopOverlay(QWidget *parent);

    void refresh(const QStringList &causes, bool mushroomReleased, int zone);

signals:
    void exitRequested();
    // Оператор подтвердил возврат сварки. Кадр [3, 6, 0] шлёт главное окно.
    void weldSafeRequested();

private:
    void updateItems();
    void onWeldSafeClicked();
    void restyleWeldButton();

    static constexpr int kItemCount = 2;
    QLabel *m_causes = nullptr;
    QLabel *m_zone = nullptr;
    ManualCheck *m_items[kItemCount] = {};
    QPushButton *m_btnWeldSafe = nullptr;
    bool m_weldSafeSent = false;
    bool m_mushroomReleased = false;
    QPushButton *m_btnExit = nullptr;
};

#endif // ESTOPOVERLAY_H
