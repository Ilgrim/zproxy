#include "config/config.h"
#include "ctl/ControlManager.h"
#include "stream/listener.h"
#include "util/system.h"
#include <csetjmp>
#include <csignal>

static jmp_buf jmpbuf;

// Log initilization
std::mutex Debug::log_lock;
int Debug::log_level = 6;
int Debug::log_facility = -1;

std::shared_ptr<SystemInfo> SystemInfo::instance = nullptr;

void cleanExit() { closelog(); }

void handleInterrupt(int sig) {
  Debug::logmsg(LOG_INFO, "Signal %s received, Exiting..\n",
                ::strsignal(sig));
  auto cm = ctl::ControlManager::getInstance();
  cm->stop();
//  ::longjmp(jmpbuf, 1);
}


int main(int argc, char *argv[]) {
  Listener listener;
  auto control_manager = ctl::ControlManager::getInstance();

  if (setjmp(jmpbuf)) {
    // we are in signal context here
    control_manager->stop();
    listener.stop();  
    exit(EXIT_SUCCESS);
  }

  Config config;
  Debug::logmsg(LOG_NOTICE, "zhttp starting...");
  config.parseConfig(argc, argv);  
  Debug::log_level = config.listeners->log_level;
  Debug::log_facility = config.log_facility;

  ::openlog("ZHTTP", LOG_PERROR | LOG_CONS | LOG_PID | LOG_NDELAY, LOG_DAEMON);
  // Syslog initialization
  if (config.daemonize) {
    if (!Environment::daemonize()) {
      Debug::logmsg(LOG_ERR,"error: daemonize failed\n");
      return EXIT_FAILURE;
    }
  }

  //  /* block all signals. we take signals synchronously via signalfd */
  //  sigset_t all;
  //  sigfillset(&all);
  //  sigprocmask(SIG_SETMASK,&all,NULL);

  ::signal(SIGPIPE, SIG_IGN);
  ::signal(SIGINT, handleInterrupt);
  ::signal(SIGTERM, handleInterrupt);
  ::signal(SIGABRT, handleInterrupt);
  ::signal(SIGHUP, handleInterrupt);

  ::umask(077);
  ::srandom(static_cast<unsigned int>(::getpid()));

  Environment::setUlimitData();

  /* record pid in file */
  if (config.pid_name != nullptr) {
    Environment::createPidFile(config.pid_name, ::getpid());
  }
  /* chroot if necessary */
  if (config.root_jail != nullptr) {
    Environment::setChrootRoot(config.root_jail);
  }

  /*Set process user and group*/
  if (config.user != nullptr) {
    Environment::setUid(std::string(config.user));
  }

  if (config.group != nullptr) {
    Environment::setGid(std::string(config.group));
  }

  if (config.ctrl_name != nullptr || config.ctrl_ip != nullptr) {
    control_manager->init(config);
    control_manager->start();
  }

  if(!listener.init(config.listeners[0])){
    Debug::LogInfo("Error initializing listener socket", LOG_ERR);
    return EXIT_FAILURE;
  }

  listener.start();
  std::this_thread::sleep_for(std::chrono::seconds(1));
  cleanExit();
  return EXIT_SUCCESS;
}
