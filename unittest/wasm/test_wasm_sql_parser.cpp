/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>
#include <openssl/bn.h>
#include <pthread.h>
#include "lib/allocator/page_arena.h"
#include "lib/charset/ob_charset.h"
#include "lib/resource/achunk_mgr.h"
#include "data_plane/blocksstable/ob_storage_datum.h"
#include "sql/parser/ob_sql_parser.h"
#include "sql/parser/parse_define.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;

static bool has_integer(const ParseNode *node, int64_t value)
{
  if (node == nullptr) { return false; }
  if (node->type_ == T_INT && node->value_ == value) { return true; }
  for (int i = 0; i < node->num_child_; ++i) {
    if (has_integer(node->children_[i], value)) { return true; }
  }
  return false;
}

static void check_sql(const char *query, ObItemType type)
{
  ObArenaAllocator arena("WasmParser");
  ParseResult result{};
  result.malloc_pool_ = &arena;
  result.charset_info_ = ObCharset::get_charset(CS_TYPE_UTF8MB4_BIN);
  result.connection_collation_ = CS_TYPE_UTF8MB4_BIN;
  result.semicolon_start_col_ = INT32_MAX;
  result.minus_ctx_.pos_ = -1;
  result.minus_ctx_.raw_sql_offset_ = -1;
  ObSQLParser parser(arena, 0);
  const int ret = parser.parse(query, strlen(query), result);
  if (ret != OB_SUCCESS) { std::fprintf(stderr, "parse error %d: %s\n", ret, result.error_msg_); }
  assert(ret == OB_SUCCESS);
  const ParseNode *statement = result.result_tree_;
  assert(statement != nullptr);
  if (statement->type_ == T_STMT_LIST) {
    assert(statement->num_child_ == 1);
    statement = statement->children_[0];
  }
  assert(statement != nullptr && statement->type_ == type);
  if (strstr(query, "9223372036854775807")) {
    assert(has_integer(statement, INT64_MAX));
  }
  assert(parse_terminate(&result) == OB_SUCCESS);
}

static void *parse_on_worker(void *)
{
  for (int round = 0; round < 20; ++round) {
    check_sql("CREATE TABLE docs (id BIGINT PRIMARY KEY, text VARCHAR(200))", T_CREATE_TABLE);
    check_sql("INSERT INTO docs VALUES (9223372036854775807, '中文\xF0\x9F\x8C\x8D')", T_INSERT);
    check_sql("SELECT id, text FROM docs WHERE id = 9223372036854775807 ORDER BY id", T_SELECT);
    check_sql("UPDATE docs SET text = 'next' WHERE id = 1", T_UPDATE);
    check_sql("DELETE FROM docs WHERE id = 1", T_DELETE);
    check_sql("BEGIN", T_BEGIN);
    check_sql("COMMIT", T_COMMIT);
    check_sql("ROLLBACK", T_ROLLBACK);
    ObArenaAllocator arena("WasmBadSql");
    ParseResult result{};
    result.malloc_pool_ = &arena;
    result.charset_info_ = ObCharset::get_charset(CS_TYPE_UTF8MB4_BIN);
    result.connection_collation_ = CS_TYPE_UTF8MB4_BIN;
    ObSQLParser parser(arena, 0);
    assert(parser.parse("SELECT FROM", 11, result) != OB_SUCCESS);
    assert(result.error_msg_ != nullptr && result.error_msg_[0] != '\0');
    assert(parse_terminate(&result) == OB_SUCCESS);
  }
  return nullptr;
}

int main()
{
  oceanbase::blocksstable::ObStorageDatum datum;
  datum.set_int(INT64_MIN);
  assert(datum.get_int() == INT64_MIN);
  datum.set_uint(UINT64_MAX);
  assert(datum.get_uint() == UINT64_MAX);
  assert(reinterpret_cast<const char *>(&datum.pack_)
         - reinterpret_cast<const char *>(&datum) == 8);
  assert(reinterpret_cast<const char *>(datum.buf_)
         - reinterpret_cast<const char *>(&datum) == 16);
  datum.set_null();
  assert(datum.is_null());
  datum.reuse();
  datum.set_double(3.25);
  assert(datum.get_double() == 3.25);
  static_assert(sizeof(BN_ULONG) == 4 && BN_BITS2 == 32,
                "Use the wasm32 OpenSSL configuration, not native headers");
  BIGNUM *number = nullptr;
  const char *decimal = "340282366920938463463374607431768211455";
  assert(BN_dec2bn(&number, decimal) == 39);
  char *roundtrip = BN_bn2dec(number);
  assert(roundtrip != nullptr && strcmp(roundtrip, decimal) == 0);
  OPENSSL_free(roundtrip);
  BN_free(number);
  oceanbase::lib::AChunkMgr::instance().set_limit(64 * 1024 * 1024);
  oceanbase::lib::AChunkMgr::instance().set_hard_limit(64 * 1024 * 1024);
  assert(ObCharset::init_charset() == OB_SUCCESS);
  assert(parse_init(nullptr) != OB_SUCCESS);
  {
    ObArenaAllocator arena("WasmPlBoundary");
    ParseResult result{};
    result.malloc_pool_ = &arena;
    result.pl_parse_info_.pl_ns_ = &arena;
    assert(parse_init(&result) == OB_PARSER_ERR_UNEXPECTED);
    assert(strstr(result.error_msg_, "symbol resolver") != nullptr);
  }
  pthread_attr_t attributes;
  assert(pthread_attr_init(&attributes) == 0);
  assert(pthread_attr_setstacksize(&attributes, 1024 * 1024) == 0);
  pthread_t threads[2];
  for (auto &thread : threads) { assert(pthread_create(&thread, &attributes, parse_on_worker, nullptr) == 0); }
  assert(pthread_attr_destroy(&attributes) == 0);
  parse_on_worker(nullptr);
  for (auto &thread : threads) { assert(pthread_join(thread, nullptr) == 0); }
  std::puts("PASS: seekdb SQL grammar, AST, int64 and invalid SQL in Wasm");
}
