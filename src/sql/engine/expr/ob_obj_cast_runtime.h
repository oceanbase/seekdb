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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_OBJ_CAST_RUNTIME_H_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_OBJ_CAST_RUNTIME_H_

#include "share/object/ob_obj_cast_runtime.h"

namespace oceanbase
{
namespace sql
{

class ObExecContext;
struct ObUserLoggingCtx;

class ObSqlObjCastRuntime final : public common::ObIObjCastRuntime
{
public:
  explicit ObSqlObjCastRuntime(ObExecContext *exec_ctx);
  explicit ObSqlObjCastRuntime(const ObUserLoggingCtx *user_logging_ctx);
  ~ObSqlObjCastRuntime() override = default;

  int get_enum_set_values(
      uint16_t subschema_id,
      const common::ObIArray<common::ObString> *&values,
      common::ObCollationType &collation_type) const override;

  int cast_collection(
      common::ObObjCastParams &params,
      const common::ObObj &input,
      common::ObObj &output,
      uint64_t cast_mode) const override;

  void report_warning(
      int64_t code,
      const common::ObString &type_name,
      const common::ObString &input,
      uint64_t cast_mode) const override;

private:
  ObExecContext *exec_ctx_;
  const ObUserLoggingCtx *user_logging_ctx_;

  DISALLOW_COPY_AND_ASSIGN(ObSqlObjCastRuntime);
};

}  // namespace sql
}  // namespace oceanbase

#endif  // OCEANBASE_SQL_ENGINE_EXPR_OB_OBJ_CAST_RUNTIME_H_
