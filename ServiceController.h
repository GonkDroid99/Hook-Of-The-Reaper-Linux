#ifndef SERVICECONTROLLER_H
#define SERVICECONTROLLER_H

#include <QObject>
#include <QSocketNotifier>
#include "HookerEngine/HookerEngine.h"
#include "COMDeviceList/ComDeviceList.h"
#include "HardwareManager/HardwareManager.h"

class ServiceController : public QObject
{
    Q_OBJECT

      public:
      explicit ServiceController(QObject *parent = nullptr);
      ~ServiceController();

      static void setupUnixSignalHandlers();

      static int sigTermFd[2];
      static int sigIntFd[2];

    private slots:
      void handleSigTerm();
      void handleSigInt();

    private:
      // Legacy gun/profile owner used by the game engine.
      ComDeviceList   *p_comDeviceList;
      // Processes emulator TCP events and translates them to gun commands.
      HookerEngine    *p_hookerEngine;
      // Discovers physical devices and persists player assignments.
      HardwareManager *p_hardwareManager;

      // Unix signal file descriptors let Qt handle SIGTERM/SIGINT safely in
      // the event loop instead of doing shutdown work inside a signal handler.
      QSocketNotifier *p_sigTermNotifier;
      QSocketNotifier *p_sigIntNotifier;


  




};
#endif // SERVICECONTROLLER_H
