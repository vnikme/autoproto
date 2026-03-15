//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Stripped AuthManager – bot and phone authentication for MTProto layer.
//
#pragma once

#include "td/telegram/net/NetActor.h"
#include "td/telegram/net/NetQuery.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <functional>

namespace td {

class AuthManager final : public NetActor {
 public:
  AuthManager(int32 api_id, const string &api_hash, ActorShared<> parent);

  bool is_bot() const {
    return is_bot_ || net_query_type_ == NetQueryType::BotAuthentication;
  }

  bool is_authorized() const;
  bool was_authorized() const;

  void check_bot_token(uint64 query_id, string bot_token);

  // Phone-based auth
  void send_code(string phone_number);
  void check_code(string code);

  const string &phone_code_hash() const {
    return phone_code_hash_;
  }

  // Auth state callback
  enum class AuthState { WaitPhoneNumber, WaitCode, Ok, Error };
  using AuthStateCallback = std::function<void(AuthState, const string &)>;
  void set_auth_state_callback(AuthStateCallback callback) {
    auth_state_callback_ = std::move(callback);
  }

  void on_authorization_lost(string source);
  void on_closing(bool destroy_flag);

 private:
  enum class State : int32 {
    None,
    WaitPhoneNumber,
    WaitCode,
    Ok,
    LoggingOut,
    DestroyingKeys,
    Closing
  } state_ = State::None;

  enum class NetQueryType : int32 { None, BotAuthentication, SendCode, SignIn };

  ActorShared<> parent_;
  int32 api_id_;
  string api_hash_;
  string bot_token_;
  string phone_number_;
  string phone_code_hash_;
  bool is_bot_ = false;
  uint64 query_id_ = 0;
  uint64 net_query_id_ = 0;
  NetQueryType net_query_type_ = NetQueryType::None;
  AuthStateCallback auth_state_callback_;

  void on_result(NetQueryPtr net_query) final;
  void start_up() final;
  void tear_down() final;
  void notify_state(AuthState state, const string &info = {});
};

}  // namespace td
