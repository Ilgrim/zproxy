//
// Created by abdess on 4/5/18.
//

#include "listener.h"
#include "../ssl/ssl_session.h"
#ifdef ENABLE_HEAP_PROFILE
#include  <gperftools/heap-profiler.h>
#endif

#define DEFAULT_MAINTENANCE_INTERVAL 2000

void Listener::HandleEvent(int fd, EVENT_TYPE event_type,
                           EVENT_GROUP event_group) {
  if (event_group == EVENT_GROUP::MAINTENANCE &&
      fd == timer_maintenance.getFileDescriptor()) {
    timer_maintenance.set(listener_config.alive_to * 1000);
    updateFd(timer_maintenance.getFileDescriptor(), EVENT_TYPE::READ,
             EVENT_GROUP::MAINTENANCE);

    for (auto serv : service_manager->getServices()) {
      serv->doMaintenance();
    }
    return;
  } else if (event_group == EVENT_GROUP::SIGNAL &&
             fd == signal_fd.getFileDescriptor()) {
    Debug::logmsg(LOG_DEBUG, "Received singal %x", signal_fd.getSignal());
    if (signal_fd.getSignal() == SIGTERM) {
      stop();
      exit(EXIT_SUCCESS);
    }
    return;
  }
  switch (event_type) {
  case EVENT_TYPE::CONNECT: {
    int new_fd;
    do {
      new_fd = listener_connection.doAccept();
      if (new_fd > 0) {
        auto sm = getManager(new_fd);
        if (sm != nullptr) {
          // sm->stream_set.size() ????
          sm->addStream(new_fd);
        } else {
          Debug::LogInfo("StreamManager not found");
        }
      }
    } while (new_fd > 0);
    break;
  }
  default:
    ::close(fd);
    break;
  }
}

std::string Listener::handleTask(ctl::CtlTask &task) {
  if (!isHandler(task))
    return JSON_OP_RESULT::ERROR;

  if(task.command ==ctl::CTL_COMMAND::EXIT) {
    Debug::logmsg(LOG_REMOVE, "Exit command received");
    is_running = false;
    return JSON_OP_RESULT::OK;
  }

  switch (task.subject) {
  case ctl::CTL_SUBJECT::DEBUG: {
    std::unique_ptr<JsonObject> root{new JsonObject()};
    std::unique_ptr<JsonObject> status{new JsonObject()};
    std::unique_ptr<JsonObject> backends_stats{new JsonObject()};
    std::unique_ptr<JsonObject> clients_stats{new JsonObject()};
    std::unique_ptr<JsonObject> ssl_stats{new JsonObject()};
    std::unique_ptr<JsonObject> events_count{new JsonObject()};
#if CACHE_ENABLED
    std::unique_ptr<JsonObject> cache_count{new JsonObject()};
#endif
    status->emplace("ClientConnection",
                  std::unique_ptr<JsonDataValue>(
                      new JsonDataValue(Counter<ClientConnection>::count)));
    status->emplace("BackendConnection",
                    std::unique_ptr<JsonDataValue>(
                      new JsonDataValue(Counter<BackendConnection>::count)));
    status->emplace("HttpStream",
                    std::unique_ptr<JsonDataValue>(
                      new JsonDataValue(Counter<HttpStream>::count)));
    // root->emplace(JSON_KEYS::DEBUG, std::unique_ptr<JsonDataValue>(new
    // JsonDataValue(Counter<HttpStream>)));
    double vm, rss;
    SystemInfo::getMemoryUsed(vm, rss);
    status->emplace("VM",
                    std::unique_ptr<JsonDataValue>(
                      new JsonDataValue(vm)));
    status->emplace("RSS",
                    std::unique_ptr<JsonDataValue>(
                      new JsonDataValue(rss)));
    root->emplace("status", std::move(status));
#if DEBUG_STREAM_EVENTS_COUNT

    clients_stats->emplace("on_client_connect",
                  std::unique_ptr<JsonDataValue>(
                      new JsonDataValue(Counter<debug__::on_client_connect>::count)));
    backends_stats->emplace("on_backend_connect",
                            std::unique_ptr<JsonDataValue>(
                      new JsonDataValue(Counter<debug__::on_backend_connect>::count)));
    backends_stats->emplace("on_backend_connect_timeout",
                            std::unique_ptr<JsonDataValue>(
                      new JsonDataValue(Counter<debug__::on_backend_connect_timeout>::count)));
    ssl_stats->emplace("on_handshake",
                       std::unique_ptr<JsonDataValue>(
                      new JsonDataValue(Counter<debug__::on_handshake>::count)));
    clients_stats->emplace("on_request",
                           std::unique_ptr<JsonDataValue>(
                      new JsonDataValue(Counter<debug__::on_request>::count)));
    backends_stats->emplace("on_response",
                            std::unique_ptr<JsonDataValue>(
                      new JsonDataValue(Counter<debug__::on_response>::count)));
    clients_stats->emplace("on_request_timeout",
                           std::unique_ptr<JsonDataValue>(
                      new JsonDataValue(Counter<debug__::on_request_timeout>::count)));
    backends_stats->emplace("on_response_timeout",
                            std::unique_ptr<JsonDataValue>(
                      new JsonDataValue(Counter<debug__::on_response_timeout>::count)));
    backends_stats->emplace("on_send_request",
                            std::unique_ptr<JsonDataValue>(
                      new JsonDataValue(Counter<debug__::on_send_request>::count)));
    clients_stats->emplace("on_send_response",
                           std::unique_ptr<JsonDataValue>(
                      new JsonDataValue(Counter<debug__::on_send_response>::count)));
    clients_stats->emplace("on_client_disconnect",
                           std::unique_ptr<JsonDataValue>(
                      new JsonDataValue(Counter<debug__::on_client_disconnect>::count)));
    backends_stats->emplace("on_backend_disconnect",
                            std::unique_ptr<JsonDataValue>(
                      new JsonDataValue(Counter<debug__::on_backend_disconnect>::count)));

    events_count->emplace("client_read",
                          std::unique_ptr<JsonDataValue>(
                              new JsonDataValue(Counter<debug__::event_client_read>::count)));
    events_count->emplace("client_write",
                          std::unique_ptr<JsonDataValue>(
                              new JsonDataValue(Counter<debug__::event_client_write>::count)));
    events_count->emplace("client_disconnect",
                          std::unique_ptr<JsonDataValue>(
                              new JsonDataValue(Counter<debug__::event_client_disconnect>::count)));
    events_count->emplace("backend_read",
                          std::unique_ptr<JsonDataValue>(
                              new JsonDataValue(Counter<debug__::event_backend_read>::count)));
    events_count->emplace("backend_write",
                          std::unique_ptr<JsonDataValue>(
                              new JsonDataValue(Counter<debug__::event_backend_write>::count)));
    events_count->emplace("backend_disconnect",
                          std::unique_ptr<JsonDataValue>(
                              new JsonDataValue(Counter<debug__::event_backend_disconnect>::count)));

    events_count->emplace("event_connect",
                          std::unique_ptr<JsonDataValue>(
                              new JsonDataValue(Counter<debug__::event_connect>::count)));
    events_count->emplace("event_connect_failed",
                          std::unique_ptr<JsonDataValue>(
                              new JsonDataValue(Counter<debug__::event_connect_fail>::count)));
#if CACHE_ENABLED
#if MEMCACHED_ENABLED == 1
    RamICacheStorage * ram_storage = MemcachedCacheStorage::getInstance();
#else
    RamICacheStorage * ram_storage = RamfsCacheStorage::getInstance();
#endif
    DiskICacheStorage * disk_storage = DiskCacheStorage::getInstance();

    int ram_free = (ram_storage->max_size - ram_storage->current_size);
    int ram_used = (ram_storage->current_size);
    int disk_used = (disk_storage->current_size);

    cache_count->emplace("cache_hit",
                          std::unique_ptr<JsonDataValue>(
                              new JsonDataValue(Counter<cache_stats__::cache_match>::count)));
    cache_count->emplace("cache_ram_entries",
                          std::unique_ptr<JsonDataValue>(
                              new JsonDataValue(Counter<cache_stats__::cache_RAM_entries>::count)));
    cache_count->emplace("cache_disk_entries",
                          std::unique_ptr<JsonDataValue>(
                              new JsonDataValue(Counter<cache_stats__::cache_DISK_entries>::count)));
    cache_count->emplace("cache_staled",
                          std::unique_ptr<JsonDataValue>(
                              new JsonDataValue(Counter<cache_stats__::cache_staled_entries>::count)));
    cache_count->emplace("cache_miss",
                          std::unique_ptr<JsonDataValue>(
                              new JsonDataValue(Counter<cache_stats__::cache_miss>::count)));
    cache_count->emplace("cache_ram_free",
                          std::unique_ptr<JsonDataValue>(
                              new JsonDataValue(ram_free)));
    cache_count->emplace("cache_ram_usage",
                          std::unique_ptr<JsonDataValue>(
                              new JsonDataValue(ram_used)));
    cache_count->emplace("cache_disk_usage",
                          std::unique_ptr<JsonDataValue>(
                              new JsonDataValue(disk_used)));
    cache_count->emplace("cache_ram_mountpoint",
                          std::unique_ptr<JsonDataValue>(
                              new JsonDataValue(ram_storage->mount_path)));
    cache_count->emplace("cache_disk_mountpoint",
                          std::unique_ptr<JsonDataValue>(
                              new JsonDataValue(disk_storage->mount_path)));
    cache_count->emplace("cache_responses_not_stored",
                          std::unique_ptr<JsonDataValue>(
                              new JsonDataValue(Counter<cache_stats__::cache_not_stored>::count)));
    root->emplace("cache",std::move(cache_count));
#endif
    root->emplace("events", std::move(events_count));
    root->emplace("backends", std::move(backends_stats));
    root->emplace("clients", std::move(clients_stats));
    root->emplace("ssl", std::move(ssl_stats));

#if ENABLE_SSL_SESSION_CACHING
    root->emplace("SESSION list size",
                  std::unique_ptr<JsonDataValue>(
                      new JsonDataValue(static_cast<int>(ssl::SslSessionManager::getInstance()->sessions.size()))));
#endif
#endif


    /*
struct debug_status{};
struct on_none:debug_status, Counter<on_none>{};
struct on_connect:debug_status, Counter<on_connect>{};
struct on_backend_connect:debug_status, Counter<on_backend_connect>{};
struct on_backend_disconnect:debug_status, Counter<on_backend_disconnect>{};
struct on_connect_timeout:debug_status, Counter<on_connect_timeout>{};
struct on_handshake: debug_status, Counter<on_handshake>{};
struct on_request:debug_status, Counter<on_request>{};
struct on_response:debug_status, Counter<on_response>{};
struct on_request_timeout:debug_status, Counter<on_request_timeout>{};
struct on_response_timeout:debug_status, Counter<on_response_timeout>{};
struct on_send_request:debug_status, Counter<on_send_request>{};
struct on_send_response:debug_status, Counter<on_send_response>{};
struct on_client_disconnect:debug_status, Counter<on_client_disconnect>{};*/


    return root->stringify();
  }
  default: { return JSON_OP_RESULT::ERROR; }
  }
}

bool Listener::isHandler(ctl::CtlTask &task) {
  return (task.target == ctl::CTL_HANDLER_TYPE::LISTENER || task.target == ctl::CTL_HANDLER_TYPE::ALL);
}

bool Listener::init(std::string address, int port) {
  return listener_connection.listen(address, port);
  // handleAccept(listener_connection.getFileDescriptor());
}

Listener::Listener()
    : is_running(false), listener_connection(), stream_manager_set() {
}

Listener::~Listener() {
  Debug::logmsg(LOG_REMOVE, "Destructor");
  is_running = false;
  for (auto &sm : stream_manager_set) {
    sm.second->stop();
    delete sm.second;
  }
#ifdef ENABLE_HEAP_PROFILE
  HeapProfilerDump("Heap profile data");
  HeapProfilerStop();
#endif

}

void Listener::doWork() {
  while (is_running) {
    if (loopOnce(EPOLL_WAIT_TIMEOUT) <= 0) {
      // something bad happend
      //      Debug::LogInfo("No event received");
    }
  }
    Debug::logmsg(LOG_REMOVE, "Exiting loop");
}

void Listener::stop() {
  is_running = false;
  if(worker_thread.joinable()) worker_thread.join();
  ctl::ControlManager::getInstance()->deAttach(std::ref(*this));
}

void Listener::start() {
  auto cm = ctl::ControlManager::getInstance();
  cm->attach(std::ref(*this));
  auto concurrency_level = std::thread::hardware_concurrency() < 2
                               ? 2
                               : std::thread::hardware_concurrency();
  auto numthreads = Config::numthreads != 0 ?  Config::numthreads : concurrency_level;
  for (int sm = 0; sm < numthreads ; sm++) {
      stream_manager_set[sm] = new StreamManager();
  }
  int service_id = 0;

  for (auto service_config = listener_config.services;
       service_config != nullptr; service_config = service_config->next) {
    if (!service_config->disabled) {
      ServiceManager::getInstance(listener_config)
          ->addService(*service_config, service_id++);
    } else {
      Debug::LogInfo("Backend " + std::string(service_config->name) +
                         " disabled in config file",
                     LOG_NOTICE);
    }
  }
#ifdef ENABLE_HEAP_PROFILE
  HeapProfilerStart("/tmp/zhttp");
#endif

  for (int i = 0; i < stream_manager_set.size(); i++) {
    auto sm = stream_manager_set[i];
    if (sm != nullptr && sm->init(listener_config)) {
      sm->setListenSocket(listener_connection.getFileDescriptor());
      sm->start(i);
    } else {
      Debug::LogInfo("StreamManager id doesn't exist : " + std::to_string(i),
                     LOG_ERR);
    }
  }
  is_running = true;
  //  worker_thread = std::thread([this] { doWork(); });
  //  helper::ThreadHelper::setThreadAffinity(
  //      0, pthread_self());  // worker_thread.native_handle());
  //#if SM_HANDLE_ACCEPT
  //  while (is_running) {
  //    std::this_thread::sleep_for(std::chrono::seconds(5));
  //  }
  //#else
  signal_fd.init();
  timer_maintenance.set(DEFAULT_MAINTENANCE_INTERVAL);
  //  addFd(signal_fd.getFileDescriptor(), EVENT_TYPE::READ,
  //  EVENT_GROUP::SIGNAL);
  addFd(timer_maintenance.getFileDescriptor(), EVENT_TYPE::READ,
        EVENT_GROUP::MAINTENANCE);

  helper::ThreadHelper::setThreadName("LISTENER", pthread_self());
  doWork();
  //#endif
}

StreamManager *Listener::getManager(int fd) {
  static unsigned long c;
  ++c;
  unsigned long id = c % stream_manager_set.size();
  return stream_manager_set[id];
}

bool Listener::init(ListenerConfig &config) {
  listener_config = config;
  service_manager = ServiceManager::getInstance(listener_config);

  if (!listener_connection.listen(listener_config.address, listener_config.port))
    return false;
#if SM_HANDLE_ACCEPT
  return true;
#else
  return handleAccept(listener_connection.getFileDescriptor());
#endif
}
