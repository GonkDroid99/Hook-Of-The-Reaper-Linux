
  #include "ServiceController.h"
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
      p_comDeviceList = new ComDeviceList(this);
      p_hookerEngine  = new HookerEngine(p_comDeviceList, false, nullptr, this);

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
      p_hookerEngine->Stop();
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
