//
// examples/channel_crawler.cpp — Crawl all messages from a public channel.
//
// Authenticates with a phone number (interactive code/password entry), resolves a
// channel username via contacts.resolveUsername, fetches channel pts via
// channels.getFullChannel, then walks backwards from (pts - 999999) using
// updates.getChannelDifference to collect all messages.
//
// Set SESSION_FILE env var to persist session across restarts.
// If the file exists, session is loaded; after auth it is saved.
//
// Build: cmake --build build --target channel_crawler
// Run:   API_ID=12345 API_HASH=abc... PHONE=+1234567890 CHANNEL=durov
//        [LIMIT=100] [SESSION_FILE=session.bin] ./build/bin/channel_crawler
//
#include "mtproto/Client.h"

#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

using namespace td;
namespace api = telegram_api;

static int64 peer_id(const api::object_ptr<api::Peer> &peer) {
  if (!peer) {
    return 0;
  }
  switch (peer->get_id()) {
    case api::peerUser::ID:
      return static_cast<const api::peerUser *>(peer.get())->user_id_;
    case api::peerChat::ID:
      return static_cast<const api::peerChat *>(peer.get())->chat_id_;
    case api::peerChannel::ID:
      return static_cast<const api::peerChannel *>(peer.get())->channel_id_;
    default:
      return 0;
  }
}

static void print_message(const api::object_ptr<api::Message> &msg) {
  if (!msg) {
    return;
  }
  if (msg->get_id() == api::message::ID) {
    auto *m = static_cast<const api::message *>(msg.get());
    std::cout << "  [" << m->id_ << "] date=" << m->date_ << " from=" << peer_id(m->from_id_) << "  " << m->message_
              << std::endl;
  } else if (msg->get_id() == api::messageService::ID) {
    auto *m = static_cast<const api::messageService *>(msg.get());
    std::cout << "  [" << m->id_ << "] (service message)" << std::endl;
  }
}

// Crawl state shared across recursive callbacks
struct CrawlState {
  ::mtproto::Client *client;
  int64 channel_id;
  int64 access_hash;
  int32 pts;
  int32 max_pts;  // end of current step window
  int32 step;     // pts window size (16384)
  int32 limit;
  int32 total_messages;
  std::function<void()> stop_fn;
};

static void crawl_next(std::shared_ptr<CrawlState> state);

static void crawl_next(std::shared_ptr<CrawlState> state) {
  // Window exhausted — start next window (mirrors Telethon's step logic)
  if (state->pts >= state->max_pts) {
    state->max_pts = state->pts + state->step;
  }

  auto input_channel = api::make_object<api::inputChannel>(state->channel_id, state->access_hash);
  auto filter = api::make_object<api::channelMessagesFilterEmpty>();

  auto req = api::make_object<api::updates_getChannelDifference>(0,                         // flags (no force)
                                                                 false,                     // force
                                                                 std::move(input_channel),  // channel
                                                                 std::move(filter),         // filter
                                                                 state->pts,                // pts
                                                                 state->limit               // limit
  );

  state->client->send(
      std::move(req), [state](td::Result<api::object_ptr<api::updates_ChannelDifference>> r_result) {
        if (r_result.is_error()) {
          auto err = r_result.error().message().str();
          std::cerr << "[crawl] getChannelDifference error at pts=" << state->pts << ": " << err << std::endl;
          state->stop_fn();
          return;
        }
        auto result = r_result.move_as_ok();

        switch (result->get_id()) {
          case api::updates_channelDifferenceTooLong::ID: {
            state->pts += 10000;
            crawl_next(state);
            return;
          }
          case api::updates_channelDifferenceEmpty::ID: {
            std::cout << "\n=== Done. Total messages: " << state->total_messages << " ===" << std::endl;
            state->stop_fn();
            return;
          }
          case api::updates_channelDifference::ID: {
            auto *diff = static_cast<api::updates_channelDifference *>(result.get());
            std::cout << "[crawl] pts=" << state->pts << " -> " << diff->pts_ << " : " << diff->new_messages_.size()
                      << " messages" << std::endl;
            for (auto &msg : diff->new_messages_) {
              print_message(msg);
            }
            state->total_messages += static_cast<int32>(diff->new_messages_.size());
            state->pts = diff->pts_;

            if (diff->final_) {
              std::cout << "\n=== Done (final). Total messages: " << state->total_messages << " ===" << std::endl;
              state->stop_fn();
            } else {
              crawl_next(state);
            }
            return;
          }
          default:
            std::cerr << "[crawl] Unknown channelDifference type: " << result->get_id() << std::endl;
            state->stop_fn();
            return;
        }
      });
}

// Step 2: After resolving channel, get full channel info to obtain pts
static void fetch_full_channel(::mtproto::Client *client, int64 channel_id, int64 access_hash, int32 limit,
                               std::function<void()> stop_fn) {
  auto input_channel = api::make_object<api::inputChannel>(channel_id, access_hash);
  auto req = api::make_object<api::channels_getFullChannel>(std::move(input_channel));

  client->send(std::move(req), [client, channel_id, access_hash, limit, stop_fn = std::move(stop_fn)](
                                                      td::Result<api::object_ptr<api::messages_chatFull>> r_result) {
             if (r_result.is_error()) {
               std::cerr << "[crawl] getFullChannel error: " << r_result.error().message().str() << std::endl;
               stop_fn();
               return;
             }
             auto result = r_result.move_as_ok();
             auto *full_chat = result->full_chat_.get();
             if (!full_chat || full_chat->get_id() != api::channelFull::ID) {
               std::cerr << "[crawl] Expected channelFull but got type " << (full_chat ? full_chat->get_id() : 0)
                         << std::endl;
               stop_fn();
               return;
             }
             auto *cf = static_cast<api::channelFull *>(full_chat);
             int32 channel_pts = cf->pts_;
             int32 start_pts = std::max(channel_pts - 999999, 1);
             constexpr int32 STEP = 16384;

             std::cout << "[crawl] Channel pts=" << channel_pts << ", starting from pts=" << start_pts
                       << ", step=" << STEP << std::endl;

             auto state = std::make_shared<CrawlState>();
             state->client = client;
             state->channel_id = channel_id;
             state->access_hash = access_hash;
             state->pts = start_pts;
             state->max_pts = start_pts + STEP;
             state->step = STEP;
             state->limit = limit;
             state->total_messages = 0;
             state->stop_fn = stop_fn;

             crawl_next(state);
           });
}

int main() {
  auto api_id_str = std::getenv("API_ID");
  auto api_hash_str = std::getenv("API_HASH");
  auto phone_str = std::getenv("PHONE");
  auto channel_str = std::getenv("CHANNEL");
  auto limit_str = std::getenv("LIMIT");
  auto session_file = std::getenv("SESSION_FILE");

  if (!api_id_str || !api_hash_str || !phone_str || !channel_str) {
    std::cerr
        << "Usage: API_ID=... API_HASH=... PHONE=+... CHANNEL=username [LIMIT=10] [SESSION_FILE=...] ./channel_crawler"
        << std::endl;
    return 1;
  }

  int32 limit = limit_str ? std::atoi(limit_str) : 1000000;

  ::mtproto::Client::Options opts;
  opts.api_id = std::atoi(api_id_str);
  opts.api_hash = api_hash_str;
  opts.device_model = "ChannelCrawler";
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

  std::string channel_username = channel_str;

  auto stop_fn = [&client]() {
    client->stop();
  };

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

      // Start crawling now that we're authenticated
      std::cout << "[crawl] Resolving channel @" << channel_username << "..." << std::endl;
      auto *cli = client.get();
      auto req = api::make_object<api::contacts_resolveUsername>(0, channel_username, std::string());

      cli->send(
          std::move(req),
          [cli, limit, stop_fn](td::Result<api::object_ptr<api::contacts_resolvedPeer>> r_result) {
            if (r_result.is_error()) {
              std::cerr << "[crawl] resolveUsername error: " << r_result.error().message().str() << std::endl;
              stop_fn();
              return;
            }
            auto result = r_result.move_as_ok();
            auto *resolved = result.get();

            for (auto &chat : resolved->chats_) {
              if (chat && chat->get_id() == api::channel::ID) {
                auto *ch = static_cast<api::channel *>(chat.get());
                std::cout << "[crawl] Resolved: channel_id=" << ch->id_ << " access_hash=" << ch->access_hash_
                          << std::endl;
                fetch_full_channel(cli, ch->id_, ch->access_hash_, limit, stop_fn);
                return;
              }
            }
            std::cerr << "[crawl] No channel found in resolveUsername result" << std::endl;
            stop_fn();
          });
    } else if (state == 3) {  // Error
      std::cerr << "[auth] Error: " << info << std::endl;
      client->stop();
    }
  });

  client->on_update([](tl_object_ptr<api::Updates>) {});

  std::cout << "Starting channel crawler for @" << channel_username << " (limit=" << limit << ")..." << std::endl;
  client->run();

  return 0;
}
