#ifndef SERVICECONTROLLER_H
#define SERVICECONTROLLER_H

#include <QObject>
#include <QSocketNotifier>
#include "HookerEngine/HookerEngine.h"
#include "COMDeviceList/ComDeviceList.h"

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
      ComDeviceList   *p_comDeviceList;
      HookerEngine    *p_hookerEngine;

      QSocketNotifier *p_sigTermNotifier;
      QSocketNotifier *p_sigIntNotifier;


  




};
#endif // SERVICECONTROLLER_H
