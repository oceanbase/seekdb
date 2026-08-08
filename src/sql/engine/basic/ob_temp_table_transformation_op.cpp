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

#define USING_LOG_PREFIX SQL_ENG

#include "ob_temp_table_transformation_op.h"
#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{
using namespace common;
using namespace storage;
using namespace share;
using namespace share::schema;
namespace sql
{
DEF_TO_STRING(ObTempTableTransformationOpSpec)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_NAME("op_spec");
  J_COLON();
  pos += ObOpSpec::to_string(buf + pos, buf_len - pos);
  J_COMMA();
  J_OBJ_END();
  return pos;
}

OB_SERIALIZE_MEMBER_INHERIT(ObTempTableTransformationOpSpec, ObOpSpec);

int ObTempTableTransformationOp::inner_rescan()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOperator::inner_rescan())) {
  } else { /*do nothing.*/ }
  return ret;
}

int ObTempTableTransformationOp::inner_open()
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObTempTableTransformationOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  clear_evaluated_flag();
  ObExecContext &ctx = get_exec_ctx();
  if (init_temp_table_) {
    for (int64_t i = 0; OB_SUCC(ret) && i < get_child_cnt() - 1; ++i) {
      int64_t temp_table_count = ctx.get_temp_table_ctx().count();
      if (OB_ISNULL(children_[i])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("child op is null");
      } else if (OB_FALSE_IT(ret = children_[i]->get_next_row())) {
      } else if (ret != OB_ITER_END) {
        LOG_WARN("failed to get next row.", K(ret));
      } else {
        ret = OB_SUCCESS;
        while(OB_SUCC(ret) && ctx.get_temp_table_ctx().count() <= temp_table_count) {
          if (OB_FAIL(check_status())) {
          } else {
            ob_usleep(1000);
          }
        }
      }
    }
    init_temp_table_ = false;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(children_[get_child_cnt() - 1])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("child op is null");
  } else if (OB_FAIL(children_[get_child_cnt() - 1]->get_next_row())) {
    if (ret != OB_ITER_END) {
      LOG_WARN("failed to get next row.", K(ret));
    } else { /*do nothing.*/ }
  } else { /*do nothing.*/ }
  return ret;
}

int ObTempTableTransformationOp::inner_get_next_batch(const int64_t max_row_cnt)
{
  int ret = OB_SUCCESS;
  const ObBatchRows *child_brs = nullptr;
  clear_evaluated_flag();
  clear_datum_eval_flag();
  if (init_temp_table_) {
    for (int64_t i = 0; OB_SUCC(ret) && i < get_child_cnt() - 1; ++i) {
      if (OB_ISNULL(children_[i])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("child op is null");
      } else if (OB_FAIL(children_[i]->get_next_batch(max_row_cnt, child_brs))) {
      }
    }
    init_temp_table_ = false;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(children_[get_child_cnt() - 1])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("child op is null");
  } else if (OB_FAIL(children_[get_child_cnt() - 1]->get_next_batch(
                 max_row_cnt, child_brs))) {
  } else { /*do nothing.*/
  }
  (void)brs_.copy(child_brs);
  return ret;
}

int ObTempTableTransformationOp::inner_close()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(destory_interm_results())) {
  }
  return ret;
}

int ObTempTableTransformationOp::destory_interm_results()
{
  int ret = OB_SUCCESS;
  ObExecContext &ctx = get_exec_ctx();
  const int64_t temp_table_count = ctx.get_temp_table_ctx().count();
  for (int64_t i = 0; OB_SUCC(ret) && i < temp_table_count; ++i) {
    ObSqlTempTableCtx &temp_table_ctx = ctx.get_temp_table_ctx().at(i);
    for (int64_t j = 0; OB_SUCC(ret) && j < temp_table_ctx.interm_result_infos_.count(); ++j) {
      ObTempTableResultInfo &result_info = temp_table_ctx.interm_result_infos_.at(j);
      if (OB_FAIL(destory_local_interm_results(result_info.interm_result_ids_))) {
      }
    }
  }
  return ret;
}


int ObTempTableTransformationOp::destory_local_interm_results(ObIArray<uint64_t> &result_ids)
{
  int ret = OB_SUCCESS;
  dtl::ObDTLIntermResultKey dtl_int_key;
  LOG_TRACE("destory interm results", K(get_exec_ctx().get_addr()), K(result_ids));
  for (int64_t i = 0; OB_SUCC(ret) && i < result_ids.count(); ++i) {
    dtl_int_key.channel_id_ = result_ids.at(i);
    if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::sql::dtl::ObDTLIntermResultManager>()->erase_interm_result_info(
                                                                            dtl_int_key))) {
      if (OB_HASH_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
        LOG_WARN("interm result may erased by DM", K(ret));
      } else {
        LOG_WARN("failed to erase interm result info in manager.", K(ret));
      }
    }
  }
  return ret;
}

void ObTempTableTransformationOp::destroy()
{
  ObOperator::destroy();
}

} // end namespace sql
} // end namespace oceanbase
