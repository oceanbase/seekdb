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

#ifndef OCEANBASE_UNITTEST_PL_OB_MOCK_PL_STMT_H_
#define OCEANBASE_UNITTEST_PL_OB_MOCK_PL_STMT_H_

#include "../../src/pl/ob_pl_code_generator.h"

using namespace oceanbase;

namespace test
{
class TestPLStmtMockService
{
public:
  TestPLStmtMockService(common::ObIAllocator &allocator) :
    allocator_(allocator),
    expr_factory_(allocator_),
    stmt_factory_(allocator_),
    cur_loc_() {}
  virtual ~TestPLStmtMockService() {}

public:
  pl::ObPLStmt *make_stmt(pl::ObPLStmtType type, pl::ObPLStmtBlock *block);
  pl::ObPLStmtBlock *make_block(pl::ObPLBlockNS *pre_ns,
                                pl::ObPLSymbolTable *symbol_table,
                                pl::ObPLLabelTable *label_table,
                                pl::ObPLConditionTable *condition_table,
                                pl::ObPLCursorTable *cursor_table,
                                common::ObIArray<ObRawExpr*> *exprs,
                                pl::ObPLExternalNS *external_ns);

public:
  common::ObIAllocator &allocator_;
  sql::ObRawExprFactory expr_factory_;
  pl::ObPLStmtFactory stmt_factory_;
  pl::SourceLocation cur_loc_;
};

}



#endif /* OCEANBASE_UNITTEST_PL_OB_MOCK_PL_STMT_H_ */
