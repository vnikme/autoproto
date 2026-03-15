//
// mtproto/Client.h — Public API for the stripped MTProto library.
//
// Usage (bot):
//   auto client = mtproto::Client::create({.api_id = 12345, .api_hash = "abc..."});
//   client->auth_with_bot_token("BOT_TOKEN");
//   client->on_update([](auto update) { /* handle updates */ });
//   client->run();
//
// Usage (phone):
//   auto client = mtproto::Client::create({...});
//   client->auth_with_phone("+1234567890");
//   client->on_auth_state([&](int state, auto info) {
//     if (state == 1) { /* WaitCode */ client->submit_auth_code(read_code()); }
//   });
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

  // Create a client restoring from a previous session
  static std::unique_ptr<Client> create(Options options, string session_data);

  ~Client();
  Client(const Client &) = delete;
  Client &operator=(const Client &) = delete;

  // Authenticate with a bot token (non-interactive)
  void auth_with_bot_token(string bot_token);

  // Authenticate with a phone number (requires code submission)
  void auth_with_phone(string phone_number);

  // Submit verification code (thread-safe, call from auth state callback)
  void submit_auth_code(string code);

  // Export current session state as an opaque base64 string
  string export_session();

  // Set handler for incoming server updates
  void on_update(std::function<void(tl_object_ptr<td::telegram_api::Updates>)> handler);

  // Auth state callback: state 0=WaitPhone, 1=WaitCode, 2=Ok, 3=Error
  void on_auth_state(std::function<void(int state, const string &info)> handler);

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
