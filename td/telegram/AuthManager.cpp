//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Stripped AuthManager: minimal implementation for MTProto layer.
//
#include "td/telegram/AuthManager.h"

#include "td/telegram/Global.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/logging.h"

namespace td {

AuthManager::AuthManager(int32 api_id, const string &api_hash, ActorShared<> parent)
    : parent_(std::move(parent)), api_id_(api_id), api_hash_(api_hash) {
}

bool AuthManager::is_authorized() const {
  return state_ == State::Ok;
}

bool AuthManager::was_authorized() const {
  return state_ == State::Ok || state_ == State::LoggingOut || state_ == State::DestroyingKeys ||
         state_ == State::Closing;
}

void AuthManager::on_authorization_lost(string source) {
  LOG(WARNING) << "Authorization lost: " << source;
  state_ = State::LoggingOut;
}

void AuthManager::check_bot_token(uint64 query_id, string bot_token) {
  bot_token_ = std::move(bot_token);
  is_bot_ = true;
  net_query_type_ = NetQueryType::BotAuthentication;
  auto query = G()->net_query_creator().create_unauth(
      telegram_api::auth_importBotAuthorization(0, api_id_, api_hash_, bot_token_));
  query_id_ = query_id;
  net_query_id_ = query->id();
  G()->net_query_dispatcher().dispatch(std::move(query));
}

void AuthManager::on_closing(bool destroy_flag) {
  state_ = destroy_flag ? State::DestroyingKeys : State::Closing;
}

void AuthManager::on_result(NetQueryPtr net_query) {
  auto status = net_query->move_as_ok();
  if (net_query_type_ == NetQueryType::BotAuthentication) {
    if (net_query->is_error()) {
      LOG(ERROR) << "Bot auth failed: " << net_query->error();
      state_ = State::WaitPhoneNumber;
    } else {
      LOG(INFO) << "Bot auth succeeded";
      state_ = State::Ok;
    }
    net_query_type_ = NetQueryType::None;
  }
}

void AuthManager::start_up() {
  state_ = State::WaitPhoneNumber;
}

void AuthManager::tear_down() {
  state_ = State::Closing;
}

}  // namespace td
