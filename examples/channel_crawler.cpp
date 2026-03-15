//
// examples/channel_crawler.cpp — Fetch recent messages from a public channel.
//
// Authenticates with a phone number (interactive code entry), resolves a
// channel username via contacts.resolveUsername, then fetches the last N
// messages via messages.getHistory and prints them.
//
// Build: cmake --build build --target channel_crawler
// Run:   API_ID=12345 API_HASH=abc... PHONE=+1234567890 CHANNEL=durov
//        LIMIT=10 ./build/bin/channel_crawler
//
#include "mtproto/Client.h"

#include "td/telegram/Global.h"
#include "td/telegram/MtprotoClient.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/overloaded.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace td;
namespace api = telegram_api;

static void fetch_history(MtprotoClient *mc, int64 channel_id, int64 access_hash, int32 limit,
                          ::mtproto::Client *client) {
  auto input_channel = api::make_object<api::inputChannel>(channel_id, access_hash);
  auto input_peer = api::make_object<api::inputPeerChannel>(channel_id, access_hash);

  auto req = api::make_object<api::messages_getHistory>(
      std::move(input_peer),  // peer
      0,                      // offset_id
      0,                      // offset_date
      0,                      // add_offset
      limit,                  // limit
      0,                      // max_id
      0,                      // min_id
      0                       // hash
  );

  mc->send(
      std::move(req),
      PromiseCreator::lambda(
          [client](Result<api::object_ptr<api::messages_Messages>> r_result) {
            if (r_result.is_error()) {
              std::cerr << "[crawl] getHistory error: " << r_result.error().message().str() << std::endl;
              client->stop();
              return;
            }
            auto result = r_result.move_as_ok();
            std::vector<api::object_ptr<api::Message>> *messages_ptr = nullptr;

            switch (result->get_id()) {
              case api::messages_messages::ID:
                messages_ptr = &static_cast<api::messages_messages *>(result.get())->messages_;
                break;
              case api::messages_messagesSlice::ID:
                messages_ptr = &static_cast<api::messages_messagesSlice *>(result.get())->messages_;
                break;
              case api::messages_channelMessages::ID:
                messages_ptr = &static_cast<api::messages_channelMessages *>(result.get())->messages_;
                break;
              default:
                std::cout << "[crawl] Empty or unsupported message container" << std::endl;
                client->stop();
                return;
            }

            auto &messages = *messages_ptr;
            std::cout << "\n=== " << messages.size() << " messages ===" << std::endl;

            for (auto &msg : messages) {
              if (!msg) {
                continue;
              }
              if (msg->get_id() == api::message::ID) {
                auto *m = static_cast<api::message *>(msg.get());
                std::cout << "[" << m->id_ << "] " << m->date_ << "  " << m->message_ << std::endl;
              } else if (msg->get_id() == api::messageService::ID) {
                auto *m = static_cast<api::messageService *>(msg.get());
                std::cout << "[" << m->id_ << "] (service message)" << std::endl;
              }
            }

            std::cout << "\nDone." << std::endl;
            client->stop();
          }));
}

int main() {
  auto api_id_str = std::getenv("API_ID");
  auto api_hash_str = std::getenv("API_HASH");
  auto phone_str = std::getenv("PHONE");
  auto channel_str = std::getenv("CHANNEL");
  auto limit_str = std::getenv("LIMIT");

  if (!api_id_str || !api_hash_str || !phone_str || !channel_str) {
    std::cerr << "Usage: API_ID=... API_HASH=... PHONE=+... CHANNEL=username [LIMIT=10] ./channel_crawler"
              << std::endl;
    return 1;
  }

  int32 limit = limit_str ? std::atoi(limit_str) : 10;

  ::mtproto::Client::Options opts;
  opts.api_id = std::atoi(api_id_str);
  opts.api_hash = api_hash_str;
  opts.device_model = "ChannelCrawler";
  opts.application_version = "1.0";

  auto client = ::mtproto::Client::create(opts);
  client->auth_with_phone(phone_str);

  std::string channel_username = channel_str;
  bool resolve_sent = false;

  client->on_auth_state([&](int state, const td::string &info) {
    if (state == 1) {  // WaitCode
      std::cout << "Enter the verification code: " << std::flush;
      std::string code;
      std::getline(std::cin, code);
      client->submit_auth_code(std::move(code));
    } else if (state == 2) {  // Ok
      std::cout << "[auth] Authorized successfully" << std::endl;
    } else if (state == 3) {  // Error
      std::cerr << "[auth] Error: " << info << std::endl;
      client->stop();
    }
  });

  client->on_update([&](tl_object_ptr<api::Updates> updates) {
    if (!updates) {
      return;
    }
    // Use the first update as a trigger that auth + connection is ready
    if (resolve_sent) {
      return;
    }
    resolve_sent = true;

    std::cout << "[crawl] Resolving channel @" << channel_username << "..." << std::endl;

    auto *mc = static_cast<MtprotoClient *>(G()->td().get_actor_unsafe());
    auto req = api::make_object<api::contacts_resolveUsername>(0, channel_username, std::string());

    mc->send(
        std::move(req),
        PromiseCreator::lambda(
            [mc, limit, client_ptr = client.get()](Result<api::object_ptr<api::contacts_resolvedPeer>> r_result) {
              if (r_result.is_error()) {
                std::cerr << "[crawl] resolveUsername error: " << r_result.error().message().str() << std::endl;
                client_ptr->stop();
                return;
              }
              auto result = r_result.move_as_ok();
              auto *resolved = result.get();

              // Find the channel in the chats list
              for (auto &chat : resolved->chats_) {
                if (chat && chat->get_id() == api::channel::ID) {
                  auto *ch = static_cast<api::channel *>(chat.get());
                  std::cout << "[crawl] Resolved: channel_id=" << ch->id_
                            << " access_hash=" << ch->access_hash_ << std::endl;
                  fetch_history(mc, ch->id_, ch->access_hash_, limit, client_ptr);
                  return;
                }
              }
              std::cerr << "[crawl] No channel found in resolveUsername result" << std::endl;
              client_ptr->stop();
            }));
  });

  std::cout << "Starting channel crawler for @" << channel_username
            << " (limit=" << limit << ")..." << std::endl;
  client->run();

  return 0;
}
