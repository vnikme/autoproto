//
// mtproto/Client.cpp — Public API implementation.
//
// MtprotoClient is now the root Actor. Client::Impl manages the
// ConcurrentScheduler, creates the actor inside it, runs the event loop.
//
#include "mtproto/Client.h"

#include "td/telegram/MtprotoClient.h"

#include "td/actor/actor.h"
#include "td/actor/ConcurrentScheduler.h"

#include "td/utils/logging.h"

#include <atomic>
#include <mutex>

namespace mtproto {

struct Client::Impl {
  td::MtprotoClient::Options actor_options;
  std::function<void(tl_object_ptr<td::telegram_api::Updates>)> update_handler;
  std::function<void(int, const string &)> auth_state_handler;
  string bot_token;
  string phone_number;
  string session_data;

  td::unique_ptr<td::ConcurrentScheduler> scheduler;
  td::MtprotoClient *actor_raw = nullptr;  // valid only while event loop is running
  td::ActorId<td::MtprotoClient> actor_id;
  std::atomic<bool> running{false};

  std::mutex pending_mutex;
  string pending_code;
  bool has_pending_code = false;

  string pending_password;
  bool has_pending_password = false;
};

Client::Client(Options options) : impl_(std::make_unique<Impl>()) {
  impl_->actor_options.api_id = options.api_id;
  impl_->actor_options.api_hash = std::move(options.api_hash);
  impl_->actor_options.dc_id = options.dc_id;
  impl_->actor_options.is_test_dc = options.is_test_dc;
  impl_->actor_options.device_model = std::move(options.device_model);
  impl_->actor_options.system_version = std::move(options.system_version);
  impl_->actor_options.application_version = std::move(options.application_version);
  impl_->actor_options.system_language_code = std::move(options.system_language_code);
  impl_->actor_options.language_code = std::move(options.language_code);
  impl_->session_data = std::move(options.session_data);
}

Client::~Client() = default;

std::unique_ptr<Client> Client::create(Options options) {
  return std::unique_ptr<Client>(new Client(std::move(options)));
}

void Client::auth_with_bot_token(string bot_token) {
  impl_->bot_token = std::move(bot_token);
}

void Client::auth_with_phone(string phone_number) {
  impl_->phone_number = std::move(phone_number);
}

void Client::submit_auth_code(string code) {
  std::lock_guard<std::mutex> lock(impl_->pending_mutex);
  impl_->pending_code = std::move(code);
  impl_->has_pending_code = true;
}

void Client::submit_password(string password) {
  std::lock_guard<std::mutex> lock(impl_->pending_mutex);
  impl_->pending_password = std::move(password);
  impl_->has_pending_password = true;
}

void Client::on_update(std::function<void(tl_object_ptr<td::telegram_api::Updates>)> handler) {
  impl_->update_handler = std::move(handler);
}

void Client::on_auth_state(std::function<void(int state, const string &info)> handler) {
  impl_->auth_state_handler = std::move(handler);
}

void Client::run() {
  constexpr td::int32 EXTRA_THREADS = 3;
  impl_->scheduler = td::make_unique<td::ConcurrentScheduler>(EXTRA_THREADS, 0);

  // Populate actor options
  auto opts = impl_->actor_options;
  opts.bot_token = std::move(impl_->bot_token);
  opts.phone_number = std::move(impl_->phone_number);
  opts.session_data = std::move(impl_->session_data);

  auto actor = impl_->scheduler->create_actor_unsafe<td::MtprotoClient>(0, "MtprotoClient", std::move(opts));
  auto *raw = actor.get_actor_unsafe();
  impl_->actor_raw = raw;
  impl_->actor_id = actor.get();

  if (impl_->update_handler) {
    raw->set_update_handler(impl_->update_handler);
  }
  if (impl_->auth_state_handler) {
    raw->set_auth_state_callback(impl_->auth_state_handler);
  }

  impl_->scheduler->start();
  impl_->running = true;

  while (impl_->running && impl_->scheduler->run_main(10)) {
    {
      std::lock_guard<std::mutex> lock(impl_->pending_mutex);
      if (impl_->has_pending_code) {
        auto guard = impl_->scheduler->get_main_guard();
        td::send_closure(impl_->actor_id, &td::MtprotoClient::check_code, std::move(impl_->pending_code));
        impl_->has_pending_code = false;
      }
      if (impl_->has_pending_password) {
        auto guard = impl_->scheduler->get_main_guard();
        td::send_closure(impl_->actor_id, &td::MtprotoClient::check_password, std::move(impl_->pending_password));
        impl_->has_pending_password = false;
      }
    }
  }

  impl_->actor_raw = nullptr;
  impl_->scheduler->finish();
  impl_->scheduler.reset();
}

void Client::stop() {
  impl_->running = false;
}

string Client::export_session() {
  if (impl_->actor_raw) {
    return impl_->actor_raw->export_session();
  }
  return {};
}

void Client::send_raw(const td::telegram_api::Function &function, td::Promise<td::BufferSlice> promise) {
  if (impl_->actor_raw) {
    impl_->actor_raw->send_raw_query_from_function(function, std::move(promise));
  } else {
    promise.set_error(td::Status::Error("Client not running"));
  }
}

}  // namespace mtproto
