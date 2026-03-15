//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MtprotoClient.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ConfigManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/net/ConnectionCreator.h"
#include "td/telegram/net/MtprotoHeader.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/net/NetQueryStats.h"
#include "td/telegram/net/TempAuthKeyWatchdog.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/TdDb.h"

#include "td/utils/logging.h"

namespace td {

static constexpr int64 ActorIdType = 1;
static constexpr int64 RequestActorIdType = 2;

MtprotoClient::MtprotoClient(Options options) : options_(std::move(options)) {
}

void MtprotoClient::start_up() {
  LOG(INFO) << "MtprotoClient::start_up — wiring MTProto stack";

  // Create Global context
  old_context_ = set_context(std::make_shared<Global>());
  G()->set_net_query_stats(std::make_shared<NetQueryStats>());
  inc_actor_refcnt();  // prevent premature destruction

  // Init Global with DC config
  G()->init(options_.dc_id, options_.is_test_dc).ensure();

  // Set ourselves as the root actor
  G()->set_td(actor_id(this));

  // Create TdDb (in-memory)
  G()->set_td_db(make_unique<TdDb>());

  init();
}

void MtprotoClient::init() {
  // Create StateManager
  G()->set_state_manager(create_actor<StateManager>("StateManager", create_reference()));

  // Create ConnectionCreator
  G()->set_connection_creator(create_actor<ConnectionCreator>("ConnectionCreator", create_reference()));

  // Create TempAuthKeyWatchdog
  G()->set_temp_auth_key_watchdog(create_actor<TempAuthKeyWatchdog>("TempAuthKeyWatchdog", create_reference()));

  // Set up MtprotoHeader
  MtprotoHeader::Options header_options;
  header_options.api_id = options_.api_id;
  header_options.device_model = options_.device_model;
  header_options.system_version = options_.system_version;
  header_options.application_version = options_.application_version;
  header_options.system_language_code = options_.system_language_code;
  header_options.language_pack = options_.language_pack;
  header_options.language_code = options_.language_code;
  G()->set_mtproto_header(make_unique<MtprotoHeader>(header_options));

  // Create NetQueryDispatcher
  auto net_query_dispatcher = make_unique<NetQueryDispatcher>([this] { return create_reference(); });
  G()->set_net_query_dispatcher(std::move(net_query_dispatcher));

  // Create AuthManager
  auth_manager_ = unique_ptr<AuthManager>(new AuthManager(options_.api_id, options_.api_hash,
                                                          ActorShared<>(actor_shared(this, ActorIdType))));
  auth_manager_actor_ = register_actor("AuthManager", auth_manager_.get());
  G()->set_auth_manager(auth_manager_actor_.get());

  // Create ConfigManager
  config_manager_ = create_actor<ConfigManager>("ConfigManager", create_reference());
  G()->set_config_manager(config_manager_.get());

  // Auto-authenticate with bot token if provided
  if (!options_.bot_token.empty()) {
    auth_manager_->check_bot_token(0, std::move(options_.bot_token));
  }

  // Auto-start phone auth if provided
  if (!options_.phone_number.empty()) {
    auth_manager_->send_code(std::move(options_.phone_number));
  }

  LOG(INFO) << "MtprotoClient initialized — MTProto stack wired";
}

void MtprotoClient::on_result(NetQueryPtr query) {
  LOG(DEBUG) << "MtprotoClient::on_result for query " << query->id();
}

void MtprotoClient::on_update(tl_object_ptr<telegram_api::Updates> updates, uint64 auth_key_id) {
  if (update_handler_) {
    update_handler_(std::move(updates));
  } else {
    LOG(WARNING) << "Received update but no handler set";
  }
}

void MtprotoClient::send_update(td_api::object_ptr<td_api::Update> update) {
  LOG(DEBUG) << "MtprotoClient::send_update: " << to_string(update);
}

void MtprotoClient::auth_with_bot_token(string bot_token, Promise<Unit> promise) {
  CHECK(auth_manager_ != nullptr);
  auth_manager_->check_bot_token(0, std::move(bot_token));
  promise.set_value(Unit());
}

void MtprotoClient::send_code(string phone_number) {
  CHECK(auth_manager_ != nullptr);
  auth_manager_->send_code(std::move(phone_number));
}

void MtprotoClient::check_code(string code) {
  CHECK(auth_manager_ != nullptr);
  auth_manager_->check_code(std::move(code));
}

void MtprotoClient::set_auth_state_callback(AuthStateCallback callback) {
  auth_manager_->set_auth_state_callback(
      [cb = std::move(callback)](AuthManager::AuthState state, const string &info) {
        cb(static_cast<int>(state), info);
      });
}

void MtprotoClient::set_update_handler(UpdateHandler handler) {
  update_handler_ = std::move(handler);
}

void MtprotoClient::send_query(NetQueryPtr query, ActorShared<NetQueryCallback> callback) {
  G()->net_query_dispatcher().dispatch_with_callback(std::move(query), std::move(callback));
}

void MtprotoClient::send_raw_query(NetQueryPtr query, Promise<BufferSlice> promise) {
  auto handler_actor = create_actor<QueryResultHandler>("QueryResultHandler", std::move(promise));
  G()->net_query_dispatcher().dispatch_with_callback(std::move(query), std::move(handler_actor));
}

void MtprotoClient::send_raw_query_from_function(const telegram_api::Function &function, Promise<BufferSlice> promise) {
  auto query = G()->net_query_creator().create(function);
  send_raw_query(std::move(query), std::move(promise));
}

ActorShared<MtprotoClient> MtprotoClient::create_reference() {
  inc_actor_refcnt();
  return actor_shared(this, ActorIdType);
}

void MtprotoClient::inc_actor_refcnt() {
  actor_refcnt_++;
}

void MtprotoClient::dec_actor_refcnt() {
  actor_refcnt_--;
  if (actor_refcnt_ == 0) {
    stop();
  }
}

void MtprotoClient::inc_request_actor_refcnt() {
  request_actor_refcnt_++;
}

void MtprotoClient::dec_request_actor_refcnt() {
  request_actor_refcnt_--;
}

void MtprotoClient::hangup_shared() {
  dec_actor_refcnt();
}

void MtprotoClient::tear_down() {
  LOG(INFO) << "MtprotoClient::tear_down";
}

}  // namespace td
