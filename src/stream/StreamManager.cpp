//
// Created by abdess on 4/5/18.
//

#include "StreamManager.h"
#include "../handlers/HttpsManager.h"
#include "../handlers/zlib_util.h"
#include "../util/Network.h"
#include "../util/common.h"
#include "../util/utils.h"
#include <cstdio>
#include <functional>
#if HELLO_WORLD_SERVER
void StreamManager::HandleEvent(int fd, EVENT_TYPE event_type,
                                EVENT_GROUP event_group) {
  switch (event_type) {
  case READ_ONESHOT: {
    HttpStream *stream = streams_set[fd];
    if (stream == nullptr) {
      stream = new HttpStream();
      stream->client_connection.setFileDescriptor(fd);
      streams_set[fd] = stream;
    }
    auto connection = stream->getConnection(fd);
    connection->read();
    connection->buffer_size = 1; // reset buffer size to avoid buffer
                                 // overflow due to not consuming buffer
                                 // data.
    updateFd(fd, EVENT_TYPE::WRITE, EVENT_GROUP::CLIENT);
    break;
  }

  case READ: {
    HttpStream *stream = streams_set[fd];
    if (stream == nullptr) {
      stream = new HttpStream();
      stream->client_connection.setFileDescriptor(fd);
      streams_set[fd] = stream;
    }
    auto connection = stream->getConnection(fd);
    connection->read();
    connection->buffer_size = 1;
    updateFd(fd, EVENT_TYPE::WRITE, EVENT_GROUP::CLIENT);
  }

  case WRITE: {
    auto stream = streams_set[fd];
    if (stream == nullptr) {
      Debug::LogInfo("Connection closed prematurely" + std::to_string(fd));
      return;
    }
    auto io_result = stream->client_connection.write(this->e200.c_str(),
                                                     this->e200.length());
    switch (io_result) {
    case IO::ERROR:
    case IO::FD_CLOSED:
    case IO::FULL_BUFFER:
      Debug::LogInfo("Something happend sentid e200", LOG_DEBUG);
      break;
    case IO::SUCCESS:
    case IO::DONE_TRY_AGAIN:
      updateFd(fd, READ, EVENT_GROUP::CLIENT);
      break;
    }

    break;
  }
  case CONNECT: {
    int new_fd;
    //      do {
    new_fd = listener_connection.doAccept();
    if (new_fd > 0) {
      addStream(new_fd);
    }
    //      } while (new_fd > 0);
    return;
  }
  case ACCEPT:
    break;
  case DISCONNECT: {
    auto stream = streams_set[fd];
    if (stream == nullptr) {
      Debug::LogInfo("Stream doesn't exist for " + std::to_string(fd));
      deleteFd(fd);
      ::close(fd);
      return;
    }
    /*      streams_set.erase(fd);
    delete stream*/
    ;
    clearStream(stream);
    break;
  }
  }
}
#else
void StreamManager::HandleEvent(int fd, EVENT_TYPE event_type,
                                EVENT_GROUP event_group) {
  switch (event_type) {
#if SM_HANDLE_ACCEPT
  case EVENT_TYPE::CONNECT: {
    DEBUG_COUNTER_HIT(debug__::event_connect);
    int new_fd;
    do {
        new_fd = listener_connection.doAccept();
        if (new_fd > 0) {
            addStream(new_fd);
        } else {
            DEBUG_COUNTER_HIT(debug__::event_connect_fail);
        }
    } while (new_fd > 0);
    return;
  }
#endif
  case EVENT_TYPE::READ:
  case EVENT_TYPE::READ_ONESHOT: {
    switch (event_group) {
    case EVENT_GROUP::ACCEPTOR:break;
    case EVENT_GROUP::SERVER: {
      DEBUG_COUNTER_HIT(debug__::event_backend_read);
      onResponseEvent(fd);
      break;
    }
    case EVENT_GROUP::CLIENT: {
      DEBUG_COUNTER_HIT(debug__::event_client_read);
      onRequestEvent(fd);
      break;
    }
    case EVENT_GROUP::CONNECT_TIMEOUT:onConnectTimeoutEvent(fd);
      break;
    case EVENT_GROUP::REQUEST_TIMEOUT:onRequestTimeoutEvent(fd);
      break;
    case EVENT_GROUP::RESPONSE_TIMEOUT:onResponseTimeoutEvent(fd);
      break;
    case EVENT_GROUP::SIGNAL:
      onSignalEvent(fd);
      break;
    case EVENT_GROUP::MAINTENANCE:break;
    default:
      deleteFd(fd);
      close(fd);
      break;
    }
    return;
  }
  case EVENT_TYPE::WRITE: {
    auto stream = streams_set[fd];
    if (stream == nullptr) {
      switch (event_group) {
      case EVENT_GROUP::ACCEPTOR:break;
      case EVENT_GROUP::SERVER:
        Debug::LogInfo("SERVER_WRITE : Stream doesn't exist for " +
            std::to_string(fd));
        break;
      case EVENT_GROUP::CLIENT:
        Debug::LogInfo("CLIENT_WRITE : Stream doesn't exist for " +
            std::to_string(fd));
        break;
      default:break;
      }
      deleteFd(fd);
      ::close(fd);
      return;
    }

    switch (event_group) {
    case EVENT_GROUP::ACCEPTOR:
      break;
    case EVENT_GROUP::SERVER: {
      DEBUG_COUNTER_HIT(debug__::event_backend_write);
      onServerWriteEvent(stream);
      break;
    }
    case EVENT_GROUP::CLIENT: {
      DEBUG_COUNTER_HIT(debug__::event_client_write);
      onClientWriteEvent(stream);
      break;
    }
    default: {
      deleteFd(fd);
      ::close(fd);
    }
    }

    return;
  }
  case EVENT_TYPE::DISCONNECT: {
    auto stream = streams_set[fd];
    if (stream == nullptr) {
      Debug::LogInfo("Remote host closed connection prematurely ", LOG_INFO);
      deleteFd(fd);
      ::close(fd);
      return;
    }
    switch (event_group) {
    case EVENT_GROUP::SERVER: {
      onServerDisconnect(stream);
      return;
    }
    case EVENT_GROUP::CLIENT: {
      onClientDisconnect(stream);
      return;
    }
    default:
      Debug::LogInfo("Why this happends!!", LOG_DEBUG);
      break;
    }
    clearStream(stream);
    break;
  }
  default:
    Debug::LogInfo("Unexpected  event type", LOG_DEBUG);
    deleteFd(fd);
    ::close(fd);
  }
}
#endif

void StreamManager::stop() { is_running = false; }

void StreamManager::start(int thread_id_) {
  ctl::ControlManager::getInstance()->attach(std::ref(*this));
  is_running = true;
  worker_id = thread_id_;
  this->worker = std::thread([this] { doWork(); });
  if (worker_id >= 0) {
    //    helper::ThreadHelper::setThreadAffinity(worker_id,
    //    worker.native_handle());
    helper::ThreadHelper::setThreadName("WORKER_" + std::to_string(worker_id),
                                        worker.native_handle());
  }
#if SM_HANDLE_ACCEPT
  handleAccept(listener_connection.getFileDescriptor());
#endif
}

StreamManager::StreamManager()
    : is_https_listener(false){
          // TODO:: do attach for config changes
      };

StreamManager::~StreamManager() {
  Debug::logmsg(LOG_REMOVE, "Destructor");
  stop();
  if (worker.joinable())
    worker.join();
  for (auto &key_pair : streams_set) {
    delete key_pair.second;
  }
}
void StreamManager::doWork() {
  while (is_running) {
    if (loopOnce(EPOLL_WAIT_TIMEOUT) <= 0) {
      // something bad happend
      //      Debug::LogInfo("No events !!");
    }
    // if(needMainatance)
    //    doMaintenance();
  }
  Debug::logmsg(LOG_REMOVE, "Exiting loop");
}

void StreamManager::addStream(int fd) {
  DEBUG_COUNTER_HIT(debug__::on_client_connect);
#if SM_HANDLE_ACCEPT
  HttpStream *stream = streams_set[fd];
  if (UNLIKELY(stream != nullptr)) {
    clearStream(stream);
  }
  stream = new HttpStream();
  stream->client_connection.setFileDescriptor(fd);
  streams_set[fd] = stream;
  stream->timer_fd.set(listener_config_.to * 1000);
  addFd(stream->timer_fd.getFileDescriptor(), EVENT_TYPE::TIMEOUT,
        EVENT_GROUP::REQUEST_TIMEOUT);
  timers_set[stream->timer_fd.getFileDescriptor()] = stream;
  stream->client_connection.enableEvents(this, EVENT_TYPE::READ,
                                         EVENT_GROUP::CLIENT);

  // set extra header to forward to the backends
  stream->request.addHeader(http::HTTP_HEADER_NAME::X_FORWARDED_FOR,
                            stream->client_connection.getPeerAddress(), true);
  if (listener_config_.add_head != NULL) {
    stream->request.addHeader(listener_config_.add_head, true);
  }
  if (this->is_https_listener) {
    stream->client_connection.ssl_conn_status = ssl::SSL_STATUS::NEED_HANDSHAKE;
  }
// configurar
#else
  if (!this->addFd(fd, READ, EVENT_GROUP::CLIENT)) {
    Debug::LogInfo("Error adding to epoll manager", LOG_NOTICE);
  }
#endif
}

int StreamManager::getWorkerId() { return worker_id; }

void StreamManager::onRequestEvent(int fd) {
  HttpStream *stream = streams_set[fd];
  if (stream != nullptr) {
    if (stream->client_connection.isCancelled()) {
      clearStream(stream);
      return;
    }
    if (UNLIKELY(fd != stream->client_connection.getFileDescriptor())) {
      Debug::LogInfo("FOUND:: Aqui ha pasado algo raro!!", LOG_REMOVE);
    }
  } else {
#if !SM_HANDLE_ACCEPT
    stream = new HttpStream();
    stream->client_connection.setFileDescriptor(fd);
    streams_set[fd] = stream;
    if (fd != stream->client_connection.getFileDescriptor()) {
      Debug::LogInfo("FOUND:: Aqui ha pasado algo raro!!", LOG_DEBUG);
    }
#endif
    deleteFd(fd);
    ::close(fd);
    return;
  }
#if PRINT_DEBUG_FLOW_BUFFERS
  Debug::logmsg(
      LOG_REMOVE, "IN buffer size: %8lu\tContent-length: %lu\tleft: %lu",
      stream->client_connection.buffer_size, stream->request.content_length,
      stream->request.message_bytes_left);
#endif
  IO::IO_RESULT result = IO::IO_RESULT::ERROR;
  if (this->is_https_listener) {
#if USE_SSL_BIO_BUFFER
    result = this->ssl_manager->handleDataRead(stream->client_connection);
#else
    result = this->ssl_manager->sslRead(stream->client_connection);
#endif
  } else {
    result = stream->client_connection.read();
  }

  switch (result) {
  case IO::IO_RESULT::SSL_HANDSHAKE_ERROR:
  case IO::IO_RESULT::SSL_NEED_HANDSHAKE: {
    if (!this->ssl_manager->handleHandshake(stream->client_connection)) {
      if ((ERR_GET_REASON(ERR_peek_error()) == SSL_R_HTTP_REQUEST) &&
          (ERR_GET_LIB(ERR_peek_error()) == ERR_LIB_SSL)) {
        /* the client speaks plain HTTP on our HTTPS port */
        Debug::logmsg(LOG_NOTICE,
                      "Client %s sent a plain HTTP message to an SSL port",
                      stream->client_connection.getPeerAddress().c_str());
        if (listener_config_.nossl_redir > 0) {
          Debug::logmsg(LOG_NOTICE,
                        "(%lx) errNoSsl from %s redirecting to \"%s\"",
                        pthread_self(),
                        stream->client_connection.getPeerAddress().c_str(),
                        listener_config_.nossl_url);
          stream->replyRedirect(listener_config_.nossl_redir,
                                listener_config_.nossl_url);
        } else {
          Debug::logmsg(LOG_NOTICE, "(%lx) errNoSsl from %s sending error",
                        pthread_self(),
                        stream->client_connection.getPeerAddress().c_str());
          stream->replyError(
              HttpStatus::Code::BadRequest,
              HttpStatus::reasonPhrase(HttpStatus::Code::BadRequest).c_str(),
              listener_config_.errnossl, this->listener_config_,
              *this->ssl_manager);
        }
      } else {
        Debug::logmsg(LOG_INFO, "Handshake error with %s ",
                      stream->client_connection.getPeerAddress().c_str());
      }
      clearStream(stream);
      return;
    }
    if (stream->client_connection.ssl_connected)
        {
            DEBUG_COUNTER_HIT(debug__::on_handshake);
            httpsHeaders(stream, ssl_manager, listener_config_.clnt_check);
            stream->backend_connection.server_name =
                stream->client_connection.server_name;
        }
    stream->client_connection.enableReadEvent();
    return;
  }
  case IO::IO_RESULT::SUCCESS:
  case IO::IO_RESULT::DONE_TRY_AGAIN:
  case IO::IO_RESULT::FULL_BUFFER:
    break;
  case IO::IO_RESULT::ZERO_DATA:
    return;
  case IO::IO_RESULT::FD_CLOSED:return;
  case IO::IO_RESULT::ERROR:
  case IO::IO_RESULT::CANCELLED:
  default: {
    Debug::LogInfo("Error reading request ", LOG_DEBUG);
    clearStream(stream);
    return;
  }
  }

  DEBUG_COUNTER_HIT(debug__::on_request);
  if (stream->upgrade.pinned_connection || stream->request.hasPendingData()) {
    // TODO:: maybe quick response
#if PRINT_DEBUG_FLOW_BUFFERS
    Debug::logmsg(
        LOG_REMOVE,
        "OUT buffer size: %8lu\tContent-length: %lu\tleft: %lu\tIO: %s",
        stream->client_connection.buffer_size, stream->request.content_length,
        stream->request.message_bytes_left, IO::getResultString(result).data());
#endif
    stream->backend_connection.enableWriteEvent();
    return;
  }
#if PRINT_DEBUG_FLOW_BUFFERS
  Debug::logmsg(
      LOG_DEBUG,
      "IN buffer size: %8lu\tContent-length: %lu\tleft: %lu\tIO: "
      "%s Header sent: %s",
      stream->client_connection.buffer_size, stream->request.content_length,
      stream->request.message_bytes_left, IO::getResultString(result).data(),
      stream->request.headers_sent ? "true" : "false");
#endif
  size_t parsed = 0;
  http_parser::PARSE_RESULT parse_result;
  // do {
  parse_result = stream->request.parseRequest(
      stream->client_connection.buffer, stream->client_connection.buffer_size,
      &parsed); // parsing http data as response structured

  switch (parse_result) {
  case http_parser::PARSE_RESULT::SUCCESS: {
    auto valid =
        http_manager::validateRequest(stream->request, listener_config_);
    if (UNLIKELY(validation::REQUEST_RESULT::OK != valid)) {
      stream->replyError(HttpStatus::Code::NotImplemented,
                         validation::request_result_reason.at(valid).c_str(),
                         listener_config_.err501, this->listener_config_,
                         *this->ssl_manager);
      this->clearStream(stream);
      return;
    }

    stream->timer_fd.unset();
    deleteFd(stream->timer_fd.getFileDescriptor());
    timers_set[stream->timer_fd.getFileDescriptor()] = nullptr;
    auto service = service_manager->getService(stream->request);
    if (service == nullptr) {
      stream->replyError(HttpStatus::Code::ServiceUnavailable,
                         validation::request_result_reason
                             .at(validation::REQUEST_RESULT::SERVICE_NOT_FOUND)
                             .c_str(),
                         listener_config_.err503, this->listener_config_,
                         *this->ssl_manager);
      this->clearStream(stream);
      return;
    }

    stream->request.setService(service);
#if CACHE_ENABLED
    // If the cache is enabled and the request is cached and it is also fresh
    if (service->cache_enabled) {
        std::chrono::steady_clock::time_point current_time = std::chrono::steady_clock::now();
        duration<long> time_span = duration_cast<duration<long>>(current_time - stream->prev_time);
        stream->prev_time = current_time;
        stream->current_time += time_span.count();
        service->t_stamp = stream->current_time;
        service->validateCacheRequest(stream->request);
        if (service->canBeServedFromCache(stream->request) != nullptr) {
            DEBUG_COUNTER_HIT(cache_stats__::cache_match);
            stream->response.reset_parser();
            if ( service->getResponseFromCache(stream->request, stream->response, stream->backend_connection.str_buffer) == 0){
                http_manager::validateResponse(*stream, listener_config_);

                if (http::http_info::http_verbs.at(std::string(
                                                       stream->request.method, stream->request.method_len)) ==
                        http::REQUEST_METHOD::HEAD) {
                    // If HTTP verb is HEAD, just send headers
                    stream->response.buffer_size =
                            stream->response.buffer_size - stream->response.message_length;
                    stream->response.message = nullptr;
                    stream->response.message_length = 0;
                    stream->response.message_bytes_left = 0;
                }
                stream->client_connection.buffer_size = 0;
                stream->request.setHeaderSent(false);
                stream->backend_connection.buffer_size = stream->response.buffer_size;
                stream->client_connection.enableWriteEvent();
                return;
            }
        }
        if (stream->request.c_opt.only_if_cached ) {
            // If the directive only-if-cached is in the request and the content
            // is not cached, reply an error 504 as stated in the rfc7234
            stream->replyError(
                        HttpStatus::Code::GatewayTimeout,
                        HttpStatus::reasonPhrase(HttpStatus::Code::GatewayTimeout).c_str(),
                        "", this->listener_config_, *this->ssl_manager);
            this->clearStream(stream);
            return;
        }
        DEBUG_COUNTER_HIT(cache_stats__::cache_miss);
        service_manager->getService(stream->request)->stats.cache_miss++;
        stream->response.reset_parser();
        stream->response.cached = false;
        stream->response.setHeaderSent(false);
        stream->backend_connection.buffer_size = 0;
    }

#endif
    auto bck = service->getBackend(*stream);
    if (bck == nullptr) {
      // No backend available
      stream->replyError(HttpStatus::Code::ServiceUnavailable,
                         validation::request_result_reason
                             .at(validation::REQUEST_RESULT::BACKEND_NOT_FOUND)
                             .c_str(),
                         listener_config_.err503, this->listener_config_,
                         *this->ssl_manager);
      this->clearStream(stream);
      return;
    } else {
      IO::IO_OP op_state = IO::IO_OP::OP_ERROR;
      static size_t total_request;
      total_request++;
      if (stream->backend_connection.buffer_size == 0) {//FIXME:: should not be necessary
        stream->response.reset_parser();
        stream->backend_connection.buffer_size = 0;
      }
      Debug::logmsg(
          LOG_DEBUG, "%lu [%s] %.*s [%s (%d) -> %s (%d)]", total_request, service->name.c_str(),
          stream->request.getRequestLine().length() - 2,
          stream->request.getRequestLine().data(),
          stream->client_connection.getPeerAddress().c_str(),
          stream->client_connection.getFileDescriptor(), bck->address.c_str(),
          stream->backend_connection.getFileDescriptor());
      switch (bck->backend_type) {
      case BACKEND_TYPE::REMOTE: {
        if (stream->backend_connection.getBackend() == nullptr ||
            !stream->backend_connection.isConnected()) {
          // null
          if (stream->backend_connection.getFileDescriptor() > 0) { //

            deleteFd(stream->backend_connection
                         .getFileDescriptor()); // Client cannot
            // be connected to more
            // than one backend at
            // time
            streams_set.erase(stream->backend_connection.getFileDescriptor());
            stream->backend_connection.closeConnection();
            if (stream->backend_connection.isConnected())
              stream->backend_connection.getBackend()->decreaseConnection();
          }
          stream->backend_connection.setBackend(bck);
          stream->backend_connection.time_start =
              std::chrono::steady_clock::now();
          op_state = stream->backend_connection.doConnect(*bck->address_info,
                                                          bck->conn_timeout);
          switch (op_state) {
          case IO::IO_OP::OP_ERROR: {
            Debug::logmsg(LOG_NOTICE, "Error connecting to backend %s", bck->address.data());
            stream->replyError(
                HttpStatus::Code::ServiceUnavailable,
                HttpStatus::reasonPhrase(HttpStatus::Code::ServiceUnavailable)
                    .c_str(),
                listener_config_.err503, this->listener_config_, *this->ssl_manager);
            stream->backend_connection.getBackend()->status =
                BACKEND_STATUS::BACKEND_DOWN;
            stream->backend_connection.closeConnection();
            clearStream(stream);
            return;
          }

          case IO::IO_OP::OP_IN_PROGRESS: {
            stream->timer_fd.set(bck->conn_timeout * 1000);
            stream->backend_connection.getBackend()->increaseConnTimeoutAlive();
            timers_set[stream->timer_fd.getFileDescriptor()] = stream;
            addFd(stream->timer_fd.getFileDescriptor(), EVENT_TYPE::READ,
                  EVENT_GROUP::CONNECT_TIMEOUT);
            if (stream->backend_connection.getBackend()->nf_mark > 0)
              Network::setSOMarkOption(
                  stream->backend_connection.getFileDescriptor(),
                  stream->backend_connection.getBackend()->nf_mark);
          }
          case IO::IO_OP::OP_SUCCESS: {
            DEBUG_COUNTER_HIT(debug__::on_backend_connect);
            stream->backend_connection.getBackend()->increaseConnection();
            streams_set[stream->backend_connection.getFileDescriptor()] =
                stream;
            /*
                        if
               (stream->backend_connection.getBackend()->backend_config.ctx !=
               nullptr)
                          ssl_manager->init(stream->backend_connection.getBackend()->backend_config);
            */
            stream->backend_connection.enableEvents(this, EVENT_TYPE::WRITE,
                                                    EVENT_GROUP::SERVER);
            break;
          }
          }
        }

        // Rewrite destination
        if (stream->request.add_destination_header) {
          std::string header_value = "http://";
          header_value += stream->backend_connection.getPeerAddress();
          header_value += ':';
          header_value += stream->request.path;
          stream->request.addHeader(http::HTTP_HEADER_NAME::DESTINATION,
                                    header_value);
        }
        if (!stream->request.host_header_found) {
          std::string header_value = "";
          header_value += stream->backend_connection.getBackend()->address;
          header_value += ':';
          header_value +=
              std::to_string(stream->backend_connection.getBackend()->port);
          stream->request.addHeader(http::HTTP_HEADER_NAME::HOST, header_value);
        }
        /* After setting the backend and the service in the first request,
         * pin the connection if the PinnedConnection service config parameter
         * is true. Note: The first request must be HTTP. */
        if (service->service_config.pinned_connection) {
          stream->upgrade.pinned_connection = true;
        }

        stream->backend_connection.enableWriteEvent();
        break;
      }
      case BACKEND_TYPE::EMERGENCY_SERVER:

        break;
      case BACKEND_TYPE::REDIRECT: {
        /*Check redirect request type ::> 0 - redirect is absolute, 1 -
         * the redirect should include the request path, or 2 if it should
         * use perl dynamic replacement */
        //              switch (bck->backend_config.redir_req) {
        //                case 1:
        //
        //                  break;
        //                case 2: break;
        //                case 0:
        //                default: break;
        //              }
        stream->replyRedirect(bck->backend_config);
        clearStream(stream);
        return;
      }
      case BACKEND_TYPE::CACHE_SYSTEM:
        break;
      }
    }
    break;
  }
  case http_parser::PARSE_RESULT::TOOLONG:
    Debug::LogInfo("Parser TOOLONG", LOG_DEBUG);
  case http_parser::PARSE_RESULT::FAILED:
    stream->replyError(
        HttpStatus::Code::BadRequest,
        HttpStatus::reasonPhrase(HttpStatus::Code::BadRequest).c_str(),
        listener_config_.err501, this->listener_config_, *this->ssl_manager);
    this->clearStream(stream);
    return;
  case http_parser::PARSE_RESULT::INCOMPLETE:
    Debug::LogInfo("Parser INCOMPLETE", LOG_DEBUG);
    stream->client_connection.enableReadEvent();
    return;
  }

  /*if ((stream->client_connection.buffer_size - parsed) > 0) {
    Debug::LogInfo("Buffer size: left size: " +
        std::to_string(stream->client_connection.buffer_size),
                   LOG_DEBUG);
    Debug::LogInfo("Current request buffer: \n " +
        std::string(stream->client_connection.buffer,
                    stream->client_connection.buffer_size),
                   LOG_DEBUG);
    Debug::LogInfo("Parsed data size: " + std::to_string(parsed), LOG_DEBUG);
  }
*/
  //} while (stream->client_connection.buffer_size > parsed &&
  //  parse_result ==
  //     http_parser::PARSE_RESULT::SUCCESS);

  stream->client_connection.enableReadEvent();
}

void StreamManager::onResponseEvent(int fd) {
  HttpStream *stream = streams_set[fd];
  if (stream == nullptr) {
    Debug::LogInfo("Backend Connection, Stream closed", LOG_DEBUG);
    deleteFd(fd);
    ::close(fd);
    return;
  }
  if (UNLIKELY(stream->client_connection
                   .isCancelled() || stream->backend_connection.isCancelled())) { // check if client is still active
    clearStream(stream);
    return;
  }
  if(stream->backend_connection.buffer_size == MAX_DATA_SIZE)
  {
      stream->client_connection.enableWriteEvent();
      return;
  }
#if PRINT_DEBUG_FLOW_BUFFERS
  Debug::logmsg(LOG_REMOVE,
                "fd:%d IN\tbuffer size: %8lu\tContent-length: %lu\tleft: %lu "
                "header_sent: %s chunk left: %d",
                stream->backend_connection.getFileDescriptor(),
                stream->backend_connection.buffer_size,
                stream->response.content_length,
                stream->response.message_bytes_left,
                stream->response.getHeaderSent() ? "true" : "false", stream->response.chunk_size_left);
#endif
  // disable response timeout timerfd
  if (stream->backend_connection.getBackend()->response_timeout > 0) {
    stream->timer_fd.unset();
    events::EpollManager::deleteFd(stream->timer_fd.getFileDescriptor());
  }
//  if (stream->backend_connection.buffer_size > 0) {
//    stream->client_connection.enableWriteEvent();
//    return;
//  }
  DEBUG_COUNTER_HIT(debug__::on_response);
  IO::IO_RESULT result;

  if (stream->backend_connection.getBackend()->isHttps()) {
#if USE_SSL_BIO_BUFFER
    result =
        stream->backend_connection.getBackend()->ssl_manager.handleDataRead(
            stream->backend_connection);
#else
    result =
        stream->backend_connection.getBackend()->ssl_manager.sslRead(
            stream->backend_connection);
#endif
  } else {
#if ENABLE_ZERO_COPY
    if (stream->response.message_bytes_left > 0 &&
        !stream->backend_connection.getBackend()->isHttps() &&
        !this->is_https_listener
        /*&& stream->response.transfer_encoding_header*/) {
      result = stream->backend_connection.zeroRead();
      if (result == IO::IO_RESULT::ERROR) {
        Debug::LogInfo("Error reading response ", LOG_DEBUG);
        clearStream(stream);
        return;
      }
// TODO::Evaluar
#if ENABLE_QUICK_RESPONSE
      result = stream->backend_connection.zeroWrite(
          stream->client_connection.getFileDescriptor(), stream->response);
      switch (result) {
      case IO::IO_RESULT::FD_CLOSED:
      case IO::IO_RESULT::ERROR: {
        Debug::LogInfo("Error Writing response ", LOG_NOTICE);
        clearStream(stream);
        return;
      }
      case IO::IO_RESULT::SUCCESS:
        return;
      case IO::IO_RESULT::DONE_TRY_AGAIN:
        stream->client_connection.enableWriteEvent();
        return;
      case IO::IO_RESULT::FULL_BUFFER:
        break;
      }
#endif
    } else
#endif
      result = stream->backend_connection.read();
  }
#if PRINT_DEBUG_FLOW_BUFFERS
  Debug::logmsg(LOG_REMOVE,
                "fd:%d IN\tbuffer size: %8lu\tContent-length: %lu\tleft: %lu "
                "header_sent: %s chunk_size_left: %d IO RESULT: %s",
                stream->backend_connection.getFileDescriptor(),
                stream->backend_connection.buffer_size,
                stream->response.content_length,
                stream->response.message_bytes_left,
                stream->response.getHeaderSent() ? "true" : "false", stream->response.chunk_size_left,
                IO::getResultString(result).data());
#endif
  switch (result) {
  case IO::IO_RESULT::SSL_HANDSHAKE_ERROR:
  case IO::IO_RESULT::SSL_NEED_HANDSHAKE: {
    stream->backend_connection.server_name = stream->client_connection.server_name;
    if (!stream->backend_connection.getBackend()->ssl_manager.handleHandshake(
            stream->backend_connection, true)) {
      Debug::logmsg(LOG_INFO, "Backend handshake error with %s ",
                    stream->backend_connection.address_str.c_str());
      stream->replyError(
          HttpStatus::Code::ServiceUnavailable,
          HttpStatus::reasonPhrase(HttpStatus::Code::ServiceUnavailable)
              .c_str(),
          listener_config_.err503, this->listener_config_, *this->ssl_manager);
      clearStream(stream);
    }
    if (stream->backend_connection.ssl_connected) {
      stream->backend_connection.enableWriteEvent();
    }
    return;
  }
  case IO::IO_RESULT::ZERO_DATA:
  case IO::IO_RESULT::SUCCESS:
  case IO::IO_RESULT::DONE_TRY_AGAIN: {
    if (stream->backend_connection.buffer_size == 0) {
      stream->backend_connection.enableReadEvent();
      return;
    }
    break;
  }
  case IO::IO_RESULT::FULL_BUFFER:
  case IO::IO_RESULT::FD_CLOSED:
    if (stream->backend_connection.buffer_size > 0)
      break;
    else
      return;
  case IO::IO_RESULT::ERROR:
  case IO::IO_RESULT::CANCELLED:
  default: {
//    Debug::LogInfo("Closing backend connection ", LOG_DEBUG);
    clearStream(stream);
    return;
  }
  }
  // TODO::FERNANDO::REPASAR, toma de muestras de tiempo, solo se debe de
  // tomar muestra si se la lectura ha sido success.
  stream->backend_connection.getBackend()->calculateLatency(
      std::chrono::duration_cast<std::chrono::duration<double>>(
          std::chrono::steady_clock::now() -
          stream->backend_connection.time_start)
          .count());
  //TODO:  stream->backend_stadistics.update();

  if (stream->upgrade.pinned_connection || stream->response.hasPendingData()) {
    if (stream->response.chunked_status != CHUNKED_STATUS::CHUNKED_DISABLED) {
      auto pending_chunk_bytes = http_manager::handleChunkedData(*stream);
      if (pending_chunk_bytes < 0) { //we don't have enough data to get next chunk size, so we wait for more data
        stream->backend_connection.enableReadEvent();
        return;
      }
    }
#if CACHE_ENABLED
    auto service = static_cast<Service *>(stream->request.getService());
    if (service->cache_enabled && service->getCacheObject(stream->request) != nullptr &&
        !stream->request.c_opt.no_store && stream->response.c_opt.cacheable ) {
      service->addData(stream->response, stream->backend_connection.buffer,
                          stream->backend_connection.buffer_size,
                          stream->request.getUrl());
    }
#endif


    // TODO:: maybe quick response
#if PRINT_DEBUG_FLOW_BUFFERS
    Debug::logmsg(
        LOG_REMOVE, "OUT buffer size: %8lu\tContent-length: %lu\tleft: %lu\tIO: %s",
        stream->backend_connection.buffer_size, stream->response.content_length,
        stream->response.message_bytes_left, IO::getResultString(result).data());
#endif
#if ENABLE_QUICK_RESPONSE
    onClientWriteEvent(stream);
#else
    stream->client_connection.enableWriteEvent();
#endif
    stream->backend_connection.enableReadEvent();
    return;
  } else {
    if (stream->backend_connection.buffer_size == 0)
      return;
    size_t parsed = 0;
    auto ret = stream->response.parseResponse(
        stream->backend_connection.buffer, stream->backend_connection.buffer_size,
        &parsed);
    static size_t total_responses;
    switch (ret) {

    case http_parser::PARSE_RESULT::SUCCESS:break;
    case http_parser::PARSE_RESULT::TOOLONG:
    case http_parser::PARSE_RESULT::FAILED: {

      Debug::logmsg(
          LOG_REMOVE,
          "%d (%d - %d) PARSE FAILED \nRESPONSE DATA IN\n\t\t buffer "
          "size: %lu \n\t\t Content length: %lu \n\t\t "
          "left: %lu\n%.*s header sent: %s \n",
          total_responses, stream->client_connection.getFileDescriptor(),
          stream->backend_connection.getFileDescriptor(),
          stream->backend_connection.buffer_size, stream->response.content_length,
          stream->response.message_bytes_left,
          stream->backend_connection.buffer_size,
          stream->backend_connection.buffer,
          stream->response.getHeaderSent() ? "true" : "false");
      clearStream(stream);
      return;
    }
    case http_parser::PARSE_RESULT::INCOMPLETE: stream->backend_connection.enableReadEvent();
      return;
    }

    total_responses++;
    Debug::logmsg(
        LOG_DEBUG, " %lu [%s] %.*s [%s (%d) <- %s (%d)]", total_responses,
        static_cast<Service *>(stream->request.getService())->name.c_str(),
        stream->response.http_message_length - 2, stream->response.http_message,
        stream->client_connection.getPeerAddress().c_str(),
        stream->client_connection.getFileDescriptor(),
        stream->backend_connection.getBackend()->address.c_str(),
        stream->backend_connection.getFileDescriptor());

    stream->backend_connection.getBackend()->setAvgTransferTime(
        std::chrono::duration_cast<std::chrono::duration<double>>(
            std::chrono::steady_clock::now() -
                stream->backend_connection.time_start)
            .count());

    if (http_manager::validateResponse(*stream, listener_config_) !=
        validation::REQUEST_RESULT::OK) {
      Debug::logmsg(LOG_NOTICE,
                    "(%lx) backend %s response validation error\n %.*s",
                    std::this_thread::get_id(),
                    stream->backend_connection.getBackend()->address.c_str(),
                    stream->backend_connection.buffer_size,
                    stream->backend_connection.buffer);
      stream->replyError(
          HttpStatus::Code::ServiceUnavailable,
          HttpStatus::reasonPhrase(HttpStatus::Code::ServiceUnavailable).c_str(),
          listener_config_.err503, this->listener_config_, *this->ssl_manager);
      this->clearStream(stream);
      return;
    }

    auto service = static_cast<Service *>(stream->request.getService());
#if CACHE_ENABLED
    if (service->cache_enabled) {
        service->validateCacheResponse(stream->response);
        regex_t *pattern = service->getCachePattern();
        regmatch_t matches[2];
        if (pattern != nullptr) {
            if (regexec(pattern, stream->request.getUrl().data(), 1, matches, 0) == 0)
                if (stream->request.c_opt.no_store == false){
                    service->handleResponse(stream->response, stream->request);
                }
                else{
                    service_manager->getService(stream->request)->stats.cache_not_stored++;
                }

        }
    }
#endif
    http_manager::setBackendCookie(service, stream);
    setStrictTransportSecurity(service, stream);
    if (!this->is_https_listener) {
      http_manager::applyCompression(service, stream);
    }
#if ENABLE_QUICK_RESPONSE
    onClientWriteEvent(stream);
#else
    stream->client_connection.enableWriteEvent();
#endif
  }
}
void StreamManager::onConnectTimeoutEvent(int fd) {
  DEBUG_COUNTER_HIT(debug__::on_backend_connect_timeout);
  HttpStream *stream = timers_set[fd];
  if (stream == nullptr) {
    Debug::LogInfo("Stream null pointer", LOG_REMOVE);
    deleteFd(fd);
    ::close(fd);
  } else if (stream->timer_fd.isTriggered()) {
    stream->backend_connection.getBackend()->status =
        BACKEND_STATUS::BACKEND_DOWN;
    Debug::logmsg(LOG_NOTICE, "(%lx) backend %s connection timeout after %d",
                  std::this_thread::get_id(),
                  stream->backend_connection.getBackend()->address.c_str(),
                  stream->backend_connection.getBackend()->conn_timeout);
    stream->replyError(
        HttpStatus::Code::ServiceUnavailable,
        HttpStatus::reasonPhrase(HttpStatus::Code::ServiceUnavailable).c_str(),
        listener_config_.err503, this->listener_config_, *this->ssl_manager);
    this->clearStream(stream);
  }
}

void StreamManager::onRequestTimeoutEvent(int fd) {
  DEBUG_COUNTER_HIT(debug__::on_request_timeout);
  HttpStream *stream = timers_set[fd];
  if (stream == nullptr) {
    Debug::LogInfo("Stream null pointer", LOG_REMOVE);
    deleteFd(fd);
    ::close(fd);
  } else if (stream->timer_fd.isTriggered()) {
    clearStream(stream); // FIXME::
  }
}

void StreamManager::onResponseTimeoutEvent(int fd) {
  DEBUG_COUNTER_HIT(debug__::on_response_timeout);
  HttpStream *stream = timers_set[fd];
  if (stream == nullptr) {
    Debug::LogInfo("Stream null pointer", LOG_REMOVE);
    deleteFd(fd);
    ::close(fd);
  } else if (stream->timer_fd.isTriggered()) {
    char caddr[50];
    if (UNLIKELY(Network::getPeerAddress(
                     stream->client_connection.getFileDescriptor(), caddr,
                     50) == nullptr)) {
      Debug::LogInfo("Error getting peer address", LOG_DEBUG);
    } else {
      Debug::logmsg(LOG_NOTICE, "(%lx) e%d %s %s from %s",
                    std::this_thread::get_id(),
                    static_cast<int>(HttpStatus::Code::GatewayTimeout),
                    validation::request_result_reason
                        .at(validation::REQUEST_RESULT::BACKEND_TIMEOUT)
                        .c_str(),
                    stream->client_connection.buffer, caddr);
    }
    stream->replyError(
        HttpStatus::Code::GatewayTimeout,
        HttpStatus::reasonPhrase(HttpStatus::Code::GatewayTimeout).c_str(),
        HttpStatus::reasonPhrase(HttpStatus::Code::GatewayTimeout).c_str(),
        this->listener_config_, *this->ssl_manager);
    this->clearStream(stream);
  }
}
void StreamManager::onSignalEvent(int fd) {
  // TODO::IMPLEMENET
}

void StreamManager::onServerWriteEvent(HttpStream *stream) {
  DEBUG_COUNTER_HIT(debug__::on_send_request);
  if (UNLIKELY(stream->backend_connection.isCancelled())) {
    clearStream(stream);
    return;
  }
  // StreamWatcher watcher(*stream);
  int fd = stream->backend_connection.getFileDescriptor();
  // Send client request to backend server
  if (stream->backend_connection.getBackend()->conn_timeout > 0 &&
      Network::isConnected(fd) && stream->timer_fd.is_set) {
    stream->timer_fd.unset();
    stream->backend_connection.getBackend()->decreaseConnTimeoutAlive();

    stream->backend_connection.getBackend()->setAvgConnTime(
        std::chrono::duration_cast<std::chrono::duration<double>>(
            std::chrono::steady_clock::now() -
            stream->backend_connection.time_start)
            .count());
    this->deleteFd(stream->timer_fd.getFileDescriptor());
  }

  /* If the connection is pinned, then we need to write the buffer
   * content without applying any kind of modification. */
  if (stream->client_connection.buffer_size == 0 &&
      !(stream->backend_connection.getBackend()->isHttps() &&
          !stream->backend_connection.ssl_connected)) { //FIXME:: why here?
    stream->client_connection.enableReadEvent();
    stream->backend_connection.enableReadEvent();
    return;
  }
  /* If the connection is pinned or we have content length remaining to send
   * , then we need to write the buffer content without
   * applying any kind of modification. */
  IO::IO_RESULT result = IO::IO_RESULT::ERROR;
  if (stream->upgrade.pinned_connection ||
      stream->request.hasPendingData()) {
    size_t written = 0;

    if (stream->backend_connection.getBackend()->isHttps()) {
#if USE_SSL_BIO_BUFFER
      result = stream->backend_connection.getBackend()->ssl_manager.handleWrite(
          stream->backend_connection, stream->client_connection.buffer,
          stream->client_connection.buffer_size, written);
#else
      result = stream->backend_connection.getBackend()->ssl_manager.sslWrite(
          stream->backend_connection, stream->client_connection.buffer,
          stream->client_connection.buffer_size, written);
#endif
    } else {
      if (stream->client_connection.buffer_size > 0)
        result = stream->client_connection.writeTo(
            stream->backend_connection.getFileDescriptor(), written);
#if ENABLE_ZERO_COPY
      else if (stream->client_connection.splice_pipe.bytes > 0)
        result = stream->client_connection.zeroWrite(
            stream->backend_connection.getFileDescriptor(), stream->request);
#endif
    }
    switch (result) {
    case IO::IO_RESULT::SSL_HANDSHAKE_ERROR:
    case IO::IO_RESULT::SSL_NEED_HANDSHAKE: {
      if (!stream->backend_connection.getBackend()->ssl_manager.handleHandshake(
              stream->backend_connection, true)) {
        Debug::logmsg(LOG_INFO, "Handshake error with %s ",
                      stream->backend_connection.getBackend()->address.data());
        stream->replyError(
            HttpStatus::Code::ServiceUnavailable,
            HttpStatus::reasonPhrase(HttpStatus::Code::ServiceUnavailable)
                .c_str(),
            listener_config_.err503, this->listener_config_,
            *this->ssl_manager);
        clearStream(stream);
      }
      if (!stream->backend_connection.ssl_connected) {
        stream->backend_connection.enableReadEvent();
      }
      return;
    }
    case IO::IO_RESULT::FD_CLOSED:
    case IO::IO_RESULT::CANCELLED:
    case IO::IO_RESULT::FULL_BUFFER:
    case IO::IO_RESULT::ERROR:
      Debug::LogInfo("Error sending request ", LOG_DEBUG);
      clearStream(stream);
      return;
    case IO::IO_RESULT::SUCCESS:
    case IO::IO_RESULT::DONE_TRY_AGAIN:
      break;
    }
    if (stream->client_connection.buffer_size > 0) {
      stream->client_connection.buffer_size -= written;
    }
    if (!stream->upgrade.pinned_connection) {
      if (stream->request.chunked_status == http::CHUNKED_STATUS::CHUNKED_LAST_CHUNK) {
        stream->response.reset_parser();
      } else if (stream->request.message_bytes_left > 0) {
        stream->request.message_bytes_left -= written;
        if (stream->request.message_bytes_left == 0) {
          stream->request.reset_parser();
        }
      }
    }
#if PRINT_DEBUG_FLOW_BUFFERS
    Debug::logmsg(
        LOG_REMOVE, "OUT buffer size: %8lu\tContent-length: %lu\tleft: %lu\twritten: %d\tIO: %s",
        stream->client_connection.buffer_size, stream->request.content_length,
        stream->request.message_bytes_left, written, IO::getResultString(result).data());
#endif
    /* Check if chunked transfer encoding is enabled. update status*/
    stream->client_connection.enableReadEvent();
    stream->backend_connection.enableReadEvent();
    stream->backend_connection.enableWriteEvent();
    return;
  }

  /*Check if the buffer has data to be send */
  if (stream->client_connection.buffer_size == 0)
    return;

  if (stream->backend_connection.getBackend()->isHttps()) {
    result =
        stream->backend_connection.getBackend()->ssl_manager.handleDataWrite(
            stream->backend_connection, stream->client_connection,
            stream->request);
  } else {
    result = stream->client_connection.writeTo(stream->backend_connection,
                                               stream->request);
  }

  switch (result) {
  case IO::IO_RESULT::SSL_HANDSHAKE_ERROR:
  case IO::IO_RESULT::SSL_NEED_HANDSHAKE: {
    stream->backend_connection.server_name = stream->client_connection.server_name;
    if (!stream->backend_connection.getBackend()->ssl_manager.handleHandshake(
            stream->backend_connection, true)) {
      Debug::logmsg(LOG_INFO, "Handshake error with %s ",
                    stream->backend_connection.address_str.data());
      clearStream(stream);
    }
    if (!stream->backend_connection.ssl_connected) {
      stream->backend_connection.enableReadEvent();
      return;
    } else {
      stream->backend_connection.enableWriteEvent();
    }
    return;
  }
  case IO::IO_RESULT::FD_CLOSED:
  case IO::IO_RESULT::CANCELLED:
  case IO::IO_RESULT::FULL_BUFFER:
  case IO::IO_RESULT::ERROR:
    Debug::LogInfo("Error sending request to backend ", LOG_DEBUG);
    clearStream(stream);
    return;
  case IO::IO_RESULT::SUCCESS:
    break;
  case IO::IO_RESULT::DONE_TRY_AGAIN:
      if (!stream->request.getHeaderSent()) {
          stream->backend_connection.enableWriteEvent();
          return;
      }
      break;
  default:
    Debug::LogInfo("Error sending data to backend server", LOG_DEBUG);
    clearStream(stream);
    return;
  }

  stream->timer_fd.set(
      stream->backend_connection.getBackend()->response_timeout * 1000);
  timers_set[stream->timer_fd.getFileDescriptor()] = stream;
  addFd(stream->timer_fd.getFileDescriptor(), EVENT_TYPE::READ,
        EVENT_GROUP::RESPONSE_TIMEOUT);
  stream->backend_connection.enableReadEvent();
  stream->backend_connection.time_start = std::chrono::steady_clock::now();
}

void StreamManager::onClientWriteEvent(HttpStream *stream) {
  DEBUG_COUNTER_HIT(debug__::on_send_response);
  if (UNLIKELY(stream->client_connection.isCancelled())) {
    clearStream(stream);
    return;
  }

#if PRINT_DEBUG_FLOW_BUFFERS
  Debug::logmsg(
      LOG_REMOVE, "IN\tbuffer size: %8lu\tContent-length: %lu\tleft: %lu",
      stream->backend_connection.buffer_size, stream->response.content_length,
      stream->response.message_bytes_left);
#endif
  IO::IO_RESULT result = IO::IO_RESULT::ERROR;
  /* If the connection is pinned, then we need to write the buffer
   * content without applying any kind of modification. */
  if (stream->upgrade.pinned_connection ||
      stream->response.hasPendingData()) {
    size_t written = 0;

    if (this->is_https_listener) {
#if USE_SSL_BIO_BUFFER
      result = this->ssl_manager->handleWrite(
          stream->client_connection, stream->backend_connection,
          written);
#else
      result = this->ssl_manager->sslWrite(
          stream->client_connection, stream->backend_connection.buffer,
          stream->backend_connection.buffer_size, written);
#endif

    } else {
      if (stream->backend_connection.buffer_size > 0)
        result = stream->backend_connection.writeTo(
            stream->client_connection.getFileDescriptor(), written);
#if ENABLE_ZERO_COPY
      else if (stream->backend_connection.splice_pipe.bytes > 0)
        result = stream->backend_connection.zeroWrite(
            stream->client_connection.getFileDescriptor(), stream->response);
#endif
    }
    switch (result) {
    case IO::IO_RESULT::SSL_HANDSHAKE_ERROR:
    case IO::IO_RESULT::SSL_NEED_HANDSHAKE: {
      if (!this->ssl_manager->handleHandshake(stream->client_connection)) {
        Debug::logmsg(LOG_INFO, "Handshake error with %s ",
                      stream->client_connection.getPeerAddress().c_str());
        clearStream(stream);
      }
      if (stream->client_connection.ssl_connected) {
        stream->backend_connection.server_name =
            stream->client_connection.server_name;
      }
      return;
    }
    case IO::IO_RESULT::FD_CLOSED:
    case IO::IO_RESULT::CANCELLED:
    case IO::IO_RESULT::FULL_BUFFER:
    case IO::IO_RESULT::ERROR:
      Debug::logmsg(LOG_DEBUG, "Error sending response: %s", IO::getResultString(result).data());
      clearStream(stream);
      return;
    case IO::IO_RESULT::SUCCESS: // TODO:: set request
    case IO::IO_RESULT::DONE_TRY_AGAIN:
      break;
    }
    if (!stream->upgrade.pinned_connection) {
      if (stream->response.chunked_status == http::CHUNKED_STATUS::CHUNKED_LAST_CHUNK) {
        stream->response.reset_parser();
      } else if (stream->response.message_bytes_left > 0) {
        stream->response.message_bytes_left -= written;
        if (stream->response.message_bytes_left <= 0)
          stream->response.reset_parser();
      }
    }
#if PRINT_DEBUG_FLOW_BUFFERS
    Debug::logmsg(
        LOG_REMOVE, "OUT buffer size: %8lu\tContent-length: %lu\tleft: %lu\twritten: %d\tIO: %s",
        stream->backend_connection.buffer_size, stream->response.content_length,
        stream->response.message_bytes_left, written, IO::getResultString(result).data());
#endif
#if CACHE_ENABLED
    if ( !stream->response.isCached())
#endif

    if(stream->backend_connection.buffer_size > 0)
        stream->client_connection.enableWriteEvent();
    else {
      stream->backend_connection.enableReadEvent();
      stream->client_connection.enableReadEvent();
    }
    return;
  }

  if (stream->backend_connection.buffer_size == 0
#if CACHE_ENABLED
        && !stream->response.isCached()
#endif
          )
    return;

  if (this->is_https_listener) {
    result = ssl_manager->handleDataWrite(stream->client_connection,
                                          stream->backend_connection,
                                          stream->response);
  } else {
    result = stream->backend_connection.writeTo(stream->client_connection,
                                                stream->response);
  }
  switch (result) {
  case IO::IO_RESULT::SSL_HANDSHAKE_ERROR:
  case IO::IO_RESULT::SSL_NEED_HANDSHAKE: {
    if (!this->ssl_manager->handleHandshake(stream->client_connection)) {
      Debug::logmsg(LOG_INFO, "Handshake error with %s ",
                    stream->client_connection.getPeerAddress().c_str());
      clearStream(stream);
    }
    stream->client_connection.enableReadEvent();
    return;
  }
  case IO::IO_RESULT::FD_CLOSED:
  case IO::IO_RESULT::CANCELLED:
  case IO::IO_RESULT::FULL_BUFFER:
  case IO::IO_RESULT::ERROR:
    Debug::logmsg(LOG_DEBUG, "Error sending response: %s", IO::getResultString(result).data());
    clearStream(stream);
    return;
  case IO::IO_RESULT::SUCCESS:break;
  case IO::IO_RESULT::DONE_TRY_AGAIN:
    if (!stream->response.getHeaderSent()) {
      //TODO:: retry with left headers data in response.
        stream->client_connection.enableWriteEvent();
        return;
    }
    break;
  default:
    Debug::logmsg(LOG_DEBUG, "Error sending response: %s", IO::getResultString(result).data());
    clearStream(stream);
    return;
  }
#if PRINT_DEBUG_FLOW_BUFFERS
  Debug::logmsg(
      LOG_REMOVE, "OUT buffer size: %8lu\tContent-length: %lu\tleft: %lu\tIO: %s",
      stream->backend_connection.buffer_size, stream->response.content_length,
      stream->response.message_bytes_left, IO::getResultString(result).data());
#endif
  if (stream->request.upgrade_header &&
      stream->request.connection_header_upgrade &&
      stream->response.http_status_code == 101) {
    stream->upgrade.pinned_connection = true;
    std::string upgrade_header_value;
    stream->request.getHeaderValue(http::HTTP_HEADER_NAME::UPGRADE,
                                   upgrade_header_value);
    auto it = http_info::upgrade_protocols.find(upgrade_header_value);
    if (it != http_info::upgrade_protocols.end())
      stream->upgrade.protocol = it->second;
  }

  if (stream->backend_connection.buffer_size > 0)
    stream->client_connection.enableWriteEvent();
  else {
#if CACHE_ENABLED
    if (!stream->response.isCached())
#endif
    stream->backend_connection.enableReadEvent();
    stream->client_connection.enableReadEvent();
  }
}

bool StreamManager::init(ListenerConfig &listener_config) {
  listener_config_ = listener_config;
  service_manager = ServiceManager::getInstance(listener_config);
  if (listener_config.ctx != nullptr ||
      !listener_config.ssl_config_file.empty()) {
    this->is_https_listener = true;
    ssl_manager = new ssl::SSLConnectionManager();
    this->is_https_listener = ssl_manager->init(listener_config);
  }
  return true;
}

/** Clears the HttpStream. It deletes all the timers and events. Finally,
 * deletes the HttpStream.
 */
void StreamManager::clearStream(HttpStream *stream) {

#if CACHE_ENABLED
    if ( stream->response.c_object != nullptr && stream->response.c_object->dirty ){
        auto service = service_manager->getService(stream->request);
        service->deleteEntry(stream->request);
    }
#endif
  // TODO:: add connection closing reason for logging purpose
  if (stream == nullptr) {
    return;
  }
//  logSslErrorStack();

  if (stream->timer_fd.getFileDescriptor() > 0) {
    deleteFd(stream->timer_fd.getFileDescriptor());
    stream->timer_fd.unset();
    timers_set[stream->timer_fd.getFileDescriptor()] = nullptr;
    timers_set.erase(stream->timer_fd.getFileDescriptor());

  }
  if (stream->client_connection.getFileDescriptor() > 0) {
//      if (this->is_https_listener && stream->client_connection.isConnected()) { //FIXME
//          ssl_manager->sslShutdown(stream->client_connection);
//      }
    deleteFd(stream->client_connection.getFileDescriptor());
    streams_set[stream->client_connection.getFileDescriptor()] = nullptr;
    streams_set.erase(stream->client_connection.getFileDescriptor());

    DEBUG_COUNTER_HIT(debug__::on_client_disconnect);
  }
  if (stream->backend_connection.getFileDescriptor() > 0) {
      if (stream->backend_connection.isConnected()){
//          if (stream->backend_connection.getBackend()->isHttps()) { //FIXME
//              ssl_manager->sslShutdown(stream->client_connection);
//          }
          stream->backend_connection.getBackend()->decreaseConnection();
      }
    deleteFd(stream->backend_connection.getFileDescriptor());
    streams_set[stream->backend_connection.getFileDescriptor()] = nullptr;
    streams_set.erase(stream->backend_connection.getFileDescriptor());
    DEBUG_COUNTER_HIT(debug__::on_backend_disconnect);
  }
  delete stream;
}

void StreamManager::setListenSocket(int fd) {
  listener_connection.setFileDescriptor(fd);
}
void StreamManager::onClientDisconnect(HttpStream *stream) {
  DEBUG_COUNTER_HIT(debug__::event_client_disconnect);
  Debug::LogInfo("Client closed connection", LOG_DEBUG);
  clearStream(stream);
}

std::string StreamManager::handleTask(ctl::CtlTask &task) {
  if (!isHandler(task))
    return JSON_OP_RESULT::ERROR;

  if(task.command ==ctl::CTL_COMMAND::EXIT) {
    Debug::logmsg(LOG_REMOVE, "Exit command received");
    is_running = false;
    return JSON_OP_RESULT::OK;
  }
}

bool StreamManager::isHandler(ctl::CtlTask &task)
{
  return task.target == ctl::CTL_HANDLER_TYPE::ALL || task.target == ctl::CTL_HANDLER_TYPE::STREAM_MANAGER;
}
void StreamManager::onServerDisconnect(HttpStream *stream) {
  DEBUG_COUNTER_HIT(debug__::event_backend_disconnect);
  Debug::LogInfo("Backend closed connection", LOG_DEBUG);
  if (stream == nullptr) {
    return;
  }
  if (stream->backend_connection.buffer_size > 0
#if ENABLE_ZERO_COPY
    || stream->backend_connection.splice_pipe.bytes > 0
#endif
      ) {
    // TODO:: remove and create enum with READY_TO_SEND_RESPONSE
    stream->backend_connection.disableEvents();
    stream->client_connection.enableWriteEvent();
    return;
  }
  if (!stream->backend_connection.isConnected() && !stream->request.getHeaderSent()) {
      Debug::logmsg(LOG_NOTICE,
                    "Error connecting to backend %s",
                    stream->backend_connection.getBackend()->address.data());
      stream->replyError(
          HttpStatus::Code::ServiceUnavailable,
          HttpStatus::reasonPhrase(HttpStatus::Code::ServiceUnavailable)
              .c_str(),
          listener_config_.err503, this->listener_config_, *this->ssl_manager);
      stream->backend_connection.getBackend()->status =
          BACKEND_STATUS::BACKEND_DOWN;

  }
  clearStream(stream);
}
