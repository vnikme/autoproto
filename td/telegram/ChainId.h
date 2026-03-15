//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/HashTableUtils.h"

namespace td {

class ChainId {
  uint64 id = 0;

 public:
  ChainId() = default;
  explicit ChainId(uint64 chain_id) : id(chain_id) {
  }

  uint64 get() const {
    return id;
  }

  bool operator==(const ChainId &other) const {
    return id == other.id;
  }

  bool operator!=(const ChainId &other) const {
    return id != other.id;
  }
};

struct ChainIdHash {
  uint32 operator()(ChainId chain_id) const {
    return Hash<uint64>()(chain_id.get());
  }
};

}  // namespace td
