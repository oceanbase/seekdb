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

#ifndef OCEANBASE_SRC_OBSERVER_VIRTUAL_TABLE_OB_SHOW_CREATE_PROCEDURE_H_
#define OCEANBASE_SRC_OBSERVER_VIRTUAL_TABLE_OB_SHOW_CREATE_PROCEDURE_H_

#include "lib/container/ob_se_array.h"
#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/schema/ob_priv_type.h"
#include "common/ob_range.h"

namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
}
namespace share
{
namespace schema
{
class ObRoutineInfo;
}
}
namespace observer
{
class ObShowCreateProcedure : public common::ObVirtualTableScannerIterator
{
public:
  ObShowCreateProcedure();
  virtual ~ObShowCreateProcedure();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();

  inline share::schema::ObSessionPrivInfo &get_session_priv()
  { return session_priv_; }

  inline common::ObIArray<uint64_t> &get_role_id_array()
  { return enable_role_id_array_; }

  int has_show_create_function_priv(const ObRoutineInfo &proc_info,
                                    bool &print_create_function_column_priv) const;
private:
  int calc_show_procedure_id(uint64_t &show_table_id);
  int fill_row_cells(uint64_t show_procedure_id,
                     const share::schema::ObRoutineInfo &proc_info);
private:
  DISALLOW_COPY_AND_ASSIGN(ObShowCreateProcedure);
  EnableRoleIdArray enable_role_id_array_;
  share::schema::ObSessionPrivInfo session_priv_;
};
}// observer
}// oceanbase

#endif /* OCEANBASE_SRC_OBSERVER_VIRTUAL_TABLE_OB_SHOW_CREATE_PROCEDURE_H_ */
