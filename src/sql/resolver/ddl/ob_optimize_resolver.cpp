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

#define USING_LOG_PREFIX SQL_RESV
#include "ob_optimize_resolver.h"

using namespace oceanbase::common;
using namespace oceanbase::common::hash;
using namespace oceanbase::sql;

int ObOptimizeTableResolver::resolve(const peg::Node &parser_tree)
{
  int ret = OB_SUCCESS;
  ObOptimizeTableStmt *stmt = nullptr;
  if (OB_ISNULL(session_info_) || peg::NodeKind::OptimizeTable != parser_tree.kind()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (OB_ISNULL(stmt = create_stmt<ObOptimizeTableStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      stmt_ = stmt;
    }
  }
  if (OB_SUCC(ret)) {
    
    obcall::ObOptimizeTableArg &arg = stmt->get_optimize_table_arg();
    
    
    void *buf = nullptr;
    ObPlacementHashSet<obcall::ObTableItem> *table_item_set = nullptr;
    if (OB_ISNULL(buf = allocator_->alloc(sizeof(ObPlacementHashSet<obcall::ObTableItem>)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      table_item_set = new(buf)ObPlacementHashSet<obcall::ObTableItem>();
      ObString database_name;
      ObString table_name;
      obcall::ObTableItem table_item;
      for (const auto &relation : peg::optimize_relations(parser_tree)) {
        if (OB_FAIL(ret)) { break; }
        database_name.reset();
        table_name.reset();
        if (OB_FAIL(resolve_table_relation_node(&relation, table_name, database_name))) {
        } else {
          table_item.reset();
          if (OB_FAIL(session_info_->get_name_case_mode(table_item.mode_))) {
          } else {
            table_item.database_name_ = database_name;
            table_item.table_name_ = table_name;
            if (OB_HASH_EXIST == table_item_set->exist_refactored(table_item)) {
              ret = OB_ERR_NONUNIQ_TABLE;
              LOG_USER_ERROR(OB_ERR_NONUNIQ_TABLE, table_item.table_name_.length(), table_item.table_name_.ptr());
            } else if (OB_FAIL(table_item_set->set_refactored(table_item))) {
            } else if (OB_FAIL(stmt->add_table_item(table_item))) {
            }
          }
        }

      }
    }
  }
  return ret;
}
