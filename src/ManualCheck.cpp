#include "ManualCheck.h"
#include "Texts.h"

#include <QHBoxLayout>
#include <QLabel>

ManualCheck::ManualCheck(const QString &label, int fontPx, QWidget *parent)
    : QPushButton(parent), m_label(label), m_fontPx(fontPx)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_text = new QLabel(this);
    m_text->setTextFormat(Qt::RichText);
    m_text->setWordWrap(true);
    m_text->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(fontPx, fontPx / 2, fontPx, fontPx / 2);
    lay->addWidget(m_text);
    restyle();
}

void ManualCheck::setDone(bool done)
{
    if (m_done == done)
        return;
    m_done = done;
    restyle();
}

void ManualCheck::restyle()
{
    const QString box = m_done ? QStringLiteral("☑") : QStringLiteral("☐");
    const QString hint = m_done ? tx("Подтверждено · нажмите ещё раз, чтобы снять")
                                : tx("Нажмите, чтобы подтвердить");
    const int hintPx = qMax(11, m_fontPx * 3 / 4);
    m_text->setText(QStringLiteral("<span style='font-size:%1px; font-weight:700;'>%2&nbsp;&nbsp;%3</span>"
                                   "<br><span style='font-size:%4px; font-weight:600;'>%5</span>")
                        .arg(m_fontPx)
                        .arg(box, m_label.toHtmlEscaped())
                        .arg(hintPx)
                        .arg(hint));

    const char *colors = m_done
        ? "background:#EDF7F0; border:2px solid #59AD6B; color:#2E8C47;"
        : "background:#EAF2FF; border:2px dashed #3B7DDD; color:#1D4F9E;";
    const char *pressed = m_done ? "background:#DCEFE2;" : "background:#D6E6FF;";
    setStyleSheet(QStringLiteral("QPushButton{%1 border-radius:8px;}"
                                 " QPushButton:pressed{%2}"
                                 " QLabel{background:transparent; border:none; color:%3;"
                                 " font-family:\"Inter\",\"Segoe UI\",sans-serif;}")
                      .arg(QString::fromUtf8(colors), QString::fromUtf8(pressed),
                           m_done ? QStringLiteral("#2E8C47") : QStringLiteral("#1D4F9E")));
    setMinimumHeight(m_text->sizeHint().height() + m_fontPx);
}
