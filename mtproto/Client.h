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
// Session persistence: set Options::session_data to previously exported binary.
// After auth, call export_session() to get the current session for saving.
//
#pragma once

#include "td/telegram/net/NetQuery.h"
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
    string session_data;  // if non-empty, raw binary session data to restore
  };

  // Create a new client instance
  static std::unique_ptr<Client> create(Options options);

  ~Client();
  Client(const Client &) = delete;
  Client &operator=(const Client &) = delete;

  // Authenticate with a bot token (non-interactive)
  void auth_with_bot_token(string bot_token);

  // Authenticate with a phone number (requires code submission)
  void auth_with_phone(string phone_number);

  // Submit verification code (thread-safe, call from auth state callback)
  void submit_auth_code(string code);

  // Submit 2FA password (thread-safe, call from auth state callback)
  void submit_password(string password);

  // Set handler for incoming server updates
  void on_update(std::function<void(tl_object_ptr<td::telegram_api::Updates>)> handler);

  // Auth state callback: state 0=WaitPhone, 1=WaitCode, 2=Ok, 3=Error, 4=WaitPassword
  void on_auth_state(std::function<void(int state, const string &info)> handler);

  // Export current session as raw binary (call from auth callback or after run)
  string export_session();

  // Send a typed MTProto request; callback receives the parsed response
  template <class T>
  void send(td::telegram_api::object_ptr<T> function,
            std::function<void(td::Result<typename T::ReturnType>)> callback) {
    send_raw(*function,
             td::PromiseCreator::lambda(
                 [cb = std::move(callback)](td::Result<td::BufferSlice> r_buffer) mutable {
                   if (r_buffer.is_error()) {
                     cb(r_buffer.move_as_error());
                     return;
                   }
                   auto buffer = r_buffer.move_as_ok();
                   auto r_result = td::fetch_result<T>(buffer);
                   if (r_result.is_error()) {
                     cb(r_result.move_as_error());
                   } else {
                     cb(r_result.move_as_ok());
                   }
                 }));
  }

  // Run the event loop (blocks until stop() is called)
  void run();

  // Stop the event loop
  void stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  explicit Client(Options options);

  // Non-template helper: serializes and dispatches a raw MTProto function
  void send_raw(const td::telegram_api::Function &function, td::Promise<td::BufferSlice> promise);
};

}  // namespace mtproto
