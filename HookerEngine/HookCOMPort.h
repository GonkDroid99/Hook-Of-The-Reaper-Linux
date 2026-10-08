#ifndef HOOKCOMPORT_H
#define HOOKCOMPORT_H

#include <QObject>
#include <QMap>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTcpSocket>
#include <QTimer>
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

    // TCP output servers (used by Sinden profiles).  HOTR is their client.
    bool IsTCPConnected(quint16 port) const;
    bool IsTCPConnecting(quint16 port) const;

public slots:

    //Connect to COM Port. On Linux, prefer the stable configured path
    //(/dev/serial/by-id or /dev/serial/by-path) over a transient tty name.
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

    void ConnectTCP(const quint16 &port, const quint8 &server);
    void DisconnectTCP();
    void WriteTCP(const QByteArray &writeData);
    void WriteTCP1(const QByteArray &writeData);

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
    void SerialTransportOpening(const quint8 &playerNum);
    void SerialTransportRecovering(const quint8 &playerNum);
    void SerialTransportConnected(const quint8 &playerNum);
    void SerialTransportDisconnected(const quint8 &playerNum);

private slots:
    void FoundTCPServer();
    void LostTCPServer();
    void FoundTCPServer1();
    void LostTCPServer1();


private:

    struct SerialReconnectInfo
    {
        bool valid = false;
        bool recovering = false;
        quint8 playerNum = 0;
        QString name;
        qint32 baud = 0;
        quint8 data = 0;
        quint8 parity = 0;
        quint8 stop = 0;
        quint8 flow = 0;
        QString path;
        QString stablePath;
        bool writeOnly = false;
    };

    QString FindStableSerialPath(const QString &deviceName) const;
    void ScheduleSerialReconnect(quint8 comPortNum);
    void ReconnectSerial(quint8 comPortNum);

    ///////////////////////////////////////////////////////////////////////////

    //How Many COM Ports Open
    quint8                          numPortOpen;

    //If 1 or More COM Ports Open
    bool                            isPortOpen;

    //Bool List to Keep Track on What COM Ports that are Open
    bool                            comPortOpen[MAXCOMPORTS];

    //Pointer Array of Serial COM Ports
    QSerialPort                     *p_ComPortArray[MAXCOMPORTS];
    QTimer                          *p_reconnectTimer[MAXCOMPORTS];
    SerialReconnectInfo             reconnectInfo[MAXCOMPORTS];

    //USB HID Devices (one per player)
    hid_device                      *p_hidConnection[MAXGAMEPLAYERS];
    bool                            hidOpen[MAXGAMEPLAYERS];

    bool                            bypassConnectFailWarning;

    quint16                         connectedTCPPort;
    quint16                         connectedTCPPort1;
    bool                            isTCPConnected;
    bool                            isTCPConnecting;
    bool                            isTCPConnected1;
    bool                            isTCPConnecting1;
    QTcpSocket                      *p_tcpServer;
    QTcpSocket                      *p_tcpServer1;

};

#endif // HOOKCOMPORT_H
