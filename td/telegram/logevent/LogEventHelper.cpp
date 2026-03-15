//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Stripped: Binlog-based log events are no-ops in the stripped build.
//
#include "td/telegram/logevent/LogEventHelper.h"

#include "td/utils/logging.h"

namespace td {

void add_log_event(LogEventIdWithGeneration &log_event_id, const Storer &storer, uint32 type, Slice name) {
  LOG(INFO) << "Save " << name << " to binlog (stripped: no-op)";
  log_event_id.log_event_id = ++log_event_id.generation;
}

void delete_log_event(LogEventIdWithGeneration &log_event_id, uint64 generation, Slice name) {
  LOG(INFO) << "Delete " << name << " log event (stripped: no-op)";
  if (log_event_id.generation == generation) {
    log_event_id.log_event_id = 0;
  }
}

Promise<Unit> get_erase_log_event_promise(uint64 log_event_id, Promise<Unit> promise) {
  return promise;  // No binlog in stripped build
}

}  // namespace td
