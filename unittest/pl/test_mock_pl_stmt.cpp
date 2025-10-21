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

#include "test_mock_pl_stmt.h"

using namespace oceanbase::pl;

namespace test
{
ObPLStmt *TestPLStmtMockService::make_stmt(ObPLStmtType type, ObPLStmtBlock *block)
{
  ObPLStmt *stmt = stmt_factory_.allocate(type, block);
  ++cur_loc_.line_;
  stmt->set_location(cur_loc_);
  if (NULL != block) {
    block->add_stmt(stmt);
  }
  return stmt;
}

ObPLStmtBlock *TestPLStmtMockService::make_block(ObPLBlockNS *pre_ns,
                                                 ObPLSymbolTable *symbol_table,
                                                 ObPLLabelTable *label_table,
                                                 ObPLConditionTable *condition_table,
                                                 ObPLCursorTable *cursor_table,
                                                 common::ObIArray<ObRawExpr*> *exprs,
                                                 ObPLExternalNS *external_ns)
{
  ObPLStmtBlock *block = static_cast<ObPLStmtBlock*>(allocator_.alloc(sizeof(ObPLStmtBlock)));
  block = new(block)ObPLStmtBlock(allocator_, pre_ns, symbol_table, label_table, condition_table, cursor_table, NULL, exprs, external_ns);
  return block;
}

}
