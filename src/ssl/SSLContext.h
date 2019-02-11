//
// Created by abdess on 1/18/19.
//

#pragma once

#include "../config/config.h"
#include "../debug/Debug.h"
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
namespace ssl {
class SSLContext {
public:
  BIO *error_bio{nullptr};
  SSL_CTX *ssl_ctx{nullptr};

  SSLContext();
  virtual ~SSLContext();
  bool init();
  bool init(const std::string &cert_file, const std::string &key_file);
  bool init(const ListenerConfig &listener_config);
};
} // namespace ssl