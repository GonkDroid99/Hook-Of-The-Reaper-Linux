#include "HookTCPSocket.h"
#include "../Global.h"


HookTCPSocket::HookTCPSocket(QObject *parent)
    : QObject{parent}
{
    //qDebug() << "HookTCPSocket Started";

    inGame = false;

    //HOTR Starts out Minimized
    isMinimized = true;

    //Is the TCP Socket Connected
    isConnected = false;

    //Is TCP Socket trying to Connect
    isConnecting = false;

    //Stop Connecting to TCP Server
    stopConnecting = false;

    //Create the New TCP Socket
    p_hookSocket = new QTcpSocket(this);

    //Connect Signal of when there is read data, to the slot that will read it
    connect(p_hookSocket,SIGNAL(readyRead()), this, SLOT(TCPReadData()));
    connect(p_hookSocket,SIGNAL(connected()), this, SLOT(SocketConnected()));
    connect(p_hookSocket,SIGNAL(disconnected()), this, SLOT(SocketDisconnected()));

    //Timer Set-up
    p_waitingForConnection = new QTimer(this);
    p_waitingForConnection->setInterval (TCPTIMERTIME);
    p_waitingForConnection->setSingleShot (true);
    connect(p_waitingForConnection, SIGNAL(timeout()), this, SLOT(TCPConnectionTimeOut()));

}

HookTCPSocket::~HookTCPSocket()
{
    p_waitingForConnection->stop();
    delete p_waitingForConnection;
}


void HookTCPSocket::TCPReadData()
{
    // TCP does not preserve the sender's writes. A single MameOutputSender
    // message can arrive split across multiple readyRead() calls, while
    // several messages can arrive in one call. Buffer until a complete CR or
    // LF terminated line is available before interpreting it.
    pendingLineData.append(p_hookSocket->readAll());

    while (true)
    {
        int delimiterIndex = -1;
        for (int index = 0; index < pendingLineData.size(); ++index)
        {
            if (pendingLineData.at(index) == '\r' || pendingLineData.at(index) == '\n')
            {
                delimiterIndex = index;
                break;
            }
        }
        if (delimiterIndex < 0)
            break;

        QByteArray lineBytes = pendingLineData.left(delimiterIndex);
        int bytesToRemove = delimiterIndex + 1;
        if (pendingLineData.at(delimiterIndex) == '\r' &&
            bytesToRemove < pendingLineData.size() &&
            pendingLineData.at(bytesToRemove) == '\n')
        {
            ++bytesToRemove;
        }
        pendingLineData.remove(0, bytesToRemove);

        const QString line = QString::fromUtf8(lineBytes).trimmed();
        if (line.isEmpty())
            continue;

        // MameOutputSender uses "signal = value". Keep an empty value valid
        // because mame_stop intentionally has no payload.
        const int separatorIndex = line.indexOf(" = ");
        const QString signal = separatorIndex >= 0 ? line.left(separatorIndex) : line;
        const QString data = separatorIndex >= 0 ? line.mid(separatorIndex + 3) : QString();

        //Check if Game Has Stopped
        if(signal.size() == 9 && inGame)
        {
            if(signal[5] == 's' && signal[6] == 't' && signal[8] == 'p')
            {
                emit GameHasStopped();
                inGame = false;
            }
        }


        if(inGame)
        {
           // qDebug() << "[HOTR] TCP in-game signal:" << splitData[0] << "=" << splitData[1]
           //         << "| inFilter:" << outputSignalsFilter.contains(splitData[0]);

            //Check if Light Guns and Light Controllers using Output Signal
            if(bothOutputSig)
            {
                if(outputSignalsBoth.contains(signal))
                    emit FilteredOutputSignalsBoth(signal, data);
            }

            //Check if Light Guns using Output Signal
            if(lgOutputSig)
            {
                if(outputSignalsFilter.contains(signal))
                    emit FilteredOutputSignals(signal, data);
            }

            //Check if Light Controllers using Output Signal
            if(lcOutputSig)
            {
                if(outputSignalsLight.contains(signal))
                    emit FilteredOutputSignalsLight(signal, data);
            }

            if(!isMinimized)
                emit FilteredTCPData(signal, data);
        }
        else
        {
            //qDebug() << "Socket Read Before Game, signal:" << signal << "data:" << data;

            //Check for Game Starting
            if(signal == MAMESTART)
            {
                if(data == MAMEEMPTY)
                    emit EmptyGameHasStarted();
                else
                    emit GameHasStarted(data);
            }
            else if(signal == GAMESTART)
                emit GameHasStarted(data);
            else
            {
                if(signal.size() >= 3 && signal[0] == 'M' && signal[1] == 'a' && signal[2] == 'm')
                {
                    QString normalizedSignal = signal;
                    if(normalizedSignal.size() >= 5 && normalizedSignal[4] == 'P' && normalizedSignal.size() == 9)
                        normalizedSignal = PAUSE;
                    else if(normalizedSignal.size() >= 5 && normalizedSignal[4] == 'O' && normalizedSignal.size() == 15)
                        normalizedSignal.replace(MAMEORIENTATION, ORIENTATION);

                    emit DataRead(normalizedSignal, data);
                    continue;
                }

                emit DataRead(signal, data);
            }
        }
    }
}


void HookTCPSocket::Connect()
{
    stopConnecting = false;
    if(!isConnected && !isConnecting)
    {
        //qDebug() << "Waiting for a TCP Connection - Connect";

        //Set the Address for the TCP Socket
        //p_hookSocket->connectToHost (TCPHOSTNAME, TCPHOSTPORT);
        //p_hookSocket->connectToHost (QHostAddress::SpecialAddress::LocalHost, TCPHOSTPORT);
        p_hookSocket->connectToHost (QHostAddress("127.0.0.1"), TCPHOSTPORT);
        //p_hookSocket->connectToHost ("localhost", TCPHOSTPORT);

        //Start Timer for Connection
        p_waitingForConnection->start ();

        //Set the Is Connecting Bool
        isConnecting = true;

        //Wait for Connection
        p_hookSocket->waitForConnected (TIMETOWAIT);
    }
}


void HookTCPSocket::Disconnect()
{
    //Set to stop TCP Socket from trying to Connect again
    stopConnecting = true;

    p_waitingForConnection->stop();

    //Close TCP Socket
    p_hookSocket->close ();

    isConnected = false;
    isConnecting = false;

    //TCP Socket Closed, so game has Stopped
    inGame = false;
    lgOutputSig = false;
    lcOutputSig = false;
    bothOutputSig = false;
    pendingLineData.clear();
}

//Used for MultiThreading
void HookTCPSocket::SocketConnected()
{
    isConnected = true;
    isConnecting = false;

    p_waitingForConnection->stop();

    emit SocketConnectedSignal();

    //qDebug() << "TCP Socket Connected";
}

void HookTCPSocket::SocketDisconnected()
{
    isConnected = false;
    isConnecting = false;
    inGame = false;
    lgOutputSig = false;
    lcOutputSig = false;
    bothOutputSig = false;
    pendingLineData.clear();

    emit SocketDisconnectedSignal();

    //qDebug() << "TCP Socket Disconnected";

    if(!stopConnecting)
        Connect();
}


void HookTCPSocket::GameStartSocket(const QStringList &outputSignals)
{
    outputSignalsFilter = outputSignals;

    //qDebug() << "Stop Filtering Data: Sent Signal to Hooker Engine";

    inGame = true;
    lgOutputSig = true;

    if(lcOutputSig)
        CombineOutputSignals();
}


void HookTCPSocket::GameStartLight(const QStringList &outputSignals)
{
    outputSignalsLight = outputSignals;

    inGame = true;
    lcOutputSig = true;

    if(lgOutputSig)
        CombineOutputSignals();
}


/*
void HookTCPSocket::GameStopSocket()
{
    inGame = false;
    //qDebug() << "HE told TCP Game Has Stopped";
}
*/

void HookTCPSocket::WindowStateTCP(const bool &isMin)
{
    isMinimized = isMin;
}

void HookTCPSocket::TCPConnectionTimeOut()
{
    if(!isConnected && !stopConnecting && isConnecting)
    {
        if(p_hookSocket->state() != QAbstractSocket::ConnectedState)
        {
            p_hookSocket->connectToHost (QHostAddress("127.0.0.1"), TCPHOSTPORT);

            p_waitingForConnection->start ();

            //Wait for Connection
            p_hookSocket->waitForConnected (TIMETOWAIT);
        }
    }
}


void HookTCPSocket::CombineOutputSignals()
{
    quint8 i;
    quint8 lgCount = outputSignalsFilter.count();
    quint8 lcCount = outputSignalsLight.count();
    quint8 count = 0;
    QStringList tempSignals;

    //Check if LG List is Bigger
    if(lgCount > lcCount)
    {
        for(i = 0; i < lgCount; i++)
        {
            if(outputSignalsLight.contains(outputSignalsFilter[i]))
            {
                //Found in Both String List. Move to New List and Remove from Old 2 Lists
                outputSignalsBoth << outputSignalsFilter[i];
                //QString tempS = outputSignalsFilter[i];
                //outputSignalsFilter.removeOne(tempS);
                //outputSignalsLight.removeOne(tempS);
                count++;
            }
            else
                tempSignals << outputSignalsFilter[i];
        }

        //Make New Light Gun List to Regular List, Which has Both Signals Removed
        outputSignalsFilter = tempSignals;

        //Remove Both Signals from Light Controller Signals List
        for(i = 0; i < outputSignalsBoth.count(); i++)
            outputSignalsLight.removeOne(outputSignalsBoth[i]);
    }
    else
    {
        for(i = 0; i < lcCount; i++)
        {
            if(outputSignalsFilter.contains(outputSignalsLight[i]))
            {
                //Found in Both String List. Move to New List and Remove from Old 2 Lists
                outputSignalsBoth << outputSignalsLight[i];
                count++;
            }
            else
                tempSignals << outputSignalsLight[i];
        }

        //Make New Light Controller List to Regular List, Which has Both Signals Removed
        outputSignalsLight = tempSignals;

        //Remove Both Signals from Light Controller Signals List
        for(i = 0; i < outputSignalsBoth.count(); i++)
            outputSignalsFilter.removeOne(outputSignalsBoth[i]);
    }

    if(count > 0)
    {
        bothOutputSig = true;
        //qDebug() << "Both" << outputSignalsBoth;
        //qDebug() << "Light Gun" << outputSignalsFilter;
        //qDebug() << "Light Controller" << outputSignalsLight;
    }
    else
        bothOutputSig = false;

}

