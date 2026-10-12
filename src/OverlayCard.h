#ifndef OVERLAYCARD_H
#define OVERLAYCARD_H

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

// Затемнение на весь экран и белая карточка по центру: заголовок, подзаголовок,
// строка статуса, тело (слева — содержимое, справа — колонка кнопок).
class OverlayCard : public QWidget
{
    Q_OBJECT
public:
    enum class ItemState { Pending, Active, Done, Error };
    enum class ButtonKind { Primary, Stop, Neutral };

    OverlayCard(const QString &title, QSize cardSize, QWidget *parent);

    void setSubtitle(const QString &text);
    void setStatus(const QString &text);

    static QString itemStyle(ItemState state);
    static QString itemMark(ItemState state);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

    QPushButton *addButton(const QString &text, ButtonKind kind);
    QLabel *addItemRow();

    QWidget *m_card = nullptr;
    QVBoxLayout *m_content = nullptr;
    QVBoxLayout *m_actions = nullptr;

private:
    QLabel *m_subtitle = nullptr;
    QLabel *m_status = nullptr;
};

#endif // OVERLAYCARD_H
