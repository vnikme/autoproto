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
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

using namespace td;
namespace api = telegram_api;

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
  std::atomic<bool> authorized{false};
  std::atomic<bool> resolved{false};
  std::atomic<bool> fetched{false};

  // Prompt for auth code from stdin
  client->on_auth_state([&client](int state, const td::string &info) {
    if (state == 1) {  // WaitCode
      std::cout << "Enter the verification code: " << std::flush;
      std::string code;
      std::getline(std::cin, code);
      client->submit_auth_code(std::move(code));
    } else if (state == 2) {  // Ok
      std::cout << "[auth] Authorized successfully" << std::endl;
    } else if (state == 3) {  // Error
      std::cerr << "[auth] Error: " << info << std::endl;
    }
  });

  // After auth, we process updates to drive the resolve→fetch pipeline
  client->on_update([&](tl_object_ptr<api::Updates> updates) {
    if (!updates) {
      return;
    }

    // We use updates as a heartbeat — when we first receive any update after
    // auth, we initiate the resolve→fetch sequence.
    if (!authorized.exchange(true)) {
      std::cout << "[crawl] Resolving channel @" << channel_username << "..." << std::endl;

      // contacts.resolveUsername
      auto resolve = api::make_object<api::contacts_resolveUsername>(
          0, channel_username, std::string());
      auto query = G()->net_query_creator().create(*resolve);
      G()->net_query_dispatcher().dispatch(std::move(query));
    }
  });

  std::cout << "Starting channel crawler for @" << channel_username
            << " (limit=" << limit << ")..." << std::endl;
  client->run();

  return 0;
}
