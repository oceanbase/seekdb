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

#ifndef OCEANBASE_SQL_OB_CREATE_OUTLINE_EXECUTOR_H_
#define OCEANBASE_SQL_OB_CREATE_OUTLINE_EXECUTOR_H_

#include "lib/container/ob_vector.h"
#include "sql/parser/parse_node.h"
#include "sql/resolver/ob_stmt_type.h"
namespace oceanbase
{
namespace common
{
class ObString;
}
namespace share
{
namespace schema
{
class ObOutlineInfo;
}
}
namespace sql
{
class ObExecContext;
class ObCreateOutlineStmt;
class ObAlterOutlineStmt;
class ObDropOutlineStmt;
class ObLogPlan;
class ObDMLStmt;
class ObOptimizerContext;

class ObOutlineExecutor
{
public:
  ObOutlineExecutor() {}
  virtual ~ObOutlineExecutor() {}
protected:
  int get_outline(ObExecContext &ctx, ObDMLStmt *outline_stmt, common::ObString &outline);
  int generate_outline_info(ObExecContext &ctx, ObCreateOutlineStmt *outline_stmt,
                            share::schema::ObOutlineInfo &outline_info);
  int generate_outline_info1(ObExecContext &ctx, ObDMLStmt *outline_stmt,
                            share::schema::ObOutlineInfo &outline_info);
  int generate_outline_info2(ObExecContext &ctx, ObCreateOutlineStmt *outline_stmt,
                            share::schema::ObOutlineInfo &outline_info);
  int generate_logical_plan(ObExecContext &ctx,
                            ObOptimizerContext &opt_ctx,
                            ObDMLStmt *outline_stmt,
                            ObLogPlan *&logical_plan);
  bool is_valid_outline_stmt_type(stmt::StmtType type);
  int print_outline(ObExecContext &ctx, ObLogPlan *log_plan, common::ObString &outline);
private:
  DISALLOW_COPY_AND_ASSIGN(ObOutlineExecutor);
};

class ObCreateOutlineExecutor : public ObOutlineExecutor
{
public:
  ObCreateOutlineExecutor() {}
  virtual ~ObCreateOutlineExecutor() {}
  int execute(ObExecContext &ctx, ObCreateOutlineStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreateOutlineExecutor);
};

class ObAlterOutlineExecutor : public ObOutlineExecutor
{
public:
  ObAlterOutlineExecutor() {}
  virtual ~ObAlterOutlineExecutor() {}
  int execute(ObExecContext &ctx, ObAlterOutlineStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObAlterOutlineExecutor);
};

class ObDropOutlineExecutor
{
public:
  ObDropOutlineExecutor() {}
  virtual ~ObDropOutlineExecutor() {}
  int execute(ObExecContext &ctx, ObDropOutlineStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObDropOutlineExecutor);
};

}//namespace sql
}//namespace oceanbase
#endif //OCEANBASE_SQL_OB_CREATE_OUTLINE_EXECUTOR_H_
