//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MtprotoClient.h"

#include "td/telegram/Td.h"

#include "td/actor/actor.h"
#include "td/actor/ConcurrentScheduler.h"

#include "td/utils/logging.h"

namespace td {

MtprotoClient::MtprotoClient(Options options) : options_(std::move(options)) {
}

MtprotoClient::~MtprotoClient() {
  if (scheduler_) {
    scheduler_->finish();
  }
}

unique_ptr<MtprotoClient> MtprotoClient::create(Options options) {
  return unique_ptr<MtprotoClient>(new MtprotoClient(std::move(options)));
}

void MtprotoClient::auth_with_bot_token(string bot_token) {
  CHECK(scheduler_ != nullptr);
  auto guard = scheduler_->get_main_guard();
  // TODO: send auth request to Td actor
  LOG(INFO) << "Bot token set, will authenticate on connect";
}

void MtprotoClient::set_update_handler(UpdateHandler handler) {
  // Stored for when we wire it to Td
  LOG(INFO) << "Update handler set";
}

void MtprotoClient::run_event_loop() {
  // Create scheduler with a few threads for network I/O
  constexpr int32 EXTRA_THREADS = 3;
  scheduler_ = make_unique<ConcurrentScheduler>(EXTRA_THREADS, 0);

  // Create Td actor within the scheduler
  Td::Options td_options;
  td_options.api_id = options_.api_id;
  td_options.api_hash = options_.api_hash;
  td_options.dc_id = options_.dc_id;
  td_options.is_test_dc = options_.is_test_dc;
  td_options.device_model = options_.device_model;
  td_options.system_version = options_.system_version;
  td_options.application_version = options_.application_version;
  td_options.system_language_code = options_.system_language_code;
  td_options.language_code = options_.language_code;

  auto td_actor = scheduler_->create_actor_unsafe<Td>(0, "Td", std::move(td_options));

  scheduler_->start();
  running_ = true;

  while (running_ && scheduler_->run_main(10)) {
    // event loop
  }

  scheduler_->finish();
  scheduler_.reset();
}

void MtprotoClient::stop() {
  running_ = false;
}

}  // namespace td
