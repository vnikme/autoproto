//
// examples/echo_bot.cpp — Bot that echoes back incoming messages.
//
// Authenticates as a bot, listens for updateShortMessage and updateNewMessage,
// and replies with the same text via messages.sendMessage.
//
// Build: cmake --build build --target echo_bot
// Run:   API_ID=12345 API_HASH=abc... BOT_TOKEN=123:ABC ./build/bin/echo_bot
//
#include "mtproto/Client.h"

#include "td/telegram/Global.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace td;
namespace api = telegram_api;

int main() {
  auto api_id_str = std::getenv("API_ID");
  auto api_hash_str = std::getenv("API_HASH");
  auto bot_token_str = std::getenv("BOT_TOKEN");

  if (!api_id_str || !api_hash_str || !bot_token_str) {
    std::cerr << "Usage: API_ID=... API_HASH=... BOT_TOKEN=... ./echo_bot" << std::endl;
    return 1;
  }

  ::mtproto::Client::Options opts;
  opts.api_id = std::atoi(api_id_str);
  opts.api_hash = api_hash_str;
  opts.device_model = "EchoBot";
  opts.application_version = "1.0";

  auto client = ::mtproto::Client::create(opts);
  client->auth_with_bot_token(bot_token_str);

  client->on_auth_state([](int state, const td::string &info) {
    // 0=WaitPhone, 1=WaitCode, 2=Ok, 3=Error
    if (state == 2) {
      std::cout << "[auth] Authorized as bot" << std::endl;
    } else if (state == 3) {
      std::cerr << "[auth] Error: " << info << std::endl;
    }
  });

  client->on_update([](tl_object_ptr<api::Updates> updates) {
    if (!updates) {
      return;
    }

    auto process_message = [](int64 user_id, const std::string &text) {
      if (text.empty()) {
        return;
      }
      std::cout << "[msg] From user " << user_id << ": " << text << std::endl;

      // Build the reply: messages.sendMessage to the originating user
      auto peer = api::make_object<api::inputPeerUser>(user_id, 0);
      auto random_id = Random::secure_int64();

      auto send_msg = api::make_object<api::messages_sendMessage>(
          0,           // flags
          false,       // no_webpage
          false,       // silent
          false,       // background
          false,       // clear_draft
          false,       // noforwards
          false,       // update_stickersets_order
          false,       // invert_media
          false,       // allow_paid_floodskip
          std::move(peer),     // peer
          nullptr,             // reply_to
          text,                // message (echo the same text)
          random_id,           // random_id
          nullptr,             // reply_markup
          std::vector<api::object_ptr<api::MessageEntity>>{},  // entities
          0,           // schedule_date
          0,           // schedule_repeat_period
          nullptr,     // send_as
          nullptr,     // quick_reply_shortcut
          0,           // effect
          0,           // allow_paid_stars
          nullptr      // suggested_post
      );

      // Dispatch via Global — we're inside the actor context here
      auto query = G()->net_query_creator().create(*send_msg);
      G()->net_query_dispatcher().dispatch(std::move(query));
      std::cout << "[msg] Echoed back to user " << user_id << std::endl;
    };

    switch (updates->get_id()) {
      case api::updateShortMessage::ID: {
        auto *msg = static_cast<api::updateShortMessage *>(updates.get());
        if (!msg->out_) {
          process_message(msg->user_id_, msg->message_);
        }
        break;
      }
      case api::updateShort::ID: {
        auto *upd_short = static_cast<api::updateShort *>(updates.get());
        if (upd_short->update_ && upd_short->update_->get_id() == api::updateNewMessage::ID) {
          auto *new_msg = static_cast<api::updateNewMessage *>(upd_short->update_.get());
          if (new_msg->message_ && new_msg->message_->get_id() == api::message::ID) {
            auto *m = static_cast<api::message *>(new_msg->message_.get());
            if (!m->out_ && m->from_id_ && m->from_id_->get_id() == api::peerUser::ID) {
              auto *peer = static_cast<api::peerUser *>(m->from_id_.get());
              process_message(peer->user_id_, m->message_);
            }
          }
        }
        break;
      }
      case api::updates::ID: {
        auto *upds = static_cast<api::updates *>(updates.get());
        for (auto &u : upds->updates_) {
          if (u && u->get_id() == api::updateNewMessage::ID) {
            auto *new_msg = static_cast<api::updateNewMessage *>(u.get());
            if (new_msg->message_ && new_msg->message_->get_id() == api::message::ID) {
              auto *m = static_cast<api::message *>(new_msg->message_.get());
              if (!m->out_ && m->from_id_ && m->from_id_->get_id() == api::peerUser::ID) {
                auto *peer = static_cast<api::peerUser *>(m->from_id_.get());
                process_message(peer->user_id_, m->message_);
              }
            }
          }
        }
        break;
      }
      default:
        break;
    }
  });

  std::cout << "Starting echo bot..." << std::endl;
  client->run();  // blocks until stop() is called

  return 0;
}
