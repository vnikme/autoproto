//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Stripped AuthManager: bot + phone authentication for MTProto layer.
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

void AuthManager::send_code(string phone_number) {
  phone_number_ = std::move(phone_number);
  net_query_type_ = NetQueryType::SendCode;
  auto settings = telegram_api::make_object<telegram_api::codeSettings>(
      0, false, false, false, false, false, false, std::vector<td::BufferSlice>{}, string(), false);
  auto query = G()->net_query_creator().create_unauth(
      telegram_api::auth_sendCode(phone_number_, api_id_, api_hash_, std::move(settings)));
  net_query_id_ = query->id();
  G()->net_query_dispatcher().dispatch(std::move(query));
}

void AuthManager::check_code(string code) {
  net_query_type_ = NetQueryType::SignIn;
  auto query = G()->net_query_creator().create_unauth(telegram_api::auth_signIn(
      telegram_api::auth_signIn::PHONE_CODE_MASK, phone_number_, phone_code_hash_, std::move(code), nullptr));
  net_query_id_ = query->id();
  G()->net_query_dispatcher().dispatch(std::move(query));
}

void AuthManager::on_closing(bool destroy_flag) {
  state_ = destroy_flag ? State::DestroyingKeys : State::Closing;
}

void AuthManager::on_result(NetQueryPtr net_query) {
  if (net_query_type_ == NetQueryType::BotAuthentication) {
    if (net_query->is_error()) {
      LOG(ERROR) << "Bot auth failed: " << net_query->error();
      state_ = State::WaitPhoneNumber;
      notify_state(AuthState::Error, PSTRING() << net_query->error());
    } else {
      auto status = net_query->move_as_ok();
      LOG(INFO) << "Bot auth succeeded";
      state_ = State::Ok;
      notify_state(AuthState::Ok);
    }
    net_query_type_ = NetQueryType::None;
  } else if (net_query_type_ == NetQueryType::SendCode) {
    if (net_query->is_error()) {
      LOG(ERROR) << "send_code failed: " << net_query->error();
      notify_state(AuthState::Error, PSTRING() << net_query->error());
    } else {
      auto buffer = net_query->move_as_ok();
      auto r_sent_code = fetch_result<telegram_api::auth_sendCode>(buffer);
      if (r_sent_code.is_error()) {
        LOG(ERROR) << "Failed to parse auth.sentCode: " << r_sent_code.error();
        notify_state(AuthState::Error, PSTRING() << r_sent_code.error());
      } else {
        auto sent_code = r_sent_code.move_as_ok();
        if (sent_code->get_id() == telegram_api::auth_sentCode::ID) {
          auto *sc = static_cast<telegram_api::auth_sentCode *>(sent_code.get());
          phone_code_hash_ = std::move(sc->phone_code_hash_);
          state_ = State::WaitCode;
          LOG(INFO) << "Code sent, waiting for user input";
          notify_state(AuthState::WaitCode);
        } else {
          LOG(INFO) << "auth_sentCodeSuccess — already authorized";
          state_ = State::Ok;
          notify_state(AuthState::Ok);
        }
      }
    }
    net_query_type_ = NetQueryType::None;
  } else if (net_query_type_ == NetQueryType::SignIn) {
    if (net_query->is_error()) {
      LOG(ERROR) << "sign_in failed: " << net_query->error();
      state_ = State::WaitCode;
      notify_state(AuthState::Error, PSTRING() << net_query->error());
    } else {
      auto status = net_query->move_as_ok();
      LOG(INFO) << "User auth succeeded";
      state_ = State::Ok;
      notify_state(AuthState::Ok);
    }
    net_query_type_ = NetQueryType::None;
  }
}

void AuthManager::notify_state(AuthState auth_state, const string &info) {
  if (auth_state_callback_) {
    auth_state_callback_(auth_state, info);
  }
}

void AuthManager::start_up() {
  state_ = State::WaitPhoneNumber;
}

void AuthManager::tear_down() {
  state_ = State::Closing;
}

}  // namespace td
