//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Stripped ConfigManager – only DC config management for MTProto bootstrap.
//
#include "td/telegram/ConfigManager.h"

#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/net/ConnectionCreator.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/DcOptions.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"

#include "td/mtproto/RSA.h"

#include "td/actor/actor.h"

#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/Clocks.h"
#include "td/utils/Random.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Time.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/UInt.h"

#include <memory>
#include <utility>

namespace td {

int VERBOSITY_NAME(config_recoverer) = VERBOSITY_NAME(INFO);

Result<SimpleConfig> decode_config(Slice input) {
  static auto rsa = mtproto::RSA::from_pem_public_key(
                        "-----BEGIN RSA PUBLIC KEY-----\n"
                        "MIIBCgKCAQEAyr+18Rex2ohtVy8sroGP\n"
                        "BwXD3DOoKCSpjDqYoXgCqB7ioln4eDCFfOBUlfXUEvM/fnKCpF46VkA\n"
                        "ftlb4VuPDeQSS/ZxZYEGqHaywlroVnXHIjgqoxiAd192xRGreuXIaUK\n"
                        "mkwlM9JID9WS2jUsTpzQ91L8MEPLJ/4zrBwZua8W5fECwCCh2c9G5Iz\n"
                        "zBm+otMS/YKwmR1olzRCyEkyAEjXWqBI9Ftv5eG8m0VkBzOG655WIYd\n"
                        "yV0HfDK/NWcvGqa0w/nriMD6mDjKOryamw0OP9QuYgMN0C9xMW9y8Sm\n"
                        "P4h92OAWodTYgY1hZCxdv6cs5UnW9+PWvS+WIbkh+GaWYxwIDAQAB\n"
                        "-----END RSA PUBLIC KEY-----\n")
                        .move_as_ok();

  if (input.size() < 344 || input.size() > 1024) {
    return Status::Error(PSLICE() << "Invalid " << tag("length", input.size()));
  }

  auto data_base64 = base64_filter(input);
  if (data_base64.size() != 344) {
    return Status::Error(PSLICE() << "Invalid " << tag("length", data_base64.size()) << " after base64_filter");
  }
  TRY_RESULT(data_rsa, base64_decode(data_base64));
  if (data_rsa.size() != 256) {
    return Status::Error(PSLICE() << "Invalid " << tag("length", data_rsa.size()) << " after base64_decode");
  }

  MutableSlice data_rsa_slice(data_rsa);
  rsa.decrypt_signature(data_rsa_slice, data_rsa_slice);

  MutableSlice data_cbc = data_rsa_slice.substr(32);
  UInt256 key;
  UInt128 iv;
  as_mutable_slice(key).copy_from(data_rsa_slice.substr(0, 32));
  as_mutable_slice(iv).copy_from(data_rsa_slice.substr(16, 16));
  aes_cbc_decrypt(as_slice(key), as_mutable_slice(iv), data_cbc, data_cbc);

  CHECK(data_cbc.size() == 224);
  string hash(32, ' ');
  sha256(data_cbc.substr(0, 208), MutableSlice(hash));
  if (data_cbc.substr(208) != Slice(hash).substr(0, 16)) {
    return Status::Error("SHA256 mismatch");
  }

  TlParser len_parser{data_cbc};
  int len = len_parser.fetch_int();
  if (len < 8 || len > 208) {
    return Status::Error(PSLICE() << "Invalid " << tag("data length", len) << " after aes_cbc_decrypt");
  }
  int constructor_id = len_parser.fetch_int();
  if (constructor_id != telegram_api::help_configSimple::ID) {
    return Status::Error(PSLICE() << "Wrong " << tag("constructor", format::as_hex(constructor_id)));
  }
  BufferSlice raw_config(data_cbc.substr(8, len - 8));
  TlBufferParser parser{&raw_config};
  auto config = telegram_api::help_configSimple::fetch(parser);
  parser.fetch_end();
  TRY_STATUS(parser.get_status());
  return std::move(config);
}

// Stub simple config fetchers – full implementations require HTTP/DNS infrastructure.
// They are provided here so the linker is satisfied; actual simple config recovery
// can be wired up later when an HTTP stack is integrated.
ActorOwn<> get_simple_config_azure(Promise<SimpleConfigResult> promise, bool, Slice, bool, int32) {
  promise.set_error(Status::Error("Not implemented in stripped build"));
  return {};
}
ActorOwn<> get_simple_config_google_dns(Promise<SimpleConfigResult> promise, bool, Slice, bool, int32) {
  promise.set_error(Status::Error("Not implemented in stripped build"));
  return {};
}
ActorOwn<> get_simple_config_mozilla_dns(Promise<SimpleConfigResult> promise, bool, Slice, bool, int32) {
  promise.set_error(Status::Error("Not implemented in stripped build"));
  return {};
}
ActorOwn<> get_simple_config_firebase_remote_config(Promise<SimpleConfigResult> promise, bool, Slice, bool, int32) {
  promise.set_error(Status::Error("Not implemented in stripped build"));
  return {};
}
ActorOwn<> get_simple_config_firebase_realtime(Promise<SimpleConfigResult> promise, bool, Slice, bool, int32) {
  promise.set_error(Status::Error("Not implemented in stripped build"));
  return {};
}
ActorOwn<> get_simple_config_firebase_firestore(Promise<SimpleConfigResult> promise, bool, Slice, bool, int32) {
  promise.set_error(Status::Error("Not implemented in stripped build"));
  return {};
}

// Minimal ConfigRecoverer: no DNS/Firebase recovery in stripped build
class ConfigRecoverer final : public Actor {
 public:
  explicit ConfigRecoverer(ActorShared<> parent) : parent_(std::move(parent)) {
  }
  void on_dc_options_update(DcOptions dc_options) {
    // In full TDLib this feeds recovered DC endpoints into the system.
    // Stripped build relies on compiled-in DC list only.
  }

 private:
  ActorShared<> parent_;
  void hangup() final {
    stop();
  }
};

// ---------------------------------------------------------------------------
// ConfigManager
// ---------------------------------------------------------------------------

ConfigManager::ConfigManager(ActorShared<> parent) : parent_(std::move(parent)) {
  lazy_request_flood_control_.add_limit(2, 60);
}

ActorShared<> ConfigManager::create_reference() {
  ref_cnt_++;
  return actor_shared(this, REFCNT_TOKEN);
}

void ConfigManager::start_up() {
  config_recoverer_ = create_actor<ConfigRecoverer>("ConfigRecoverer", create_reference());
  auto dc_options_update = load_dc_options_update();
  if (!dc_options_update.dc_options.empty()) {
    send_closure(config_recoverer_, &ConfigRecoverer::on_dc_options_update, std::move(dc_options_update));
  }

  expire_time_ = load_config_expire_time();
  set_timeout_at(expire_time_.at());
}

void ConfigManager::hangup_shared() {
  ref_cnt_--;
  try_stop();
}

void ConfigManager::hangup() {
  config_recoverer_.reset();
  ref_cnt_--;
  try_stop();
}

void ConfigManager::loop() {
  if (expire_time_ && expire_time_.is_in_past()) {
    request_config(false);
    expire_time_ = {};
  }
}

void ConfigManager::try_stop() {
  if (ref_cnt_ == 0) {
    stop();
  }
}

void ConfigManager::request_config(bool reopen_sessions) {
  if (G()->close_flag()) {
    return;
  }
  if (config_sent_cnt_ != 0 && !reopen_sessions) {
    return;
  }
  lazy_request_flood_control_.add_event(Time::now());
  request_config_from_dc_impl(DcId::main(), reopen_sessions);
}

void ConfigManager::lazy_request_config() {
  if (G()->close_flag()) {
    return;
  }
  if (config_sent_cnt_ != 0) {
    return;
  }
  expire_time_.relax(Timestamp::at(lazy_request_flood_control_.get_wakeup_at()));
  set_timeout_at(expire_time_.at());
}

void ConfigManager::on_dc_options_update(DcOptions dc_options) {
  save_dc_options_update(dc_options);
  send_closure(G()->connection_creator(), &ConnectionCreator::on_dc_options, DcOptions(dc_options));
  send_closure(config_recoverer_, &ConfigRecoverer::on_dc_options_update, std::move(dc_options));
}

void ConfigManager::request_config_from_dc_impl(DcId dc_id, bool reopen_sessions) {
  config_sent_cnt_++;
  reopen_sessions_after_get_config_ |= reopen_sessions;
  auto query = G()->net_query_creator().create_unauth(telegram_api::help_getConfig(), dc_id);
  query->total_timeout_limit_ = 60 * 60 * 24;
  G()->net_query_dispatcher().dispatch_with_callback(std::move(query),
                                                     actor_shared(this, 8 + static_cast<uint64>(reopen_sessions)));
}

void ConfigManager::on_result(NetQueryPtr net_query) {
  auto token = get_link_token();
  CHECK(token == 8 || token == 9);
  CHECK(config_sent_cnt_ > 0);
  config_sent_cnt_--;
  auto r_config = fetch_result<telegram_api::help_getConfig>(std::move(net_query));
  if (r_config.is_error()) {
    if (!G()->close_flag()) {
      LOG(WARNING) << "Failed to get config: " << r_config.error();
      expire_time_ = Timestamp::in(60.0);
      set_timeout_in(expire_time_.in());
    }
  } else {
    on_dc_options_update(DcOptions());
    process_config(r_config.move_as_ok());
    if (token == 9) {
      G()->net_query_dispatcher().update_mtproto_header();
      reopen_sessions_after_get_config_ = false;
    }
  }
}

void ConfigManager::process_config(tl_object_ptr<telegram_api::config> config) {
  bool is_from_main_dc = G()->net_query_dispatcher().get_main_dc_id().get_value() == config->this_dc_;

  LOG(INFO) << to_string(config);
  auto reload_in = clamp(config->expires_ - config->date_, 60, 86400);
  save_config_expire(Timestamp::in(reload_in));
  reload_in -= Random::fast(0, reload_in / 5);
  if (!is_from_main_dc) {
    reload_in = 0;
  }
  expire_time_ = Timestamp::in(reload_in);
  set_timeout_at(expire_time_.at());
  LOG_IF(ERROR, config->test_mode_ != G()->is_test_dc()) << "Wrong parameter is_test";

  // Forward DC options to ConnectionCreator
  DcOptions dc_options(config->dc_options_);
  send_closure(G()->connection_creator(), &ConnectionCreator::on_dc_options, std::move(dc_options));

  // Store selected options
  G()->set_option_boolean("test_mode", config->test_mode_);
  if (is_from_main_dc || !G()->have_option("dc_txt_domain_name")) {
    G()->set_option_string("dc_txt_domain_name", config->dc_txt_domain_name_);
  }
  if (is_from_main_dc || !G()->have_option("t_me_url")) {
    auto url = config->me_url_prefix_;
    if (!url.empty()) {
      if (url.back() != '/') {
        url.push_back('/');
      }
      G()->set_option_string("t_me_url", url);
    }
  }
  G()->set_option_integer("webfile_dc_id", config->webfile_dc_id_);
}

void ConfigManager::save_dc_options_update(const DcOptions &dc_options) {
  if (dc_options.dc_options.empty()) {
    G()->td_db()->get_binlog_pmc()->erase("dc_options_update");
    return;
  }
  G()->td_db()->get_binlog_pmc()->set("dc_options_update", log_event_store(dc_options).as_slice().str());
}

DcOptions ConfigManager::load_dc_options_update() {
  auto log_event_dc_options = G()->td_db()->get_binlog_pmc()->get("dc_options_update");
  DcOptions dc_options;
  if (!log_event_dc_options.empty()) {
    log_event_parse(dc_options, log_event_dc_options).ensure();
  }
  return dc_options;
}

Timestamp ConfigManager::load_config_expire_time() {
  auto expires_in = to_integer<int32>(G()->td_db()->get_binlog_pmc()->get("config_expire")) - Clocks::system();
  if (expires_in < 0 || expires_in > 60 * 60 /* 1 hour */) {
    return Timestamp::now();
  } else {
    return Timestamp::in(expires_in);
  }
}

void ConfigManager::save_config_expire(Timestamp timestamp) {
  G()->td_db()->get_binlog_pmc()->set("config_expire", to_string(static_cast<int>(Clocks::system() + timestamp.in())));
}

}  // namespace td
