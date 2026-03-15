//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Stripped ConfigManager – only DC config / options management for MTProto layer.
//
#pragma once

#include "td/telegram/net/DcId.h"
#include "td/telegram/net/DcOptions.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/FloodControlStrict.h"
#include "td/utils/logging.h"
#include "td/utils/Promise.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

#include <limits>

namespace td {

extern int VERBOSITY_NAME(config_recoverer);

using SimpleConfig = tl_object_ptr<telegram_api::help_configSimple>;
struct SimpleConfigResult {
  Result<SimpleConfig> r_config;
  Result<int32> r_http_date;
};

Result<SimpleConfig> decode_config(Slice input);

ActorOwn<> get_simple_config_azure(Promise<SimpleConfigResult> promise, bool prefer_ipv6, Slice domain_name,
                                   bool is_test, int32 scheduler_id);

ActorOwn<> get_simple_config_google_dns(Promise<SimpleConfigResult> promise, bool prefer_ipv6, Slice domain_name,
                                        bool is_test, int32 scheduler_id);

ActorOwn<> get_simple_config_mozilla_dns(Promise<SimpleConfigResult> promise, bool prefer_ipv6, Slice domain_name,
                                         bool is_test, int32 scheduler_id);

ActorOwn<> get_simple_config_firebase_remote_config(Promise<SimpleConfigResult> promise, bool prefer_ipv6,
                                                    Slice domain_name, bool is_test, int32 scheduler_id);

ActorOwn<> get_simple_config_firebase_realtime(Promise<SimpleConfigResult> promise, bool prefer_ipv6, Slice domain_name,
                                               bool is_test, int32 scheduler_id);

ActorOwn<> get_simple_config_firebase_firestore(Promise<SimpleConfigResult> promise, bool prefer_ipv6,
                                                Slice domain_name, bool is_test, int32 scheduler_id);

class ConfigRecoverer;

class ConfigManager final : public NetQueryCallback {
 public:
  explicit ConfigManager(ActorShared<> parent);

  void request_config(bool reopen_sessions);

  void lazy_request_config();

  void on_dc_options_update(DcOptions dc_options);

 private:
  ActorShared<> parent_;
  int32 config_sent_cnt_{0};
  bool reopen_sessions_after_get_config_{false};
  ActorOwn<ConfigRecoverer> config_recoverer_;
  int ref_cnt_{1};
  Timestamp expire_time_;

  FloodControlStrict lazy_request_flood_control_;

  static constexpr uint64 REFCNT_TOKEN = std::numeric_limits<uint64>::max() - 2;

  void start_up() final;
  void hangup_shared() final;
  void hangup() final;
  void loop() final;
  void try_stop();

  void on_result(NetQueryPtr net_query) final;

  void request_config_from_dc_impl(DcId dc_id, bool reopen_sessions);
  void process_config(tl_object_ptr<telegram_api::config> config);

  static void save_dc_options_update(const DcOptions &dc_options);
  static DcOptions load_dc_options_update();

  static Timestamp load_config_expire_time();
  static void save_config_expire(Timestamp timestamp);

  ActorShared<> create_reference();
};

}  // namespace td
