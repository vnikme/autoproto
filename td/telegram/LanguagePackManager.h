//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/Slice.h"

namespace td {

class LanguagePackManager {
 public:
  static bool is_custom_language_code(Slice language_code) {
    return !language_code.empty() && language_code[0] == 'X';
  }
};

}  // namespace td
