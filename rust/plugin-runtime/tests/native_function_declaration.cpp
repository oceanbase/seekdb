/*
 * Copyright (c) 2026 OceanBase.
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
#include "sql/parser/ob_parser.h"
#include "sql/resolver/ddl/native_function_declaration.h"
#include "sql/resolver/ddl/native_function_default.h"
#include "sql/resolver/ddl/ob_create_routine_resolver.h"
#include "sql/resolver/ob_schema_checker.h"
#include "sql/session/ob_sql_session_info.h"
#include "lib/charset/ob_charset.h"
#include "lib/allocator/ob_allocator_v2.h"
#include <cstdlib>
#include <iostream>

using namespace oceanbase::common;
using namespace oceanbase::sql;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __LINE__ << ": " << #expr << std::endl; std::abort(); } } while (false)

static int declaration(const std::string &sql, NativeFunctionDeclaration &output)
{
  ObArenaAllocator arena;
  ObParser parser(arena, 0);
  ParseResult result{};
  const int ret = parser.parse(ObString(sql.size(), sql.data()), result);
  if (ret != OB_SUCCESS) return ret;
  CHECK(result.result_tree_ && result.result_tree_->num_child_ == 1);
  const auto *function = result.result_tree_->children_[0];
  CHECK(function && function->type_ == T_SF_CREATE && function->num_child_ == 6);
  const auto *body = function->children_[5];
  CHECK(body && body->type_ == T_SF_NATIVE_BODY);
  CHECK(body->str_len_ > 0); // Original source remains available for diagnostics.
  const int read_status = NativeFunctionDeclaration::read(body, output);
  // With no SUPER privilege native syntax must stop before name/owner
  // resolution or PL compilation, leaving the outgoing argument unpopulated.
  ObSQLSessionInfo session;
  ObSchemaChecker checker;
  ObResolverParams params;
  params.allocator_ = &arena;
  params.session_info_ = &session;
  params.schema_checker_ = &checker;
  ObCreateFunctionResolver resolver(params);
  oceanbase::obcall::ObCreateRoutineArg arg;
  CHECK(resolver.resolve_impl(*function, &arg) ==
        (read_status == OB_SUCCESS ? OB_ERR_NO_PRIVILEGE : read_status));
  CHECK(arg.routine_info_.get_routine_name().empty());
  CHECK(arg.routine_info_.get_routine_params().empty());
  return read_status;
}

int main()
{
  CHECK(ObCharset::init_charset() == OB_SUCCESS);
  NativeFunctionDeclaration output;
  for (const char *text : {"0", "-1", "+ 3.5", ".5", "1.", "1e-8", "-2E+3", "NULL", "true", " FALSE "}) {
    CHECK(NativeFunctionDefault::supported(ObString::make_string(text)));
  }
  for (const char *text : {"", " ", ".", "-", "1e", "1e+", "1 2", "1+2", "--1", "f()", "@x",
                          "NULL; SELECT 1", "'text'", "1/*comment*/", "0x12", "infinity", "nan", "1e 3"}) {
    CHECK(!NativeFunctionDefault::supported(ObString::make_string(text)));
  }
  const char embedded[] = {'1', '\0', '2'};
  CHECK(!NativeFunctionDefault::supported(ObString(sizeof(embedded), embedded)));
  CHECK(declaration("CREATE FUNCTION defaults(x DOUBLE DEFAULT -1.5, y DOUBLE DEFAULT NULL) RETURNS DOUBLE "
      "AS 'org.seekdb.gis', 'org.seekdb.gis.area' LANGUAGE C", output) == OB_SUCCESS);
  CHECK(declaration("CREATE FUNCTION lines(VARIADIC points GEOMETRY[]) RETURNS GEOMETRY "
      "AS 'org.seekdb.gis', 'org.seekdb.gis.function.st_linestring' LANGUAGE C", output) == OB_SUCCESS);
  const std::string prefix = "CREATE FUNCTION demo(input_value DOUBLE) RETURNS DOUBLE ";
  for (const char *language : {"C", "c", "`C`"}) {
    CHECK(declaration(prefix + "AS 'org.seekdb.gis', 'org.seekdb.gis.area' LANGUAGE " + language, output) == OB_SUCCESS);
    // Ownership must survive all parser/source temporaries above.
    CHECK(output.module_id_ == "org.seekdb.gis" && output.implementation_id_ == "org.seekdb.gis.area");
  }
  CHECK(declaration("CREATE FUNCTION `db`.`area_alias`(g GEOMETRY) RETURNS DOUBLE "
      "DETERMINISTIC NO SQL SQL SECURITY INVOKER AS 'org.seekdb.gis', 'org.seekdb.gis.area' LANGUAGE C", output) == OB_SUCCESS);
  for (const char *module : {"", "../gis", "$libdir/gis", "org.GIS", "org.gis''x", "模块", "org.gis\\0hidden"}) {
    CHECK(declaration(prefix + "AS '" + module + "', 'org.seekdb.gis.area' LANGUAGE C", output) == OB_INVALID_ARGUMENT);
    CHECK(output.module_id_.empty() && output.implementation_id_.empty());
  }
  CHECK(declaration(prefix + "AS 'org.gis', '' LANGUAGE C", output) == OB_INVALID_ARGUMENT);
  CHECK(declaration(prefix + "AS 'org.gis', 'area' LANGUAGE python", output) == OB_NOT_SUPPORTED);
  CHECK(declaration(prefix + "AS '" + std::string(256, 'a') + "', 'area' LANGUAGE C", output) == OB_INVALID_ARGUMENT);
  CHECK(NativeFunctionDeclaration::read(nullptr, output) == OB_INVALID_ARGUMENT);
  CHECK(output.module_id_.empty());
  for (const std::string &sql : {
      prefix + "AS 'org.gis' LANGUAGE C",
      prefix + "AS 'org.gis', 'area', 'extra' LANGUAGE C",
      std::string("CREATE FUNCTION bad(VARIADIC points GEOMETRY) RETURNS GEOMETRY AS 'org.gis', 'line' LANGUAGE C"),
      prefix + "AS 'org.gis', 'area' LANGUAGE C RETURN 1"}) {
    ObArenaAllocator arena;
    ObParser parser(arena, 0);
    ParseResult result{};
    CHECK(parser.parse(ObString(sql.size(), sql.data()), result) != OB_SUCCESS);
  }
  for (const char *sql : {
      "CREATE FUNCTION ordinary(x INT) RETURNS INT RETURN x + 1",
      "CREATE PROCEDURE ordinary() SELECT 1 AS one",
      "CREATE FUNCTION ordinary() RETURNS INT BEGIN DECLARE x INT; SELECT 1 AS one INTO x; RETURN x; END",
      "SELECT 1 AS one",
      "FLUSH PRIVILEGES"}) {
    ObArenaAllocator arena;
    ObParser parser(arena, 0);
    ParseResult result{};
    CHECK(parser.parse(ObString::make_string(sql), result) == OB_SUCCESS);
  }
  std::cout << "PASS: native grammar, owned identities and privileged CREATE admission; no catalog installation/execution claims" << std::endl;
}
