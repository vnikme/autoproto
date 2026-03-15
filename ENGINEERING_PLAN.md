# mtproto-cpp: Engineering Plan

## Goal

Strip TDLib down to bare MTProto + TL layer. Keep TDLib's own network/async
infrastructure (actors, EventLoop, callbacks). Auto-generate full API from TL
schema. No business logic, no persistence, no managers.

**Style:** Same as TDLib — typed C++ structs, callbacks, TDLib's actor/network layer.  
**Async:** TDLib's own EventLoop + Promise/callback pattern (no Boost, no libuv).  
**Bindings:** C++ only for now; flat C API exposed for future bindings.

---

## Source Base

```bash
git clone git@github.com:vnikme/autoproto.git
cd autoproto
```

TL schema update source (for protocol upgrades):
```
https://github.com/telegramdesktop/tdesktop/raw/dev/Telegram/Resources/tl/api.tl
```

---

## Phase 1: Delete

Remove entirely — no stubs, just `rm -rf`. These directories and files have no
place in a protocol-only library.

### Entire directories

```
td/db/                          # SqliteDb, Binlog, BinlogHelper — all of it
```

### td/telegram/ — files to delete

```
td/telegram/AccountManager.*
td/telegram/AnimationsManager.*
td/telegram/AttachMenuManager.*
td/telegram/AutosaveManager.*
td/telegram/BackgroundManager.*
td/telegram/BotInfoManager.*
td/telegram/CallManager.*
td/telegram/ContactsManager.*
td/telegram/DialogsManager.*
td/telegram/DocumentsManager.*
td/telegram/DownloadManager.*
td/telegram/EmojiGroupManager.*
td/telegram/FileManager.*
td/telegram/ForumTopicManager.*
td/telegram/GameManager.*
td/telegram/GroupCallManager.*
td/telegram/InlineQueriesManager.*
td/telegram/LanguagePackManager.*
td/telegram/LinkManager.*
td/telegram/MessagesManager.*
td/telegram/NotificationManager.*
td/telegram/PollManager.*
td/telegram/ReactionManager.*
td/telegram/SavedMessagesManager.*
td/telegram/ScheduledMessagesManager.*
td/telegram/SecretChatsManager.*
td/telegram/SponsoredMessageManager.*
td/telegram/StateManager.*
td/telegram/StickersManager.*
td/telegram/StorageManager.*
td/telegram/ThemeManager.*
td/telegram/TopDialogManager.*
td/telegram/TranscriptionManager.*
td/telegram/UpdatesManager.*
td/telegram/VideoNotesManager.*
td/telegram/VoiceNotesManager.*
td/telegram/WebPagesManager.*
td/telegram/Td.*                # main class — replaced by MtprotoClient
td/telegram/TdDb.*              # database layer
```

---

## Phase 2: Keep

These modules are kept **in full** — they are the core of what this library is.

```
td/mtproto/         # MTProto protocol implementation — crypto, transport, session
td/tl/              # TL serialization runtime + generator
td/net/             # Network layer — TCP, TLS, DC connections
td/utils/           # Logging, crypto utils, Slice, Status, Promise, actors
```

These files from `td/telegram/` are kept (stripped where noted):

```
td/telegram/AuthManager.*       # keep — handles key exchange, SRP, 2FA
td/telegram/DcManager.*         # keep — DC routing and switching
td/telegram/NetQueryDispatcher.*# keep — query routing to connections
td/telegram/Global.*            # keep — global config/dc list
td/telegram/ConfigManager.*     # keep — DC config fetching
```

---

## Phase 3: Stubs (Temporary Scaffolding)

> **These are build-time scaffolding only — they will all be deleted by end of
> Phase 3. The final library contains zero stubs.**

After Phase 1, compilation will fail with missing symbols from deleted managers.
The goal is to make the compiler happy long enough to trace and cut every
reference to deleted code from the modules we actually keep.

For each missing symbol, write a minimal stub — **no logic, only signatures**:

```cpp
// td/telegram/StubManager.h
#pragma once
#include "td/actor/actor.h"

namespace td {
class Td;

class StubManager final : public Actor {
 public:
  explicit StubManager(Td *td) {}
  void tear_down() final {}
};

}  // namespace td
```

### Iteration loop

```
cmake --build . 2>&1 | grep error
  → identify missing symbol
  → trace which kept file includes/calls it
  → remove that #include / call from the kept file
  → if removal breaks too much: add a temporary stub
  → repeat
```

Each iteration either **removes a dependency** or **adds a temporary stub**.
Stubs are only added when removal isn't yet possible (other things still depend
on it). As dependencies untangle, stubs get deleted one by one.

### Done when

- `td/mtproto/`, `td/net/`, `td/tl/`, `td/utils/` compile with zero references
  to any deleted manager
- No stub files remain in the tree
- Only the kept files from Phase 2 are present

---

## Phase 4: MtprotoClient

Replace `td/telegram/Td.*` with a minimal client class that wires together
the kept modules.

```cpp
// td/telegram/MtprotoClient.h
#pragma once
#include "td/actor/actor.h"
#include "td/telegram/telegram_api.h"
#include "td/utils/Promise.h"

namespace td {

class AuthManager;
class DcManager;
class NetQueryDispatcher;

class MtprotoClient final : public Actor {
 public:
  struct Options {
    int32 api_id;
    string api_hash;
    int32 dc_id = 2;
  };

  explicit MtprotoClient(Options options);

  // Send any TL request; result delivered via promise/callback
  void send(tl_object_ptr<TlObject> request,
            Promise<tl_object_ptr<TlObject>> promise);

  // All unsolicited updates from server arrive here
  void set_update_handler(std::function<void(tl_object_ptr<telegram_api::Updates>)> handler);

 private:
  Options options_;
  unique_ptr<AuthManager> auth_manager_;
  unique_ptr<DcManager> dc_manager_;
  unique_ptr<NetQueryDispatcher> net_query_dispatcher_;

  void on_result(NetQueryPtr query);
  void start_up() final;
  void tear_down() final;
};

}  // namespace td
```

---

## Phase 5: TL Generation Pipeline

TDLib ships its own TL parser and code writer. Wire them as a standalone CMake
target so updating the protocol is a one-command operation.

```cmake
# CMakeLists.txt (generation section)

set(TL_SCHEMA "${CMAKE_SOURCE_DIR}/tl/api.tl")
set(TL_OUTPUT_H   "${CMAKE_SOURCE_DIR}/td/telegram/telegram_api.h")
set(TL_OUTPUT_CPP "${CMAKE_SOURCE_DIR}/td/telegram/telegram_api.cpp")

add_custom_target(generate_tl
    COMMAND $<TARGET_FILE:tl_writer>
            ${TL_SCHEMA}
            ${TL_OUTPUT_H}
            ${TL_OUTPUT_CPP}
    DEPENDS ${TL_SCHEMA} tl_writer
    COMMENT "Regenerating TL bindings from schema"
)
```

### Protocol update procedure

```bash
# 1. Pull new schema
curl -o tl/api.tl \
  https://github.com/telegramdesktop/tdesktop/raw/dev/Telegram/Resources/tl/api.tl

# 2. Regenerate
cmake --build . --target generate_tl

# 3. Fix any breaking changes in MtprotoClient / public API
# 4. Bump version and tag
git tag vX.Y.Z
```

---

## Phase 6: Public API

The public surface exposed to library consumers.

```cpp
#include "mtproto/Client.h"

// ── Initialise ────────────────────────────────────────────────────────────────

auto client = mtproto::Client::create({
    .api_id   = 12345,
    .api_hash = "abc...",
    .dc_id    = 2,
});

// ── Authenticate ──────────────────────────────────────────────────────────────

// Bot token (non-interactive)
client->auth_with_bot_token("TOKEN");

// Interactive phone auth
client->auth_phone("+79001234567", [](mtproto::CodeNeeded info) {
    std::string code = /* read from stdin or wherever */;
    info.submit(code);
});

// Reuse saved session
client->auth_with_key(saved_auth_key);

// Export session for persistence by caller
AuthKey key = client->export_auth_key();   // caller saves to Postgres / disk

// ── Send any MTProto request ──────────────────────────────────────────────────

// Full typed API — everything generated from TL schema
client->send(
    td::make_tl_object<td::telegram_api::channels_getChannelDifference>(
        std::move(input_channel),
        td::make_tl_object<td::telegram_api::channelMessagesFilterEmpty>(),
        pts,
        limit,
        /*force=*/false
    ),
    [](td::Result<td::tl_object_ptr<td::telegram_api::updates_ChannelDifference>> result) {
        if (result.is_error()) { /* handle */ return; }
        auto diff = result.move_as_ok();
        // diff is fully typed
    }
);

// ── Receive unsolicited updates ───────────────────────────────────────────────

client->on_update([](td::tl_object_ptr<td::telegram_api::Updates> update) {
    // updatesTooLong, updateShortMessage, updatesCombined, etc.
});
```

---

## Phase 7: CMake Structure

```cmake
cmake_minimum_required(VERSION 3.20)
project(mtproto-cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Core: stripped TDLib internals ────────────────────────────────────────────
file(GLOB_RECURSE MTPROTO_CORE_SOURCES
    "td/mtproto/*.cpp"
    "td/tl/*.cpp"
    "td/net/*.cpp"
    "td/utils/*.cpp"
    "td/telegram/AuthManager.cpp"
    "td/telegram/DcManager.cpp"
    "td/telegram/NetQueryDispatcher.cpp"
    "td/telegram/Global.cpp"
    "td/telegram/ConfigManager.cpp"
    "td/telegram/telegram_api.cpp"   # generated
)

add_library(mtproto-core STATIC ${MTPROTO_CORE_SOURCES})
target_include_directories(mtproto-core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(mtproto-core PUBLIC OpenSSL::SSL OpenSSL::Crypto)

# ── Public wrapper ────────────────────────────────────────────────────────────
add_library(mtproto SHARED
    src/Client.cpp
    src/Auth.cpp
)
target_link_libraries(mtproto PUBLIC mtproto-core)

# ── Flat C API (for future bindings) ─────────────────────────────────────────
add_library(mtproto-c SHARED
    src/c_api.cpp
)
target_link_libraries(mtproto-c PRIVATE mtproto)

# ── Smoke test example ────────────────────────────────────────────────────────
add_executable(example_channel_diff examples/channel_diff.cpp)
target_link_libraries(example_channel_diff PRIVATE mtproto)
```

---

## Phase 8: Smoke Test

Minimal working example — the definition of "it works":

```cpp
// examples/channel_diff.cpp
//
// Flow: connect → bot auth → getChannelDifference(pts=0) → print updates → exit

#include "mtproto/Client.h"
#include <iostream>

int main() {
    auto client = mtproto::Client::create({
        .api_id   = MY_API_ID,
        .api_hash = MY_API_HASH,
        .dc_id    = 2,
    });

    client->auth_with_bot_token(MY_BOT_TOKEN);

    client->send(
        td::make_tl_object<td::telegram_api::channels_getChannelDifference>(
            /* input_channel */, 
            td::make_tl_object<td::telegram_api::channelMessagesFilterEmpty>(),
            /*pts=*/0, /*limit=*/100, /*force=*/false
        ),
        [](auto result) {
            if (result.is_error()) {
                std::cerr << "Error: " << result.error() << "\n";
                return;
            }
            std::cout << "Got diff: " << result.ok()->get_id() << "\n";
        }
    );

    client->run_event_loop();   // blocks until done
    return 0;
}
```

---

## Execution Order for Claude

Run these phases strictly in order. After each cmake step, commit if green.

```
1.  git clone git@github.com:vnikme/autoproto.git
2.  Phase 1  — delete all listed files/dirs
3.  cmake --build . 2>&1 | grep error   (expect many errors)
4.  Phase 2/3 — write stubs for every missing symbol, one file at a time
5.  Repeat step 3-4 until td/mtproto + td/net compile clean
6.  Phase 4  — write MtprotoClient replacing Td.*
7.  Phase 5  — wire TL generation target, verify generate_tl works
8.  Phase 6  — write public Client.h / Client.cpp wrapper
9.  Phase 7  — wire CMakeLists.txt
10. Phase 8  — compile and run examples/channel_diff
11. Tag v0.1.0
```

---

## Future Considerations (not in scope for v0.1)

- **Flood wait handling** — automatic retry with `FLOOD_WAIT_X` backoff
- **Reconnect on drop** — session resume after TCP disconnect
- **Multi-account** — multiple `MtprotoClient` instances, one per account
- **Multi-DC** — transparent request routing to media/upload DCs
- **Python bindings** — pybind11 over the C API
- **Go bindings** — cgo over the C API
