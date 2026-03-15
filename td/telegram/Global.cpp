//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Global.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/net/ConnectionCreator.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/net/NetQueryStats.h"
#include "td/telegram/net/TempAuthKeyWatchdog.h"
#include "td/telegram/StateManager.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/Clocks.h"

#include <cmath>

namespace td {

Global::Global() {
  auto current_scheduler_id = Scheduler::instance()->sched_id();
  auto max_scheduler_id = Scheduler::instance()->sched_count() - 1;
  gc_scheduler_id_ = min(current_scheduler_id + 2, max_scheduler_id);
  slow_net_scheduler_id_ = min(current_scheduler_id + 3, max_scheduler_id);
}

Global::~Global() = default;

void Global::log_out(Slice reason) {
  send_closure(auth_manager_, &AuthManager::on_authorization_lost, reason.str());
}

void Global::close_all(Promise<> on_finished) {
  set_close_flag();
  on_finished.set_value(Unit());
}

Status Global::init(int32 dc_id, bool is_test_dc) {
  is_test_dc_ = is_test_dc;

  auto system_time = Clocks::system();
  auto default_time_difference = system_time - Time::now();
  server_time_difference_ = default_time_difference;
  server_time_difference_was_updated_ = false;
  dns_time_difference_ = default_time_difference;
  dns_time_difference_was_updated_ = false;

  return Status::OK();
}

ActorId<ConnectionCreator> Global::connection_creator() const {
  return connection_creator_.get();
}
void Global::set_connection_creator(ActorOwn<ConnectionCreator> connection_creator) {
  connection_creator_ = std::move(connection_creator);
}

ActorId<TempAuthKeyWatchdog> Global::temp_auth_key_watchdog() const {
  return temp_auth_key_watchdog_.get();
}
void Global::set_temp_auth_key_watchdog(ActorOwn<TempAuthKeyWatchdog> actor) {
  temp_auth_key_watchdog_ = std::move(actor);
}

void Global::set_state_manager(ActorOwn<StateManager> state_manager) {
  state_manager_ = std::move(state_manager);
}

MtprotoHeader &Global::mtproto_header() {
  return *mtproto_header_;
}
void Global::set_mtproto_header(unique_ptr<MtprotoHeader> mtproto_header) {
  mtproto_header_ = std::move(mtproto_header);
}

int32 Global::get_retry_after(int32 error_code, Slice error_message) {
  if (error_code != 429) {
    return 0;
  }

  Slice retry_after_prefix("Too Many Requests: retry after ");
  if (!begins_with(error_message, retry_after_prefix)) {
    return 0;
  }

  auto r_retry_after = to_integer_safe<int32>(error_message.substr(retry_after_prefix.size()));
  if (r_retry_after.is_ok() && r_retry_after.ok() > 0) {
    return r_retry_after.ok();
  }
  return 0;
}

int32 Global::to_unix_time(double server_time) const {
  LOG_CHECK(1.0 <= server_time && server_time <= 2140000000.0)
      << server_time << ' ' << is_server_time_reliable() << ' ' << get_server_time_difference() << ' ' << Time::now()
      << ' ' << saved_diff_ << ' ' << saved_system_time_;
  return static_cast<int32>(server_time);
}

void Global::update_server_time_difference(double diff, bool force) {
  if (force || !server_time_difference_was_updated_ || server_time_difference_ < diff) {
    server_time_difference_ = diff;
    server_time_difference_was_updated_ = true;
  }
}

void Global::save_server_time() {
  // No-op: no persistent storage in stripped version
}

void Global::do_save_server_time_difference() {
  // No-op: no persistent storage in stripped version
}

void Global::update_dns_time_difference(double diff) {
  dns_time_difference_ = diff;
  dns_time_difference_was_updated_ = true;
}

double Global::get_dns_time_difference() const {
  bool dns_flag = dns_time_difference_was_updated_;
  double dns_diff = dns_time_difference_;
  bool server_flag = server_time_difference_was_updated_;
  double server_diff = server_time_difference_;
  if (dns_flag != server_flag) {
    return dns_flag ? dns_diff : server_diff;
  }
  if (dns_flag) {
    return max(dns_diff, server_diff);
  }
  return Clocks::system() - Time::now();
}

DcId Global::get_webfile_dc_id() const {
  auto dc_id = narrow_cast<int32>(get_option_integer("webfile_dc_id"));
  if (!DcId::is_valid(dc_id)) {
    if (is_test_dc()) {
      dc_id = 2;
    } else {
      dc_id = 4;
    }
    CHECK(DcId::is_valid(dc_id));
  }
  return DcId::internal(dc_id);
}

void Global::set_net_query_stats(std::shared_ptr<NetQueryStats> net_query_stats) {
  net_query_creator_.set_create_func(
      [net_query_stats = std::move(net_query_stats)] { return td::make_unique<NetQueryCreator>(net_query_stats); });
}

void Global::set_net_query_dispatcher(unique_ptr<NetQueryDispatcher> net_query_dispatcher) {
  net_query_dispatcher_ = std::move(net_query_dispatcher);
}

// Simple in-memory option store
void Global::set_option_empty(Slice name) {
  auto key = name.str();
  options_string_.erase(key);
  options_integer_.erase(key);
  options_boolean_.erase(key);
}

void Global::set_option_boolean(Slice name, bool value) {
  options_boolean_[name.str()] = value;
}

void Global::set_option_integer(Slice name, int64 value) {
  options_integer_[name.str()] = value;
}

void Global::set_option_string(Slice name, Slice value) {
  options_string_[name.str()] = value.str();
}

bool Global::have_option(Slice name) const {
  auto key = name.str();
  return options_string_.count(key) || options_integer_.count(key) || options_boolean_.count(key);
}

bool Global::get_option_boolean(Slice name, bool default_value) const {
  auto it = options_boolean_.find(name.str());
  if (it != options_boolean_.end()) {
    return it->second;
  }
  return default_value;
}

int64 Global::get_option_integer(Slice name, int64 default_value) const {
  auto it = options_integer_.find(name.str());
  if (it != options_integer_.end()) {
    return it->second;
  }
  return default_value;
}

string Global::get_option_string(Slice name, string default_value) const {
  auto it = options_string_.find(name.str());
  if (it != options_string_.end()) {
    return it->second;
  }
  return default_value;
}

double get_global_server_time() {
  return G()->server_time();
}

}  // namespace td
