#include "backend.h"

Backend::Backend() : status(BACKEND_STATUS::NO_BACKEND) {
  //  ctl::ControlManager::getInstance()->attach(std::ref(*this));
}

Backend::~Backend() {
  Debug::logmsg(LOG_REMOVE, "Destructor");
  //  ctl::ControlManager::getInstance()->deAttach(std::ref(*this));
}

std::string Backend::handleTask(ctl::CtlTask& task) {
  if (!isHandler(task)) return "";
  //  Debug::logmsg(LOG_REMOVE, "Backend %d handling task", backend_id);
  if (task.command == ctl::CTL_COMMAND::GET) {
      switch (task.subject) {
        case ctl::CTL_SUBJECT::STATUS: {
            JsonObject status_;
            switch (this->status) {
              case BACKEND_STATUS::BACKEND_UP:
                status_.emplace(JSON_KEYS::STATUS,
                                new JsonDataValue(JSON_KEYS::STATUS_UP));
                break;
              case BACKEND_STATUS::BACKEND_DOWN:
                status_.emplace(JSON_KEYS::STATUS,
                                new JsonDataValue(JSON_KEYS::STATUS_DOWN));
                break;
              case BACKEND_STATUS::BACKEND_DISABLED:
                status_.emplace(JSON_KEYS::STATUS,
                                new JsonDataValue(JSON_KEYS::STATUS_DISABLED));
                break;
              default:
                status_.emplace(JSON_KEYS::STATUS,
                                new JsonDataValue(JSON_KEYS::UNKNOWN));
                break;
              }
            return status_.stringify();
          }
        case ctl::CTL_SUBJECT::WEIGHT: {
            JsonObject weight_;
            weight_.emplace(JSON_KEYS::WEIGHT,
                            new JsonDataValue(this->weight));
            return weight_.stringify();
          }
        default:auto backend_status = this->getBackendJson();
          if (backend_status != nullptr)
            return backend_status->stringify();
          return JSON_OP_RESULT::ERROR;
        }
    } else if (task.command == ctl::CTL_COMMAND::UPDATE) {
      auto status_object = JsonParser::parse(task.data);
      if (status_object == nullptr)
        return JSON_OP_RESULT::ERROR;
      switch (task.subject) {
        case ctl::CTL_SUBJECT::CONFIG:
          // TODO:: update  config (timeouts, headers)
          break;
        case ctl::CTL_SUBJECT::STATUS: {

            if (status_object->at(JSON_KEYS::STATUS)->isValue()) {
                auto value =
                    dynamic_cast<JsonDataValue *>(status_object->at(JSON_KEYS::STATUS).get())
                    ->string_value;
                if (value == JSON_KEYS::STATUS_ACTIVE ||
                    value == JSON_KEYS::STATUS_UP) {
                    this->status = BACKEND_STATUS::BACKEND_UP;
                  } else if (value == JSON_KEYS::STATUS_DOWN) {
                    this->status = BACKEND_STATUS::BACKEND_DOWN;
                  } else if (value == JSON_KEYS::STATUS_DISABLED) {
                    this->status = BACKEND_STATUS::BACKEND_DISABLED;
                  }
                Debug::logmsg(LOG_NOTICE, "Set Backend %d %s", backend_id,
                              value.c_str());
                return JSON_OP_RESULT::OK;
              }
            break;
          }
        case ctl::CTL_SUBJECT::WEIGHT:{
            if (status_object->at(JSON_KEYS::WEIGHT)->isValue()) {
                auto value =
                    dynamic_cast<JsonDataValue *>(status_object->at(JSON_KEYS::WEIGHT).get())
                    ->number_value;
                this->weight = static_cast<int>(value);
                return JSON_OP_RESULT::OK;
              }
            return JSON_OP_RESULT::ERROR;
          }
        default:break;
        }
    }
  return "";
}

bool Backend::isHandler(ctl::CtlTask& task) {
  return /*task.target == ctl::CTL_HANDLER_TYPE::BACKEND &&*/
      (task.backend_id == this->backend_id || task.backend_id == -1);
}

std::unique_ptr<JsonObject> Backend::getBackendJson() {
  std::unique_ptr<JsonObject> root{new JsonObject()};

  root->emplace(JSON_KEYS::NAME, std::unique_ptr<JsonDataValue>(new JsonDataValue(this->name)));
  root->emplace(JSON_KEYS::ID, std::unique_ptr<JsonDataValue>(new JsonDataValue(this->backend_id)));
  root->emplace(JSON_KEYS::TYPE, std::unique_ptr<JsonDataValue>(new JsonDataValue(static_cast<int>(this->backend_type))));
  if (this->backend_type != BACKEND_TYPE::REDIRECT) {
      root->emplace(JSON_KEYS::ADDRESS, std::unique_ptr<JsonDataValue>(new JsonDataValue(this->address)));
      root->emplace(JSON_KEYS::PORT, std::unique_ptr<JsonDataValue>(new JsonDataValue(this->port)));
      root->emplace(JSON_KEYS::WEIGHT, std::unique_ptr<JsonDataValue>(new JsonDataValue(this->weight)));
      switch (this->status) {
        case BACKEND_STATUS::BACKEND_UP:
          root->emplace(JSON_KEYS::STATUS, std::unique_ptr<JsonDataValue>(
                          new JsonDataValue(JSON_KEYS::STATUS_ACTIVE)));
          break;
        case BACKEND_STATUS::BACKEND_DOWN:
          root->emplace(JSON_KEYS::STATUS, std::unique_ptr<JsonDataValue>(
                          new JsonDataValue(JSON_KEYS::STATUS_DOWN)));
          break;
        case BACKEND_STATUS::BACKEND_DISABLED:
          root->emplace(JSON_KEYS::STATUS, std::unique_ptr<JsonDataValue>(
                          new JsonDataValue(JSON_KEYS::STATUS_DISABLED)));
          break;
        default:
          root->emplace(JSON_KEYS::STATUS, std::unique_ptr<JsonDataValue>(new JsonDataValue(JSON_KEYS::UNKNOWN)));
          break;
        }
      root->emplace(JSON_KEYS::CONNECTIONS, std::unique_ptr<JsonDataValue>(new JsonDataValue(this->established_conn)));
      root->emplace(JSON_KEYS::PENDING_CONNS, std::unique_ptr<JsonDataValue>(new JsonDataValue(this->pending_connections)));
      root->emplace(JSON_KEYS::RESPONSE_TIME, std::unique_ptr<JsonDataValue>(new JsonDataValue(this->avg_response_time)));
      root->emplace(JSON_KEYS::CONNECT_TIME, std::unique_ptr<JsonDataValue>(new JsonDataValue(this->avg_conn_time)));
    }
  return root;
}

void Backend::doMaintenance() {
  if (this->status != BACKEND_STATUS::BACKEND_DOWN)
    return;

  Connection checkOut;
  auto res = checkOut.doConnect(*address_info, 0, false);

  switch(res) {
    case IO::IO_OP::OP_SUCCESS: {
      // poundlogs, BackEnd 192.168.100.253:80 resurrect in farm: 'poundlogs', service: 'assur'
      Debug::logmsg(LOG_NOTICE,
                    "BackEnd %s:%d resurrect in farm: '%s', service: '%s'",
                    this->address.data(),
                    this->port,
                    this->backend_config.f_name.data(),
                    this->backend_config.srv_name.data());
        this->status = BACKEND_STATUS::BACKEND_UP;
        break;
      }
    default:this->status = BACKEND_STATUS::BACKEND_DOWN;
    }
}
bool Backend::isHttps() {
  return ctx != nullptr;
}
