#include "HookCOMPort.h"
#include "../Global.h"
#include <QDir>
#include <QFileInfo>

//Constructor
HookCOMPort::HookCOMPort(QObject *parent)
    : QObject{parent}
{
    qDebug() << "HookCOMPort Started";

    numPortOpen = 0;
    isPortOpen = false;
    bypassConnectFailWarning = false;
    connectedTCPPort = 0;
    connectedTCPPort1 = 0;
    isTCPConnected = false;
    isTCPConnecting = false;
    isTCPConnected1 = false;
    isTCPConnecting1 = false;
    for(quint8 i = 0; i < MAXCOMPORTS; i++)
    {
        p_ComPortArray[i] = nullptr;
        comPortOpen[i] = false;
        p_reconnectTimer[i] = new QTimer(this);
        p_reconnectTimer[i]->setSingleShot(true);
        p_reconnectTimer[i]->setInterval(1000);
        connect(p_reconnectTimer[i], &QTimer::timeout, this, [this, i]() {
            ReconnectSerial(i);
        });
    }

    //Init USB HID
    if (hid_init())
    {
        QString critMessage = "The USB HID Init failed.";
        emit ErrorMessage("USB HID Init Failed", critMessage);
    }

    for(quint8 i = 0; i < MAXGAMEPLAYERS; i++)
    {
        p_hidConnection[i] = nullptr;
        hidOpen[i] = false;
    }

    p_tcpServer = new QTcpSocket(this);
    p_tcpServer1 = new QTcpSocket(this);
    connect(p_tcpServer, &QTcpSocket::connected, this, &HookCOMPort::FoundTCPServer);
    connect(p_tcpServer, &QTcpSocket::disconnected, this, &HookCOMPort::LostTCPServer);
    connect(p_tcpServer1, &QTcpSocket::connected, this, &HookCOMPort::FoundTCPServer1);
    connect(p_tcpServer1, &QTcpSocket::disconnected, this, &HookCOMPort::LostTCPServer1);

}

//Deconstructor
HookCOMPort::~HookCOMPort()
{
    DisconnectTCP();
    //Close and Delete Serial COM Ports
    for(quint8 i = 0; i < MAXCOMPORTS; i++)
    {
        if(comPortOpen[i])
            p_ComPortArray[i]->close ();

        if(p_ComPortArray[i] != nullptr)
            delete p_ComPortArray[i];
    }

    //Close all USB HID connections
    for(quint8 i = 0; i < MAXGAMEPLAYERS; i++)
    {
        if(hidOpen[i])
            hid_close(p_hidConnection[i]);
    }

    hid_exit();
}

bool HookCOMPort::IsTCPConnected(quint16 port) const
{
    if (connectedTCPPort == port)
        return isTCPConnected;
    if (connectedTCPPort1 == port)
        return isTCPConnected1;
    return false;
}

bool HookCOMPort::IsTCPConnecting(quint16 port) const
{
    if (connectedTCPPort == port)
        return isTCPConnecting;
    if (connectedTCPPort1 == port)
        return isTCPConnecting1;
    return false;
}

QString HookCOMPort::FindStableSerialPath(const QString &deviceName) const
{
    const QString target = QFileInfo(deviceName).canonicalFilePath();
    if(target.isEmpty())
        return QString();

    // Prefer by-id because it follows the gun, then by-path and the HOTR
    // convenience links. These are only used as recovery identities; the
    // existing lightguns.hor format can continue storing ttyUSB names.
    const QStringList stableDirectories = {
        QStringLiteral("/dev/serial/by-id"),
        QStringLiteral("/dev/serial/by-path"),
        QStringLiteral("/dev/hotr")
    };
    for(const QString &directory : stableDirectories)
    {
        const QFileInfoList entries = QDir(directory).entryInfoList(
            QDir::System | QDir::Hidden | QDir::NoDotAndDotDot,
            QDir::Name);
        for(const QFileInfo &entry : entries)
        {
            if(entry.isSymLink() && entry.canonicalFilePath() == target)
                return entry.absoluteFilePath();
        }
    }
    return QString();
}


//public slots

void HookCOMPort::Connect(const quint8 &playerNum, const quint8 &comPortNum, const QString &comPortName, const qint32 &comPortBaud, const quint8 &comPortData, const quint8 &comPortParity, const quint8 &comPortStop, const quint8 &comPortFlow, const QString &comPortPath, const bool &isWriteOnly)
{
    if(comPortNum >= MAXCOMPORTS)
        return;

    SerialReconnectInfo &saved = reconnectInfo[comPortNum];
    const bool wasRecovering = saved.recovering;
    saved.valid = true;
    saved.playerNum = playerNum;
    saved.name = comPortName;
    saved.baud = comPortBaud;
    saved.data = comPortData;
    saved.parity = comPortParity;
    saved.stop = comPortStop;
    saved.flow = comPortFlow;
    saved.path = comPortPath;
    saved.writeOnly = isWriteOnly;

    if(saved.stablePath.isEmpty() || !QFileInfo::exists(saved.stablePath))
    {
        const QString discoveredPath = FindStableSerialPath(
            !comPortPath.isEmpty() ? comPortPath : comPortName);
        if(!discoveredPath.isEmpty())
            saved.stablePath = discoveredPath;
    }

    // ttyUSB numbers are not stable across unplug/replug. If a stable path
    // is configured, always use it for the actual open. In particular, do not
    // fall back to the old tty name during recovery: it may now belong to a
    // different device.
    const QString connectionName = !saved.stablePath.isEmpty()
        ? saved.stablePath
        : (!comPortPath.isEmpty() ? comPortPath
        : comPortName);

    if (wasRecovering)
        emit SerialTransportRecovering(saved.playerNum);
    else
        emit SerialTransportOpening(saved.playerNum);

    //qDebug() << "Creating a New Serial Com Port at: " << comPortNum << " With the name of: " << comPortName;

    //Check if it is Already Open, if so, do nothing
    if(comPortOpen[comPortNum] == false)
    {
        bool isOpen;


        if(p_ComPortArray[comPortNum] != nullptr)
        {
            p_ComPortArray[comPortNum]->deleteLater();
            p_ComPortArray[comPortNum] = nullptr;
        }
        p_ComPortArray[comPortNum] = new QSerialPort(connectionName, this);

        if(isWriteOnly)
            isOpen = p_ComPortArray[comPortNum]->open(QIODevice::WriteOnly);
        else
            isOpen = p_ComPortArray[comPortNum]->open(QIODevice::ReadWrite);

        if(!isOpen)
        {
            //If Failed to Open COM Port
            QSerialPort::SerialPortError portError = p_ComPortArray[comPortNum]->error();
            QString critMessage = "Can not open the Serial COM Port: "+connectionName+" on Port: "+QString::number(comPortNum)+". Make sure a light gun or a COM device is on that port number. Serial Port Error: "+QString::number(portError);
            emit ErrorMessage("Serial COM Port Error",critMessage);
            qDebug() << critMessage;
            saved.recovering = true;
            emit SerialTransportRecovering(saved.playerNum);
            ScheduleSerialReconnect(comPortNum);
            return;
        }
        else
        {
            //If Open COM Port
            comPortOpen[comPortNum] = true;
            QSerialPort *serial = p_ComPortArray[comPortNum];

            // Apply the line settings after open. On Linux the driver can reset
            // termios to its default (typically 9600) while opening the device,
            // even though QSerialPort still reports the requested cached value.
            const QSerialPort::Directions baudDirections = isWriteOnly ? QSerialPort::Output : QSerialPort::AllDirections;
            const bool baudSet = serial->setBaudRate((QSerialPort::BaudRate)comPortBaud, baudDirections);
            const bool dataSet = serial->setDataBits((QSerialPort::DataBits)comPortData);
            const bool paritySet = serial->setParity((QSerialPort::Parity)comPortParity);
            const bool stopSet = serial->setStopBits((QSerialPort::StopBits)comPortStop);
            const bool flowSet = serial->setFlowControl((QSerialPort::FlowControl)comPortFlow);
            qDebug() << "[HOTR] Serial OPEN" << serial->portName()
                     << "configuredPath=" << comPortPath
                     << "stablePath=" << saved.stablePath
                     << "baud=" << serial->baudRate() << "data=" << serial->dataBits()
                     << "parity=" << serial->parity() << "stop=" << serial->stopBits()
                     << "flow=" << serial->flowControl()
                     << "settingsApplied=" << (baudSet && dataSet && paritySet && stopSet && flowSet);
            connect(serial, &QSerialPort::bytesWritten, serial, [serial](qint64 count) {
                qDebug() << "[HOTR] Serial WRITTEN" << serial->portName()
                         << "bytes=" << count << "pending=" << serial->bytesToWrite();
            });
            connect(serial, &QSerialPort::errorOccurred, serial, [this, serial, comPortNum](QSerialPort::SerialPortError error) {
                // Once recovery has started, Qt can report the same unplugged
                // device error repeatedly. Do not let that callback flood the
                // log or keep the event loop busy while the retry timer owns
                // recovery.
                if(error == QSerialPort::NoError || reconnectInfo[comPortNum].recovering)
                    return;
                qDebug() << "[HOTR] Serial ERROR" << serial->portName()
                         << serial->errorString() << "code=" << error;
            });
            connect(serial, &QSerialPort::errorOccurred, this,
                    [this, comPortNum](QSerialPort::SerialPortError error) {
                const bool disconnectedError =
                    error == QSerialPort::ResourceError ||
                    error == QSerialPort::DeviceNotFoundError ||
                    error == QSerialPort::PermissionError ||
                    error == QSerialPort::UnknownError;
                if(!disconnectedError ||
                   !reconnectInfo[comPortNum].valid ||
                   reconnectInfo[comPortNum].recovering)
                    return;

                reconnectInfo[comPortNum].recovering = true;
                if(p_ComPortArray[comPortNum] != nullptr)
                    p_ComPortArray[comPortNum]->close();
                if(comPortOpen[comPortNum])
                {
                    comPortOpen[comPortNum] = false;
                    if(numPortOpen > 0)
                        numPortOpen--;
                    if(numPortOpen == 0)
                        isPortOpen = false;
                }
                qDebug() << "[HOTR] Serial device disconnected; retrying port=" << comPortNum;
                emit LightGunDisconnected(reconnectInfo[comPortNum].playerNum);
                emit SerialTransportRecovering(reconnectInfo[comPortNum].playerNum);
                ScheduleSerialReconnect(comPortNum);
            });

            if(!isWriteOnly)
                connect(p_ComPortArray[comPortNum],SIGNAL(readyRead()),this,SLOT(ReadData()));

            numPortOpen++;
            isPortOpen = true;
            saved.recovering = false;
            p_reconnectTimer[comPortNum]->stop();
            if(wasRecovering)
            {
                qDebug() << "[HOTR] Serial device recovered port=" << comPortNum;
                emit LightGunConnected(saved.playerNum);
            }
            emit SerialTransportConnected(saved.playerNum);
        }

    }
    //qDebug() << "Done with Connecting Port";
    //qDebug() << "comPortOpen[comPortNum]: " << comPortOpen[comPortNum] << " comPortNum: " << comPortNum << " comPortName: " << comPortName;

}

void HookCOMPort::Disconnect(const quint8 &playerNum, const quint8 &comPortNum)
{
    Q_UNUSED(playerNum)

    if(comPortNum >= MAXCOMPORTS)
        return;
    reconnectInfo[comPortNum].valid = false;
    reconnectInfo[comPortNum].recovering = false;
    p_reconnectTimer[comPortNum]->stop();

    //qDebug() << "comPortOpen[comPortNum]: " << comPortOpen[comPortNum] << " comPortNum: " << comPortNum;

    if(comPortOpen[comPortNum])
    {


        if(p_ComPortArray[comPortNum]->bytesToWrite () > 0)
        {
            // Closing QSerialPort discards queued bytes. Game profiles often
            // send their final reset command immediately before closing, so
            // drain the kernel/driver queue before releasing the device.
            p_ComPortArray[comPortNum]->flush();
            if(!p_ComPortArray[comPortNum]->waitForBytesWritten(COMPORTWAITFORWRITE))
            {
                qDebug() << "[HOTR] Serial close drain timed out port=" << comPortNum
                         << "pending=" << p_ComPortArray[comPortNum]->bytesToWrite()
                         << "error=" << p_ComPortArray[comPortNum]->errorString();
            }
        }

        //qDebug() << "Closing COM Port #" << comPortNum;

        p_ComPortArray[comPortNum]->close ();

        bool isOpen = p_ComPortArray[comPortNum]->isOpen ();

        if(isOpen)
        {
            QSerialPort::SerialPortError portError = p_ComPortArray[comPortNum]->error();
            QString critMessage = "Serial Port could not close, something is wrong. Serial COM Port: "+QString::number(comPortNum)+". Serial Port Error: "+QString::number(portError)+"  "+QString::number(p_ComPortArray[comPortNum]->bytesToWrite());
            emit ErrorMessage("Serial COM Port Error",critMessage);
        }

        //delete p_ComPortArray[comPortNum];
        //p_ComPortArray[comPortNum] = nullptr;

        comPortOpen[comPortNum] = false;

        if(numPortOpen > 0)
            numPortOpen--;

        if(numPortOpen == 0)
            isPortOpen = false;

        emit SerialTransportDisconnected(playerNum);
    }

}

void HookCOMPort::WriteData(const quint8 &comPortNum, const QByteArray &writeData)
{
    qint64 bytesWritten = -1;
    bool writeDone = false;

    if(comPortOpen[comPortNum] && p_ComPortArray[comPortNum] != nullptr && p_ComPortArray[comPortNum]->isOpen())
    {
        bytesWritten = p_ComPortArray[comPortNum]->write(writeData);

        qDebug() << "[HOTR] Serial write port=" << comPortNum
                 << "data=" << writeData
                 << "hex=" << writeData.toHex()
                 << "requested=" << writeData.size()
                 << "queued=" << bytesWritten
                 << "pending=" << p_ComPortArray[comPortNum]->bytesToWrite();

        //qDebug() << "bytesWritten is : " << bytesWritten << " and QByteArray size is: " << writeData.size();

        //qDebug() << "Data to be Written: " << QString::fromStdString (writeData.toStdString ()) << " to Port #" << comPortNum << " bytesWritten:" << bytesWritten;

        //If the data has not been Written, flush & wait 100 milli-secs
        if(bytesWritten != writeData.size())
        {
            p_ComPortArray[comPortNum]->flush ();
            writeDone = p_ComPortArray[comPortNum]->waitForBytesWritten (COMPORTWAITFORWRITE);
        }
        else
            writeDone = true;

        if(!writeDone)
        {
            //If Failed to Write then emit Error Signal
            QSerialPort::SerialPortError portError = p_ComPortArray[comPortNum]->error();
            QString critMessage = "A write failed on the Serial COM Port: "+QString::number(comPortNum)+". Make sure a light gun or a COM device is on that port number. Serial Port Error: "+QString::number(portError);
            emit ErrorMessage("Serial COM Port Error",critMessage);
            qDebug() << "[HOTR] Serial write failed port=" << comPortNum
                     << "error=" << p_ComPortArray[comPortNum]->errorString()
                     << "queued=" << bytesWritten;
        }
    }
    else
    {
        qDebug() << "[HOTR] Serial write skipped port=" << comPortNum
                 << "openFlag=" << comPortOpen[comPortNum]
                 << "portOpen=" << (p_ComPortArray[comPortNum] != nullptr && p_ComPortArray[comPortNum]->isOpen())
                 << "data=" << writeData.toHex();
    }
}

void HookCOMPort::ScheduleSerialReconnect(quint8 comPortNum)
{
    if(comPortNum < MAXCOMPORTS && reconnectInfo[comPortNum].valid &&
       !p_reconnectTimer[comPortNum]->isActive())
        p_reconnectTimer[comPortNum]->start();
}

void HookCOMPort::ReconnectSerial(quint8 comPortNum)
{
    if(comPortNum >= MAXCOMPORTS || !reconnectInfo[comPortNum].valid)
        return;

    const SerialReconnectInfo saved = reconnectInfo[comPortNum];
    qDebug() << "[HOTR] Serial reconnect attempt port=" << comPortNum
             << "device=" << saved.name;
    Connect(saved.playerNum, comPortNum,
            saved.stablePath.isEmpty()
                ? (saved.path.isEmpty() ? saved.name : saved.path)
                : saved.stablePath,
            saved.baud, saved.data,
            saved.parity, saved.stop, saved.flow, saved.path, saved.writeOnly);
}

void HookCOMPort::ReadData()
{
    quint8 tempNum = UNASSIGN;
    QByteArray tempReadData;

    for(quint8 i = 0; i < MAXCOMPORTS; i++)
    {
        if(p_ComPortArray[i] != nullptr && p_ComPortArray[i]->canReadLine())
        {
            tempNum = i;
            break;
        }
    }

    if(tempNum != UNASSIGN)
    {
        tempReadData = p_ComPortArray[tempNum]->readAll ();
        emit ReadDataSig(tempNum, tempReadData);
    }
}

void HookCOMPort::DisconnectAll()
{
    for(quint8 i = 0; i < MAXCOMPORTS; i++)
    {
        reconnectInfo[i].valid = false;
        reconnectInfo[i].recovering = false;
        p_reconnectTimer[i]->stop();
    }

    if(isPortOpen)
    {
        //Close All Connections
        for(quint8 i = 0; i < MAXCOMPORTS; i++)
        {
            if(comPortOpen[i])
            {
                p_ComPortArray[i]->close ();
                comPortOpen[i] = false;
                emit SerialTransportDisconnected(reconnectInfo[i].playerNum);
            }
        }
        isPortOpen = false;
        numPortOpen = 0;
    }

    //Close all USB HID connections
    for(quint8 i = 0; i < MAXGAMEPLAYERS; i++)
    {
        if(hidOpen[i])
        {
            hid_close(p_hidConnection[i]);
            hidOpen[i] = false;
        }
    }
}


void HookCOMPort::ConnectHID(const quint8 &playerNum, const HIDInfo &lgHIDInfo)
{
    if(!hidOpen[playerNum])
    {
        QByteArray pathBA = lgHIDInfo.path.toUtf8();
        char* pathPtr = pathBA.data();

        p_hidConnection[playerNum] = hid_open_path(pathPtr);

        if(!p_hidConnection[playerNum])
        {
            if(!bypassConnectFailWarning)
            {
                const wchar_t* errorWChar = hid_error(nullptr);
                QString errorMSG = QString::fromWCharArray(errorWChar);
                QString critMessage = "The USB HID failed to connect for player: "+QString::number(playerNum+1)+"\nError Message: "+errorMSG;
                emit ErrorMessage("USB HID Failed to Open", critMessage);
            }
        }
        else
        {
            hidOpen[playerNum] = true;
            emit LightGunConnected(playerNum);
        }
    }
}


void HookCOMPort::DisconnectHID(const quint8 &playerNum)
{
    if(hidOpen[playerNum])
    {
        hid_close(p_hidConnection[playerNum]);
        p_hidConnection[playerNum] = nullptr;
        hidOpen[playerNum] = false;
        emit LightGunDisconnected(playerNum);
    }
}


void HookCOMPort::WriteDataHID(const quint8 &playerNum, const QByteArray &writeData)
{
    if(hidOpen[playerNum])
    {
        const unsigned char *constDataPtr = reinterpret_cast<const unsigned char*>(writeData.constData());
        std::size_t size = writeData.size();
        quint8 retry = 0;
        qint16 bytesWritten = hid_write(p_hidConnection[playerNum], constDataPtr, size);

        while(bytesWritten == -1 && retry != WRITERETRYATTEMPTS)
        {
            bytesWritten = hid_write(p_hidConnection[playerNum], constDataPtr, size);
            retry++;
        }

        if(bytesWritten == -1)
        {
            const wchar_t* errorWChar = hid_error(p_hidConnection[playerNum]);
            QString errorMSG = QString::fromWCharArray(errorWChar);
            QString critMessage = "The USB HID failed to write data.\nError Message: "+errorMSG;
            emit ErrorMessage("USB HID Write Failed", critMessage);
        }
    }
}

void HookCOMPort::ConnectTCP(const quint16 &port, const quint8 &server)
{
    QTcpSocket *socket = server == 0 ? p_tcpServer : p_tcpServer1;
    bool *connected = server == 0 ? &isTCPConnected : &isTCPConnected1;
    bool *connecting = server == 0 ? &isTCPConnecting : &isTCPConnecting1;
    quint16 *connectedPort = server == 0 ? &connectedTCPPort : &connectedTCPPort1;

    if (*connected || *connecting)
        return;
    if (server == 1 && connectedTCPPort == port)
        return;

    *connectedPort = port;
    *connecting = true;
    socket->connectToHost(QHostAddress::LocalHost, port);
    if (!socket->waitForConnected(TIMETOWAITTCPSERVER))
    {
        *connecting = false;
        *connectedPort = 0;
        emit ErrorMessage("Light Gun TCP Connection Failed",
                          "Hook of the Reaper could not connect to the Light Guns TCP Socket Server.");
    }
}

void HookCOMPort::DisconnectTCP()
{
    p_tcpServer->disconnectFromHost();
    p_tcpServer1->disconnectFromHost();
    isTCPConnected = isTCPConnecting = false;
    isTCPConnected1 = isTCPConnecting1 = false;
    connectedTCPPort = connectedTCPPort1 = 0;
}

void HookCOMPort::WriteTCP(const QByteArray &writeData)
{
    if (isTCPConnected)
        p_tcpServer->write(writeData);
}

void HookCOMPort::WriteTCP1(const QByteArray &writeData)
{
    if (isTCPConnected1)
        p_tcpServer1->write(writeData);
}

void HookCOMPort::FoundTCPServer()
{
    isTCPConnected = true;
    isTCPConnecting = false;
}

void HookCOMPort::LostTCPServer()
{
    isTCPConnected = false;
    isTCPConnecting = false;
    connectedTCPPort = 0;
}

void HookCOMPort::FoundTCPServer1()
{
    isTCPConnected1 = true;
    isTCPConnecting1 = false;
}

void HookCOMPort::LostTCPServer1()
{
    isTCPConnected1 = false;
    isTCPConnecting1 = false;
    connectedTCPPort1 = 0;
}






