//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Td — internal actor that wires together AuthManager, ConfigManager,
// ConnectionCreator, NetQueryDispatcher, and the rest of the MTProto stack.
//
#pragma once

#include "td/telegram/net/NetQuery.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"

#include <functional>
#include <memory>

namespace td {

class AuthManager;
class ConfigManager;

class Td final : public Actor {
 public:
  struct Options {
    int32 api_id = 0;
    string api_hash;
    int32 dc_id = 2;
    bool is_test_dc = false;
    string device_model = "Server";
    string system_version = "1.0";
    string application_version = "1.0";
    string system_language_code = "en";
    string language_pack;
    string language_code = "en";
    string bot_token;  // if non-empty, authenticate as bot on start
  };

  explicit Td(Options options);

  unique_ptr<AuthManager> auth_manager_;
  ActorOwn<AuthManager> auth_manager_actor_;

  // Send a raw MTProto request; result delivered via callback
  void send_query(NetQueryPtr query, ActorShared<NetQueryCallback> callback);

  // Called when the net layer delivers a result for a dispatched query
  void on_result(NetQueryPtr query);

  // Called when an update is received from the server
  void on_update(tl_object_ptr<telegram_api::Updates> updates, uint64 auth_key_id);

  // Forward a td_api update (e.g. verification requests)
  void send_update(td_api::object_ptr<td_api::Update> update);

  // Bot token authentication
  void auth_with_bot_token(string bot_token, Promise<Unit> promise);

  // Update handler
  using UpdateHandler = std::function<void(tl_object_ptr<telegram_api::Updates>)>;
  void set_update_handler(UpdateHandler handler);

  ActorShared<Td> create_reference();

 private:
  Options options_;
  UpdateHandler update_handler_;
  std::shared_ptr<ActorContext> old_context_;

  ActorOwn<ConfigManager> config_manager_;

  int32 actor_refcnt_ = 0;
  int32 request_actor_refcnt_ = 0;

  void inc_actor_refcnt();
  void dec_actor_refcnt();
  void inc_request_actor_refcnt();
  void dec_request_actor_refcnt();

  void start_up() final;
  void tear_down() final;
  void hangup_shared() final;

  void init();
};

}  // namespace td
