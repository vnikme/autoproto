//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SequenceDispatcher.h"

#include "td/telegram/Global.h"
#include "td/telegram/net/NetQueryDispatcher.h"

#include "td/utils/logging.h"

namespace td {

// Simple passthrough implementation – dispatches queries immediately without
// sequence ordering.  Full TDLib had elaborate retry/reorder logic here.
class MultiSequenceDispatcherImpl final : public MultiSequenceDispatcher {
 public:
  void send(NetQueryPtr query) final {
    G()->net_query_dispatcher().dispatch(std::move(query));
  }
  void on_result(NetQueryPtr query) final {
    G()->net_query_dispatcher().dispatch(std::move(query));
  }
};

ActorOwn<MultiSequenceDispatcher> MultiSequenceDispatcher::create(Slice name) {
  auto impl = create_actor<MultiSequenceDispatcherImpl>(name);
  return ActorOwn<MultiSequenceDispatcher>(impl.release());
}

}  // namespace td
