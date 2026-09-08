// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include "sql/engine/expr/ob_expr_regexp_context.h"
#include "lib/allocator/page_arena.h"
#include "lib/resource/achunk_mgr.h"
#include <unicode/uclean.h>
#include <cassert>
#include <cstdio>
#include <pthread.h>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;

static ObString utf16(ObArenaAllocator &allocator, const char *input)
{
  ObString result;
  assert(ObExprRegexContext::convert_to_regexp_utf16(allocator,
    ObString::make_string(input), CS_TYPE_UTF8MB4_BIN, result) == OB_SUCCESS);
  return result;
}

static void *check_regex(void *)
{
  for (int round = 0; round < 8; ++round) {
    ObArenaAllocator allocator;
    ObExprRegexpSessionVariables limits;
    limits.regexp_stack_limit_ = 1024 * 1024;
    limits.regexp_time_limit_ = 1000;
    ObExprRegexContext regex;
    // Unicode property matching requires the real ICU data package.
    assert(regex.init(allocator, limits, ObString::make_string("\\p{Han}+"),
      0, true, CS_TYPE_UTF8MB4_BIN) == OB_SUCCESS);
    const ObString text = utf16(allocator, "abc中文 def汉字😀");
    bool matched = false;
    assert(regex.match(allocator, text, CS_TYPE_UTF16_BIN, 0, matched) == OB_SUCCESS && matched);
    int64_t position = -1;
    assert(regex.find(allocator, text, CS_TYPE_UTF16_BIN, 0, 2, 0, 0, position) == OB_SUCCESS);
    assert(position == 10);
    ObString substring, result;
    assert(regex.substr(allocator, text, CS_TYPE_UTF16_BIN, 0, 2, 0, substring) == OB_SUCCESS);
    assert(ObExprRegexContext::convert_from_regexp_utf16(allocator, substring,
      CS_TYPE_UTF8MB4_BIN, result) == OB_SUCCESS);
    assert(result == ObString::make_string("汉字"));
    assert(regex.replace(allocator, text, CS_TYPE_UTF16_BIN, utf16(allocator, "X"),
      0, 0, substring) == OB_SUCCESS);
    assert(ObExprRegexContext::convert_from_regexp_utf16(allocator, substring,
      CS_TYPE_UTF8MB4_BIN, result) == OB_SUCCESS);
    assert(result == ObString::make_string("abcX defX😀"));
    assert(regex.init(allocator, limits, ObString::make_string("^abc$"),
      UREGEX_CASE_INSENSITIVE, true, CS_TYPE_UTF8MB4_BIN) == OB_SUCCESS);
    assert(regex.match(allocator, utf16(allocator, "ABC"), CS_TYPE_UTF16_BIN, 0, matched) == OB_SUCCESS && matched);
    assert(regex.init(allocator, limits, ObString::make_string("["),
      0, true, CS_TYPE_UTF8MB4_BIN) != OB_SUCCESS);
    assert(!regex.is_inited());
    assert(regex.init(allocator, limits, ObString::make_string("."),
      0, true, CS_TYPE_UTF8MB4_BIN) == OB_SUCCESS);
    assert(regex.substr(allocator, utf16(allocator, "😀"), CS_TYPE_UTF16_BIN,
      0, 1, 0, substring) == OB_SUCCESS);
    assert(ObExprRegexContext::convert_from_regexp_utf16(allocator, substring,
      CS_TYPE_UTF8MB4_BIN, result) == OB_SUCCESS);
    assert(result == ObString::make_string("😀"));
  }
  return nullptr;
}

int main()
{
  UVersionInfo version;
  u_getVersion(version);
  assert(version[0] == 69 && version[1] == 1);
  UErrorCode status = U_ZERO_ERROR;
  u_init(&status);
  assert(U_SUCCESS(status));
  lib::AChunkMgr::instance().set_limit(64 * 1024 * 1024);
  lib::AChunkMgr::instance().set_hard_limit(64 * 1024 * 1024);
  pthread_t workers[2];
  for (auto &worker : workers) assert(pthread_create(&worker, nullptr, check_regex, nullptr) == 0);
  check_regex(nullptr);
  for (auto &worker : workers) assert(pthread_join(worker, nullptr) == 0);
  u_cleanup();
  puts("PASS: seekdb regex context, Unicode properties, substrings/replacements and concurrent reuse");
}
