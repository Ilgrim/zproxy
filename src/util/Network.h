#ifndef NETWORK_H
#define NETWORK_H
#include "../debug/Debug.h"
#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>

class Network {
public:
  inline static char *getPeerAddress(int socket_fd, char *buf, size_t bufsiz,
                                     bool include_port = false) {
    int result;
    sockaddr_in adr_inet{};
    socklen_t len_inet = sizeof adr_inet;
	result = ::getpeername(socket_fd, reinterpret_cast<sockaddr *>(&adr_inet), &len_inet);
    if (result == -1) {
      return nullptr;
    }

    if (snprintf(buf, bufsiz, "%s", inet_ntoa(adr_inet.sin_addr)) == -1) {
      return nullptr; /* Buffer too small */
    }
    if (include_port) {
      //      result = snprintf(buf, bufsiz, "%s:%u",
      //                        inet_ntoa(adr_inet.sin_addr),
      //                        (unsigned) ntohs(adr_inet.sin_port));
      //      if (result == -1) {
      //        return nullptr; /* Buffer too small */
      //      }
    }
    return buf;
  }

  inline static int getHost(const char *name, addrinfo *res, int ai_family) {
    struct addrinfo *chain, *ap;
    struct addrinfo hints;
    int ret_val;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = ai_family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;
	if ((ret_val = getaddrinfo(name, nullptr, &hints, &chain)) == 0) {
	  for (ap = chain; ap != nullptr; ap = ap->ai_next)
        if (ap->ai_socktype == SOCK_STREAM)
          break;

	  if (ap == nullptr) {
        freeaddrinfo(chain);
        return EAI_NONAME;
      }
      *res = *ap;
	  if ((res->ai_addr =  static_cast<sockaddr *>(malloc(ap->ai_addrlen))) == nullptr) {
        freeaddrinfo(chain);
        return EAI_MEMORY;
      }
      memcpy(res->ai_addr, ap->ai_addr, ap->ai_addrlen);
      freeaddrinfo(chain);
    }
    return ret_val;
  }

  inline static addrinfo *getAddress(const std::string &address, int port) {
    struct sockaddr_in in {};
    struct sockaddr_in6 in6 {};
    auto *addr = new addrinfo(); /* IPv4/6 address */

	if (getHost(address.data(), addr, PF_UNSPEC)) {
      Debug::LogInfo("Unknown Listener address");
      delete addr;
      return nullptr;
    }
    if (addr->ai_family != AF_INET && addr->ai_family != AF_INET6) {
      Debug::LogInfo("Unknown Listener address family");
      delete addr;
      return nullptr;
    }
    switch (addr->ai_family) {
    case AF_INET:
      memcpy(&in, addr->ai_addr, sizeof(in));
      in.sin_port = static_cast<in_port_t>(htons(static_cast<uint16_t>(port)));
      memcpy(addr->ai_addr, &in, sizeof(in));
      break;
    case AF_INET6:
      memcpy(&in6, addr->ai_addr, sizeof(in6));
      in6.sin6_port = htons(static_cast<uint16_t>(port));
      memcpy(addr->ai_addr, &in6, sizeof(in6));
      break;
    default:
      Debug::LogInfo("Unknown Listener address family", LOG_ERR);
    }
    return addr;
  }

  inline static int getPeerPort(int socket_fd) {
    int port = -1;

    sockaddr_in adr_inet{};
    socklen_t len_inet = sizeof(adr_inet);

    if (::getpeername(socket_fd, reinterpret_cast<sockaddr *>(&adr_inet), &len_inet) != -1) {
      port = ntohs(adr_inet.sin_port);
      return port;
    }
    return -1;
  }

  inline static int getPeerPort(struct addrinfo *addr) {
    int port;
    port = ntohs((reinterpret_cast<sockaddr_in *>(addr->ai_addr))->sin_port);
    return port;
  }

  inline static int getlocalPort(int socket_fd) {
    int port = -1;
    sockaddr_in adr_inet{};
    socklen_t len_inet = sizeof(adr_inet);
    if (::getsockname(socket_fd, reinterpret_cast<sockaddr *>(&adr_inet), &len_inet) != -1) {
      port = ntohs(adr_inet.sin_port);
      return port;
    }
    return -1;
  }
  inline static char *getlocalAddress(int socket_fd, char *buf, size_t bufsiz,
                                      bool include_port = false) {
     int result;
     sockaddr_in adr_inet{};
     socklen_t len_inet = sizeof adr_inet;
     result = ::getsockname(socket_fd, reinterpret_cast<sockaddr *>(&adr_inet), &len_inet);
     if (result == -1) {
       return nullptr;
     }
     if (snprintf(buf, bufsiz, "%s", inet_ntoa(adr_inet.sin_addr)) == -1) {
       return nullptr; /* Buffer too small */
     }
     if (include_port) {
       //      result = snprintf(buf, bufsiz, "%s:%u",
       //                        inet_ntoa(adr_inet.sin_addr),
       //                        (unsigned) ntohs(adr_inet.sin_port));
       //      if (result == -1) {
       //        return nullptr; /* Buffer too small */
       //      }
     }
     return buf;
   }



  /*
   * Translate inet/inet6 address/port into a string
   */
  static void addr2str(char *const res, size_t res_len,
                       const struct addrinfo *addr, const int no_port) {
    char buf[MAXBUF];
    int port;
    void *src;

    ::memset(res, 0, res_len);
    switch (addr->ai_family) {
    case AF_INET:
	  src = static_cast<void *>(&(reinterpret_cast<sockaddr_in *>(addr->ai_addr))->sin_addr.s_addr);
	  port = ntohs(( reinterpret_cast<sockaddr_in *>(addr->ai_addr))->sin_port);
	  if (inet_ntop(AF_INET, src, buf, MAXBUF - 1) == nullptr)
        strncpy(buf, "(UNKNOWN)", MAXBUF - 1);
      break;
    case AF_INET6:
	  src =  static_cast<void *>(&( reinterpret_cast<sockaddr_in6 *>(addr->ai_addr))->sin6_addr.s6_addr);
	  port = ntohs(( reinterpret_cast<sockaddr_in6 *>(addr->ai_addr))->sin6_port);
      if (IN6_IS_ADDR_V4MAPPED(
			  &(( reinterpret_cast<sockaddr_in6 *>(addr->ai_addr))->sin6_addr))) {
		src = static_cast<void *>(&( reinterpret_cast<sockaddr_in6 *>(addr->ai_addr))
				  ->sin6_addr.s6_addr[12]);
		if (inet_ntop(AF_INET, src, buf, MAXBUF - 1) == nullptr)
          strncpy(buf, "(UNKNOWN)", MAXBUF - 1);
      } else {
		if (inet_ntop(AF_INET6, src, buf, MAXBUF - 1) == nullptr)
          strncpy(buf, "(UNKNOWN)", MAXBUF - 1);
      }
      break;
    case AF_UNIX:
	  strncpy(buf, reinterpret_cast<char *>(addr->ai_addr), MAXBUF - 1);
      port = 0;
      break;
    default:
      strncpy(buf, "(UNKNOWN)", MAXBUF - 1);
      port = 0;
      break;
    }
    if (no_port)
      ::snprintf(res, res_len, "%s", buf);
    else
      ::snprintf(res, res_len, "%s:%d", buf, port);
    return;
  }
  static bool HostnameToIp(const char * hostname , char* ip)
  {
    struct hostent *he;
    struct in_addr **addr_list;
    int i;

	if ( (he = gethostbyname( hostname ) ) == nullptr)
    {
      // get the host info

      return false;
    }
	addr_list = reinterpret_cast<in_addr **>(he->h_addr_list);
	for(i = 0; addr_list[i] != nullptr; i++)
    {
      //Return the first one;
      strcpy(ip , inet_ntoa(*addr_list[i]) );
      return true;
    }

    return false;
  }
  static bool setSocketNonBlocking(int fd, bool blocking = false) {
    // set socket non blocking
    int flags;
    flags = ::fcntl(fd, F_GETFL, NULL);
    if (blocking) {
      flags &= (~O_NONBLOCK);
    } else {
      flags |= O_NONBLOCK;
    }
    if (::fcntl(fd, F_SETFL, flags) < 0) {
      std::string error = "fcntl(2) failed";
      error += std::strerror(errno);
      Debug::LogInfo(error);
      return false;
    }
    return true;
  }

  inline static bool setSocketTimeOut(int sock_fd, unsigned int seconds) {
    struct timeval tv;
    tv.tv_sec = seconds; /* 30 Secs Timeout */
	return setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv,
                      sizeof(timeval)) != -1;
  }

  inline static bool setSoReuseAddrOption(int sock_fd) {
    int flag = 1;
    return setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) !=
           -1;
  }

  inline static bool setTcpNoDelayOption(int sock_fd) {
    int flag = 1;
    return setsockopt(sock_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) !=
           -1;
  }

  inline static bool setTcpDeferAcceptOption(int sock_fd) {
    int flag = 5;
    return setsockopt(sock_fd, SOL_TCP, TCP_DEFER_ACCEPT, &flag,
                      sizeof(flag)) != -1;
  }

  inline static bool setSoKeepAliveOption(int sock_fd) {
    int flag = 1;
    return setsockopt(sock_fd, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag)) !=
           -1;
  }
  inline static bool setSoLingerOption(int sock_fd, bool enable = false) {
    struct linger l {};
    l.l_onoff = enable ? 1: 0;
    l.l_linger = enable ? 10 : 0;
    return setsockopt(sock_fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l)) != -1;
  }

  inline static bool setTcpLinger2Option(int sock_fd) {
    int flag = 5;
    return setsockopt(sock_fd, SOL_SOCKET, TCP_LINGER2, &flag, sizeof(flag)) !=
           -1;
  }

  /*useful for use with send file, wait 200 ms to to fill TCP packet*/
  inline static bool setTcpCorkOption(int sock_fd) {
    int flag = 1;
    return setsockopt(sock_fd, IPPROTO_TCP, TCP_CORK, &flag, sizeof(flag)) !=
           -1;
  }
#ifdef SO_ZEROCOPY
  /*useful for use with send file, wait 200 ms to to fill TCP packet*/
  inline static bool setSoZeroCopy(int sock_fd) {
    int flag = 1;
    return setsockopt(sock_fd, SOL_SOCKET, SO_ZEROCOPY, &flag, sizeof(flag)) !=
           -1;
  }
#endif
  //set netfilter mark, need root privileges
  inline static bool setSOMarkOption(int sock_fd, int nf_mark) {
    //enter_suid()/leave_suid().
    return nf_mark != 0 && setsockopt(sock_fd, SOL_SOCKET, SO_MARK, &nf_mark, sizeof(nf_mark)) !=
        -1;

  }
  inline static bool isConnected(int sock_fd) {
    int error_code = -1;
    socklen_t error_code_size = sizeof(error_code);
    return ::getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &error_code,
                        &error_code_size) != -1 &&
           error_code == 0;
  }
  /*return -1 in case of erro and set errno*/
  inline static int getSocketSendBufferSize(int socket_fd) {
    int res, size;
    unsigned int m = sizeof(size);
	res = getsockopt(socket_fd, SOL_SOCKET, SO_SNDBUF, &size, &m);
    return res != 0 ? -1 : size;
  }

  /*return -1 in case of erro and set errno*/
  inline static int getSocketReceiveBufferSize(int socket_fd) {
    int res, size;
    unsigned int m = sizeof(size);
	res = getsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &size, &m);
    return res != 0 ? -1 : size;
  }

  inline static int setSocketSendBufferSize(int socket_fd, unsigned int new_size) {
	unsigned int m = sizeof(new_size);
	return  ::setsockopt(socket_fd, SOL_SOCKET, SO_SNDBUF, &new_size, m) != -1 ;
  }

  inline static int setSocketReceiveBufferSize(int socket_fd, unsigned int new_size) {
	unsigned int m = sizeof(new_size);
	return  ::setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &new_size, m) != -1 ;
  }

static std::string read(int fd) {
    int size = 65536;
    char buffer[size];
    bool should_close = false, done = false;
    ssize_t count = -1;
    size_t buffer_size = 0;

    while (!done) {
      count = ::recv(fd, buffer + buffer_size, size, MSG_NOSIGNAL);
      if (count == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          std::string error = "read() failed  ";
          error += std::strerror(errno);
          Debug::LogInfo(error, LOG_NOTICE);
          should_close = true;
        }
        done = true;
      } else if (count == 0) {
        //  The  remote has closed the connection, wait for EPOLLRDHUP
        should_close = true;
        done = true;
      } else {
        buffer_size += static_cast<size_t>(count);
      }
    }
    return std::string(buffer);
  }

};


#endif // NETWORK_H
