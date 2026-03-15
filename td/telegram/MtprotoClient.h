//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// MtprotoClient — the root Actor that wires together AuthManager, ConfigManager,
// ConnectionCreator, NetQueryDispatcher, and the rest of the MTProto stack.
// Replaces the original TDLib Td class per the engineering plan (Phase 4).
//
#pragma once

#include "td/telegram/net/NetQuery.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/Promise.h"

#include <functional>
#include <memory>

namespace td {

class AuthManager;
class ConfigManager;

// Helper actor that receives a NetQuery result and forwards to a Promise<BufferSlice>
class QueryResultHandler final : public NetQueryCallback {
 public:
  explicit QueryResultHandler(Promise<BufferSlice> promise) : promise_(std::move(promise)) {
  }
  void on_result(NetQueryPtr query) final {
    if (query->is_error()) {
      promise_.set_error(query->move_as_error());
    } else {
      promise_.set_value(query->move_as_ok());
    }
    stop();
  }

 private:
  Promise<BufferSlice> promise_;
};

class MtprotoClient final : public Actor {
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
    string bot_token;       // if non-empty, authenticate as bot on start
    string phone_number;    // if non-empty, start phone auth on start
  };

  explicit MtprotoClient(Options options);

  unique_ptr<AuthManager> auth_manager_;
  ActorOwn<AuthManager> auth_manager_actor_;

  // Send a raw MTProto request; result delivered via callback
  void send_query(NetQueryPtr query, ActorShared<NetQueryCallback> callback);

  // Send a raw MTProto query and deliver the raw buffer result
  void send_raw_query(NetQueryPtr query, Promise<BufferSlice> promise);

  // Send a typed MTProto function; serializes the request and parses the response
  template <class T>
  void send(telegram_api::object_ptr<T> function, Promise<typename T::ReturnType> promise) {
    send_raw_query_from_function(
        *function,
        PromiseCreator::lambda(
            [promise = std::move(promise)](Result<BufferSlice> r_buffer) mutable {
              if (r_buffer.is_error()) {
                promise.set_error(r_buffer.move_as_error());
                return;
              }
              auto buffer = r_buffer.move_as_ok();
              auto r_result = fetch_result<T>(buffer);
              if (r_result.is_error()) {
                promise.set_error(r_result.move_as_error());
              } else {
                promise.set_value(r_result.move_as_ok());
              }
            }));
  }

  // Non-template helper: creates NetQueryPtr from Function and dispatches with promise
  void send_raw_query_from_function(const telegram_api::Function &function, Promise<BufferSlice> promise);

  // Called when the net layer delivers a result for a dispatched query
  void on_result(NetQueryPtr query);

  // Called when an update is received from the server
  void on_update(tl_object_ptr<telegram_api::Updates> updates, uint64 auth_key_id);

  // Forward a td_api update (e.g. verification requests)
  void send_update(td_api::object_ptr<td_api::Update> update);

  // Bot token authentication
  void auth_with_bot_token(string bot_token, Promise<Unit> promise);

  // Phone authentication
  void send_code(string phone_number);
  void check_code(string code);

  // Auth state callback (forwarded from AuthManager)
  using AuthStateCallback = std::function<void(int state, const string &info)>;
  void set_auth_state_callback(AuthStateCallback callback);

  // Update handler
  using UpdateHandler = std::function<void(tl_object_ptr<telegram_api::Updates>)>;
  void set_update_handler(UpdateHandler handler);

  ActorShared<MtprotoClient> create_reference();

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
