//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Minimal Td stub – provides only the interface the net/ layer needs.
//
#pragma once

#include "td/telegram/AuthManager.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"

#include <memory>

namespace td {

class Td final : public Actor {
 public:
  Td() = default;

  unique_ptr<AuthManager> auth_manager_;
  ActorOwn<AuthManager> auth_manager_actor_;

  // Called when the net layer delivers a result for a dispatched query
  void on_result(NetQueryPtr query) {
    LOG(WARNING) << "Td::on_result stub called";
  }

  // Called when an update is received from the server
  void on_update(tl_object_ptr<telegram_api::Updates> updates, uint64 auth_key_id) {
    LOG(WARNING) << "Td::on_update stub called";
  }

  // Called to forward a td_api update to the client callback
  void send_update(td_api::object_ptr<td_api::Update> update) {
    LOG(WARNING) << "Td::send_update stub called";
  }
};

}  // namespace td
