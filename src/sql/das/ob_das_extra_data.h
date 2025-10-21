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

#ifndef OBDEV_SRC_SQL_DAS_OB_DAS_EXTRA_DATA_H_
#define OBDEV_SRC_SQL_DAS_OB_DAS_EXTRA_DATA_H_
#include "sql/das/ob_das_define.h"
#include "sql/das/ob_das_task.h"
#include "rpc/obrpc/ob_rpc_proxy.h"
namespace oceanbase
{
namespace sql
{
class ObDASExtraData
{
public:
  ObDASExtraData();
  ~ObDASExtraData() {}
  int init(const int64_t task_id,
           const int64_t timeout_ts,
           const common::ObAddr &result_addr,
           rpc::frame::ObReqTransport *transport,
           const bool enable_rich_format);
  void set_output_info(const ExprFixedArray *output_exprs, ObEvalCtx *eval_ctx)
  {
    output_exprs_ = output_exprs;
    eval_ctx_ = eval_ctx;
  }
  int get_next_row();
  int get_next_rows(int64_t &count, int64_t capacity);
  void set_has_more(const bool has_more) { has_more_ = has_more; }
  void set_need_check_output_datum(bool v) { need_check_output_datum_ = v; }
  void set_tsc_monitor_info(ObTSCMonitorInfo *tsc_monitor_info) { tsc_monitor_info_ = tsc_monitor_info; }
  TO_STRING_KV(KPC_(output_exprs));
private:
  int fetch_result();
private:
  const ExprFixedArray *output_exprs_;
  ObEvalCtx *eval_ctx_;
  int64_t task_id_;
  int64_t timeout_ts_;
  common::ObAddr result_addr_;
  ObDASDataFetchRes result_;
  ObChunkDatumStore::Iterator result_iter_;
  ObTempRowStore::Iterator vec_result_iter_;
  bool has_more_;
  bool need_check_output_datum_;
  bool enable_rich_format_;
  ObTSCMonitorInfo *tsc_monitor_info_;
};
}  // namespace sql
}  // namespace oceanbase
#endif /* OBDEV_SRC_SQL_DAS_OB_DAS_EXTRA_DATA_H_ */
