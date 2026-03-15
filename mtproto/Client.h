//
// mtproto/Client.h — Public API for the stripped MTProto library.
//
// Usage:
//   auto client = mtproto::Client::create({.api_id = 12345, .api_hash = "abc..."});
//   client->auth_with_bot_token("BOT_TOKEN");
//   client->on_update([](auto update) { /* handle updates */ });
//   client->run();
//
#pragma once

#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <functional>
#include <memory>

namespace mtproto {

using td::int32;
using td::string;
using td::tl_object_ptr;

class Client {
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

  // Create a new client instance
  static std::unique_ptr<Client> create(Options options);

  ~Client();
  Client(const Client &) = delete;
  Client &operator=(const Client &) = delete;

  // Authenticate with a bot token (non-interactive)
  void auth_with_bot_token(string bot_token);

  // Set handler for incoming server updates
  void on_update(std::function<void(tl_object_ptr<td::telegram_api::Updates>)> handler);

  // Run the event loop (blocks until stop() is called)
  void run();

  // Stop the event loop
  void stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  explicit Client(Options options);
};

}  // namespace mtproto
