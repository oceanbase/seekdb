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

#ifndef _OB_SQ_OB_PX_EXCHANGE_H_
#define _OB_SQ_OB_PX_EXCHANGE_H_

#include "lib/ob_define.h"
#include "sql/engine/ob_operator.h"
namespace oceanbase
{
namespace sql
{

class ObPxExchangeOpInput : public ObOpInput
{
public:
  ObPxExchangeOpInput(ObExecContext &ctx, const ObOpSpec &spec)
  : ObOpInput(ctx, spec),
    task_id_(common::OB_INVALID_ID),
    sqc_id_(common::OB_INVALID_ID),
    dfo_id_(common::OB_INVALID_ID)
  {}
  virtual ~ObPxExchangeOpInput() = default;
  virtual void reset() override { /*@TODO fix reset member by xiaochu*/ }
  virtual void set_task_id(int64_t task_id) { task_id_ = task_id; }
  virtual void set_sqc_id(int64_t sqc_id) { sqc_id_ = sqc_id; }
  virtual void set_dfo_id(int64_t dfo_id) { dfo_id_ = dfo_id; }
  int64_t get_task_id() const { return task_id_; }
  int64_t get_sqc_id() const { return sqc_id_; }
  int64_t get_dfo_id() const { return dfo_id_; }
protected:
  int64_t task_id_; // Currently mainly used to find its own ch set from ch sets
  int64_t sqc_id_;
  int64_t dfo_id_;
};

}
}
#endif /* _OB_SQ_OB_PX_EXCHANGE_H_ */
//// end of header file
