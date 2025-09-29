/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX SHARE

#include "ob_tablet_split_util.h"
#include "storage/ob_partition_range_spliter.h"
#include "storage/tablet/ob_mds_scan_param_helper.h"
#include "storage/tablet/ob_tablet_mds_table_mini_merger.h"
#include "storage/tablet/ob_tablet_medium_info_reader.h"
#include "storage/truncate_info/ob_tablet_truncate_info_reader.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "share/compaction/ob_shared_storage_compaction_util.h"

#ifdef OB_BUILD_SHARED_STORAGE
#include "close_modules/shared_storage/meta_store/ob_shared_storage_obj_meta.h"
#include "storage/meta_store/ob_tenant_storage_meta_service.h"
#include "close_modules/shared_storage/storage/incremental/ob_ss_minor_compaction.h"
#include "storage/incremental/ob_shared_meta_service.h"
#include "storage/compaction_v2/ob_ss_compact_helper.h"
#endif

using namespace oceanbase::common;
using namespace oceanbase::obrpc;
using namespace oceanbase::share;
using namespace oceanbase::storage;
using namespace oceanbase::compaction;

bool ObTabletSplitRegisterMdsArg::is_valid() const
{
  /*      exec_tenant_id_(common::OB_INVALID_TENANT_ID),*/
  bool is_valid = !split_info_array_.empty() && OB_INVALID_TENANT_ID != tenant_id_
      && parallelism_ > 0 && ls_id_.is_valid() && is_tablet_split(task_type_) && table_schema_ != nullptr;
  for (int64_t i = 0; is_valid && i < lob_schema_versions_.count(); ++i) {
    is_valid = is_valid && lob_schema_versions_.at(i);
  }
  for (int64_t i = 0; is_valid && i < split_info_array_.count(); i++) {
    is_valid = is_valid && split_info_array_.at(i).is_valid();
  }
  return is_valid;
}

int ObTabletSplitRegisterMdsArg::assign(const ObTabletSplitRegisterMdsArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!other.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(other));
  } else if (OB_FAIL(split_info_array_.assign(other.split_info_array_))) {
    LOG_WARN("assign tablet split info failed", K(ret));
  } else if (OB_FAIL(lob_schema_versions_.assign(other.lob_schema_versions_))) {
    LOG_WARN("assign tablet lob_schema_versions_ failed", K(ret));
  } else {
    parallelism_ = other.parallelism_;
    is_no_logging_ = other.is_no_logging_;
    tenant_id_ = other.tenant_id_;
    src_local_index_tablet_count_ = other.src_local_index_tablet_count_;
    ls_id_ = other.ls_id_;
    task_type_ = other.task_type_;
    table_schema_ = other.table_schema_;
  }
  return ret;
}