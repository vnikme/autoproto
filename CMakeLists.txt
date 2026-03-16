cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(TDLib VERSION 1.8.62 LANGUAGES CXX C)

if (NOT DEFINED CMAKE_MODULE_PATH)
  set(CMAKE_MODULE_PATH "")
endif()
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake" "${CMAKE_MODULE_PATH}")

if (NOT DEFINED CMAKE_INSTALL_LIBDIR)
  set(CMAKE_INSTALL_LIBDIR "lib")
endif()
if (NOT DEFINED CMAKE_INSTALL_BINDIR)
  set(CMAKE_INSTALL_BINDIR "bin")
endif()
if (NOT DEFINED CMAKE_INSTALL_INCLUDEDIR)
  set(CMAKE_INSTALL_INCLUDEDIR "include")
endif()

if (POLICY CMP0074)
  # use environment variables to find libraries
  cmake_policy(SET CMP0074 NEW)
endif()

include(PreventInSourceBuild)
prevent_in_source_build()

option(TD_INSTALL_STATIC_LIBRARIES "Enable installation of static libraries." ON)
option(TD_INSTALL_SHARED_LIBRARIES "Enable installation of shared libraries." ON)
option(TD_ENABLE_JNI "Use \"ON\" to enable JNI-compatible TDLib API.")
option(TD_ENABLE_DOTNET "Use \"ON\" or \"CX\" to enable generation of C++/CLI or C++/CX TDLib API bindings respectively.")
if (NOT CMAKE_CROSSCOMPILING)
  option(TD_GENERATE_SOURCE_FILES "Use \"ON\" to just generate TDLib source files.")
endif()

include(CTest)

option(TD_ENABLE_LTO "Use \"ON\" to enable Link Time Optimization.")

if (TD_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT IPO_SUPPORTED)
  if (IPO_SUPPORTED)
    # set_property(DIRECTORY PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE) do not work?
    string(REPLACE ";" " " CXX_FLAGS_IPO "${CMAKE_CXX_COMPILE_OPTIONS_IPO}")
    message(STATUS "Use link time optimization CXX options: ${CXX_FLAGS_IPO}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_FLAGS_IPO}")

    string(REPLACE ";" " " C_FLAGS_IPO "${CMAKE_C_COMPILE_OPTIONS_IPO}")
    message(STATUS "Use link time optimization C options: ${C_FLAGS_IPO}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${C_FLAGS_IPO}")

    string(REPLACE ";" " " LINK_FLAGS_IPO "${CMAKE_CXX_LINK_OPTIONS_IPO}")
    message(STATUS "Use link time optimization linker options: ${LINK_FLAGS_IPO}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${LINK_FLAGS_IPO}")
  endif()
endif()

# Configure Ccache if available
find_program(CCACHE_FOUND ccache)
#set(CCACHE_FOUND 0)
if (CCACHE_FOUND)
  message(STATUS "Found ccache")
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
else()
  message(STATUS "Could NOT find ccache (this is NOT an error)")
endif()

set(MEMPROF "" CACHE STRING "Use one of \"ON\", \"FAST\" or \"SAFE\" to enable memory profiling. \
Works under macOS and Linux when compiled using glibc. \
In FAST mode stack is unwinded only using frame pointers, which may fail. \
In SAFE mode stack is unwinded using backtrace function from execinfo.h, which may be very slow. \
By default both methods are used to achieve the maximum speed and accuracy")

if (EMSCRIPTEN)
  # use prebuilt zlib
  set(ZLIB_FOUND 1)
  set(ZLIB_LIBRARIES)
  set(ZLIB_INCLUDE_DIR)

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s ALLOW_MEMORY_GROWTH=1 -s MEMFS_APPEND_TO_TYPED_ARRAYS=1 -s USE_ZLIB=1 -s MODULARIZE=1 \
    -s EXPORT_NAME=\"'createTdwebModule'\" -s WEBSOCKET_URL=\"'wss:#'\" -s EXPORTED_RUNTIME_METHODS=\"['FS','cwrap']\" -lidbfs.js -lworkerfs.js")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s ALLOW_MEMORY_GROWTH=1 -s MEMFS_APPEND_TO_TYPED_ARRAYS=1 -s USE_ZLIB=1 -s MODULARIZE=1 \
    -s EXPORT_NAME=\"'createTdwebModule'\" -s WEBSOCKET_URL=\"'wss:#'\" -s EXPORTED_RUNTIME_METHODS=\"['FS','cwrap']\" -lidbfs.js -lworkerfs.js")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -s DEMANGLE_SUPPORT=1 -s ASSERTIONS=1")
  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -s DEMANGLE_SUPPORT=1 -s ASSERTIONS=1")

  set(TD_EMSCRIPTEN td_wasm)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s WASM=1 --post-js ${CMAKE_CURRENT_SOURCE_DIR}/post.js")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s WASM=1")
endif()

if (NOT TD_GENERATE_SOURCE_FILES)
  if (NOT OPENSSL_FOUND)
    find_package(OpenSSL)
  endif()
  if (OPENSSL_FOUND)
    message(STATUS "Found OpenSSL: ${OPENSSL_INCLUDE_DIR} ${OPENSSL_LIBRARIES}")
  endif()
endif()

set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if (THREADS_HAVE_PTHREAD_ARG)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif()

include(TdSetUpCompiler)
td_set_up_compiler()

if (MSVC)
  option(TD_ENABLE_MULTI_PROCESSOR_COMPILATION "Use \"ON\" to enable multi-processor compilation.")

  if (TD_ENABLE_MULTI_PROCESSOR_COMPILATION)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
  endif()

  # https://github.com/tdlib/td/issues/2912
  # https://github.com/tdlib/td/issues/3202
  add_definitions(-D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
endif()

if (CLANG OR GCC)
  if (MEMPROF)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-no-pie CXX_NO_PIE_FLAG)
    if (CXX_NO_PIE_FLAG)
      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -no-pie")
    elseif (APPLE)
      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-no_pie")
    endif()
    include(AddCXXCompilerFlag)
    add_cxx_compiler_flag("-static-libstdc++")
    add_cxx_compiler_flag("-static-libgcc")
  endif()
endif()

include(GetGitRevisionDescription)
get_git_head_revision(TD_GIT_REFSPEC TD_GIT_COMMIT_HASH)
message(STATUS "Git state: ${TD_GIT_COMMIT_HASH}")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/td/telegram/GitCommitHash.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/td/telegram/GitCommitHash.cpp" @ONLY)

add_subdirectory(tdtl)

if (TD_GENERATE_SOURCE_FILES)
  set(TDUTILS_MIME_TYPE ON CACHE BOOL "" FORCE)
  set(TDUTILS_USE_EXTERNAL_DEPENDENCIES OFF CACHE BOOL "" FORCE)
endif()
set(TDUTILS_ENABLE_INSTALL ${TD_INSTALL_STATIC_LIBRARIES} CACHE BOOL "" FORCE)
add_subdirectory(tdutils)

add_subdirectory(td/generate)

if (NOT CMAKE_CROSSCOMPILING)
  set(TD_ALWAYS_GENERATE_SOURCE "")
  if (TD_GENERATE_SOURCE_FILES)
    set(TD_ALWAYS_GENERATE_SOURCE "ALL")
  endif()
  add_custom_target(prepare_cross_compiling ${TD_ALWAYS_GENERATE_SOURCE} DEPENDS tl_generate_mtproto tl_generate_common tdmime_auto tl_generate_json)
  if (TD_ENABLE_DOTNET)
    add_dependencies(prepare_cross_compiling generate_dotnet_api)
  endif()
endif()

if (NOT OPENSSL_FOUND OR TD_GENERATE_SOURCE_FILES)
  if (NOT TD_GENERATE_SOURCE_FILES)
    message(WARNING "Can't find OpenSSL: stop building")
  endif()
  return()
endif()

if (NOT ZLIB_FOUND)
  find_package(ZLIB)
endif()
if (NOT ZLIB_FOUND)
  message(WARNING "Can't find zlib: stop building")
  return()
endif()

if (NOT TDUTILS_MIME_TYPE OR NOT TDUTILS_USE_EXTERNAL_DEPENDENCIES)
  message(WARNING "Option TDUTILS_MIME_TYPE and TDUTILS_USE_EXTERNAL_DEPENDENCIES must not be disabled: stop TDLib building")
  return()
endif()

set(TDACTOR_ENABLE_INSTALL ${TD_INSTALL_STATIC_LIBRARIES} CACHE BOOL "" FORCE)
add_subdirectory(tdactor)

set(TDNET_ENABLE_INSTALL ${TD_INSTALL_STATIC_LIBRARIES} CACHE BOOL "" FORCE)
add_subdirectory(tdnet)

if (BUILD_TESTING)
  add_subdirectory(test)
endif()

if (NOT CMAKE_CROSSCOMPILING)
  add_subdirectory(benchmark)
endif()


get_directory_property(HAS_PARENT PARENT_DIRECTORY)
if (HAS_PARENT)
  set(TL_TD_JSON_AUTO ${TL_TD_JSON_AUTO_SOURCE} PARENT_SCOPE) # used in tdbot
  set(TD_TEST_SOURCE ${TD_TEST_SOURCE} PARENT_SCOPE) # used to build tests
endif()


# SOURCE SETS

set_source_files_properties(${TL_TD_API_AUTO_SOURCE} PROPERTIES GENERATED TRUE)
if (TD_ENABLE_JNI OR ANDROID)
  set(TL_JNI_OBJECT_SOURCE
    td/tl/tl_jni_object.cpp
    td/tl/tl_jni_object.h
  )
else()
  set(TL_JNI_OBJECT_SOURCE)
endif()

set(TL_TD_API_SOURCE
  ${TL_TD_API_AUTO_SOURCE}
  ${TL_JNI_OBJECT_SOURCE}
  td/tl/TlObject.h
)

set_source_files_properties(${TL_MTPROTO_AUTO_SOURCE} PROPERTIES GENERATED TRUE)

set_source_files_properties(${TL_TD_AUTO_SOURCE} PROPERTIES GENERATED TRUE)

set_source_files_properties(${TL_TD_JSON_AUTO_SOURCE} PROPERTIES GENERATED TRUE)
set(TL_TD_JSON_SOURCE
  ${TL_TD_JSON_AUTO_SOURCE}
  td/tl/tl_json.h
)

set_source_files_properties(${TL_C_AUTO_SOURCE} PROPERTIES GENERATED TRUE)
set(TL_C_SCHEME_SOURCE
  ${TL_C_AUTO_SOURCE}
)

set_source_files_properties(${TL_DOTNET_AUTO_SOURCE} PROPERTIES GENERATED TRUE)
set(TL_DOTNET_SCHEME_SOURCE
  ${TL_DOTNET_AUTO_SOURCE}
  td/tl/tl_dotnet_object.h
)

set(TD_MTPROTO_SOURCE
  td/mtproto/AuthData.cpp
  td/mtproto/ConnectionManager.cpp
  td/mtproto/DhHandshake.cpp
  td/mtproto/Handshake.cpp
  td/mtproto/HandshakeActor.cpp
  td/mtproto/HttpTransport.cpp
  td/mtproto/IStreamTransport.cpp
  td/mtproto/KDF.cpp
  td/mtproto/Ping.cpp
  td/mtproto/PingConnection.cpp
  td/mtproto/ProxySecret.cpp
  td/mtproto/RawConnection.cpp
  td/mtproto/RSA.cpp
  td/mtproto/SessionConnection.cpp
  td/mtproto/TcpTransport.cpp
  td/mtproto/TlsInit.cpp
  td/mtproto/TlsReaderByteFlow.cpp
  td/mtproto/Transport.cpp
  td/mtproto/utils.cpp

  td/mtproto/AuthData.h
  td/mtproto/AuthKey.h
  td/mtproto/ConnectionManager.h
  td/mtproto/CryptoStorer.h
  td/mtproto/DhCallback.h
  td/mtproto/DhHandshake.h
  td/mtproto/Handshake.h
  td/mtproto/HandshakeActor.h
  td/mtproto/HandshakeConnection.h
  td/mtproto/HttpTransport.h
  td/mtproto/IStreamTransport.h
  td/mtproto/KDF.h
  td/mtproto/MessageId.h
  td/mtproto/MtprotoQuery.h
  td/mtproto/NoCryptoStorer.h
  td/mtproto/PacketInfo.h
  td/mtproto/PacketStorer.h
  td/mtproto/Ping.h
  td/mtproto/PingConnection.h
  td/mtproto/ProxySecret.h
  td/mtproto/RawConnection.h
  td/mtproto/RSA.h
  td/mtproto/SessionConnection.h
  td/mtproto/TcpTransport.h
  td/mtproto/TlsInit.h
  td/mtproto/TlsReaderByteFlow.h
  td/mtproto/Transport.h
  td/mtproto/TransportType.h
  td/mtproto/utils.h

  ${TL_MTPROTO_AUTO_SOURCE}

  td/tl/TlObject.h
  td/tl/tl_object_parse.h
  td/tl/tl_object_store.h
)

set(TDLIB_SOURCE
  td/telegram/AuthManager.cpp
  td/telegram/ConfigManager.cpp
  td/telegram/DhCache.cpp
  td/telegram/Global.cpp
  td/telegram/JsonValue.cpp
  td/telegram/MtprotoClient.cpp
  td/telegram/SequenceDispatcher.cpp
  td/telegram/logevent/LogEventHelper.cpp
  td/telegram/net/AuthDataShared.cpp
  td/telegram/net/ConnectionCreator.cpp
  td/telegram/net/DcAuthManager.cpp
  td/telegram/net/DcOptionsSet.cpp
  td/telegram/net/MtprotoHeader.cpp
  td/telegram/net/NetActor.cpp
  td/telegram/net/NetQuery.cpp
  td/telegram/net/NetQueryCreator.cpp
  td/telegram/net/NetQueryDelayer.cpp
  td/telegram/net/NetQueryDispatcher.cpp
  td/telegram/net/NetQueryStats.cpp
  td/telegram/net/NetQueryVerifier.cpp
  td/telegram/net/NetStatsManager.cpp
  td/telegram/net/Proxy.cpp
  td/telegram/net/PublicRsaKeySharedCdn.cpp
  td/telegram/net/PublicRsaKeySharedMain.cpp
  td/telegram/net/PublicRsaKeyWatchdog.cpp
  td/telegram/net/Session.cpp
  td/telegram/net/SessionMultiProxy.cpp
  td/telegram/net/SessionProxy.cpp

  td/telegram/AuthManager.h
  td/telegram/ChainId.h
  td/telegram/ConfigManager.h
  td/telegram/ConnectionState.h
  td/telegram/DhCache.h
  td/telegram/DhConfig.h
  td/telegram/files/FileType.h
  td/telegram/GitCommitHash.h
  td/telegram/Global.h
  td/telegram/JsonValue.h
  td/telegram/LanguagePackManager.h
  td/telegram/MtprotoClient.h
  td/telegram/SequenceDispatcher.h
  td/telegram/StateManager.h
  td/telegram/TdDb.h
  td/telegram/UniqueId.h
  td/telegram/Version.h
  td/telegram/logevent/LogEvent.h
  td/telegram/logevent/LogEventHelper.h
  td/telegram/net/AuthDataShared.h
  td/telegram/net/AuthKeyState.h
  td/telegram/net/ConnectionCreator.h
  td/telegram/net/DcAuthManager.h
  td/telegram/net/DcId.h
  td/telegram/net/DcOptions.h
  td/telegram/net/DcOptionsSet.h
  td/telegram/net/MtprotoHeader.h
  td/telegram/net/NetActor.h
  td/telegram/net/NetQuery.h
  td/telegram/net/NetQueryCounter.h
  td/telegram/net/NetQueryCreator.h
  td/telegram/net/NetQueryDelayer.h
  td/telegram/net/NetQueryDispatcher.h
  td/telegram/net/NetQueryStats.h
  td/telegram/net/NetQueryVerifier.h
  td/telegram/net/NetStatsManager.h
  td/telegram/net/NetType.h
  td/telegram/net/Proxy.h
  td/telegram/net/PublicRsaKeySharedCdn.h
  td/telegram/net/PublicRsaKeySharedMain.h
  td/telegram/net/PublicRsaKeyWatchdog.h
  td/telegram/net/Session.h
  td/telegram/net/SessionMultiProxy.h
  td/telegram/net/SessionProxy.h
  td/telegram/net/TempAuthKeyWatchdog.h

  ${TL_TD_AUTO_SOURCE}

  td/tl/TlObject.h
  td/tl/tl_object_parse.h
  td/tl/tl_object_store.h

  ${CMAKE_CURRENT_BINARY_DIR}/td/telegram/GitCommitHash.cpp
)

set(MEMPROF_SOURCE
  memprof/memprof.cpp
  memprof/memprof.h
)

set(MEMPROF_STAT_SOURCE
  memprof/memprof_stat.cpp
  memprof/memprof_stat.h
)

# LIBRARIES

# memprof - simple library for memory usage profiling
add_library(memprof STATIC ${MEMPROF_SOURCE})
target_include_directories(memprof PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(memprof PRIVATE tdutils)
if (MEMPROF)
  target_compile_definitions(memprof PRIVATE -DUSE_MEMPROF=1)
  if (MEMPROF STREQUAL "SAFE")
    target_compile_definitions(memprof PRIVATE -DUSE_MEMPROF_SAFE=1)
  elseif (MEMPROF STREQUAL "FAST")
    target_compile_definitions(memprof PRIVATE -DUSE_MEMPROF_FAST=1)
  elseif (NOT MEMPROF)
    message(FATAL_ERROR "Unsupported MEMPROF value \"${MEMPROF}\"")
  endif()
endif()

add_library(memprof_stat EXCLUDE_FROM_ALL STATIC ${MEMPROF_STAT_SOURCE})
target_include_directories(memprof_stat PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(memprof_stat PRIVATE tdutils)


add_library(tdapi STATIC ${TL_TD_API_SOURCE})
target_include_directories(tdapi PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}> INTERFACE $<BUILD_INTERFACE:${TL_TD_AUTO_INCLUDE_DIR}>)
target_link_libraries(tdapi PRIVATE tdutils)

if (TD_ENABLE_JNI AND NOT ANDROID) # jni is available by default on Android
  if (NOT JNI_FOUND)
    find_package(JNI REQUIRED COMPONENTS JVM)
  endif()
  message(STATUS "Found JNI: ${JNI_INCLUDE_DIRS} ${JNI_LIBRARIES}")
  target_include_directories(tdapi SYSTEM PUBLIC ${JAVA_INCLUDE_PATH} ${JAVA_INCLUDE_PATH2})
  target_link_libraries(tdapi PUBLIC ${JAVA_JVM_LIBRARY})
endif()

if (NOT CMAKE_CROSSCOMPILING)
  add_dependencies(tdapi tl_generate_common)
endif()

add_library(tdmtproto STATIC ${TD_MTPROTO_SOURCE})
target_include_directories(tdmtproto PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}> $<BUILD_INTERFACE:${TL_TD_AUTO_INCLUDE_DIR}>)
target_include_directories(tdmtproto SYSTEM PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(tdmtproto PUBLIC tdactor tdnet tdutils PRIVATE ${OPENSSL_CRYPTO_LIBRARY} ${CMAKE_DL_LIBS} ${ZLIB_LIBRARIES})
if (WIN32)
  if (MINGW)
    target_link_libraries(tdmtproto PRIVATE ws2_32 mswsock crypt32)
  else()
    target_link_libraries(tdmtproto PRIVATE ws2_32 Mswsock Crypt32)
  endif()
endif()
if (NOT CMAKE_CROSSCOMPILING)
  add_dependencies(tdmtproto tl_generate_mtproto)
endif()

# tdcore - internal TDLib interface
add_library(tdcore STATIC ${TDLIB_SOURCE})
target_include_directories(tdcore PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}> $<BUILD_INTERFACE:${TL_TD_AUTO_INCLUDE_DIR}>)
target_link_libraries(tdcore PUBLIC tdapi tdnet tdactor tdutils PRIVATE tdmtproto)

# mtproto - public API wrapper
add_library(mtproto STATIC
  mtproto/Client.cpp
  mtproto/Client.h
)
target_include_directories(mtproto PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(mtproto PUBLIC tdcore)

# Smoke test executable
add_executable(smoke_test examples/smoke_test.cpp)
target_link_libraries(smoke_test PRIVATE mtproto)
set_target_properties(smoke_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Echo bot example
add_executable(echo_bot examples/echo_bot.cpp)
target_link_libraries(echo_bot PRIVATE mtproto)
set_target_properties(echo_bot PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Channel crawler example
add_executable(channel_crawler examples/channel_crawler.cpp)
target_link_libraries(channel_crawler PRIVATE mtproto)
set_target_properties(channel_crawler PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Userbot example
add_executable(userbot examples/userbot.cpp)
target_link_libraries(userbot PRIVATE mtproto)
set_target_properties(userbot PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(TD_CORE_PART_TARGETS)

# Convenience target: regenerate all TL bindings from schema files
if (NOT CMAKE_CROSSCOMPILING)
  add_custom_target(generate_tl
    DEPENDS tl_generate_tlo tl_generate_mtproto tl_generate_common
    COMMENT "Regenerating all TL bindings from schema files"
  )
endif()

if (NOT CMAKE_CROSSCOMPILING)
  add_dependencies(tdcore tl_generate_common)
  if (TD_ENABLE_JNI)
    add_dependencies(tdcore td_generate_java_api)
  endif()
endif()

