#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFutureWatcher>
#include <QString>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class NatsClient;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void on_btnStart_clicked();
    void on_btnPause_clicked();
    void on_btnStop_clicked();
    void on_btnProg1_clicked();
    void on_btnProg2_clicked();
    void on_btnAdmin_clicked();

private:
    enum class Mode { Ready, Run, Pause };
    enum class Program { Detail1, Detail2 };

    void refreshUi();
    void applyProgramVisuals();
    void applyModeVisuals();

    // NATS: старт скрипта, привязанного к выбранной детали.
    QString scriptFilenameForProgram(Program p) const;
    void startSelectedProgramScript();

    Ui::MainWindow *ui = nullptr;
    Mode m_mode = Mode::Ready;
    Program m_program = Program::Detail1;

    NatsClient *m_nats = nullptr;
    QFutureWatcher<QString> *m_execWatcher = nullptr;
};

#endif // MAINWINDOW_H