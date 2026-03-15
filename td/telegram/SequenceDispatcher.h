//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/NetQuery.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"

namespace td {

class MultiSequenceDispatcher : public NetQueryCallback {
 public:
  virtual void send(NetQueryPtr query) = 0;
  static ActorOwn<MultiSequenceDispatcher> create(Slice name);
};

}  // namespace td
