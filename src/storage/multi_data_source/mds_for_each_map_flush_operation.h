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

#ifndef STORAGE_MULTI_DATA_SOURCE_MDS_FOR_EACH_MAP_FLUSH_OPERATION_H
#define STORAGE_MULTI_DATA_SOURCE_MDS_FOR_EACH_MAP_FLUSH_OPERATION_H
#include "share/scn.h"
#include "common/ob_tablet_id.h"
#include "storage/checkpoint/ob_checkpoint_diagnose.h"
namespace oceanbase
{
namespace storage
{
namespace mds
{
class MdsTableBase;
struct FlushOp {
  FlushOp(share::SCN do_flush_limit_scn, int64_t &scan_mds_table_cnt, share::SCN max_consequent_callbacked_scn, int64_t trace_id = checkpoint::INVALID_TRACE_ID)
  : do_flush_limit_scn_(do_flush_limit_scn),
  scan_mds_table_cnt_(scan_mds_table_cnt),
  max_consequent_callbacked_scn_(max_consequent_callbacked_scn),
  is_dag_full_(false),
  trace_id_(trace_id) {}
  bool operator()(const ObTabletID &, MdsTableBase *&mds_table);
  bool dag_full() const { return is_dag_full_; }
  share::SCN do_flush_limit_scn_;
  int64_t &scan_mds_table_cnt_;
  share::SCN max_consequent_callbacked_scn_;
  bool is_dag_full_;
  int64_t trace_id_;
};

}
}
}
#endif
