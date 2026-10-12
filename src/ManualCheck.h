#ifndef MANUALCHECK_H
#define MANUALCHECK_H

#include <QPushButton>

class QLabel;

// Пункт, который оператор подтверждает руками нажатием. Выглядит иначе, чем пункты,
// которые пульт проверяет сам: пунктирная рамка, ☐ и подсказка «Нажмите, чтобы подтвердить».
class ManualCheck : public QPushButton
{
    Q_OBJECT
public:
    ManualCheck(const QString &label, int fontPx, QWidget *parent);

    void setDone(bool done);
    bool isDone() const { return m_done; }

private:
    void restyle();

    QString m_label;
    int m_fontPx;
    bool m_done = false;
    QLabel *m_text = nullptr;
};

#endif // MANUALCHECK_H
