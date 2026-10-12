#include "OverlayCard.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

namespace {

const char *kItemBase =
    " border-radius:8px; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    " font-size:18px; font-weight:600; padding:16px 18px;";

const char *kBtnPrimary =
    "QPushButton{background:#388F51; color:white; border:none; border-radius:10px;"
    " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:18px; font-weight:700;}"
    " QPushButton:pressed{background:#2F7A44;}"
    " QPushButton:disabled{background:#A9B0B8; color:white;}";

const char *kBtnStop =
    "QPushButton{background:#C45C1A; color:white; border:none; border-radius:10px;"
    " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:18px; font-weight:700;}"
    " QPushButton:pressed{background:#A34C16;}"
    " QPushButton:disabled{background:#A9B0B8; color:white;}";

const char *kBtnNeutral =
    "QPushButton{background:#5B6470; color:white; border:none; border-radius:10px;"
    " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:18px; font-weight:700;}"
    " QPushButton:pressed{background:#4A525C;}"
    " QPushButton:disabled{background:#A9B0B8; color:white;}";

} // namespace

OverlayCard::OverlayCard(const QString &title, QSize cardSize, QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setFocusPolicy(Qt::StrongFocus);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addStretch();
    auto *row = new QHBoxLayout();
    row->addStretch();

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("overlayCard"));
    card->setFixedSize(cardSize);
    card->setStyleSheet(QStringLiteral("QFrame#overlayCard { background:#FFFFFF; border-radius:16px; }"));
    m_card = card;

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(36, 32, 36, 32);
    cardLayout->setSpacing(16);

    auto *titleLabel = new QLabel(title, card);
    titleLabel->setStyleSheet(QStringLiteral(
        "color:#1F2126; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        " font-size:28px; font-weight:700; background:transparent;"));
    cardLayout->addWidget(titleLabel);

    m_subtitle = new QLabel(card);
    m_subtitle->setStyleSheet(QStringLiteral(
        "color:#737880; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        " font-size:15px; font-weight:600; background:transparent;"));
    m_subtitle->hide();
    cardLayout->addWidget(m_subtitle);

    m_status = new QLabel(card);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral(
        "color:#1F2126; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        " font-size:15px; background:transparent;"));
    m_status->hide();
    cardLayout->addWidget(m_status);

    auto *body = new QHBoxLayout();
    body->setSpacing(28);
    m_content = new QVBoxLayout();
    m_content->setSpacing(12);
    body->addLayout(m_content, 1);
    m_actions = new QVBoxLayout();
    m_actions->setSpacing(12);
    body->addLayout(m_actions);
    cardLayout->addLayout(body, 1);

    row->addWidget(card);
    row->addStretch();
    root->addLayout(row);
    root->addStretch();
}

void OverlayCard::setSubtitle(const QString &text)
{
    m_subtitle->setText(text);
    m_subtitle->setVisible(!text.isEmpty());
}

void OverlayCard::setStatus(const QString &text)
{
    m_status->setText(text);
    m_status->setVisible(!text.isEmpty());
}

QString OverlayCard::itemStyle(ItemState state)
{
    const char *colors = "background:#F3F4F6; border:1px solid #C5C9D0; color:#737880;";
    switch (state) {
    case ItemState::Pending:
        break;
    case ItemState::Active:
        colors = "background:#FFF6E5; border:1px solid #E0B15A; color:#8A5A00;";
        break;
    case ItemState::Done:
        colors = "background:#EDF7F0; border:1px solid #59AD6B; color:#2E8C47;";
        break;
    case ItemState::Error:
        colors = "background:#FCEAEA; border:1px solid #D96B6B; color:#C43B3B;";
        break;
    }
    return QString::fromUtf8(colors) + QString::fromUtf8(kItemBase);
}

QString OverlayCard::itemMark(ItemState state)
{
    switch (state) {
    case ItemState::Done: return QStringLiteral("✓  ");
    case ItemState::Error: return QStringLiteral("!  ");
    default: return QStringLiteral("●  ");
    }
}

QPushButton *OverlayCard::addButton(const QString &text, ButtonKind kind)
{
    auto *button = new QPushButton(text, m_card);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(300, 68);
    const char *style = kind == ButtonKind::Primary ? kBtnPrimary
        : kind == ButtonKind::Stop                  ? kBtnStop
                                                    : kBtnNeutral;
    button->setStyleSheet(QString::fromUtf8(style));
    m_actions->addWidget(button);
    return button;
}

QLabel *OverlayCard::addItemRow()
{
    auto *label = new QLabel(m_card);
    label->setWordWrap(true);
    label->setMinimumHeight(64);
    label->setStyleSheet(itemStyle(ItemState::Pending));
    m_content->addWidget(label);
    return label;
}

void OverlayCard::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(31, 33, 38, 150));
}

void OverlayCard::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (QWidget *parent = parentWidget())
        setGeometry(parent->rect());
    raise();
}
