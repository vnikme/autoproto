//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DhConfig.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/MtprotoHeader.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/TdDb.h"

#include "td/net/NetStats.h"

#include "td/actor/actor.h"
#include "td/actor/SchedulerLocalStorage.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/logging.h"
#include "td/utils/Promise.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace td {

class AuthManager;
class ConfigManager;
class ConnectionCreator;
class NetQueryDispatcher;
class NetQueryStats;
class StateManager;
class Td;
class TempAuthKeyWatchdog;

class Global final : public ActorContext {
 public:
  Global();
  ~Global() final;
  Global(const Global &) = delete;
  Global &operator=(const Global &) = delete;
  Global(Global &&) = delete;
  Global &operator=(Global &&) = delete;

  static constexpr int32 ID = -572104940;
  int32 get_id() const final {
    return ID;
  }

  void log_out(Slice reason);

  void close_all(Promise<> on_finished);

  Status init(int32 dc_id, bool is_test_dc) TD_WARN_UNUSED_RESULT;

  bool is_test_dc() const {
    return is_test_dc_;
  }

  NetQueryCreator &net_query_creator() {
    return *net_query_creator_.get();
  }

  void set_net_query_stats(std::shared_ptr<NetQueryStats> net_query_stats);

  void set_net_query_dispatcher(unique_ptr<NetQueryDispatcher> net_query_dispatcher);

  NetQueryDispatcher &net_query_dispatcher() {
    CHECK(have_net_query_dispatcher());
    return *net_query_dispatcher_;
  }

  bool have_net_query_dispatcher() const {
    return net_query_dispatcher_.get() != nullptr;
  }

  void set_option_empty(Slice name);
  void set_option_boolean(Slice name, bool value);
  void set_option_integer(Slice name, int64 value);
  void set_option_string(Slice name, Slice value);

  bool have_option(Slice name) const;
  bool get_option_boolean(Slice name, bool default_value = false) const;
  int64 get_option_integer(Slice name, int64 default_value = 0) const;
  string get_option_string(Slice name, string default_value = "") const;

  bool is_server_time_reliable() const {
    return server_time_difference_was_updated_.load(std::memory_order_relaxed);
  }
  double server_time() const {
    return Time::now() + get_server_time_difference();
  }
  int32 unix_time() const {
    return to_unix_time(server_time());
  }

  void update_server_time_difference(double diff, bool force);

  void save_server_time();

  double get_server_time_difference() const {
    return server_time_difference_.load(std::memory_order_relaxed);
  }

  void update_dns_time_difference(double diff);

  double get_dns_time_difference() const;

  ActorId<AuthManager> auth_manager() const {
    return auth_manager_;
  }
  void set_auth_manager(ActorId<AuthManager> auth_manager) {
    auth_manager_ = auth_manager;
  }

  ActorId<ConfigManager> config_manager() const {
    return config_manager_;
  }
  void set_config_manager(ActorId<ConfigManager> config_manager) {
    config_manager_ = config_manager;
  }

  ActorId<ConnectionCreator> connection_creator() const;
  void set_connection_creator(ActorOwn<ConnectionCreator> connection_creator);

  ActorId<TempAuthKeyWatchdog> temp_auth_key_watchdog() const;
  void set_temp_auth_key_watchdog(ActorOwn<TempAuthKeyWatchdog> actor);

  ActorId<Td> td() const {
    return td_;
  }
  void set_td(ActorId<Td> td) {
    td_ = td;
  }

  ActorId<StateManager> state_manager() const {
    return state_manager_.get();
  }
  void set_state_manager(ActorOwn<StateManager> state_manager);

  TdDb *td_db() {
    return td_db_.get();
  }
  void set_td_db(unique_ptr<TdDb> td_db) {
    td_db_ = std::move(td_db);
  }

  MtprotoHeader &mtproto_header();
  void set_mtproto_header(unique_ptr<MtprotoHeader> mtproto_header);
  bool have_mtproto_header() const {
    return mtproto_header_ != nullptr;
  }

  int32 get_gc_scheduler_id() const {
    return gc_scheduler_id_;
  }

  int32 get_slow_net_scheduler_id() const {
    return slow_net_scheduler_id_;
  }

  int32 get_main_session_scheduler_id() const {
    return gc_scheduler_id_;
  }

  void notify_speed_limited(int32 error_code) {
    // no-op stub
  }

  DcId get_webfile_dc_id() const;

  std::shared_ptr<DhConfig> get_dh_config() {
#if !TD_HAVE_ATOMIC_SHARED_PTR
    std::lock_guard<std::mutex> guard(dh_config_mutex_);
    auto res = dh_config_;
    return res;
#else
    return atomic_load(&dh_config_);
#endif
  }

  void set_dh_config(std::shared_ptr<DhConfig> new_dh_config) {
#if !TD_HAVE_ATOMIC_SHARED_PTR
    std::lock_guard<std::mutex> guard(dh_config_mutex_);
    dh_config_ = std::move(new_dh_config);
#else
    atomic_store(&dh_config_, std::move(new_dh_config));
#endif
  }

  static Status request_aborted_error() {
    return Status::Error(500, "Request aborted");
  }

  template <class T>
  void ignore_result_if_closing(Result<T> &result) const {
    if (close_flag() && result.is_ok()) {
      result = request_aborted_error();
    }
  }

  void set_close_flag() {
    close_flag_ = true;
  }
  bool close_flag() const {
    return close_flag_.load();
  }

  Status close_status() const {
    return close_flag() ? request_aborted_error() : Status::OK();
  }

  bool is_expected_error(const Status &error) const {
    CHECK(error.is_error());
    if (error.code() == 401) {
      return true;
    }
    if (error.code() == 420 || error.code() == 429) {
      return true;
    }
    return close_flag();
  }

  static int32 get_retry_after(int32 error_code, Slice error_message);

  static int32 get_retry_after(const Status &error) {
    return get_retry_after(error.code(), error.message());
  }

  const std::vector<std::shared_ptr<NetStatsCallback>> &get_net_stats_file_callbacks() {
    return net_stats_file_callbacks_;
  }
  void set_net_stats_file_callbacks(std::vector<std::shared_ptr<NetStatsCallback>> callbacks) {
    net_stats_file_callbacks_ = std::move(callbacks);
  }

 private:
  std::shared_ptr<DhConfig> dh_config_;

  bool is_test_dc_ = false;

  ActorId<AuthManager> auth_manager_;
  ActorId<ConfigManager> config_manager_;
  ActorOwn<ConnectionCreator> connection_creator_;
  ActorOwn<TempAuthKeyWatchdog> temp_auth_key_watchdog_;
  ActorId<Td> td_;
  ActorOwn<StateManager> state_manager_;
  unique_ptr<TdDb> td_db_;

  unique_ptr<MtprotoHeader> mtproto_header_;

  int32 gc_scheduler_id_ = 0;
  int32 slow_net_scheduler_id_ = 0;

  std::atomic<double> server_time_difference_{0.0};
  std::atomic<bool> server_time_difference_was_updated_{false};
  std::atomic<double> dns_time_difference_{0.0};
  std::atomic<bool> dns_time_difference_was_updated_{false};
  std::atomic<bool> close_flag_{false};
  std::atomic<double> system_time_saved_at_{-1e10};
  double saved_diff_ = 0.0;
  double saved_system_time_ = 0.0;

#if !TD_HAVE_ATOMIC_SHARED_PTR
  std::mutex dh_config_mutex_;
#endif

  std::vector<std::shared_ptr<NetStatsCallback>> net_stats_file_callbacks_;

  LazySchedulerLocalStorage<unique_ptr<NetQueryCreator>> net_query_creator_;
  unique_ptr<NetQueryDispatcher> net_query_dispatcher_;

  // Simple in-memory option store (replaces OptionManager)
  FlatHashMap<string, string> options_string_;
  FlatHashMap<string, int64> options_integer_;
  FlatHashMap<string, bool> options_boolean_;

  int32 to_unix_time(double server_time) const;

  void do_save_server_time_difference();
};

#define G() G_impl(__FILE__, __LINE__)

inline Global *G_impl(const char *file, int line) {
  ActorContext *context = Scheduler::context();
  LOG_CHECK(context != nullptr && context->get_id() == Global::ID)
      << "Context = " << context << " in " << file << " at " << line;
  return static_cast<Global *>(context);
}

double get_global_server_time();

}  // namespace td
