/*
 * Copyright (c) 2025 OceanBase.
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

#ifndef OCEANBASE_UNITTEST_PL_TEST_PL_UTILS_H_
#define OCEANBASE_UNITTEST_PL_TEST_PL_UTILS_H_

#include <fstream>
#include <dirent.h>
#include <getopt.h>
#include <gtest/gtest.h>
#define private public
#include "share/ob_errno.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/oblog/ob_log_module.h"
#include "../sql/test_sql_utils.h"
#include "pl/ob_pl.h"
#include "pl/ob_pl_stmt.h"

namespace test
{

class TestPLUtils : public TestSqlUtils
{
public:
  TestPLUtils() : TestSqlUtils(), case_id_(0) {}
  virtual ~TestPLUtils(){}
  virtual void init();
  virtual void destroy();

// function members
  void resolve_test(const char* test_file, const char* result_file, const char* tmp_file);
  void compile_test(const char* test_file, const char* result_file, const char* tmp_file);
  void resolve_pl(const char* pl_str, std::ofstream &of_result);
  void compile_pl(const char *query_str, std::ofstream &of_result);
  int do_resolve(const char* pl_str, sql::ObRawExprFactory &expr_factory, oceanbase::pl::ObPLFunctionAST &func);
  int do_compile(const char* pl_str, oceanbase::pl::ObPLFunction &func);

public:
  int64_t case_id_;

private:
  DISALLOW_COPY_AND_ASSIGN(TestPLUtils);
};

}

#endif /* OCEANBASE_UNITTEST_PL_TEST_PL_UTILS_H_ */
