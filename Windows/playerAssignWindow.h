#ifndef PLAYERASSIGNWINDOW_H
#define PLAYERASSIGNWINDOW_H

#include <QDialog>


#include <QMessageBox>


#include "../COMDeviceList/ComDeviceList.h"
#include "../HardwareManager/HardwareManager.h"
#include <QHash>


namespace Ui {
class playerAssignWindow;
}

class playerAssignWindow : public QDialog
{
    Q_OBJECT

public:
    explicit playerAssignWindow(ComDeviceList *cdList, HardwareManager *hardwareManager = nullptr,
                                QWidget *parent = nullptr);
    ~playerAssignWindow();

private slots:
    //Assignment of Light Guns to Players
    void on_assignPushButton_clicked();

    //Assignment of Light Guns to Players, and Close Window
    void on_okPushButton_clicked();

    //Close Window
    void on_cancelPushButton_clicked();

private:

    //Get the Combo Boxes Index
    void GetComboBoxIndexes();

    //Assign Save Light Guns to Players
    void AssignPlayers();


    ///////////////////////////////////////////////////////////////////////////

    //For Window Stuff
    Ui::playerAssignWindow  *ui;

    // Legacy profile list used by the existing game engine. Do not delete.
    ComDeviceList           *p_comDeviceList;
    // New persistent hardware registry used for automatic detection.
    HardwareManager         *p_hardwareManager;
    // Legacy LightGun index -> persistent HardwareManager identity.
    QHash<int, QString>      hardwareIdentityByLightGun;


    //Number of Light Gun in the List
    quint8                  numberLightGuns;

    quint8                  playersAssignment[MAXPLAYERLIGHTGUNS]; // Saved legacy assignments.
    qint16                  playersIndex[MAXPLAYERLIGHTGUNS];      // Current combo-box selections.

};

#endif // PLAYERASSIGNWINDOW_H
