
  #include "ServiceController.h"
  #include "DefaultLightGunSettings.h"
  #include <QCoreApplication>
  #include <sys/socket.h>
  #include <unistd.h>
  #include <csignal>

  int ServiceController::sigTermFd[2];
  int ServiceController::sigIntFd[2];

  static void termSignalHandler(int) { char a = 1;
  ::write(ServiceController::sigTermFd[1], &a, sizeof(a)); }
  static void intSignalHandler(int)  { char a = 1; ::write(ServiceController::sigIntFd[1],
    &a, sizeof(a)); }

  ServiceController::ServiceController(QObject *parent)
      : QObject(parent)
  {
      // Initialization order matters: profile defaults must exist before the
      // legacy list loads, and the hardware manager must be connected before
      // its first discovery scan emits signals.
      // The service does not construct the GUI, so it must initialize the
      // shared built-in profiles before ComDeviceList creates any guns.
      initializeDefaultLightGunSettings();
      p_comDeviceList = new ComDeviceList(this);
      p_hardwareManager = new HardwareManager(this);
      p_hookerEngine  = new HookerEngine(p_comDeviceList, false, nullptr, p_hardwareManager, this);
      // HookerEngine owns the compatibility bridge into ComDeviceList.  Start
      // discovery only after that bridge is connected; otherwise the first
      // headless scan emits deviceConnected before anyone can apply the
      // stable serial path to the loaded legacy profile.
      p_hardwareManager->start();
      p_hookerEngine->SynchronizeHardwareDevices();

      // Wire up Unix signal sockets
      ::socketpair(AF_UNIX, SOCK_STREAM, 0, sigTermFd);
      ::socketpair(AF_UNIX, SOCK_STREAM, 0, sigIntFd);

      p_sigTermNotifier = new QSocketNotifier(sigTermFd[0], QSocketNotifier::Read, this);
      p_sigIntNotifier  = new QSocketNotifier(sigIntFd[0],  QSocketNotifier::Read, this);

      connect(p_sigTermNotifier, &QSocketNotifier::activated, this,
  &ServiceController::handleSigTerm);
      connect(p_sigIntNotifier,  &QSocketNotifier::activated, this,
  &ServiceController::handleSigInt);

      p_hookerEngine->Start();
      qInfo() << "HOTR service started. Waiting for game connection on port 8000.";
  }

  ServiceController::~ServiceController()
  {
      // Stop the game bridge before stopping discovery so no new hardware
      // events arrive while the legacy list is being destroyed.
      p_hookerEngine->Stop();
      p_hardwareManager->stop();
      qInfo() << "HOTR service stopped.";
  }

  void ServiceController::setupUnixSignalHandlers()
  {
      struct sigaction term, intr;
      term.sa_handler = termSignalHandler;
      sigemptyset(&term.sa_mask);
      term.sa_flags = SA_RESTART;
      ::sigaction(SIGTERM, &term, nullptr);

      intr.sa_handler = intSignalHandler;
      sigemptyset(&intr.sa_mask);
      intr.sa_flags = SA_RESTART;
      ::sigaction(SIGINT, &intr, nullptr);
  }

  void ServiceController::handleSigTerm()
  {
      p_sigTermNotifier->setEnabled(false);
      char tmp;
      ::read(sigTermFd[0], &tmp, sizeof(tmp));
      qInfo() << "HOTR service: SIGTERM received, shutting down.";
      QCoreApplication::quit();
  }

  void ServiceController::handleSigInt()
  {
      p_sigIntNotifier->setEnabled(false);
      char tmp;
      ::read(sigIntFd[0], &tmp, sizeof(tmp));
      qInfo() << "HOTR service: SIGINT received (Ctrl+C), shutting down.";
      QCoreApplication::quit();
  }
