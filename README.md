# autoproto

[![License](https://img.shields.io/badge/license-Boost%201.0-blue.svg)](LICENSE_1_0.txt)

A stripped-down C++ MTProto library derived from [TDLib](https://github.com/tdlib/td).
Provides direct access to the Telegram MTProto protocol and raw `telegram_api::` types
without TDLib's high-level client logic.

---

- [What It Is](#what-it-is)
- [Architecture](#architecture)
- [Dependencies](#dependencies)
- [Building](#building)
- [Public API](#public-api)
- [Session Management](#session-management)
- [Code Examples](#code-examples)
- [Examples](#examples)
- [Updating the TL Schema](#updating-the-tl-schema)
- [Scope & Line Counts](#scope)
- [License](#license)

---

## What It Is

autoproto keeps TDLib's battle-tested networking, cryptography, and actor
infrastructure while removing every high-level Telegram client abstraction —
`MessagesManager`, `ContactsManager`, `UpdatesManager`, `StickersManager`,
`FileManager`, `StorageManager`, `DownloadManager`, `DialogManager`, and
approximately 150 other manager classes. The JSON, C, JNI, and CLI client
interfaces are also gone. What remains is ~88 k lines of handwritten C++
that give you a typed interface to the raw MTProto protocol.

You work directly with `telegram_api::` types (`messages_sendMessage`,
`updateNewMessage`, `inputPeerUser`, …) instead of TDLib's `td_api::`
wrappers. The library handles key exchange, encryption, session management,
DC configuration, and authentication. You handle everything else.

The public API is a single class — `mtproto::Client` — declared in
`mtproto/Client.h`. Two headers are all you need:

```cpp
#include "mtproto/Client.h"
#include "td/telegram/telegram_api.h"
```

## Architecture

```
┌──────────────────────────────────────────────────┐
│  mtproto::Client             (mtproto/Client.h)  │
│    create · auth · send<T> · on_update · run     │
├──────────────────────────────────────────────────┤
│  MtprotoClient               (root Actor)        │
│    AuthManager · ConfigManager                   │
│    ConnectionCreator · NetQueryDispatcher         │
│    StateManager · TempAuthKeyWatchdog             │
├──────────────────────────────────────────────────┤
│  MTProto transport            (td/mtproto/)      │
│    key exchange · encryption · sessions          │
├──────────────────────────────────────────────────┤
│  TL serialization             (td/tl/ + tdtl/)   │
├──────────────────────────────────────────────────┤
│  Actor framework              (tdactor/)         │
│  Utilities                    (tdutils/)         │
│  Network                      (tdnet/)           │
└──────────────────────────────────────────────────┘
```

- **`mtproto::Client`** — thread-safe public API. Creates a
  `ConcurrentScheduler` with 3 worker threads, spawns the `MtprotoClient`
  actor, runs the event loop.
- **`MtprotoClient`** — internal root actor. Wires up `Global`, `TdDb`
  (in-memory only), `NetQueryDispatcher`, `AuthManager`, `ConfigManager`,
  `ConnectionCreator`. Handles `updatesTooLong` internally by running
  `getState` → `getDifference` and delivering results through the normal
  update handler.
- **`AuthManager`** — handles `auth.importBotAuthorization` (bots) and
  `auth.sendCode` / `auth.signIn` / `auth.checkPassword` (phone auth).

## Dependencies

| Dependency                                      | Purpose                                 | Required   |
| ----------------------------------------------- | --------------------------------------- | ---------- |
| C++17 compiler (GCC 7+, Clang 5+, MSVC 2017.7+) | Language standard                       | Yes        |
| OpenSSL                                         | Cryptography, TLS                       | Yes        |
| zlib                                            | Compression                             | Yes        |
| CMake 3.10+                                     | Build system                            | Yes        |
| gperf                                           | MIME type lookup generation             | Build only |
| PHP                                             | `SplitSource.php` for low-memory builds | Optional   |

## Building

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

### Build targets

| Target            | Description                                                  |
| ----------------- | ------------------------------------------------------------ |
| `mtproto`         | Static library (`libmtproto.a`)                              |
| `echo_bot`        | Bot that echoes incoming PMs                                 |
| `userbot`         | Phone-auth userbot that echoes PMs                           |
| `channel_crawler` | Crawls a public channel's message history                    |
| `smoke_test`      | Verifies compile + link + actor init (no credentials needed) |

Build a single target:

```bash
cmake --build . --target echo_bot
```

On low-memory machines, split large generated sources first:

```bash
php SplitSource.php
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --target mtproto
cd .. && php SplitSource.php --undo
```

## Public API

Everything below is declared in `mtproto/Client.h`.

### Options

```cpp
struct Options {
  int32 api_id = 0;
  string api_hash;
  int32 dc_id = 2;              // initial DC (default: DC 2)
  bool is_test_dc = false;      // connect to test DCs
  string device_model = "Server";
  string system_version = "1.0";
  string application_version = "1.0";
  string system_language_code = "en";
  string language_code = "en";
  string session_data;           // raw binary from export_session()
};
```

### Methods

| Method                                           | Description                                       |
| ------------------------------------------------ | ------------------------------------------------- |
| `static std::unique_ptr<Client> create(Options)` | Create a client instance                          |
| `void auth_with_bot_token(string)`               | Authenticate as a bot                             |
| `void auth_with_phone(string)`                   | Start phone authentication flow                   |
| `void submit_auth_code(string)`                  | Submit the verification code (thread-safe)        |
| `void submit_password(string)`                   | Submit the 2FA password (thread-safe)             |
| `void on_update(handler)`                        | Set callback for incoming `telegram_api::Updates` |
| `void on_auth_state(handler)`                    | Set callback for auth state transitions           |
| `string export_session()`                        | Export current session as raw binary              |
| `void send<T>(object_ptr<T>, callback)`          | Send a typed MTProto request                      |
| `void run()`                                     | Run the event loop (blocks until `stop()`)        |
| `void stop()`                                    | Stop the event loop                               |

### `send<T>()`

Type-safe RPC. The template parameter `T` must be a `telegram_api::Function`
subclass. The callback receives `td::Result<typename T::ReturnType>` — the
parsed response or an error.

```cpp
template <class T>
void send(td::telegram_api::object_ptr<T> function,
          std::function<void(td::Result<typename T::ReturnType>)> callback);
```

### Auth states

| State | Name            | Meaning                                    |
| ----- | --------------- | ------------------------------------------ |
| 0     | WaitPhoneNumber | Waiting for phone number                   |
| 1     | WaitCode        | Verification code required                 |
| 2     | Ok              | Authenticated                              |
| 3     | Error           | Failed — `info` contains the error message |
| 4     | WaitPassword    | 2FA password required                      |

## Session Management

autoproto does **not** write any files. Session state lives in memory only
(in-memory `TdDb`). The caller owns persistence:

1. Set `Options::session_data` to a previously exported binary blob before
   calling `run()`.
2. When auth state reaches `Ok` (state 2), call `export_session()` to get
   the current session as a raw binary string.
3. Save that string however you like (file, database, KV store).

On the next run, pass the saved data back through `Options::session_data`
to skip re-authentication.

## Code Examples

All snippets below use only two includes:

```cpp
#include "mtproto/Client.h"
#include "td/telegram/telegram_api.h"
```

### Bot authentication

```cpp
namespace api = td::telegram_api;

mtproto::Client::Options opts;
opts.api_id  = 12345;
opts.api_hash = "your_api_hash";

auto client = mtproto::Client::create(opts);
client->auth_with_bot_token("123456:ABC-DEF...");

client->on_auth_state([](int state, const td::string &info) {
  if (state == 2) std::cout << "Authorized" << std::endl;
  if (state == 3) std::cerr << "Error: " << info << std::endl;
});

client->on_update([](td::tl_object_ptr<api::Updates> updates) {
  // handle raw MTProto updates
});

client->run();
```

### Phone authentication with 2FA

```cpp
auto client = mtproto::Client::create(opts);
client->auth_with_phone("+1234567890");

client->on_auth_state([&](int state, const td::string &info) {
  if (state == 1) {  // WaitCode
    std::cout << "Enter code: " << std::flush;
    std::string code;
    std::getline(std::cin, code);
    client->submit_auth_code(std::move(code));
  } else if (state == 4) {  // WaitPassword
    std::cout << "Enter 2FA password: " << std::flush;
    std::string pw;
    std::getline(std::cin, pw);
    client->submit_password(std::move(pw));
  } else if (state == 2) {
    std::cout << "Authorized" << std::endl;
  } else if (state == 3) {
    std::cerr << "Error: " << info << std::endl;
  }
});

client->run();
```

### Typed send

```cpp
namespace api = td::telegram_api;

auto req = api::make_object<api::messages_sendMessage>(
    0, false, false, false, false, false, false, false, false,
    api::make_object<api::inputPeerUser>(user_id, access_hash),
    nullptr, "Hello!", td::Random::secure_int64(),
    nullptr, std::vector<api::object_ptr<api::MessageEntity>>{},
    0, 0, nullptr, nullptr, 0, 0, nullptr);

client->send(std::move(req),
  [](td::Result<api::object_ptr<api::Updates>> result) {
    if (result.is_error()) {
      std::cerr << result.error().message().str() << std::endl;
    }
  });
```

### Session round-trip

```cpp
// Save after auth
client->on_auth_state([&](int state, const td::string &) {
  if (state == 2) {
    auto data = client->export_session();
    std::ofstream out("session.bin", std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
  }
});

// Restore on next run
std::ifstream in("session.bin", std::ios::binary);
if (in.good()) {
  opts.session_data = td::string(
      std::istreambuf_iterator<char>(in),
      std::istreambuf_iterator<char>());
}
auto client = mtproto::Client::create(opts);
```

## Examples

Four complete examples live in `examples/`.

### echo_bot

Bot that echoes back every incoming private message. Handles
`updateShortMessage`, `updateShort` + `updateNewMessage`, `updates`, and
`updatesCombined` containers. Replies via `messages.sendMessage`.

```bash
API_ID=12345 API_HASH=abc... BOT_TOKEN=123:ABC \
  [SESSION_FILE=session.bin] ./build/bin/echo_bot
```

See [examples/echo_bot.cpp](examples/echo_bot.cpp).

### userbot

Phone-authenticated userbot that echoes private messages. Supports
interactive code and 2FA password entry. Identical echo logic to `echo_bot`
but runs as a user account.

```bash
API_ID=12345 API_HASH=abc... PHONE=+1234567890 \
  [SESSION_FILE=session.bin] ./build/bin/userbot
```

See [examples/userbot.cpp](examples/userbot.cpp).

### channel_crawler

Resolves a public channel via `contacts.resolveUsername`, fetches its `pts`
through `channels.getFullChannel`, then walks the full message history using
`updates.getChannelDifference` with step=16384.

```bash
API_ID=12345 API_HASH=abc... PHONE=+1234567890 \
  CHANNEL=durov [LIMIT=100] [SESSION_FILE=session.bin] \
  ./build/bin/channel_crawler
```

See [examples/channel_crawler.cpp](examples/channel_crawler.cpp).

### smoke_test

Minimal integration test — creates a client with dummy credentials, starts
the actor system, runs the event loop for 2 seconds, and stops cleanly. No
network credentials required.

```bash
./build/bin/smoke_test
```

See [examples/smoke_test.cpp](examples/smoke_test.cpp).

## Updating the TL Schema

When a new Telegram MTProto layer is released:

### 1. Download the updated schema

```bash
curl -L -o td/generate/scheme/telegram_api.tl \
  https://github.com/telegramdesktop/tdesktop/raw/dev/Telegram/Resources/tl/api.tl
```

The MTProto protocol schema (`mtproto_api.tl`) changes very rarely.

### 2. Rebuild

```bash
cd build && cmake .. && cmake --build .
```

CMake detects `.tl` changes and automatically runs `tl-parser` → `.tlo`,
then `generate_mtproto` and `generate_common` to produce `telegram_api.h`,
`telegram_api_*.cpp`, etc.

### 3. Fix compilation errors

A schema update may add new constructor fields. The compiler will flag every
mismatch — update your `make_object<>()` calls accordingly.

### Schema files

| File                                 | Purpose                        |
| ------------------------------------ | ------------------------------ |
| `td/generate/scheme/telegram_api.tl` | Telegram API methods and types |
| `td/generate/scheme/mtproto_api.tl`  | MTProto protocol primitives    |
| `td/generate/scheme/secret_api.tl`   | Secret chat layer              |
| `td/generate/scheme/e2e_api.tl`      | End-to-end encryption types    |

Generated C++ code goes to `td/generate/auto/td/telegram/`.

<a name="scope"></a>

## Scope & Line Counts

Handwritten C++ (excluding auto-generated TL code):

| Component         | Directory                 | Lines     |
| ----------------- | ------------------------- | --------- |
| tdutils           | `tdutils/`                | 45,568    |
| tdactor           | `tdactor/`                | 4,080     |
| tdnet             | `tdnet/`                  | 4,175     |
| tdtl              | `tdtl/`                   | 2,826     |
| MTProto transport | `td/mtproto/`             | 7,393     |
| TL runtime        | `td/tl/`                  | 1,655     |
| Core managers     | `td/telegram/`            | 12,267    |
| Code generators   | `td/generate/` (non-auto) | 7,064     |
| Public API        | `mtproto/`                | 272       |
| Examples          | `examples/`               | 781       |
| Tests             | `test/`                   | 11,037    |
| **Total**         |                           | **~97 k** |

Auto-generated TL serialization code (`td/generate/auto/`) adds ~390 k
lines on top but is not checked in.

For comparison, the full TDLib tree is ~453 k lines of handwritten code.
autoproto is roughly a fifth of that.

## License

Licensed under the Boost Software License. See [LICENSE_1_0.txt](LICENSE_1_0.txt).
