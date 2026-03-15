//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// MtprotoClient — public entry point for using the stripped MTProto library.
// Creates and manages the TDLib actor system, Td actor, and provides a simple
// interface for sending requests and receiving updates.
//
#pragma once

#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"

#include <functional>
#include <memory>

namespace td {

class ConcurrentScheduler;
class Td;

class MtprotoClient {
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
    string language_code = "en";
  };

  static unique_ptr<MtprotoClient> create(Options options);

  ~MtprotoClient();
  MtprotoClient(const MtprotoClient &) = delete;
  MtprotoClient &operator=(const MtprotoClient &) = delete;

  // Authenticate with a bot token
  void auth_with_bot_token(string bot_token);

  // Set handler for incoming updates
  using UpdateHandler = std::function<void(tl_object_ptr<telegram_api::Updates>)>;
  void set_update_handler(UpdateHandler handler);

  // Run the event loop (blocks until stopped)
  void run_event_loop();

  // Stop the event loop
  void stop();

 private:
  explicit MtprotoClient(Options options);

  Options options_;
  unique_ptr<ConcurrentScheduler> scheduler_;
  bool running_ = false;
  string bot_token_;
  UpdateHandler update_handler_;
};

}  // namespace td
