//
// examples/echo_bot.cpp — Bot that echoes back incoming messages.
//
// Authenticates as a bot, listens for updateShortMessage and updateNewMessage,
// and replies with the same text via messages.sendMessage.
//
// Set SESSION_FILE env var to persist session across restarts.
// If the file exists, session is loaded; after auth it is saved.
//
// Build: cmake --build build --target echo_bot
// Run:   API_ID=12345 API_HASH=abc... BOT_TOKEN=123:ABC [SESSION_FILE=session.bin] ./build/bin/echo_bot
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

static std::unordered_map<int64, int64> g_user_hashes;

static void cache_users(const std::vector<api::object_ptr<api::User>> &users) {
  for (auto &u : users) {
    if (u && u->get_id() == api::user::ID) {
      auto *usr = static_cast<const api::user *>(u.get());
      g_user_hashes[usr->id_] = usr->access_hash_;
    }
  }
}

static void send_echo(::mtproto::Client *client, int64 user_id, int64 access_hash, const std::string &text) {
  if (text.empty()) {
    return;
  }
  std::cout << "[msg] From user " << user_id << ": " << text << std::endl;

  auto peer = api::make_object<api::inputPeerUser>(user_id, access_hash);
  auto random_id = Random::secure_int64();

  auto send_msg = api::make_object<api::messages_sendMessage>(
      0, false, false, false, false, false, false, false, false, std::move(peer), nullptr, text, random_id, nullptr,
      std::vector<api::object_ptr<api::MessageEntity>>{}, 0, 0, nullptr, nullptr, 0, 0, nullptr);

  client->send(std::move(send_msg), [user_id](td::Result<api::object_ptr<api::Updates>> result) {
    if (result.is_error()) {
      std::cerr << "[msg] Failed to echo to user " << user_id << ": " << result.error().message().str() << std::endl;
      return;
    }
    std::cout << "[msg] Echoed back to user " << user_id << std::endl;
  });
}

int main() {
  auto api_id_str = std::getenv("API_ID");
  auto api_hash_str = std::getenv("API_HASH");
  auto bot_token_str = std::getenv("BOT_TOKEN");
  auto session_file = std::getenv("SESSION_FILE");

  if (!api_id_str || !api_hash_str || !bot_token_str) {
    std::cerr << "Usage: API_ID=... API_HASH=... BOT_TOKEN=... [SESSION_FILE=...] ./echo_bot" << std::endl;
    return 1;
  }

  ::mtproto::Client::Options opts;
  opts.api_id = std::atoi(api_id_str);
  opts.api_hash = api_hash_str;
  opts.device_model = "EchoBot";
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
  client->auth_with_bot_token(bot_token_str);

  client->on_auth_state([&](int state, const td::string &info) {
    if (state == 2) {
      std::cout << "[auth] Authorized as bot" << std::endl;
      if (!sf.empty()) {
        auto data = client->export_session();
        if (!data.empty()) {
          std::ofstream out(sf, std::ios::binary | std::ios::trunc);
          out.write(data.data(), static_cast<std::streamsize>(data.size()));
        }
      }
    } else if (state == 3) {
      std::cerr << "[auth] Error: " << info << std::endl;
    }
  });

  auto *cli = client.get();
  client->on_update([cli](tl_object_ptr<api::Updates> updates) {
    if (!updates) {
      return;
    }

    std::cout << "[update] Received update type_id=" << updates->get_id() << std::endl;

    // Helper: process a single Update object (handles updateNewMessage)
    auto process_single_update = [cli](const api::object_ptr<api::Update> &u) {
      if (!u || u->get_id() != api::updateNewMessage::ID) {
        return;
      }
      auto *new_msg = static_cast<const api::updateNewMessage *>(u.get());
      if (!new_msg->message_ || new_msg->message_->get_id() != api::message::ID) {
        return;
      }
      auto *m = static_cast<const api::message *>(new_msg->message_.get());
      if (m->out_) {
        return;
      }
      // Extract user_id from from_id_ or peer_id_
      int64 user_id = 0;
      if (m->from_id_ && m->from_id_->get_id() == api::peerUser::ID) {
        user_id = static_cast<const api::peerUser *>(m->from_id_.get())->user_id_;
      } else if (m->peer_id_ && m->peer_id_->get_id() == api::peerUser::ID) {
        user_id = static_cast<const api::peerUser *>(m->peer_id_.get())->user_id_;
      }
      if (user_id == 0) {
        return;
      }
      auto it = g_user_hashes.find(user_id);
      int64 hash = (it != g_user_hashes.end()) ? it->second : 0;
      send_echo(cli, user_id, hash, m->message_);
    };

    switch (updates->get_id()) {
      case api::updateShortMessage::ID: {
        auto *msg = static_cast<api::updateShortMessage *>(updates.get());
        if (msg->out_) {
          break;
        }
        auto it = g_user_hashes.find(msg->user_id_);
        if (it != g_user_hashes.end()) {
          send_echo(cli, msg->user_id_, it->second, msg->message_);
        } else {
          std::vector<api::object_ptr<api::InputMessage>> ids;
          ids.push_back(api::make_object<api::inputMessageID>(msg->id_));
          auto req = api::make_object<api::messages_getMessages>(std::move(ids));
          int64 uid = msg->user_id_;
          std::string text = msg->message_;
          cli->send(std::move(req), [cli, uid, text = std::move(text)](
                                        td::Result<api::object_ptr<api::messages_Messages>> r_result) mutable {
            if (r_result.is_error()) {
              return;
            }
            auto result = r_result.move_as_ok();
            if (result->get_id() == api::messages_messages::ID) {
              cache_users(static_cast<api::messages_messages *>(result.get())->users_);
            } else if (result->get_id() == api::messages_messagesSlice::ID) {
              cache_users(static_cast<api::messages_messagesSlice *>(result.get())->users_);
            }
            auto it2 = g_user_hashes.find(uid);
            if (it2 != g_user_hashes.end()) {
              send_echo(cli, uid, it2->second, text);
            }
          });
        }
        break;
      }
      case api::updateShort::ID: {
        auto *upd = static_cast<api::updateShort *>(updates.get());
        process_single_update(upd->update_);
        break;
      }
      case api::updates::ID: {
        auto *upds = static_cast<api::updates *>(updates.get());
        cache_users(upds->users_);
        for (auto &u : upds->updates_) {
          process_single_update(u);
        }
        break;
      }
      case api::updatesCombined::ID: {
        auto *upds = static_cast<api::updatesCombined *>(updates.get());
        cache_users(upds->users_);
        for (auto &u : upds->updates_) {
          process_single_update(u);
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

  std::cout << "Starting echo bot..." << std::endl;
  client->run();  // blocks until stop() is called

  return 0;
}
