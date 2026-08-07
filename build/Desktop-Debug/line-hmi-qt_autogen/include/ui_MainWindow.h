/********************************************************************************
** Form generated from reading UI file 'MainWindow.ui'
**
** Created by: Qt User Interface Compiler version 6.8.3
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QWidget *centralwidget;
    QLabel *screenLabel;
    QPushButton *btnAdmin;
    QPushButton *btnProg1;
    QPushButton *btnProg2;
    QPushButton *btnStart;
    QPushButton *btnPause;
    QPushButton *btnStop;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName("MainWindow");
        MainWindow->resize(1920, 1080);
        MainWindow->setMinimumSize(QSize(1920, 1080));
        MainWindow->setMaximumSize(QSize(1920, 1080));
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName("centralwidget");
        screenLabel = new QLabel(centralwidget);
        screenLabel->setObjectName("screenLabel");
        screenLabel->setGeometry(QRect(50, -130, 1920, 1080));
        screenLabel->setScaledContents(true);
        screenLabel->setAlignment(Qt::AlignmentFlag::AlignCenter);
        btnAdmin = new QPushButton(centralwidget);
        btnAdmin->setObjectName("btnAdmin");
        btnAdmin->setGeometry(QRect(1790, 16, 106, 40));
        btnAdmin->setCursor(QCursor(Qt::CursorShape::PointingHandCursor));
        btnAdmin->setFocusPolicy(Qt::FocusPolicy::NoFocus);
        btnAdmin->setFlat(true);
        btnProg1 = new QPushButton(centralwidget);
        btnProg1->setObjectName("btnProg1");
        btnProg1->setGeometry(QRect(1520, 121, 184, 78));
        btnProg1->setCursor(QCursor(Qt::CursorShape::PointingHandCursor));
        btnProg1->setFocusPolicy(Qt::FocusPolicy::NoFocus);
        btnProg1->setFlat(true);
        btnProg2 = new QPushButton(centralwidget);
        btnProg2->setObjectName("btnProg2");
        btnProg2->setGeometry(QRect(1712, 121, 184, 78));
        btnProg2->setCursor(QCursor(Qt::CursorShape::PointingHandCursor));
        btnProg2->setFocusPolicy(Qt::FocusPolicy::NoFocus);
        btnProg2->setFlat(true);
        btnStart = new QPushButton(centralwidget);
        btnStart->setObjectName("btnStart");
        btnStart->setGeometry(QRect(1520, 260, 376, 68));
        btnStart->setCursor(QCursor(Qt::CursorShape::PointingHandCursor));
        btnStart->setFocusPolicy(Qt::FocusPolicy::NoFocus);
        btnStart->setFlat(true);
        btnPause = new QPushButton(centralwidget);
        btnPause->setObjectName("btnPause");
        btnPause->setGeometry(QRect(1520, 338, 376, 58));
        btnPause->setCursor(QCursor(Qt::CursorShape::PointingHandCursor));
        btnPause->setFocusPolicy(Qt::FocusPolicy::NoFocus);
        btnPause->setFlat(true);
        btnStop = new QPushButton(centralwidget);
        btnStop->setObjectName("btnStop");
        btnStop->setGeometry(QRect(1520, 406, 376, 58));
        btnStop->setCursor(QCursor(Qt::CursorShape::PointingHandCursor));
        btnStop->setFocusPolicy(Qt::FocusPolicy::NoFocus);
        btnStop->setFlat(true);
        MainWindow->setCentralWidget(centralwidget);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "\320\237\320\260\320\275\320\265\320\273\321\214 \321\203\320\277\321\200\320\260\320\262\320\273\320\265\320\275\320\270\321\217 \320\273\320\270\320\275\320\270\320\265\320\271 \302\267 SCARA HMI", nullptr));
        btnAdmin->setText(QString());
        btnProg1->setText(QString());
        btnProg2->setText(QString());
        btnStart->setText(QString());
        btnPause->setText(QString());
        btnStop->setText(QString());
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
