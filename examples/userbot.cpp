//
// examples/userbot.cpp — Userbot that echoes back private messages.
//
// Authenticates with a phone number (interactive code/password entry), then
// listens for incoming private messages and replies with the same text.
//
// Set SESSION_FILE env var to persist session across restarts.
// If the file exists, session is loaded; after auth it is saved.
//
// Build: cmake --build build --target userbot
// Run:   API_ID=12345 API_HASH=abc... PHONE=+1234567890 [SESSION_FILE=session.bin] ./build/bin/userbot
//
#include "mtproto/Client.h"

#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

using namespace td;
namespace api = telegram_api;

// Simple in-memory cache: user_id → access_hash
static std::unordered_map<int64, int64> g_user_hashes;

static void cache_users(const std::vector<api::object_ptr<api::User>> &users) {
  for (auto &u : users) {
    if (u && u->get_id() == api::user::ID) {
      auto *usr = static_cast<const api::user *>(u.get());
      g_user_hashes[usr->id_] = usr->access_hash_;
    }
  }
}

static void send_text(::mtproto::Client *client, int64 user_id, int64 access_hash, const std::string &text) {
  auto peer = api::make_object<api::inputPeerUser>(user_id, access_hash);
  auto random_id = Random::secure_int64();

  auto send_msg =
      api::make_object<api::messages_sendMessage>(0,                // flags
                                                  false,            // no_webpage
                                                  false,            // silent
                                                  false,            // background
                                                  false,            // clear_draft
                                                  false,            // noforwards
                                                  false,            // update_stickersets_order
                                                  false,            // invert_media
                                                  false,            // allow_paid_floodskip
                                                  std::move(peer),  // peer
                                                  nullptr,          // reply_to
                                                  text,             // message
                                                  random_id,        // random_id
                                                  nullptr,          // reply_markup
                                                  std::vector<api::object_ptr<api::MessageEntity>>{},  // entities
                                                  0,                                                   // schedule_date
                                                  0,        // schedule_repeat_period
                                                  nullptr,  // send_as
                                                  nullptr,  // quick_reply_shortcut
                                                  0,        // effect
                                                  0,        // allow_paid_stars
                                                  nullptr   // suggested_post
      );

  client->send(std::move(send_msg), [user_id](td::Result<api::object_ptr<api::Updates>> result) {
    if (result.is_error()) {
      std::cerr << "[msg] Failed to send to user " << user_id << ": " << result.error().message().str() << std::endl;
    }
  });
}

int main() {
  auto api_id_str = std::getenv("API_ID");
  auto api_hash_str = std::getenv("API_HASH");
  auto phone_str = std::getenv("PHONE");
  auto session_file = std::getenv("SESSION_FILE");

  if (!api_id_str || !api_hash_str || !phone_str) {
    std::cerr << "Usage: API_ID=... API_HASH=... PHONE=+... [SESSION_FILE=...] ./userbot" << std::endl;
    return 1;
  }

  ::mtproto::Client::Options opts;
  opts.api_id = std::atoi(api_id_str);
  opts.api_hash = api_hash_str;
  opts.device_model = "Userbot";
  opts.application_version = "1.0";

  std::string sf;
  if (session_file && session_file[0] != '\0') {
    sf = session_file;
    std::ifstream in(sf, std::ios::binary);
    if (in.good()) {
      opts.session_data = td::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
  }

  auto client = ::mtproto::Client::create(opts);
  client->auth_with_phone(phone_str);

  client->on_auth_state([&](int state, const td::string &info) {
    if (state == 1) {  // WaitCode
      std::cout << "Enter the verification code: " << std::flush;
      std::string code;
      std::getline(std::cin, code);
      client->submit_auth_code(std::move(code));
    } else if (state == 4) {  // WaitPassword
      std::cout << "Enter 2FA password: " << std::flush;
      std::string password;
      std::getline(std::cin, password);
      client->submit_password(std::move(password));
    } else if (state == 2) {  // Ok
      std::cout << "[auth] Authorized successfully" << std::endl;
      if (!sf.empty()) {
        auto data = client->export_session();
        if (!data.empty()) {
          std::ofstream out(sf, std::ios::binary | std::ios::trunc);
          out.write(data.data(), static_cast<std::streamsize>(data.size()));
        }
      }
    } else if (state == 3) {  // Error
      std::cerr << "[auth] Error: " << info << std::endl;
      client->stop();
    }
  });

  auto *cli = client.get();
  client->on_update([cli](tl_object_ptr<api::Updates> updates) {
    if (!updates) {
      return;
    }

    auto echo_back = [cli](int64 user_id, int64 access_hash, const std::string &text) {
      if (text.empty()) {
        return;
      }
      std::cout << "[msg] From user " << user_id << ": " << text << std::endl;
      send_text(cli, user_id, access_hash, text);
      std::cout << "[msg] Echoed back to user " << user_id << std::endl;
    };

    switch (updates->get_id()) {
      case api::updateShortMessage::ID: {
        auto *msg = static_cast<api::updateShortMessage *>(updates.get());
        if (msg->out_) {
          break;
        }
        auto it = g_user_hashes.find(msg->user_id_);
        if (it != g_user_hashes.end()) {
          echo_back(msg->user_id_, it->second, msg->message_);
        } else {
          std::vector<api::object_ptr<api::InputMessage>> ids;
          ids.push_back(api::make_object<api::inputMessageID>(msg->id_));
          auto req = api::make_object<api::messages_getMessages>(std::move(ids));
          int64 uid = msg->user_id_;
          std::string text = msg->message_;
          cli->send(std::move(req), [cli, uid, text = std::move(text)](
                                        td::Result<api::object_ptr<api::messages_Messages>> r_result) mutable {
            if (r_result.is_error()) {
              std::cerr << "[msg] Failed to fetch message for user " << uid << ": " << r_result.error().message().str()
                        << std::endl;
              return;
            }
            auto result = r_result.move_as_ok();
            switch (result->get_id()) {
              case api::messages_messages::ID:
                cache_users(static_cast<api::messages_messages *>(result.get())->users_);
                break;
              case api::messages_messagesSlice::ID:
                cache_users(static_cast<api::messages_messagesSlice *>(result.get())->users_);
                break;
              default:
                break;
            }
            auto it2 = g_user_hashes.find(uid);
            if (it2 != g_user_hashes.end()) {
              std::cout << "[msg] From user " << uid << ": " << text << std::endl;
              send_text(cli, uid, it2->second, text);
              std::cout << "[msg] Echoed back to user " << uid << std::endl;
            } else {
              std::cout << "[msg] From user " << uid << ": " << text << " (could not resolve access_hash)" << std::endl;
            }
          });
        }
        break;
      }
      case api::updates::ID: {
        auto *upds = static_cast<api::updates *>(updates.get());
        cache_users(upds->users_);
        for (auto &u : upds->updates_) {
          if (u && u->get_id() == api::updateNewMessage::ID) {
            auto *new_msg = static_cast<api::updateNewMessage *>(u.get());
            if (new_msg->message_ && new_msg->message_->get_id() == api::message::ID) {
              auto *m = static_cast<api::message *>(new_msg->message_.get());
              if (!m->out_ && m->from_id_ && m->from_id_->get_id() == api::peerUser::ID) {
                auto *peer = static_cast<api::peerUser *>(m->from_id_.get());
                if (m->peer_id_ && m->peer_id_->get_id() == api::peerUser::ID) {
                  auto it = g_user_hashes.find(peer->user_id_);
                  int64 hash = (it != g_user_hashes.end()) ? it->second : 0;
                  echo_back(peer->user_id_, hash, m->message_);
                }
              }
            }
          }
        }
        break;
      }
      case api::updatesTooLong::ID:
        // Handled internally by MtprotoClient (triggers getState + getDifference)
        break;
      default:
        break;
    }
  });

  std::cout << "Starting userbot..." << std::endl;
  client->run();

  return 0;
}
