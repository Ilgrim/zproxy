//
// Created by abdess on 4/5/18.
//

#pragma once
#include "../config/pound_struct.h"
#include "../event/TimerFd.h"
#include "../event/epoll_manager.h"
#include "../http/http_stream.h"
#include "../service/ServiceManager.h"
#include "../service/backend.h"
#include "../ssl/SSLConnectionManager.h"
#include <thread>
#include <unordered_map>
#include "../stats/counter.h"
#include "../handlers/http_manager.h"

#include <vector>

#if DEBUG_STREAM_EVENTS_COUNT

struct StreamWatcher{
  HttpStream * stream{nullptr};
  StreamWatcher(HttpStream &http_stream): stream(&http_stream){
    if(stream == nullptr){
      Debug::logmsg(LOG_DEBUG,"\nIN Null HttpStream");
    }else {
      Debug::logmsg(LOG_DEBUG, "\nIN Stream data");
      showData();
    }
  }
  void showData(){
    Debug::logmsg(LOG_DEBUG, "\n\tRequest"
                             "\n\t\tBuffer size: %d"
                             "\n\t\tContent-length: %d"
                             "\n\t\tMessage bytes: %d"
                             "\n\t\tBytes left: %d"
                             "\n\tResponse"
                             "\n\t\tBuffer size: %d"
                             "\n\t\tContent-length: %d"
                             "\n\t\tMessage bytes: %d"
                             "\n\t\tBytes left: %d",
                              stream->client_connection.buffer_size,
                              stream->request.content_length,
                              stream->request.message_length,
                              stream->request.message_bytes_left,
                              stream->backend_connection.buffer_size,
                              stream->response.content_length,
                              stream->response.message_length,
                              stream->response.message_bytes_left
    );
  }
  virtual  ~StreamWatcher(){
    if(stream == nullptr){
      Debug::logmsg(LOG_DEBUG," OUT Null HttpStream");
    }else {
      Debug::logmsg(LOG_DEBUG, "\nOUT Stream data");
      showData();
    }
  }
};

namespace debug__ {
#define DEBUG_COUNTER_HIT(x) std::unique_ptr<x> UNIQUE_NAME(counter_hit) (new x)

DEFINE_OBJECT_COUNTER(on_client_connect)
DEFINE_OBJECT_COUNTER(on_backend_connect)
DEFINE_OBJECT_COUNTER(on_backend_connect_timeout)
DEFINE_OBJECT_COUNTER(on_backend_disconnect)
DEFINE_OBJECT_COUNTER(on_handshake)
DEFINE_OBJECT_COUNTER(on_request)
DEFINE_OBJECT_COUNTER(on_response)
DEFINE_OBJECT_COUNTER(on_request_timeout)
DEFINE_OBJECT_COUNTER(on_response_timeout)
DEFINE_OBJECT_COUNTER(on_send_request)
DEFINE_OBJECT_COUNTER(on_send_response)
DEFINE_OBJECT_COUNTER(on_client_disconnect)
}
#else
#define DEBUG_COUNTER_HIT(x)
#endif

using namespace events;
using namespace http;

/**
 * @class StreamManager StreamManager.h "src/stream/StreamManager.h"
 * @brief Manage the streams and the operations related with them.
 *
 * It is event-driven and in order to accomplish that it inherits from
 * EpollManager. This class is the main core of the project, managing all the
 * operations with the clients and the backends. It is used to manage both HTTP
 * and HTTPS connections.
 */
class StreamManager : public EpollManager {
#if HELLO_WORLD_SERVER
  std::string e200 =
      "HTTP/1.1 200 OK\r\nServer: zhttp 1.0\r\nExpires: now\r\nPragma: "
      "no-cache\r\nCache-control: no-cache,no-store\r\nContent-Type: "
      "text/html\r\nContent-Length: 11\r\n\r\nHello World\n";
#endif

  int worker_id{};
  std::thread worker;
  ServiceManager *service_manager{};
  ssl::SSLConnectionManager * ssl_manager{};
  Connection listener_connection;
  bool is_running{};
  ListenerConfig listener_config_;
  std::unordered_map<int, HttpStream *> streams_set;
  std::unordered_map<int, HttpStream *> timers_set;
  void HandleEvent(int fd, EVENT_TYPE event_type,
                   EVENT_GROUP event_group) override;
  void doWork();

public:
  StreamManager();
  StreamManager(const StreamManager &) = delete;
  ~StreamManager();

  /**
   * @brief Adds a HttpStream to the stream set of the StreamManager.
   *
   * If the @p fd is already stored in the set it clears the
   * older one and adds the new one. In addition sets the connect timeout
   * TimerFd.
   *
   * @param fd is the file descriptor to add.
   */
  void addStream(int fd);

  /**
   * @brief Returns the worker id associated to the StreamManager.
   *
   * As there is a StreamManager attached to each worker, this function gets the
   * worker of this StreamManager.
   *
   * @return worker_id of the StreamManager.
   */
  int getWorkerId();

  /**
   * @brief Initialize the StreamManager.
   *
   * Initialize the StreamManager with the configuration set in the
   * @p listener_config. If the listener_config is a HTTPS one, the
   * StreamManager initializes ssl::SSLConnectionManager too.
   *
   * @param listener_config from the configuration file.
   * @returns @c true if everything is fine.
   */
  bool init(ListenerConfig &listener_config);

  /**
   * @brief Starts the StreamManager event manager.
   *
   * Sets the thread name to WORKER_"{worker_id}" and call doWork().
   *
   * @param thread_id_ thread id to call functions on them.
   */
  void start(int thread_id_ = 0);

  /**
   * @brief Stops the StreamManager event manager.
   */
  void stop();

  /**
   * @brief Sets the StreamManager as a listener.
   * @param fd is the file descriptor of the connection used as a listener.
   */
  void setListenSocket(int fd);

  /**
   * @brief Handles the write event from the backend.
   *
   * It handles HTTP and HTTPS responses. If there is not any error it is
   * going to send a read event to the client or read again from the backend
   * if needed. It modifies the response headers or content when needed calling
   * validateResponse() function.
   *
   * @param fd is the file descriptor from the backend connection used to get
   * the HttpStream.
   */
  inline void onResponseEvent(int fd);

  /**
   * @brief Handles the read event from the client.
   *
   * It handles HTTP and HTTPS requests. If there is not any error it is
   * going to send a write event to the backend or read again from the client
   * if needed. It modifies the request headers or content when needed calling
   * validateRequest() function.
   *
   * @param fd is the file descriptor from the client connection used to get
   * the HttpStream.
   */
  inline void onRequestEvent(int fd);

  /**
   * @brief Handles the connect timeout event.
   *
   * This means the backend connect operation has take too long. It replies a
   * 503 service unavailable error to the client and clearStream() on
   * the HttpStream. Furthermore, it updates the backend status to
   * BACKEND_STATUS::BACKEND_DOWN.
   *
   * @param fd is the file descriptor used to get the HttpStream.
   */
  inline void onConnectTimeoutEvent(int fd);

  /**
   * @brief Handles the response timeout event.
   *
   * This means the backend take too long sending the response. It clearStream()
   * on the HttpStream and replies a 504 Gateway Timeout error to the client.
   *
   * @param fd is the file descriptor used to get the HttpStream.
   */
  inline void onResponseTimeoutEvent(int fd);

  /**
   * @brief Handles the request timeout event.
   *
   * This means the client take too long sending the request. It clearStream()
   * on the HttpStream and do not send any error to the client.
   *
   * @param fd is the file descriptor used to get the HttpStream.
   */
  inline void onRequestTimeoutEvent(int fd);
  inline void onSignalEvent(int fd);

  /**
   * @brief Writes all the client buffer data to the backend.
   *
   * If there is any error it clearStream() on the HttpStream. If not, it enables
   * the backend read event by calling enableReadEvent().
   *
   * @param stream HttpStream to get the data and the both client and backend
   * connection information.
   */
  inline void onServerWriteEvent(HttpStream *stream);

  /**
   * @brief Writes all the backend buffer data to the client.
   *
   * If there is any error it clearStrea() on the HttpStream. If not, it enables
   * the client read event by calling enableReadEvent().
   *
   * @param stream HttpStream to get the data and the both client and backend
   * connection information.
   */
  inline void onClientWriteEvent(HttpStream *stream);


  /**
   * @brief Clears the HttpStream.
   *
   * It deletes all the timers and events. Finally, deletes the HttpStream.
   *
   * @param stream is the HttpStream to clear.
   */
  void clearStream(HttpStream *stream);

  /** True if the listener is HTTPS, false if not. */
  bool is_https_listener;

};

