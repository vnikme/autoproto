# autoproto

A stripped-down C++ MTProto library derived from [TDLib](https://github.com/tdlib/td). Provides direct access to the Telegram MTProto protocol and TL-serialized API without TDLib's high-level abstractions (UI state management, message database, file manager, etc.).

## Table of Contents
- [Overview](#overview)
- [Architecture](#architecture)
- [Dependencies](#dependencies)
- [Building](#building)
- [Usage](#usage)
- [Examples](#examples)
- [Updating the TL Schema](#updating-the-tl-schema)
- [Project Structure](#project-structure)
- [License](#license)

<a name="overview"></a>
## Overview

autoproto keeps TDLib's battle-tested networking, cryptography, and actor infrastructure while removing the high-level Telegram client logic. What remains is a typed C++ interface to the raw MTProto protocol:

- **MTProto transport** — key exchange, encryption, session management
- **TL serialization** — auto-generated typed C++ structs from `.tl` schemas
- **Actor framework** — TDLib's `EventLoop` / `ConcurrentScheduler` / `Promise<T>` async model
- **Network layer** — TCP, TLS, connection management, DC migration
- **Authentication** — bot token and phone number auth flows

You work directly with `telegram_api::` types (e.g. `messages_sendMessage`, `updateNewMessage`) rather than TDLib's `td_api::` wrappers.

<a name="architecture"></a>
## Architecture

```
┌──────────────────────────────────────────────┐
│  mtproto::Client        (public C++ API)     │
│    auth_with_bot_token / auth_with_phone     │
│    on_update / on_auth_state / run / stop    │
├──────────────────────────────────────────────┤
│  MtprotoClient          (root Actor)         │
│    send<T>(function, promise)                │
│    send_raw_query(query, promise)            │
├──────────────────────────────────────────────┤
│  AuthManager · ConfigManager                 │
│  ConnectionCreator · NetQueryDispatcher      │
│  StateManager · TempAuthKeyWatchdog          │
├──────────────────────────────────────────────┤
│  MTProto transport       (td/mtproto/)       │
│  TL serialization        (td/tl/)            │
│  Actor framework         (tdactor/)          │
│  Utilities               (tdutils/)          │
│  Network                 (tdnet/)            │
└──────────────────────────────────────────────┘
```

**Key classes:**
- `mtproto::Client` — thread-safe public API. Creates a `ConcurrentScheduler`, spawns `MtprotoClient` actor, runs the event loop.
- `MtprotoClient` — internal root actor. Wires up `Global`, `TdDb`, `NetQueryDispatcher`, `AuthManager`, `ConfigManager`, `ConnectionCreator`.
- `AuthManager` — handles `auth.importBotAuthorization` (bots) and `auth.sendCode` / `auth.signIn` (phone auth).

<a name="dependencies"></a>
## Dependencies

- C++17 compiler (GCC 7+, Clang 5+, MSVC 2017.7+)
- OpenSSL
- zlib
- gperf (build only)
- CMake 3.10+
- PHP (optional, for `SplitSource.php` on low-memory builds)

<a name="building"></a>
## Building

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

This builds the core library (`mtproto`), examples (`echo_bot`, `channel_crawler`), and `smoke_test`.

To build only specific targets:
```bash
cmake --build . --target mtproto        # library only
cmake --build . --target echo_bot       # echo bot example
cmake --build . --target smoke_test     # integration test
```

On low-memory machines, split large generated source files first:
```bash
php SplitSource.php
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --target mtproto
cd .. && php SplitSource.php --undo
```

<a name="usage"></a>
## Usage

### Bot authentication

```cpp
#include "mtproto/Client.h"

int main() {
  ::mtproto::Client::Options opts;
  opts.api_id = 12345;
  opts.api_hash = "your_api_hash";

  auto client = ::mtproto::Client::create(opts);
  client->auth_with_bot_token("123456:ABC-DEF...");

  client->on_auth_state([](int state, const td::string &info) {
    if (state == 2) { /* Authorized */ }
    if (state == 3) { /* Error: info contains message */ }
  });

  client->on_update([](td::tl_object_ptr<td::telegram_api::Updates> updates) {
    // Handle raw MTProto updates
  });

  client->run();  // blocks until stop()
}
```

### Phone authentication (interactive)

```cpp
auto client = ::mtproto::Client::create(opts);
client->auth_with_phone("+1234567890");

client->on_auth_state([&client](int state, const td::string &info) {
  if (state == 1) {  // WaitCode
    std::string code;
    std::cout << "Enter code: ";
    std::getline(std::cin, code);
    client->submit_auth_code(std::move(code));
  }
});

client->run();
```

### Sending raw MTProto queries

Inside the actor context (e.g. in an `on_update` callback), use `Global` to dispatch queries directly:

```cpp
#include "td/telegram/Global.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/telegram_api.h"

namespace api = td::telegram_api;

// Build a TL function
auto req = api::make_object<api::messages_sendMessage>(
    0, false, false, false, false, false, false, false, false,
    api::make_object<api::inputPeerUser>(user_id, 0),
    nullptr, "Hello!", td::Random::secure_int64(),
    nullptr, std::vector<api::object_ptr<api::MessageEntity>>{},
    0, 0, nullptr, nullptr, 0, 0, nullptr);

// Create and dispatch the query
auto query = td::G()->net_query_creator().create(*req);
td::G()->net_query_dispatcher().dispatch(std::move(query));
```

### Auth state values

| State | Name | Meaning |
|-------|------|---------|
| 0 | WaitPhoneNumber | Waiting for phone number |
| 1 | WaitCode | Verification code required |
| 2 | Ok | Authenticated |
| 3 | Error | Authentication failed (info contains error message) |

<a name="examples"></a>
## Examples

### echo_bot

Bot that echoes back every incoming private message.

```bash
API_ID=12345 API_HASH=abc... BOT_TOKEN=123:ABC ./build/bin/echo_bot
```

Handles `updateShortMessage`, `updateShort`+`updateNewMessage`, and `updates` containers. Replies via `messages.sendMessage`. See [examples/echo_bot.cpp](examples/echo_bot.cpp).

### channel_crawler

Authenticates with a phone number (interactive code entry) and resolves a public channel via `contacts.resolveUsername`.

```bash
API_ID=12345 API_HASH=abc... PHONE=+1234567890 CHANNEL=durov LIMIT=10 \
  ./build/bin/channel_crawler
```

See [examples/channel_crawler.cpp](examples/channel_crawler.cpp).

<a name="updating-the-tl-schema"></a>
## Updating the TL Schema

When a new Telegram MTProto layer is released, update the TL schema files and rebuild to regenerate the C++ types.

### 1. Download the updated schema

The canonical source for the latest Telegram API schema is [tdesktop](https://github.com/telegramdesktop/tdesktop):

```bash
curl -L -o td/generate/scheme/telegram_api.tl \
  https://github.com/telegramdesktop/tdesktop/raw/dev/Telegram/Resources/tl/api.tl
```

The MTProto protocol schema (`mtproto_api.tl`) changes very rarely and usually does not need updating.

### 2. Rebuild

```bash
cd build
cmake ..
cmake --build .
```

CMake detects changes to `.tl` files and automatically:
1. Runs `tl-parser` to compile `.tl` → `.tlo` (binary TL format)
2. Runs `generate_mtproto` and `generate_common` to produce `telegram_api.h`, `telegram_api_*.cpp`, `mtproto_api.h`, etc.
3. Rebuilds all dependent targets

### 3. Fix compilation errors

A schema update may introduce new constructors with additional fields. Update any code that constructs these objects (e.g. `messages_sendMessage` arguments may change). The compiler will flag all mismatches.

### Schema files

| File | Purpose |
|------|---------|
| `td/generate/scheme/telegram_api.tl` | Telegram API methods and types |
| `td/generate/scheme/mtproto_api.tl` | MTProto protocol primitives |
| `td/generate/scheme/secret_api.tl` | Secret chat layer |
| `td/generate/scheme/e2e_api.tl` | End-to-end encryption types |

Generated C++ code is placed in `td/generate/auto/td/telegram/`.

<a name="project-structure"></a>
## Project Structure

```
autoproto/
├── mtproto/                     # Public C++ API
│   ├── Client.h                 #   mtproto::Client interface
│   └── Client.cpp               #   pimpl implementation
├── td/
│   ├── telegram/                # Core managers and actors
│   │   ├── MtprotoClient.h/cpp  #   Root actor (wires MTProto stack)
│   │   ├── AuthManager.h/cpp    #   Bot + phone authentication
│   │   ├── ConfigManager.h/cpp  #   DC configuration
│   │   ├── Global.h             #   Global singleton (G() macro)
│   │   └── net/                 #   Network layer
│   │       ├── NetQueryDispatcher.h/cpp
│   │       ├── NetQueryCreator.h/cpp
│   │       ├── ConnectionCreator.h/cpp
│   │       └── Session.h/cpp
│   ├── mtproto/                 # MTProto protocol implementation
│   │   ├── Handshake.h/cpp      #   Key exchange (DH)
│   │   ├── SessionConnection.h  #   Protocol session
│   │   └── AuthKey.h            #   Auth key management
│   ├── tl/                      # TL serialization runtime
│   └── generate/                # Code generation
│       ├── scheme/              #   .tl schema files
│       └── auto/                #   generated C++ (build output)
├── tdactor/                     # Actor framework + scheduler
├── tdutils/                     # Utilities (logging, crypto, Status, Promise)
├── tdnet/                       # Network utilities (Socks5, HTTP)
├── tddb/                        # Database layer (TdDb, SqliteKeyValue)
├── tde2e/                       # End-to-end encryption
├── examples/                    # Usage examples
│   ├── echo_bot.cpp
│   └── channel_crawler.cpp
├── test/                        # Tests
└── benchmark/                   # Benchmarks
```

### Modules kept from TDLib

| Module | Directory | Purpose |
|--------|-----------|---------|
| tdutils | `tdutils/` | Logging, `Slice`, `Status`, `Promise`, `BufferSlice`, crypto helpers |
| tdactor | `tdactor/` | `Actor`, `ActorOwn<T>`, `ActorId<T>`, `ConcurrentScheduler` |
| tdnet | `tdnet/` | TCP, TLS, Socks5, HTTP transport |
| tdtl | `tdtl/` | TL serialization/deserialization runtime |
| tddb | `tddb/` | SQLite-backed key-value and binlog storage |
| tde2e | `tde2e/` | End-to-end encryption primitives |
| MTProto | `td/mtproto/` | Key exchange, encryption, session management |
| Network | `td/telegram/net/` | Connections, auth keys, query dispatch |
| Managers | `td/telegram/` | Auth, config, DC management |

### Modules removed from TDLib

All high-level Telegram client logic has been removed: `MessagesManager`, `ContactsManager`, `UpdatesManager`, `StickersManager`, `FileManager`, `StorageManager`, `DownloadManager`, `DialogManager`, and approximately 150 other manager classes. The JSON/C/JNI/CLI client interfaces were also removed. Only the MTProto transport and typed TL API remain.

<a name="license"></a>
## License

Licensed under the Boost Software License. See [LICENSE_1_0.txt](LICENSE_1_0.txt).
