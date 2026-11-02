#ifndef ROBOTDATATABLE_H
#define ROBOTDATATABLE_H

#include "NatsClient.h"

#include <QFrame>
#include <QFutureWatcher>
#include <QVector>

class QLabel;
class QPushButton;
class QTableWidget;
class QWidget;

// Таблица точек робота из KV robot_points. Та же, что в админке.
class RobotDataTable : public QFrame
{
    Q_OBJECT

public:
    explicit RobotDataTable(NatsClient *nats, QWidget *parent = nullptr);
    ~RobotDataTable() override;

    int preferredWidth() const;
    int contentHeight() const;

signals:
    void contentHeightChanged();

private:
    struct PointsLoadResult {
        QString error;
        QVector<NatsClient::RobotPoint> points;
    };

    void setBusy(bool busy);
    void reload();
    void onPointsLoaded();
    void onPointSaved();
    void fillTable();
    int pageCount() const;
    void clampPage();
    void goToPage(int page);
    void updatePager();
    void editPoint(const NatsClient::RobotPoint &point);
    void savePoint(const QString &kvKey, double x, double y, double z, double r);

    static constexpr int kPageSize = 10;

    NatsClient *m_nats = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_btnRefresh = nullptr;
    QTableWidget *m_table = nullptr;
    QWidget *m_pager = nullptr;
    QPushButton *m_btnPrevPage = nullptr;
    QPushButton *m_btnNextPage = nullptr;
    QLabel *m_pageLabel = nullptr;
    QVector<NatsClient::RobotPoint> m_points;
    int m_page = 0;
    bool m_pagerOn = false;

    QFutureWatcher<PointsLoadResult> *m_loadWatcher = nullptr;
    QFutureWatcher<QString> *m_saveWatcher = nullptr;

    bool m_poseKnown = false;
    double m_poseX = 0;
    double m_poseY = 0;
    double m_poseZ = 0;
    double m_poseR = 0;
};

#endif // ROBOTDATATABLE_H
