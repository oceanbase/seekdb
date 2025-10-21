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

#include "mds_for_each_map_flush_operation.h"
#include "mds_table_base.h"

namespace oceanbase
{
namespace storage
{
namespace mds
{

bool FlushOp::operator()(const ObTabletID &, MdsTableBase *&mds_table)
{
  int ret = OB_SUCCESS;
  if (mds_table->is_switched_to_empty_shell()) {
    MDS_LOG(INFO, "skip empty shell tablet mds_table flush",
            KPC(mds_table), K(scan_mds_table_cnt_), K_(max_consequent_callbacked_scn));
  } else if (checkpoint::INVALID_TRACE_ID != trace_id_ && FALSE_IT(mds_table->set_trace_id(trace_id_))) {
  } else if (OB_FAIL(mds_table->flush(do_flush_limit_scn_, max_consequent_callbacked_scn_))) {
    MDS_LOG(WARN, "flush mds table failed",
            KR(ret), KPC(mds_table), K_(scan_mds_table_cnt), K_(max_consequent_callbacked_scn));
    if (OB_SIZE_OVERFLOW == ret) {
      is_dag_full_ = true;
    }
  } else {
    ++scan_mds_table_cnt_;
  }
  return !is_dag_full_;// true means iterating the next mds table
}

}
}
}
