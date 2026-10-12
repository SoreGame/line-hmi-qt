#include "PrepOverlay.h"
#include "PlcClient.h"
#include "Texts.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

OverlayCard::ItemState fromSensor(bool ready)
{
    return ready ? OverlayCard::ItemState::Done : OverlayCard::ItemState::Pending;
}

OverlayCard::ItemState fromInit(LineController::InitState state)
{
    switch (state) {
    case LineController::InitState::Running: return OverlayCard::ItemState::Active;
    case LineController::InitState::Done: return OverlayCard::ItemState::Done;
    case LineController::InitState::Failed: return OverlayCard::ItemState::Error;
    case LineController::InitState::Idle: break;
    }
    return OverlayCard::ItemState::Pending;
}

} // namespace

PrepOverlay::PrepOverlay(QWidget *parent)
    : OverlayCard(tx("Предподготовка"), QSize(1080, 620), parent)
{
    for (QLabel *&row : m_rows)
        row = addItemRow();
    m_content->addStretch();

    m_btnStop = addButton(tx("Стоп"), ButtonKind::Stop);
    m_actions->addStretch();
    connect(m_btnStop, &QPushButton::clicked, this, &PrepOverlay::stopRequested);
}

void PrepOverlay::refresh(const PlcClient &plc, LineController::InitState init)
{
    const int program = plc.statusInt16(PlcClient::kProgramIndex);
    setSubtitle(program > 0 ? tx("Деталь %1").arg(program) : QString());

    const struct {
        const char *name;
        ItemState state;
    } tasks[] = {
        {"Ламель на выдаче", fromSensor(plc.lamellaIssued())},
        {program == 2 ? "Малые держатели ламелей на месте" : "Большие держатели ламелей на месте",
         fromSensor(plc.holdersPresent(program))},
        {"Робот готов", fromInit(init)},
        {"Клапаны сварки в безопасном положении", fromSensor(plc.weldValvesSafe())},
    };
    for (int i = 0; i < 4; ++i) {
        m_rows[i]->setText(itemMark(tasks[i].state) + tx(tasks[i].name));
        m_rows[i]->setStyleSheet(itemStyle(tasks[i].state));
    }

    const bool stopping = plc.mode() == PlcClient::Mode::Stopping;
    m_btnStop->setEnabled(!stopping);
    setStatus(stopping ? tx("Стоп запрошен · предподготовка останавливается") : QString());
}
