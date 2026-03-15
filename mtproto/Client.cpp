//
// mtproto/Client.cpp — Public API implementation.
//
#include "mtproto/Client.h"

#include "td/telegram/MtprotoClient.h"

namespace mtproto {

struct Client::Impl {
  td::MtprotoClient::Options td_options;
  td::unique_ptr<td::MtprotoClient> client;
  std::function<void(tl_object_ptr<td::telegram_api::Updates>)> update_handler;
  string bot_token;
};

Client::Client(Options options) : impl_(std::make_unique<Impl>()) {
  impl_->td_options.api_id = options.api_id;
  impl_->td_options.api_hash = std::move(options.api_hash);
  impl_->td_options.dc_id = options.dc_id;
  impl_->td_options.is_test_dc = options.is_test_dc;
  impl_->td_options.device_model = std::move(options.device_model);
  impl_->td_options.system_version = std::move(options.system_version);
  impl_->td_options.application_version = std::move(options.application_version);
  impl_->td_options.system_language_code = std::move(options.system_language_code);
  impl_->td_options.language_code = std::move(options.language_code);
}

Client::~Client() = default;

std::unique_ptr<Client> Client::create(Options options) {
  return std::unique_ptr<Client>(new Client(std::move(options)));
}

void Client::auth_with_bot_token(string bot_token) {
  impl_->bot_token = std::move(bot_token);
}

void Client::on_update(std::function<void(tl_object_ptr<td::telegram_api::Updates>)> handler) {
  impl_->update_handler = std::move(handler);
}

void Client::run() {
  impl_->client = td::MtprotoClient::create(impl_->td_options);

  if (impl_->update_handler) {
    impl_->client->set_update_handler(impl_->update_handler);
  }

  if (!impl_->bot_token.empty()) {
    impl_->client->auth_with_bot_token(std::move(impl_->bot_token));
  }

  impl_->client->run_event_loop();
}

void Client::stop() {
  if (impl_->client) {
    impl_->client->stop();
  }
}

}  // namespace mtproto
