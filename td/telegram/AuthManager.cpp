//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Stripped AuthManager: bot + phone authentication for MTProto layer.
//
#include "td/telegram/AuthManager.h"

#include "td/telegram/Global.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/BigNum.h"
#include "td/utils/buffer.h"
#include "td/utils/crypto.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"

namespace td {

AuthManager::AuthManager(int32 api_id, const string &api_hash, ActorShared<> parent)
    : parent_(std::move(parent)), api_id_(api_id), api_hash_(api_hash) {
}

bool AuthManager::is_authorized() const {
  return state_ == State::Ok;
}

bool AuthManager::was_authorized() const {
  return state_ == State::Ok || state_ == State::LoggingOut || state_ == State::DestroyingKeys ||
         state_ == State::Closing;
}

void AuthManager::on_authorization_lost(string source) {
  LOG(WARNING) << "Authorization lost: " << source;
  state_ = State::LoggingOut;
}

void AuthManager::check_bot_token(uint64 query_id, string bot_token) {
  bot_token_ = std::move(bot_token);
  is_bot_ = true;
  net_query_type_ = NetQueryType::BotAuthentication;
  auto query = G()->net_query_creator().create_unauth(
      telegram_api::auth_importBotAuthorization(0, api_id_, api_hash_, bot_token_));
  query_id_ = query_id;
  net_query_id_ = query->id();
  G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this));
}

void AuthManager::send_code(string phone_number) {
  phone_number_ = std::move(phone_number);
  net_query_type_ = NetQueryType::SendCode;
  auto settings = telegram_api::make_object<telegram_api::codeSettings>(
      0, false, false, false, false, false, false, std::vector<td::BufferSlice>{}, string(), false);
  auto query = G()->net_query_creator().create_unauth(
      telegram_api::auth_sendCode(phone_number_, api_id_, api_hash_, std::move(settings)));
  net_query_id_ = query->id();
  G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this));
}

void AuthManager::check_code(string code) {
  net_query_type_ = NetQueryType::SignIn;
  auto query = G()->net_query_creator().create_unauth(telegram_api::auth_signIn(
      telegram_api::auth_signIn::PHONE_CODE_MASK, phone_number_, phone_code_hash_, std::move(code), nullptr));
  net_query_id_ = query->id();
  G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this));
}

void AuthManager::check_password(string password) {
  pending_password_ = std::move(password);
  net_query_type_ = NetQueryType::GetPassword;
  auto query = G()->net_query_creator().create_unauth(telegram_api::account_getPassword());
  net_query_id_ = query->id();
  G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this));
}

void AuthManager::set_authorized() {
  state_ = State::Ok;
  notify_state(AuthState::Ok);
}

void AuthManager::on_closing(bool destroy_flag) {
  state_ = destroy_flag ? State::DestroyingKeys : State::Closing;
}

void AuthManager::on_result(NetQueryPtr net_query) {
  if (net_query_type_ == NetQueryType::BotAuthentication) {
    if (net_query->is_error()) {
      auto error_msg = net_query->error().message().str();
      if (error_msg.find("retry after") != std::string::npos || net_query->error().code() == 429) {
        // Extract retry-after seconds from "Too Many Requests: retry after N"
        int32 retry_after = 5;
        auto pos = error_msg.find("retry after ");
        if (pos != std::string::npos) {
          retry_after = std::max(to_integer<int32>(error_msg.substr(pos + 12)), 1);
        }
        LOG(WARNING) << "Bot auth flood wait: retrying in " << retry_after << "s";
        notify_state(AuthState::Error, PSTRING() << "FLOOD_WAIT: retry in " << retry_after << "s");
        net_query_type_ = NetQueryType::None;
        set_timeout_in(retry_after);
        return;
      }
      LOG(ERROR) << "Bot auth failed: " << net_query->error();
      state_ = State::WaitPhoneNumber;
      notify_state(AuthState::Error, PSTRING() << net_query->error());
    } else {
      auto status = net_query->move_as_ok();
      LOG(INFO) << "Bot auth succeeded";
      state_ = State::Ok;
      notify_state(AuthState::Ok);
    }
    net_query_type_ = NetQueryType::None;
  } else if (net_query_type_ == NetQueryType::SendCode) {
    if (net_query->is_error()) {
      LOG(ERROR) << "send_code failed: " << net_query->error();
      notify_state(AuthState::Error, PSTRING() << net_query->error());
    } else {
      auto buffer = net_query->move_as_ok();
      auto r_sent_code = fetch_result<telegram_api::auth_sendCode>(buffer);
      if (r_sent_code.is_error()) {
        LOG(ERROR) << "Failed to parse auth.sentCode: " << r_sent_code.error();
        notify_state(AuthState::Error, PSTRING() << r_sent_code.error());
      } else {
        auto sent_code = r_sent_code.move_as_ok();
        if (sent_code->get_id() == telegram_api::auth_sentCode::ID) {
          auto *sc = static_cast<telegram_api::auth_sentCode *>(sent_code.get());
          phone_code_hash_ = std::move(sc->phone_code_hash_);
          state_ = State::WaitCode;
          LOG(INFO) << "Code sent, waiting for user input";
          notify_state(AuthState::WaitCode);
        } else {
          LOG(INFO) << "auth_sentCodeSuccess — already authorized";
          state_ = State::Ok;
          notify_state(AuthState::Ok);
        }
      }
    }
    net_query_type_ = NetQueryType::None;
  } else if (net_query_type_ == NetQueryType::SignIn) {
    if (net_query->is_error()) {
      auto error_message = net_query->error().message().str();
      if (error_message.find("SESSION_PASSWORD_NEEDED") != std::string::npos) {
        LOG(INFO) << "2FA password required";
        state_ = State::WaitPassword;
        notify_state(AuthState::WaitPassword);
      } else {
        LOG(ERROR) << "sign_in failed: " << net_query->error();
        state_ = State::WaitCode;
        notify_state(AuthState::Error, PSTRING() << net_query->error());
      }
    } else {
      auto status = net_query->move_as_ok();
      LOG(INFO) << "User auth succeeded";
      state_ = State::Ok;
      notify_state(AuthState::Ok);
    }
    net_query_type_ = NetQueryType::None;
  } else if (net_query_type_ == NetQueryType::GetPassword) {
    if (net_query->is_error()) {
      LOG(ERROR) << "account.getPassword failed: " << net_query->error();
      notify_state(AuthState::Error, PSTRING() << net_query->error());
      net_query_type_ = NetQueryType::None;
      return;
    }
    auto buffer = net_query->move_as_ok();
    auto r_password = fetch_result<telegram_api::account_getPassword>(buffer);
    if (r_password.is_error()) {
      LOG(ERROR) << "Failed to parse account.password: " << r_password.error();
      notify_state(AuthState::Error, PSTRING() << r_password.error());
      net_query_type_ = NetQueryType::None;
      return;
    }
    auto password_obj = r_password.move_as_ok();
    if (!password_obj->has_password_ || !password_obj->current_algo_) {
      LOG(ERROR) << "No password set on account but SESSION_PASSWORD_NEEDED received";
      notify_state(AuthState::Error, "Unexpected: no password algo from server");
      net_query_type_ = NetQueryType::None;
      return;
    }
    // Compute SRP check
    auto algo = std::move(password_obj->current_algo_);
    if (algo->get_id() != telegram_api::passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow::ID) {
      LOG(ERROR) << "Unsupported password KDF algo";
      notify_state(AuthState::Error, "Unsupported password KDF algorithm");
      net_query_type_ = NetQueryType::None;
      return;
    }
    auto *kdf =
        static_cast<telegram_api::passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow *>(algo.get());
    auto salt1 = kdf->salt1_.as_slice();
    auto salt2 = kdf->salt2_.as_slice();
    int32 g_val = kdf->g_;
    auto p_bytes = kdf->p_.as_slice();
    auto srp_B_bytes = password_obj->srp_B_.as_slice();
    int64 srp_id = password_obj->srp_id_;

    // H(x) = SHA256(x)
    // PH1(password, salt1, salt2) = SHA256(salt1 | H(salt1 | password | salt1) | salt2)
    // But actually:
    // x = PH2(password, salt1, salt2) = SHA256(salt2 | pbkdf2(sha512, PH1, salt1, 100000) | salt2)
    // v = g^x mod p
    // SRP: k = H(p | g_padded), u = H(A | B), x = PH2, S = (B - k*v)^(a + u*x) mod p, K = H(S), M1 = ...
    // Telegram uses a simplified version:
    // 1. PH1 = SHA256(salt1 | password | salt1)
    // 2. PH2 = SHA256(salt2 | pbkdf2_sha512(PH1, salt1, 100000) | salt2)
    // 3. p, g are given, B is srp_B
    // 4. Generate random 256-byte a, compute A = g^a mod p
    // 5. u = SHA256(A | B)
    // 6. x = PH2
    // 7. k = SHA256(p | g_padded_256)
    // 8. v = g^x mod p — not computed directly, but:
    //    S = (B - k*v)^(a + u*x) mod p = B^(a + u*x) - ... but Telegram uses:
    //    k_v = k * g^x mod p
    //    t = (B - k_v) mod p
    //    S_a = a + u * x
    //    S = t^S_a mod p
    // 9. K = SHA256(S)
    // 10. M1 = SHA256(H(p) xor H(g) | H(salt1) | H(salt2) | A | B | K)
    //     but Telegram simplified: M1 = SHA256(A_256 | B_256 | K)... No:
    //     Actually M1 = SHA256(p_hash xor g_hash | salt1_hash | salt2_hash | A | B | K)

    // Step 1: PH1
    string ph1_input;
    ph1_input.append(salt1.data(), salt1.size());
    ph1_input.append(pending_password_);
    ph1_input.append(salt1.data(), salt1.size());
    string ph1_hash = sha256(ph1_input);

    string ph1;
    ph1.append(salt2.data(), salt2.size());
    ph1.append(ph1_hash);
    ph1.append(salt2.data(), salt2.size());
    ph1 = sha256(ph1);

    // Step 2: PBKDF2
    string pbkdf2_result(64, '\0');
    pbkdf2_sha512(ph1, salt1, 100000, {&pbkdf2_result[0], pbkdf2_result.size()});

    // Step 3: PH2 = SHA256(salt2 | pbkdf2_result | salt2)
    string ph2_input;
    ph2_input.append(salt2.data(), salt2.size());
    ph2_input.append(pbkdf2_result);
    ph2_input.append(salt2.data(), salt2.size());
    string x_bytes = sha256(ph2_input);

    // Step 4: Setup BigNum context
    BigNumContext ctx;
    BigNum p_bn = BigNum::from_binary(p_bytes);
    BigNum g_bn;
    g_bn.set_value(g_val);
    BigNum B_bn = BigNum::from_binary(srp_B_bytes);
    BigNum x_bn = BigNum::from_binary(x_bytes);

    // Step 5: Generate random a, compute A = g^a mod p
    string a_bytes(256, '\0');
    Random::secure_bytes({&a_bytes[0], a_bytes.size()});
    BigNum a_bn = BigNum::from_binary(a_bytes);
    BigNum A_bn;
    BigNum::mod_exp(A_bn, g_bn, a_bn, p_bn, ctx);
    string A_str = A_bn.to_binary(256);

    // Step 6: u = SHA256(A | B)
    string u_input;
    u_input.append(A_str);
    string B_str = B_bn.to_binary(256);
    u_input.append(B_str);
    string u_bytes_str = sha256(u_input);
    BigNum u_bn = BigNum::from_binary(u_bytes_str);

    // Step 7: k = SHA256(p | g_padded_256)
    string k_input;
    k_input.append(p_bytes.data(), p_bytes.size());
    string g_padded = g_bn.to_binary(256);
    k_input.append(g_padded);
    string k_bytes = sha256(k_input);
    BigNum k_bn = BigNum::from_binary(k_bytes);

    // Step 8: v = g^x mod p, k_v = k * v mod p
    BigNum v_bn;
    BigNum::mod_exp(v_bn, g_bn, x_bn, p_bn, ctx);
    BigNum kv_bn;
    BigNum::mod_mul(kv_bn, k_bn, v_bn, p_bn, ctx);

    // t = (B - k_v) mod p
    BigNum t_bn;
    BigNum::mod_sub(t_bn, B_bn, kv_bn, p_bn, ctx);

    // S_a = (a + u * x) — no mod here, it's the exponent
    BigNum ux_bn;
    BigNum::mul(ux_bn, u_bn, x_bn, ctx);
    BigNum sa_bn;
    BigNum::add(sa_bn, a_bn, ux_bn);

    // S = t^S_a mod p
    BigNum S_bn;
    BigNum::mod_exp(S_bn, t_bn, sa_bn, p_bn, ctx);
    string S_str = S_bn.to_binary(256);

    // Step 9: K = SHA256(S)
    string K = sha256(S_str);

    // Step 10: M1 = SHA256(H(p) xor H(g) | H(salt1) | H(salt2) | A | B | K)
    string p_hash = sha256(Slice(p_bytes));
    string g_hash = sha256(Slice(g_padded));
    string pg_xor(32, '\0');
    for (int i = 0; i < 32; i++) {
      pg_xor[i] = static_cast<char>(static_cast<unsigned char>(p_hash[i]) ^ static_cast<unsigned char>(g_hash[i]));
    }
    string salt1_hash = sha256(salt1);
    string salt2_hash = sha256(salt2);
    string m1_input;
    m1_input.append(pg_xor);
    m1_input.append(salt1_hash);
    m1_input.append(salt2_hash);
    m1_input.append(A_str);
    m1_input.append(B_str);
    m1_input.append(K);
    string M1 = sha256(m1_input);

    pending_password_.clear();

    // Send auth.checkPassword
    auto srp_check =
        telegram_api::make_object<telegram_api::inputCheckPasswordSRP>(srp_id, BufferSlice(A_str), BufferSlice(M1));
    net_query_type_ = NetQueryType::CheckPassword;
    auto cquery = G()->net_query_creator().create_unauth(telegram_api::auth_checkPassword(std::move(srp_check)));
    net_query_id_ = cquery->id();
    G()->net_query_dispatcher().dispatch_with_callback(std::move(cquery), actor_shared(this));
  } else if (net_query_type_ == NetQueryType::CheckPassword) {
    if (net_query->is_error()) {
      LOG(ERROR) << "auth.checkPassword failed: " << net_query->error();
      state_ = State::WaitPassword;
      notify_state(AuthState::Error, PSTRING() << net_query->error());
    } else {
      auto status = net_query->move_as_ok();
      LOG(INFO) << "Password auth succeeded";
      state_ = State::Ok;
      notify_state(AuthState::Ok);
    }
    net_query_type_ = NetQueryType::None;
  }
}

void AuthManager::notify_state(AuthState auth_state, const string &info) {
  if (auth_state_callback_) {
    auth_state_callback_(auth_state, info);
  }
}

void AuthManager::start_up() {
  if (state_ == State::None) {
    state_ = State::WaitPhoneNumber;
  }
}

void AuthManager::timeout_expired() {
  if (!bot_token_.empty()) {
    LOG(INFO) << "Retrying bot auth after flood wait";
    check_bot_token(0, bot_token_);
  }
}

void AuthManager::tear_down() {
  state_ = State::Closing;
}

}  // namespace td
