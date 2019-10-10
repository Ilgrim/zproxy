//
// Created by abdess on 4/5/18.
//

#include "connection.h"
#include "../util/Network.h"
#include "../util/common.h"
#include <sys/un.h>

#define PRINT_BUFFER_SIZE                                                      \
  Debug::LogInfo("BUFFER::SIZE = " + std::to_string(buffer_size), LOG_DEBUG);
//  Debug::LogInfo("BUFFER::STRLEN = " + std::to_string(strlen(buffer)),
//  LOG_DEBUG);

Connection::Connection()
    : buffer_size(0), address(nullptr), last_read_(0), last_write_(0),
      // string_buffer(),
      address_str(""), is_connected(false), ssl(nullptr),
      ssl_connected(false) {
  // address.ai_addr = new sockaddr();
}
Connection::~Connection() {
  is_connected = false;
  if (ssl != nullptr) {
    SSL_shutdown(ssl);
    SSL_clear(ssl);
    SSL_free(ssl);
#if USE_SSL_BIO_BUFFER
    if (io != NULL) {
      BIO_free(io);
      io = NULL;
    }
    if (ssl_bio != NULL) {
      BIO_free(ssl_bio);
      ssl_bio = NULL;
    }
#endif
  }
  if (fd_ > 0)
    this->closeConnection();
  if (address != nullptr) {
    if (address->ai_addr != nullptr)
      delete address->ai_addr;
  }
  delete address;
}

IO::IO_RESULT Connection::read() {

//  Debug::logmsg(LOG_REMOVE, "READ IN write %d  buffer %d", splice_pipe.bytes, buffer_size);
  bool done = false;
  ssize_t count;
  IO::IO_RESULT result = IO::IO_RESULT::ERROR;
  //  PRINT_BUFFER_SIZE
  while (!done) {
    count = ::recv(fd_, buffer + buffer_size, MAX_DATA_SIZE - buffer_size,
                   MSG_NOSIGNAL);
    if (count < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        std::string error = "read() failed  ";
        error += std::strerror(errno);
        Debug::LogInfo(error, LOG_NOTICE);
        result = IO::IO_RESULT::ERROR;
      } else {
        result = IO::IO_RESULT::DONE_TRY_AGAIN;
      }
      done = true;
    } else if (count == 0) {
      //  The  remote has closed the connection, wait for EPOLLRDHUP
      done = true;
      result = IO::IO_RESULT::FD_CLOSED;
    } else {
      // PRINT_BUFFER_SIZE
      buffer_size += static_cast<size_t>(count);
      // PRINT_BUFFER_SIZE
      if ((MAX_DATA_SIZE - buffer_size) == 0) {
//        PRINT_BUFFER_SIZE
//        Debug::LogInfo("Buffer maximum size reached !!", LOG_DEBUG);
        return IO::IO_RESULT::FULL_BUFFER;
      } else
        result = IO::IO_RESULT::SUCCESS;
      done = true;
    }
  }
  // PRINT_BUFFER_SIZE
//  Debug::logmsg(LOG_REMOVE, "READ IN write %d  buffer %d", splice_pipe.bytes, buffer_size);
  return result;
}

std::string Connection::getPeerAddress() {
  if (this->fd_ > 0 && address_str.empty()) {
    char addr[50];
    Network::getPeerAddress(this->fd_, addr, 50);
    address_str = std::string(addr);
  }
  return address_str;
}

#if ENABLE_ZERO_COPY
#if !FAKE_ZERO_COPY
IO::IO_RESULT Connection::zeroRead() {
  IO::IO_RESULT result = IO::IO_RESULT::ZERO_DATA;
  for (;;) {
    if (splice_pipe.bytes >= BUFSZ) {
      result = IO::IO_RESULT::FULL_BUFFER;
      break;
    }
    auto n = splice(fd_, nullptr, splice_pipe.pipe[1], nullptr, BUFSZ,
                    SPLICE_F_NONBLOCK | SPLICE_F_MOVE);
    if (n > 0)
      splice_pipe.bytes += n;
    if (n == 0)
      break;
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        result = IO::IO_RESULT::DONE_TRY_AGAIN;
        break;
      }
      result = IO::IO_RESULT::ERROR;
      break;
    }
  }
  Debug::logmsg(LOG_REMOVE, "ZERO READ write %d  buffer %d", splice_pipe.bytes, buffer_size);
  return result;
}

IO::IO_RESULT Connection::zeroWrite(int dst_fd,
                                    http_parser::HttpData &http_data) {
  //  Debug::LogInfo("ZERO_BUFFER::SIZE = " + std::to_string(splice_pipe.bytes),
  //  LOG_DEBUG);

  Debug::logmsg(LOG_REMOVE, "ZERO WRITE write %d  left %d  buffer %d", splice_pipe.bytes, http_data.message_bytes_left, buffer_size);
  while (splice_pipe.bytes > 0) {
    int bytes = splice_pipe.bytes;
    if (bytes > BUFSZ)
      bytes = BUFSZ;
    auto n = ::splice(splice_pipe.pipe[0], nullptr, dst_fd, nullptr, bytes,
                      SPLICE_F_NONBLOCK | SPLICE_F_MOVE);
    //    Debug::LogInfo("ZERO_BUFFER::SIZE = " +
    //    std::to_string(splice_pipe.bytes), LOG_DEBUG);
    if (n == 0)
      break;
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return IO::IO_RESULT::DONE_TRY_AGAIN;
      return IO::IO_RESULT::ERROR;
    }
    splice_pipe.bytes -= n;
    http_data.message_bytes_left -= n;
  }
  /* bytes > 0, add dst to epoll set */
  /* else remove if it was added */
  return IO::IO_RESULT::SUCCESS;
}

#else
IO::IO_RESULT Connection::zeroRead() {
  IO::IO_RESULT result = IO::IO_RESULT::ZERO_DATA;
//  Debug::logmsg(LOG_REMOVE, "ZERO READ IN %d  buffer %d", splice_pipe.bytes, buffer_size);
  for (;;) {
    if (splice_pipe.bytes >= BUFSZ) {
      result = IO::IO_RESULT::FULL_BUFFER;
      break;
    }
    auto n = ::read(fd_,buffer_aux + splice_pipe.bytes, BUFSZ - splice_pipe.bytes);
    if (n > 0)
      splice_pipe.bytes += n;
    if (n == 0)
      break;
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        result = IO::IO_RESULT::DONE_TRY_AGAIN;
        break;
      }
      result = IO::IO_RESULT::ERROR;
      break;
    }
  }
//  Debug::logmsg(LOG_REMOVE, "ZERO READ OUT %d  buffer %d", splice_pipe.bytes, buffer_size);
  return result;
}

IO::IO_RESULT Connection::zeroWrite(int dst_fd,
        http_parser::HttpData &http_data) {
//  Debug::logmsg(LOG_REMOVE, "ZERO WRITE write %d  left %d  buffer %d", splice_pipe.bytes, http_data.message_bytes_left, buffer_size);
  int sent = 0;
  while (splice_pipe.bytes > 0) {
    int bytes = splice_pipe.bytes;
    if (bytes > BUFSZ)
      bytes = BUFSZ;
    auto n = ::write(dst_fd,buffer_aux + sent,bytes - sent);
    if (n == 0)
      break;
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return IO::IO_RESULT::DONE_TRY_AGAIN;
      return IO::IO_RESULT::ERROR;
    }
    splice_pipe.bytes -= n;
    http_data.message_bytes_left -= n;
    sent += n;
  }
  /* bytes > 0, add dst to epoll set */
  /* else remove if it was added */
  return IO::IO_RESULT::SUCCESS;
}
#endif
#endif
IO::IO_RESULT Connection::writeTo(int fd, size_t & sent) {

  bool done = false;
  sent = 0;
  ssize_t count;
  IO::IO_RESULT result = IO::IO_RESULT::ERROR;

  //  Debug::LogInfo("#IN#bufer_size" +
  //  std::to_string(string_buffer.string().length()));
//  PRINT_BUFFER_SIZE
  while (!done) {
    count = ::send(fd, buffer + sent, buffer_size - sent, MSG_NOSIGNAL);
    if (count < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK /* && errno != EPIPE &&
          errno != ECONNRESET*/) {
        std::string error = "write() failed  ";
        error += std::strerror(errno);
        Debug::LogInfo(error, LOG_NOTICE);
        result = IO::IO_RESULT::ERROR;
      } else {
        result = IO::IO_RESULT::DONE_TRY_AGAIN;
      }
      done = true;
      break;
    } else if (count == 0) {
      done = true;
      break;
    } else {
      sent += static_cast<size_t>(count);
      result = IO::IO_RESULT::SUCCESS;
    }
  }
  if (sent > 0 && result != IO::IO_RESULT::ERROR) {
    buffer_size -= sent;
    //    string_buffer.erase(static_cast<unsigned int>(sent));
  }
  //  Debug::LogInfo("#OUT#bufer_size" +
  //  std::to_string(string_buffer.string().length()));
//  PRINT_BUFFER_SIZE
  return result;
}


IO::IO_RESULT Connection::writeTo(int target_fd,
                                  http_parser::HttpData &http_data) {
  //  PRINT_BUFFER_SIZE
  if (http_data.iov.empty())
    http_data.prepareToSend();

  size_t nwritten = 0;
  size_t iovec_written = 0;
  Debug::logmsg(LOG_REMOVE, "\nIOV size: %d", http_data.iov.size());
  auto result = writeIOvec(target_fd, http_data.iov, iovec_written, nwritten);
  Debug::logmsg(LOG_REMOVE, "IOV size: %d iov written %d bytes_written: %d IO RESULT: %s\n", http_data.iov.size(),
                iovec_written, nwritten, IO::getResultString(result).data());
  if (result != IO::IO_RESULT::SUCCESS)
    return result;

  buffer_size = 0;// buffer_offset;
  http_data.message_length = 0;
  http_data.setHeaderSent(true);
#if PRINT_DEBUG_FLOW_BUFFERS
  Debug::logmsg(LOG_REMOVE, "\tbuffer offset: %d", buffer_offset);
  Debug::logmsg(LOG_REMOVE, "\tOut buffer size: %d", buffer_size);
  Debug::logmsg(LOG_REMOVE, "\tbuffer offset: %d", buffer_offset);
  Debug::logmsg(LOG_REMOVE, "\tcontent length: %d", http_data.content_length);
  Debug::logmsg(LOG_REMOVE, "\tmessage length: %d", http_data.message_length);
  Debug::logmsg(LOG_REMOVE, "\tmessage bytes left: %d", http_data.message_bytes_left);
#endif
//    PRINT_BUFFER_SIZE
  return IO::IO_RESULT::SUCCESS;
}

IO::IO_RESULT Connection::writeIOvec(int target_fd, std::vector<iovec> &iov, size_t &iovec_written, size_t &nwritten) {
  IO::IO_RESULT result = IO::IO_RESULT::ERROR;
  ssize_t count = 0;
  auto nvec = iov.size();
  nwritten = 0;
  iovec_written = 0;
  do {
    count = ::writev(target_fd, &(iov[iovec_written]), static_cast<int>(nvec - iovec_written));
    Debug::logmsg(LOG_REMOVE,
                  "writev() count %d errno: %d = %s iovecwritten %d",
                  count,
                  errno,
                  std::strerror(errno),
                  iovec_written);
    if (count < 0) {
      if (count == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        result = IO::IO_RESULT::DONE_TRY_AGAIN; //do not persist changes        
      } else {
        std::string error = "writev() failed  ";
        error += std::strerror(errno);
        Debug::LogInfo(error, LOG_NOTICE);
        result = IO::IO_RESULT::ERROR;
      }
      break;
    } else {
      size_t remaining = static_cast<size_t>(count);
      for (auto it = iov.begin() + static_cast<ssize_t>(iovec_written); it != iov.end(); it++) {
        if (remaining >= it->iov_len) {
          remaining -= it->iov_len;
//          iov.erase(it++);
          it->iov_len = 0;
          iovec_written++;
        } else {
          Debug::logmsg(LOG_REMOVE,
                        "Recalculating data ... remaining %d niovec_written: %d iov size %d",
                        remaining,
                        iovec_written,
                        iov.size());
          it->iov_len -= remaining;
          it->iov_base = static_cast<char *>(iov[iovec_written].iov_base) + remaining;
          break;
        }
      }

      nwritten += static_cast<size_t>(count);
      if (errno == EINPROGRESS && remaining != 0)
        return IO::IO_RESULT::DONE_TRY_AGAIN;
      else
        result = IO::IO_RESULT::SUCCESS;
#if PRINT_DEBUG_FLOW_BUFFERS
      Debug::logmsg(LOG_REMOVE,
                    "# Headers sent, size: %d iovec_written: %d nwritten: %d IO::RES %s",
                    nvec,
                    iovec_written,
                    nwritten,
                    IO::getResultString(result).data());
#endif

    }
  } while (iovec_written < nvec);

  return result;
}

IO::IO_RESULT Connection::writeTo(const Connection &target_connection,
                                  http_parser::HttpData &http_data) {
  return writeTo(target_connection.getFileDescriptor(), http_data);
}

IO::IO_RESULT Connection::write(const char *data, size_t size) {
  bool done = false;
  size_t sent = 0;
  ssize_t count;
  IO::IO_RESULT result = IO::IO_RESULT::ERROR;

  //  Debug::LogInfo("#IN#bufer_size" +
  //  std::to_string(string_buffer.string().length()));
  //  PRINT_BUFFER_SIZE
  while (!done) {
    count = ::send(fd_, data + sent, size - sent, MSG_NOSIGNAL);
    if (count < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK /* && errno != EPIPE &&
          errno != ECONNRESET*/) {
        std::string error = "write() failed  ";
        error += std::strerror(errno);
        Debug::LogInfo(error, LOG_NOTICE);
        result = IO::IO_RESULT::ERROR;
      } else {
        result = IO::IO_RESULT::DONE_TRY_AGAIN;
      }
      done = true;
      break;
    } else if (count == 0) {
      done = true;
      break;
    } else {
      sent += static_cast<size_t>(count);
      result = IO::IO_RESULT::SUCCESS;
    }
  }
  if (sent > 0 && result != IO::IO_RESULT::ERROR) {
    //    size -= sent;
    //    string_buffer.erase(static_cast<unsigned int>(sent));
  }
  //  Debug::LogInfo("#OUT#bufer_size" +
  //  std::to_string(string_buffer.string().length()));
  //  PRINT_BUFFER_SIZE
  return result;
}

void Connection::closeConnection() {
  is_connected = false;
  if (fd_ > 0) {
//    ::shutdown(fd_, 2);
    ::close(fd_);
  }
}
IO::IO_OP Connection::doConnect(addrinfo &address_, int timeout, bool async) {
  int result = -1;
  if ((fd_ = socket(address_.ai_family, SOCK_STREAM, 0)) < 0) {
    Debug::logmsg(LOG_WARNING, "socket() failed ");
    return IO::IO_OP::OP_ERROR;
  }
  if (LIKELY(async))
    Network::setSocketNonBlocking(fd_);
  if ((result = ::connect(fd_, address_.ai_addr, sizeof(address_))) < 0) {
    if (errno == EINPROGRESS && timeout > 0) {
      return IO::IO_OP::OP_IN_PROGRESS;

    } else {
      Debug::logmsg(LOG_NOTICE, "connect() error %d - %s\n", errno,
                    strerror(errno));
      return IO::IO_OP::OP_ERROR;
    }
  }
  // Create stream object if connected
  return result != -1 ? IO::IO_OP::OP_SUCCESS : IO::IO_OP::OP_ERROR;
}

IO::IO_OP Connection::doConnect(const std::string &af_unix_socket_path,
                                int timeout) {
  int result = -1;
  if ((fd_ = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    Debug::logmsg(LOG_WARNING, "socket() failed ");
    return IO::IO_OP::OP_ERROR;
  }
  Network::setTcpNoDelayOption(fd_);
  Network::setSoKeepAliveOption(fd_);
  Network::setSoLingerOption(fd_, true);

  if (timeout > 0)
    Network::setSocketNonBlocking(fd_);

  struct sockaddr_un serveraddr;
  strcpy(serveraddr.sun_path, af_unix_socket_path.c_str());
  serveraddr.sun_family = AF_UNIX;
  if ((result = ::connect(fd_, (struct sockaddr *)&serveraddr,
                          SUN_LEN(&serveraddr))) < 0) {
    if (errno == EINPROGRESS && timeout > 0) {
      return IO::IO_OP::OP_IN_PROGRESS;

    } else {
      Debug::logmsg(LOG_NOTICE, "connect() error %d - %s\n", errno,
                    strerror(errno));
      return IO::IO_OP::OP_ERROR;
    }
  }
  // Create stream object if connected
  return result != -1 ? IO::IO_OP::OP_SUCCESS : IO::IO_OP::OP_ERROR;
}

bool Connection::isConnected() {
  if (fd_ > 0)
    return Network::isConnected(this->fd_);
  else
    return false;
}

int Connection::doAccept() {
  int new_fd = -1;
  sockaddr_in clnt_addr{};
  socklen_t clnt_length = sizeof(clnt_addr);

  if ((new_fd = accept4(fd_, (sockaddr *)&clnt_addr, &clnt_length,
                        SOCK_NONBLOCK | SOCK_CLOEXEC)) < 0) {
    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
      return 0; // We have processed all incoming connections.
    }
    std::string error = "accept() failed  ";
    error += std::strerror(errno);
    Debug::LogInfo(error);
    // break;
    return -1;
  }
  if (clnt_addr.sin_family == AF_INET || clnt_addr.sin_family == AF_INET6 ||
      clnt_addr.sin_family == AF_UNIX) {
    Network::setTcpNoDelayOption(new_fd);
    Network::setSoKeepAliveOption(new_fd);
    Network::setSoLingerOption(new_fd, true);
    return new_fd;
  } else {
    ::close(new_fd);
    Debug::logmsg(LOG_WARNING, "HTTP connection prematurely closed by peer");
  }

  return -1;
}
bool Connection::listen(std::string address_str_, int port) {
  this->address = Network::getAddress(address_str_, port);
  if (this->address != nullptr)
    return listen(*this->address);
  return false;
}

bool Connection::listen(addrinfo &address_) {
  this->address = &address_;
  /* prepare the socket */
  if ((fd_ = socket(this->address->ai_family == AF_INET ? PF_INET : PF_INET6,
                    SOCK_STREAM, 0)) < 0) {
    Debug::logmsg(LOG_ERR, "socket () failed %s s - aborted", strerror(errno));
    return false;
  }

  Network::setSoLingerOption(fd_);
  Network::setSoReuseAddrOption(fd_);
  Network::setTcpDeferAcceptOption(fd_);

  if (::bind(fd_, address->ai_addr,
             static_cast<socklen_t>(address->ai_addrlen)) < 0) {
    Debug::logmsg(LOG_ERR, "bind () failed %s s - aborted", strerror(errno));
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  ::listen(fd_, SOMAXCONN);
  return true;
}
bool Connection::listen(std::string af_unix_name) {
  if (af_unix_name.empty())
    return false;
  // unlink possible previously created path.
  unlink(af_unix_name.c_str());

  // Initialize AF_UNIX socket
  sockaddr_un ctrl{};
  ::memset(&ctrl, 0, sizeof(ctrl));
  ctrl.sun_family = AF_UNIX;
  ::strncpy(ctrl.sun_path, af_unix_name.c_str(), sizeof(ctrl.sun_path) - 1);

  if ((fd_ = ::socket(PF_UNIX, SOCK_STREAM, 0)) < 0) {
    Debug::logmsg(LOG_ERR, "Control \"%s\" create: %s", ctrl.sun_path,
                  strerror(errno));
    return false;
  }
  if (::bind(fd_, (struct sockaddr *)&ctrl, (socklen_t)sizeof(ctrl)) < 0) {
    Debug::logmsg(LOG_ERR, "Control \"%s\" bind: %s", ctrl.sun_path,
                  strerror(errno));
    return false;
  }
  ::listen(fd_, SOMAXCONN);

  return false;
}


