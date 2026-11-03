// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#pragma once
#include "sql/parser/ob_parser.h"
#include "share/schema/catalog_dml_sql_helper.h"
#include <string>
#include <vector>

namespace catalog_sql_namespace_test {
using namespace oceanbase::common;
inline void check(const std::string &sql)
{
  ObArenaAllocator arena;
  oceanbase::sql::ObParser parser(arena, DEFAULT_MYSQL_MODE);
  ParseResult parsed{};
  const int parsed_status = parser.parse(ObString(sql.size(), sql.data()), parsed);
  if (parsed_status != OB_SUCCESS) std::cerr << "catalog fixture SQL: " << sql << std::endl;
  CHECK(parsed_status == OB_SUCCESS);
  int relations = 0;
  std::vector<const ParseNode *> pending{parsed.result_tree_};
  while (!pending.empty()) {
    const auto *node = pending.back(); pending.pop_back();
    if (!node) continue;
    if (node->type_ == T_RELATION_FACTOR) {
      if (node->num_child_ < 2 || !node->children_ || !node->children_[0])
        std::cerr << "unqualified catalog fixture SQL: " << sql << std::endl;
      CHECK(node->num_child_ >= 2 && node->children_ && node->children_[0]);
      const auto *database = node->children_[0];
      CHECK(database->str_value_ &&
          std::string(database->str_value_, database->str_len_) == OB_SYS_DATABASE_NAME);
      ++relations;
    }
    for (int i = 0; i < node->num_child_; ++i) pending.push_back(node->children_[i]);
  }
  CHECK(relations > 0);
}
inline void run()
{
  using namespace oceanbase::share;
  using namespace oceanbase::share::schema;
  ExtensionVersionRows client;
  client.write_status = OB_SUCCESS;
  CatalogDMLSqlHelper catalog(client);
  ObDMLSqlSplicer splicer;
  CHECK(splicer.add_pk_column("routine_id", uint64_t{42}) == OB_SUCCESS);
  CHECK(splicer.add_column("routine_body",
      ObHexEscapeSqlStr(ObString::make_string("SELECT '__all_routine'"))) == OB_SUCCESS);
  const char *tables[] = {OB_ALL_ROUTINE_TNAME, OB_ALL_ROUTINE_HISTORY_TNAME,
      OB_ALL_ROUTINE_PARAM_TNAME, OB_ALL_ROUTINE_PARAM_HISTORY_TNAME,
      OB_ALL_ROUTINE_PRIVILEGE_TNAME, OB_ALL_ROUTINE_PRIVILEGE_HISTORY_TNAME};
  int64_t rows = 99;
  for (const char *table : tables) {
    CHECK(catalog.exec_insert(table, splicer, rows) == OB_SUCCESS && rows == 1);
    CHECK(catalog.exec_update(table, splicer, rows) == OB_SUCCESS && rows == 1);
    CHECK(catalog.exec_delete(table, splicer, rows) == OB_SUCCESS && rows == 1);
    CHECK(catalog.exec_replace(table, splicer, rows) == OB_SUCCESS && rows == 1);
  }
  CHECK(client.written.size() == 24);
  for (const auto &sql : client.written) check(sql);
  ObSqlString unqualified;
  CHECK(splicer.splice_insert_sql(tables[0], unqualified) == OB_SUCCESS);
  std::string expected = unqualified.ptr();
  const auto table_offset = expected.find(tables[0]);
  CHECK(table_offset != std::string::npos);
  expected.insert(table_offset, "oceanbase.");
  CHECK(client.written.front() == expected); // Body literals/columns are unchanged.
  CHECK(catalog.exec_insert(nullptr, splicer, rows) == OB_INVALID_ARGUMENT && rows == 0);
  client.write_status = OB_TIMEOUT;
  CHECK(catalog.exec_insert(tables[0], splicer, rows) == OB_TIMEOUT && rows == 0);
  // Ordinary DML retains its caller-selected namespace; only catalog writers
  // opt into the helper. No connection acquisition or transaction control.
  client.write_status = OB_SUCCESS;
  ObDMLExecHelper ordinary(client);
  CHECK(ordinary.exec_insert("user_table", splicer, rows) == OB_SUCCESS);
  CHECK(client.written.back().find("INSERT INTO user_table ") == 0);
  CHECK(client.starts == 0 && client.ends == 0);
}
}
