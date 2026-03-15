//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Minimal TdDb stub – provides in-memory key-value stores that replace the
// BinlogKeyValue / SQLite stores used by the full TDLib.
//
#pragma once

#include "td/db/KeyValueSyncInterface.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Promise.h"
#include "td/utils/Slice.h"

#include <functional>
#include <memory>
#include <unordered_map>

namespace td {

// Simple in-memory KeyValueSyncInterface implementation
class InMemoryKeyValue final : public KeyValueSyncInterface {
 public:
  SeqNo set(string key, string value) final {
    map_[std::move(key)] = std::move(value);
    return ++seq_no_;
  }
  bool isset(const string &key) final {
    return map_.count(key) > 0;
  }
  string get(const string &key) final {
    auto it = map_.find(key);
    return it != map_.end() ? it->second : string();
  }
  void for_each(std::function<void(Slice, Slice)> func) final {
    for (auto &kv : map_) {
      func(kv.first, kv.second);
    }
  }
  std::unordered_map<string, string, Hash<string>> prefix_get(Slice prefix) final {
    std::unordered_map<string, string, Hash<string>> result;
    for (auto &kv : map_) {
      if (begins_with(kv.first, prefix)) {
        result[kv.first] = kv.second;
      }
    }
    return result;
  }
  FlatHashMap<string, string> get_all() final {
    FlatHashMap<string, string> result;
    for (auto &kv : map_) {
      result[kv.first] = kv.second;
    }
    return result;
  }
  SeqNo erase(const string &key) final {
    map_.erase(key);
    return ++seq_no_;
  }
  SeqNo erase_batch(vector<string> keys) final {
    for (auto &key : keys) {
      map_.erase(key);
    }
    return ++seq_no_;
  }
  void erase_by_prefix(Slice prefix) final {
    vector<string> to_erase;
    for (auto &kv : map_) {
      if (begins_with(kv.first, prefix)) {
        to_erase.push_back(kv.first);
      }
    }
    for (auto &key : to_erase) {
      map_.erase(key);
    }
  }
  void force_sync(Promise<> &&promise, const char * /*source*/) final {
    promise.set_value(Unit());
  }
  void close(Promise<> promise) final {
    promise.set_value(Unit());
  }

 private:
  FlatHashMap<string, string> map_;
  SeqNo seq_no_ = 0;
};

class TdDb {
 public:
  TdDb() : binlog_pmc_(std::make_shared<InMemoryKeyValue>()), config_pmc_(std::make_shared<InMemoryKeyValue>()) {
  }

  std::shared_ptr<KeyValueSyncInterface> get_binlog_pmc_shared() {
    return binlog_pmc_;
  }
  std::shared_ptr<KeyValueSyncInterface> get_config_pmc_shared() {
    return config_pmc_;
  }

// Macro-compatible accessors (original TdDb uses macros for debug logging)
#define get_binlog_pmc() get_binlog_pmc_impl(__FILE__, __LINE__)
  KeyValueSyncInterface *get_binlog_pmc_impl(const char * /*file*/, int /*line*/) {
    return binlog_pmc_.get();
  }
  KeyValueSyncInterface *get_config_pmc() {
    return config_pmc_.get();
  }

  bool is_test_dc() const {
    return is_test_dc_;
  }
  void set_is_test_dc(bool is_test) {
    is_test_dc_ = is_test;
  }

 private:
  std::shared_ptr<InMemoryKeyValue> binlog_pmc_;
  std::shared_ptr<InMemoryKeyValue> config_pmc_;
  bool is_test_dc_ = false;
};

}  // namespace td
