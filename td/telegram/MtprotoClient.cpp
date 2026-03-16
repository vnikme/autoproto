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
#include "td/telegram/net/AuthDataShared.h"
#include "td/telegram/net/AuthKeyState.h"
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
  td_db_raw_ = G()->td_db();

  // Import session if provided
  if (!options_.session_data.empty()) {
    LOG(INFO) << "Session import: attempting to restore from " << options_.session_data.size() << " chars";
    if (!G()->td_db()->import_session(options_.session_data)) {
      LOG(ERROR) << "Failed to import session data (" << options_.session_data.size() << " chars) — starting fresh";
    } else {
      LOG(INFO) << "Session data imported successfully (" << options_.session_data.size() << " chars)";
    }
  }

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
  auth_manager_ = unique_ptr<AuthManager>(
      new AuthManager(options_.api_id, options_.api_hash, ActorShared<>(actor_shared(this, ActorIdType))));
  auth_manager_actor_ = register_actor("AuthManager", auth_manager_.get());
  G()->set_auth_manager(auth_manager_actor_.get());

  // Create ConfigManager
  config_manager_ = create_actor<ConfigManager>("ConfigManager", create_reference());
  G()->set_config_manager(config_manager_.get());

  // Apply pending auth state callback
  if (pending_auth_state_callback_) {
    auth_manager_->set_auth_state_callback(
        [cb = std::move(pending_auth_state_callback_)](AuthManager::AuthState state, const string &info) {
          cb(static_cast<int>(state), info);
        });
  }

  // Auto-authenticate — but skip if session already has a valid auth key
  // The actual main DC may differ from options_.dc_id after DC migration
  int32 effective_dc_id = options_.dc_id;
  auto s_main_dc_id = G()->td_db()->get_binlog_pmc()->get("main_dc_id");
  if (!s_main_dc_id.empty()) {
    effective_dc_id = to_integer<int32>(s_main_dc_id);
  }
  auto main_dc_key = AuthDataShared::get_auth_key_for_dc(DcId::internal(effective_dc_id));
  auto key_state = get_auth_key_state(main_dc_key);
  if (key_state == AuthKeyState::OK) {
    LOG(INFO) << "Session has valid auth key for DC" << options_.dc_id << " — skipping auth flow";
    auth_manager_->set_authorized();
  } else if (!options_.bot_token.empty()) {
    auth_manager_->check_bot_token(0, std::move(options_.bot_token));
  } else if (!options_.phone_number.empty()) {
    auth_manager_->send_code(std::move(options_.phone_number));
  }

  LOG(INFO) << "MtprotoClient initialized — MTProto stack wired";

  // Signal that network is available so Session and ConnectionCreator start working
  send_closure(G()->state_manager(), &StateManager::on_network, NetType::WiFi);
  send_closure(G()->state_manager(), &StateManager::on_online, true);
}

void MtprotoClient::on_result(NetQueryPtr query) {
  LOG(DEBUG) << "MtprotoClient::on_result for query " << query->id();
}

void MtprotoClient::on_update(tl_object_ptr<telegram_api::Updates> updates, uint64 auth_key_id) {
  LOG(INFO) << "MtprotoClient::on_update type_id=" << (updates ? updates->get_id() : 0);
  if (updates && updates->get_id() == telegram_api::updatesTooLong::ID) {
    LOG(INFO) << "Received updatesTooLong — starting one-shot sync";
    handle_updates_too_long();
    return;
  }
  if (update_handler_) {
    update_handler_(std::move(updates));
  } else {
    LOG(WARNING) << "Received update but no handler set";
  }
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

void MtprotoClient::check_password(string password) {
  CHECK(auth_manager_ != nullptr);
  auth_manager_->check_password(std::move(password));
}

void MtprotoClient::set_auth_state_callback(AuthStateCallback callback) {
  if (auth_manager_) {
    auth_manager_->set_auth_state_callback(
        [cb = std::move(callback)](AuthManager::AuthState state, const string &info) {
          cb(static_cast<int>(state), info);
        });
  } else {
    pending_auth_state_callback_ = std::move(callback);
  }
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

string MtprotoClient::export_session() const {
  if (td_db_raw_ == nullptr) {
    return string();
  }
  return td_db_raw_->export_session();
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
  if (G()->have_net_query_dispatcher()) {
    G()->net_query_dispatcher().stop();
  }
  // Do NOT destroy the dispatcher here — session actors may still be processing
  // during scheduler shutdown. The Global destructor will clean it up.
}

void MtprotoClient::handle_updates_too_long() {
  if (sync_pending_) {
    LOG(INFO) << "Sync already in progress, ignoring duplicate updatesTooLong";
    return;
  }
  sync_pending_ = true;
  send(telegram_api::make_object<telegram_api::updates_getState>(),
       PromiseCreator::lambda(
           [self = actor_id(this)](Result<tl_object_ptr<telegram_api::updates_state>> r_state) mutable {
             send_closure(self, &MtprotoClient::on_get_state_result, std::move(r_state));
           }));
}

void MtprotoClient::on_get_state_result(Result<tl_object_ptr<telegram_api::updates_state>> r_state) {
  if (r_state.is_error()) {
    LOG(ERROR) << "updates.getState failed: " << r_state.error();
    sync_pending_ = false;
    return;
  }
  auto state = r_state.move_as_ok();
  LOG(INFO) << "updates.getState: pts=" << state->pts_ << " qts=" << state->qts_
            << " date=" << state->date_ << " seq=" << state->seq_;
  run_get_difference(state->pts_, state->date_, state->qts_);
}

void MtprotoClient::run_get_difference(int32 pts, int32 date, int32 qts) {
  LOG(INFO) << "Running updates.getDifference pts=" << pts << " date=" << date << " qts=" << qts;
  send(telegram_api::make_object<telegram_api::updates_getDifference>(0, pts, 0, 0, date, qts, 0),
       PromiseCreator::lambda(
           [self = actor_id(this)](Result<tl_object_ptr<telegram_api::updates_Difference>> r_diff) mutable {
             send_closure(self, &MtprotoClient::on_get_difference_result, std::move(r_diff));
           }));
}

void MtprotoClient::on_get_difference_result(Result<tl_object_ptr<telegram_api::updates_Difference>> r_diff) {
  sync_pending_ = false;
  if (r_diff.is_error()) {
    LOG(ERROR) << "updates.getDifference failed: " << r_diff.error();
    return;
  }
  auto diff = r_diff.move_as_ok();
  switch (diff->get_id()) {
    case telegram_api::updates_differenceEmpty::ID:
      LOG(INFO) << "getDifference: empty — caught up";
      break;
    case telegram_api::updates_difference::ID: {
      auto *d = static_cast<telegram_api::updates_difference *>(diff.get());
      LOG(INFO) << "getDifference: " << d->new_messages_.size() << " messages, "
                << d->other_updates_.size() << " other updates";
      deliver_difference_updates(std::move(d->new_messages_), std::move(d->other_updates_),
                                 std::move(d->chats_), std::move(d->users_));
      break;
    }
    case telegram_api::updates_differenceSlice::ID: {
      auto *d = static_cast<telegram_api::updates_differenceSlice *>(diff.get());
      LOG(INFO) << "getDifference: slice with " << d->new_messages_.size() << " messages";
      deliver_difference_updates(std::move(d->new_messages_), std::move(d->other_updates_),
                                 std::move(d->chats_), std::move(d->users_));
      break;
    }
    case telegram_api::updates_differenceTooLong::ID:
      LOG(INFO) << "getDifference: too long — some updates skipped";
      break;
    default:
      break;
  }
}

void MtprotoClient::deliver_difference_updates(std::vector<tl_object_ptr<telegram_api::Message>> msgs,
                                               std::vector<tl_object_ptr<telegram_api::Update>> other,
                                               std::vector<tl_object_ptr<telegram_api::Chat>> chats,
                                               std::vector<tl_object_ptr<telegram_api::User>> users) {
  if (msgs.empty() && other.empty()) {
    return;
  }
  for (auto &msg : msgs) {
    other.push_back(telegram_api::make_object<telegram_api::updateNewMessage>(std::move(msg), 0, 0));
  }
  auto upd = telegram_api::make_object<telegram_api::updates>();
  upd->updates_ = std::move(other);
  upd->users_ = std::move(users);
  upd->chats_ = std::move(chats);
  if (update_handler_) {
    update_handler_(std::move(upd));
  }
}

}  // namespace td
