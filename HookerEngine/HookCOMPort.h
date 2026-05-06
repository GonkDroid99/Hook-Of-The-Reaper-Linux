#ifndef HOOKCOMPORT_H
#define HOOKCOMPORT_H

#include <QObject>
#include <QMap>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QDebug>

#include <QByteArray>

#include <hidapi.h>

#include "../Global.h"

class HookCOMPort : public QObject
{
    Q_OBJECT

public:
    explicit HookCOMPort(QObject *parent = nullptr);
    ~HookCOMPort();


public:

    //Check if Serial COM Port is Connected
    bool IsCOMConnected(const quint8 &comPortNum) { return comPortNum < MAXCOMPORTS && comPortOpen[comPortNum]; }

    //Check if USB HID Device is Connected
    bool IsUSBHIDConnected(const quint8 &playerNum) { return playerNum < MAXGAMEPLAYERS && hidOpen[playerNum]; }

    //TCP Server not supported on Linux — always returns false
    bool IsTCPConnected(quint16 port) { Q_UNUSED(port) return false; }
    bool IsTCPConnecting(quint16 port) { Q_UNUSED(port) return false; }

public slots:

    //Connect to COM Port (playerNum and comPortPath ignored on Linux — uses QSerialPort by name)
    void Connect(const quint8 &playerNum, const quint8 &comPortNum, const QString &comPortName, const qint32 &comPortBaud, const quint8 &comPortData, const quint8 &comPortParity, const quint8 &comPortStop, const quint8 &comPortFlow, const QString &comPortPath, const bool &isWriteOnly);

    //Disconnect to COM Port
    void Disconnect(const quint8 &playerNum, const quint8 &comPortNum);

    //Write Data to COM Port
    void WriteData(const quint8 &comPortNum, const QByteArray &writeData);

    //Read Data from COM Port
    void ReadData();

    //Disconnect All Open COM Ports and USB HID Devices
    void DisconnectAll();

    //Connect USB HID Device
    void ConnectHID(const quint8 &playerNum, const HIDInfo &lgHIDInfo);

    //Disconnect USB HID Device
    void DisconnectHID(const quint8 &playerNum);

    //Write Data to USB HID Device
    void WriteDataHID(const quint8 &playerNum, const QByteArray &writeData);

    //Bypass COM port connect-fail warning pop-up (stub for API compatibility)
    void SetBypassCOMPortConnectFailWarning(const bool &bypass) { bypassConnectFailWarning = bypass; }

    //Bypass serial write checks (stub for API compatibility)
    void SetBypassSerialWriteChecks(const bool &bypass) { Q_UNUSED(bypass) }

signals:

    //Signal Used to Move Read Data to Hooker Engine
    void ReadDataSig(const quint8 &comPortNum, const QByteArray &readData);

    //Signal Used to Display Error Message from COM Port
    void ErrorMessage(const QString &title, const QString &errorMsg);

    //Light Gun Connected/Disconnected via USB HID
    void LightGunConnected(const quint8 &playerNum);
    void LightGunDisconnected(const quint8 &playerNum);

private slots:



private:

    ///////////////////////////////////////////////////////////////////////////

    //How Many COM Ports Open
    quint8                          numPortOpen;

    //If 1 or More COM Ports Open
    bool                            isPortOpen;

    //Bool List to Keep Track on What COM Ports that are Open
    bool                            comPortOpen[MAXCOMPORTS];

    //Pointer Array of Serial COM Ports
    QSerialPort                     *p_ComPortArray[MAXCOMPORTS];

    //USB HID Devices (one per player)
    hid_device                      *p_hidConnection[MAXGAMEPLAYERS];
    bool                            hidOpen[MAXGAMEPLAYERS];

    bool                            bypassConnectFailWarning;

};

#endif // HOOKCOMPORT_H
