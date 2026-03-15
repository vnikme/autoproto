//
// examples/smoke_test.cpp — Minimal smoke test for the MTProto library.
//
// Verifies the library compiles, links, and the actor system initializes.
// Does NOT connect to Telegram (no real API credentials).
//
// Build: cmake --build . --target smoke_test
// Run:   ./bin/smoke_test
//
#include "mtproto/Client.h"

#include <iostream>
#include <thread>
#include <chrono>

int main() {
  std::cout << "=== MTProto Library Smoke Test ===" << std::endl;

  // 1. Create client with test credentials (won't actually connect)
  mtproto::Client::Options options;
  options.api_id = 1;
  options.api_hash = "test";
  options.dc_id = 2;
  options.is_test_dc = true;
  options.device_model = "SmokeTest";
  options.application_version = "0.1.0";

  auto client = mtproto::Client::create(options);
  if (!client) {
    std::cerr << "FAIL: Could not create client" << std::endl;
    return 1;
  }
  std::cout << "OK: Client created" << std::endl;

  // 2. Set update handler
  bool update_handler_set = false;
  client->on_update([&update_handler_set](auto update) {
    update_handler_set = true;
    std::cout << "  Received update" << std::endl;
  });
  std::cout << "OK: Update handler set" << std::endl;

  // 3. Set bot token (won't actually auth — no real token)
  client->auth_with_bot_token("0:SMOKETEST");
  std::cout << "OK: Bot token set" << std::endl;

  // 4. Run event loop briefly in a thread, then stop
  std::cout << "OK: Starting event loop (will stop after 2 seconds)..." << std::endl;

  std::thread stop_thread([&client]() {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    client->stop();
  });

  client->run();  // blocks until stop() is called

  stop_thread.join();
  std::cout << "OK: Event loop stopped cleanly" << std::endl;

  std::cout << "=== SMOKE TEST PASSED ===" << std::endl;
  return 0;
}
