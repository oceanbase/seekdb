// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#pragma once
#include <cstdint>
#include <limits>

namespace oceanbase { namespace common {
// Visit template spans without allocation. Literal braces are preserved; numeric
// placeholders must name an argument. Both sizing and writing use this parser.
template <typename Emit>
bool visit_prompt_template(const char *text, int64_t size, int64_t count, Emit emit)
{
  for (int64_t i = 0; i < size;) {
    int64_t end = i + 1;
    int64_t index = 0;
    bool overflow = false;
    if (text[i] == '{' && end < size && text[end] >= '0' && text[end] <= '9') {
      while (end < size && text[end] >= '0' && text[end] <= '9') {
        const int digit = text[end++] - '0';
        if (index > (std::numeric_limits<int64_t>::max() - digit) / 10) {
          overflow = true;
        } else if (!overflow) {
          index = index * 10 + digit;
        }
      }
      if (end < size && text[end] == '}') {
        if (overflow || index >= count || !emit(nullptr, 0, index)) { return false; }
        i = end + 1;
        continue;
      }
    }
    if (!emit(text + i, 1, -1)) { return false; }
    ++i;
  }
  return true;
}
} } // namespace oceanbase::common
