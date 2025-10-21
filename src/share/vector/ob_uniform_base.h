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

#ifndef OCEANBASE_SHARE_VECTOR_OB_UNIFORM_BASE_H_
#define OCEANBASE_SHARE_VECTOR_OB_UNIFORM_BASE_H_

#include "share/vector/ob_vector_base.h"

namespace oceanbase
{
namespace sql
{
  struct ObEvalInfo;
}

namespace common
{

class ObUniformBase : public ObVectorBase
{
public:
  ObUniformBase(ObDatum *datums, sql::ObEvalInfo *eval_info)
    : ObVectorBase(), datums_(datums), eval_info_(eval_info)
  {}

  DECLARE_TO_STRING;

  inline ObDatum *get_datums() { return datums_; }
  inline const ObDatum *get_datums() const { return datums_; }
  OB_INLINE void set_datums(ObDatum *datums) { datums_ = datums; }
  inline sql::ObEvalInfo *get_eval_info() { return eval_info_; }
  inline const sql::ObEvalInfo *get_eval_info() const { return eval_info_; }
  virtual int to_rows(const sql::RowMeta &row_meta,
                       sql::ObCompactRow **stored_rows,
                       const uint16_t selector[],
                       const int64_t size,
                       const int64_t col_idx) const override final;

  virtual int to_rows(const sql::RowMeta &row_meta, sql::ObCompactRow **stored_rows,
                       const int64_t size, const int64_t col_idx) const override final;

protected:
  ObDatum *datums_;
  sql::ObEvalInfo *eval_info_; // just used for maintain has_null flag in uniform vector
};

}
}
#endif // OCEANBASE_SHARE_VECTOR_OB_UNIFORM_Base_H_
