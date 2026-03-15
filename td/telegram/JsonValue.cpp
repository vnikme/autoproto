//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/JsonValue.h"

#include "td/utils/JsonBuilder.h"
#include "td/utils/misc.h"

namespace td {

static telegram_api::object_ptr<telegram_api::JSONValue> json_to_tl(const JsonValue &json_value) {
  switch (json_value.type()) {
    case JsonValue::Type::Null:
      return telegram_api::make_object<telegram_api::jsonNull>();
    case JsonValue::Type::Boolean:
      return telegram_api::make_object<telegram_api::jsonBool>(json_value.get_boolean());
    case JsonValue::Type::Number:
      return telegram_api::make_object<telegram_api::jsonNumber>(to_double(json_value.get_number()));
    case JsonValue::Type::String:
      return telegram_api::make_object<telegram_api::jsonString>(json_value.get_string().str());
    case JsonValue::Type::Array: {
      vector<telegram_api::object_ptr<telegram_api::JSONValue>> values;
      for (size_t i = 0; i < json_value.get_array().size(); i++) {
        values.push_back(json_to_tl(json_value.get_array()[i]));
      }
      return telegram_api::make_object<telegram_api::jsonArray>(std::move(values));
    }
    case JsonValue::Type::Object: {
      vector<telegram_api::object_ptr<telegram_api::jsonObjectValue>> members;
      json_value.get_object().foreach([&](Slice key, const JsonValue &val) {
        members.push_back(telegram_api::make_object<telegram_api::jsonObjectValue>(
            key.str(), json_to_tl(val)));
      });
      return telegram_api::make_object<telegram_api::jsonObject>(std::move(members));
    }
    default:
      UNREACHABLE();
      return nullptr;
  }
}

Result<telegram_api::object_ptr<telegram_api::JSONValue>> get_input_json_value(MutableSlice json) {
  TRY_RESULT(json_value, json_decode(json));
  return json_to_tl(json_value);
}

}  // namespace td
