//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// In-memory key-value store replacing TDLib's SQLite/Binlog persistence.
// Session state is exported/imported as an opaque string for caller-managed persistence.
//
#pragma once

#include "td/utils/base64.h"
#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/HashTableUtils.h"
#include "td/utils/Promise.h"
#include "td/utils/Slice.h"

#include <functional>
#include <memory>
#include <unordered_map>

namespace td {

// Inlined from tddb — the only interface we need
class KeyValueSyncInterface {
 public:
  using SeqNo = uint64;

  KeyValueSyncInterface() = default;
  KeyValueSyncInterface(const KeyValueSyncInterface &) = delete;
  KeyValueSyncInterface &operator=(const KeyValueSyncInterface &) = delete;
  KeyValueSyncInterface(KeyValueSyncInterface &&) = default;
  KeyValueSyncInterface &operator=(KeyValueSyncInterface &&) = default;
  virtual ~KeyValueSyncInterface() = default;

  virtual SeqNo set(string key, string value) = 0;
  virtual bool isset(const string &key) = 0;
  virtual string get(const string &key) = 0;
  virtual void for_each(std::function<void(Slice, Slice)> func) = 0;
  virtual std::unordered_map<string, string, Hash<string>> prefix_get(Slice prefix) = 0;
  virtual FlatHashMap<string, string> get_all() = 0;
  virtual SeqNo erase(const string &key) = 0;
  virtual SeqNo erase_batch(vector<string> keys) = 0;
  virtual void erase_by_prefix(Slice prefix) = 0;
  virtual void force_sync(Promise<> &&promise, const char *source) = 0;
  virtual void close(Promise<> promise) = 0;
};

// In-memory KeyValueSyncInterface implementation
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

  // Serialize all entries as length-prefixed key-value pairs
  string serialize_all() const {
    string result;
    for (auto &kv : map_) {
      auto key_len = static_cast<uint32>(kv.first.size());
      auto val_len = static_cast<uint32>(kv.second.size());
      result.append(reinterpret_cast<const char *>(&key_len), 4);
      result.append(kv.first);
      result.append(reinterpret_cast<const char *>(&val_len), 4);
      result.append(kv.second);
    }
    return result;
  }

  // Deserialize length-prefixed entries into the map
  bool deserialize_all(Slice data) {
    map_.clear();
    size_t pos = 0;
    while (pos + 4 <= data.size()) {
      uint32 key_len;
      std::memcpy(&key_len, data.data() + pos, 4);
      pos += 4;
      if (pos + key_len + 4 > data.size()) {
        return false;
      }
      string key(data.data() + pos, key_len);
      pos += key_len;
      uint32 val_len;
      std::memcpy(&val_len, data.data() + pos, 4);
      pos += 4;
      if (pos + val_len > data.size()) {
        return false;
      }
      string value(data.data() + pos, val_len);
      pos += val_len;
      map_[std::move(key)] = std::move(value);
    }
    return pos == data.size();
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

  // Export all session state as a base64 string
  string export_session() const {
    // Format: [4 bytes binlog_len][binlog_data][config_data]
    auto binlog_data = binlog_pmc_->serialize_all();
    auto config_data = config_pmc_->serialize_all();
    auto binlog_len = static_cast<uint32>(binlog_data.size());
    string raw;
    raw.append(reinterpret_cast<const char *>(&binlog_len), 4);
    raw.append(binlog_data);
    raw.append(config_data);
    return base64_encode(raw);
  }

  // Import session state from a base64 string
  bool import_session(Slice session_str) {
    auto r_raw = base64_decode(session_str);
    if (r_raw.is_error()) {
      return false;
    }
    auto raw = r_raw.move_as_ok();
    if (raw.size() < 4) {
      return false;
    }
    uint32 binlog_len;
    std::memcpy(&binlog_len, raw.data(), 4);
    if (4 + binlog_len > raw.size()) {
      return false;
    }
    if (!binlog_pmc_->deserialize_all(Slice(raw.data() + 4, binlog_len))) {
      return false;
    }
    if (!config_pmc_->deserialize_all(Slice(raw.data() + 4 + binlog_len, raw.size() - 4 - binlog_len))) {
      return false;
    }
    return true;
  }

 private:
  std::shared_ptr<InMemoryKeyValue> binlog_pmc_;
  std::shared_ptr<InMemoryKeyValue> config_pmc_;
  bool is_test_dc_ = false;
};

}  // namespace td
