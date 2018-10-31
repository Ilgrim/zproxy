//
// Created by abdess on 10/11/18.
//

#include "JsonDataValue.h"
#include <functional>

json::JsonDataValue::JsonDataValue(const json::JsonDataValue &value) {
  setValue(value);
}
json::JsonDataValue::JsonDataValue(const json::JsonDataValue &&value) {
  setValue(value);
}
json::JsonDataValue::JsonDataValue(const std::string &value) {
  setValue(value);
}
json::JsonDataValue::JsonDataValue(const char *value) { setValue(value); }

json::JsonDataValue::JsonDataValue(int value)
{
    setValue(static_cast<long>(value));
}
json::JsonDataValue::JsonDataValue(unsigned int value) {
  setValue(static_cast<long>(value));
}
json::JsonDataValue::JsonDataValue(double value) { setValue(value); }
json::JsonDataValue::JsonDataValue(bool value) { setValue(value); }
json::JsonDataValue::JsonDataValue(const json::JsonArray &json_array) {
  setValue(json_array);
}
json::JsonDataValue::JsonDataValue(const json::JsonObject &json_object) {
  setValue(json_object);
}
json::JsonDataValue::JsonDataValue(long value) { setValue(value); }
json::JsonDataValue::~JsonDataValue() {
  //  if (array_value != nullptr) delete array_value;
  //  if (object_value != nullptr)delete object_value;
}
json::JsonDataValue &json::JsonDataValue::operator=(
    const json::JsonDataValue &other) {
  setValue(other);
  return *this;
}
bool json::JsonDataValue::isValue() { return true; }
void json::JsonDataValue::setValue(const json::JsonDataValue &value) {
  switch (value.json_type) {
    case JSON_VALUE_TYPE::JSON_T_NULL:
      setNullValue();
      break;
    case JSON_VALUE_TYPE::JSON_T_STRING:
      setValue(value.string_value);
      break;
    case JSON_VALUE_TYPE::JSON_T_BOOL:
      setValue(value.bool_value);
      break;
    case JSON_VALUE_TYPE::JSON_T_NUMBER:
      setValue(value.number_value);
      break;
    case JSON_VALUE_TYPE::JSON_T_OBJECT:
      setValue(value.object_value);
      break;
    case JSON_VALUE_TYPE::JSON_T_ARRAY:
      setValue(value.array_value);
      break;
  }
}
void json::JsonDataValue::setValue(const std::string &value) {
  string_value = std::string(value);
  json_type = JSON_VALUE_TYPE::JSON_T_STRING;
}
void json::JsonDataValue::setValue(const char *value) {
  string_value = std::string(value);
  json_type = JSON_VALUE_TYPE::JSON_T_STRING;
}
void json::JsonDataValue::setValue(double value) {
  double_value = value;
  json_type = JSON_VALUE_TYPE::JSON_T_DOUBLE;
}
void json::JsonDataValue::setValue(long value) {
  number_value = value;
  json_type = JSON_VALUE_TYPE::JSON_T_NUMBER;
}
void json::JsonDataValue::setValue(bool value) {
  bool_value = value;
  json_type = JSON_VALUE_TYPE::JSON_T_BOOL;
}
void json::JsonDataValue::setNullValue() {
  string_value = "null";
  json_type = JSON_VALUE_TYPE::JSON_T_NULL;
}
void json::JsonDataValue::setValue(const json::JsonArray &json_arry) {
  array_value = new JsonArray(json_arry);
  json_type = JSON_VALUE_TYPE::JSON_T_ARRAY;
}
void json::JsonDataValue::setValue(const json::JsonObject &json_object) {
  object_value = new JsonObject(json_object);
  json_type = JSON_VALUE_TYPE::JSON_T_OBJECT;
}

std::string json::JsonDataValue::stringify(bool prettyfy, int tabs) {
  switch (json_type) {
    case JSON_VALUE_TYPE::JSON_T_NULL:
      break;
    case JSON_VALUE_TYPE::JSON_T_STRING:
      return "\"" + string_value + "\"";
    case JSON_VALUE_TYPE::JSON_T_BOOL:
      return bool_value ? "true" : "false";
    case JSON_VALUE_TYPE::JSON_T_NUMBER:
      return std::to_string(number_value);
    case JSON_VALUE_TYPE::JSON_T_DOUBLE:
      return std::to_string(double_value);
    case JSON_VALUE_TYPE::JSON_T_OBJECT:
      return object_value->stringify(prettyfy, tabs + 1);
    case JSON_VALUE_TYPE::JSON_T_ARRAY:
      return array_value->stringify(prettyfy, tabs + 1);
  }
  return std::string("null");
}
void json::JsonDataValue::freeJson() {
  switch (json_type) {
    case JSON_VALUE_TYPE::JSON_T_NULL:
      break;
    case JSON_VALUE_TYPE::JSON_T_STRING:
      break;
    case JSON_VALUE_TYPE::JSON_T_BOOL:
      break;
    case JSON_VALUE_TYPE::JSON_T_NUMBER:
      break;
    case JSON_VALUE_TYPE::JSON_T_OBJECT:
      if (object_value != nullptr) {
        object_value->freeJson();
        delete object_value;
      }
      break;
    case JSON_VALUE_TYPE::JSON_T_ARRAY:
      if (array_value != nullptr) {
        array_value->freeJson();
        delete array_value;
      }
      break;
  }
}
