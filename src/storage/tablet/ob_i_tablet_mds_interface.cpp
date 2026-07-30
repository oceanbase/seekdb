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


#include "storage/tablet/ob_i_tablet_mds_interface.h"
#include "share/rc/ob_module_provider.h"
#include "storage/meta_mem/ob_storage_meta_mem_mgr.h"
#include "storage/tablet/ob_mds_scan_param_helper.h"
#include "storage/tablet/ob_mds_schema_helper.h"

namespace oceanbase
{
namespace storage
{
int ObITabletMdsInterface::get_tablet_status(
    const share::SCN &snapshot,
    ObTabletCreateDeleteMdsUserData &data,
    const int64_t timeout) const
{
  #define PRINT_WRAPPER KR(ret), K(snapshot), K(data), K(timeout), K(*this)
  MDS_TG(10_ms);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!check_is_inited_())) {
    ret = OB_NOT_INIT;
    MDS_LOG_GET(WARN, "not inited");
  } else if (OB_UNLIKELY(!snapshot.is_max())) {
    ret = OB_NOT_SUPPORTED;
    MDS_LOG_GET(WARN, "only support read latest data currently");
  } else if (CLICK_FAIL((get_snapshot<mds::DummyKey, ObTabletCreateDeleteMdsUserData>(
      mds::DummyKey(),
      ReadTabletStatusOp(data),
      snapshot,
      timeout)))) {
    if (OB_EMPTY_RESULT != ret) {
      MDS_LOG(WARN, "fail to get snapshot", K(ret));
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

int ObITabletMdsInterface::get_latest_tablet_status(
    ObTabletCreateDeleteMdsUserData &data,
    mds::MdsWriter &writer,
    mds::TwoPhaseCommitState &trans_stat,
    share::SCN &trans_version,
    const int64_t read_seq) const
{
  #define PRINT_WRAPPER KR(ret), K(data)
  MDS_TG(10_ms);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!check_is_inited_())) {
    ret = OB_NOT_INIT;
    MDS_LOG_GET(WARN, "not inited");
  } else if (CLICK_FAIL((get_latest<ObTabletCreateDeleteMdsUserData>(
      ReadTabletStatusOp(data),
      writer,
      trans_stat,
      trans_version,
      read_seq)))) {
    if (OB_EMPTY_RESULT != ret) {
      MDS_LOG(WARN, "fail to get latest", K(ret));
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

int ObITabletMdsInterface::get_latest_ddl_data(
    ObTabletBindingMdsUserData &data,
    mds::MdsWriter &writer,
    mds::TwoPhaseCommitState &trans_stat,
    share::SCN &trans_version,
    const int64_t read_seq) const
{
  #define PRINT_WRAPPER KR(ret), K(data)
  MDS_TG(10_ms);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!check_is_inited_())) {
    ret = OB_NOT_INIT;
    MDS_LOG_GET(WARN, "not inited");
  } else {
    if (CLICK_FAIL((get_latest<ObTabletBindingMdsUserData>(
        ReadBindingInfoOp(data),
        writer,
        trans_stat,
        trans_version,
        read_seq)))) {
      if (OB_EMPTY_RESULT != ret) {
        MDS_LOG_GET(WARN, "fail to get latest", K(lbt()));
      }
    } else if (!data.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      MDS_LOG_GET(WARN, "invalid user data", K(lbt()));
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

int ObITabletMdsInterface::get_ddl_data(
    const share::SCN &snapshot,
    ObTabletBindingMdsUserData &data,
    const int64_t timeout) const
{
  #define PRINT_WRAPPER KR(ret), K(data), K(snapshot), K(timeout)
  MDS_TG(10_ms);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!check_is_inited_())) {
    ret = OB_NOT_INIT;
    MDS_LOG_GET(WARN, "not inited");
  } else if (OB_UNLIKELY(!snapshot.is_max())) {
    ret = OB_NOT_SUPPORTED;
    MDS_LOG_GET(WARN, "only support read latest data currently");
  } else {
    if (CLICK_FAIL((get_snapshot<mds::DummyKey, ObTabletBindingMdsUserData>(mds::DummyKey(),
        ReadBindingInfoOp(data), snapshot, timeout)))) {
      if (OB_EMPTY_RESULT != ret) {
        MDS_LOG_GET(WARN, "fail to get snapshot", K(lbt()));
      } else {
        data.set_default_value(); // use default value
        ret = OB_SUCCESS;
      }
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

int ObITabletMdsInterface::get_autoinc_seq(
    ObIAllocator &allocator,
    const share::SCN &snapshot,
    share::ObTabletAutoincSeq &data,
    const int64_t timeout) const
{
  #define PRINT_WRAPPER KR(ret), K(data), K(snapshot), K(timeout)
  MDS_TG(10_ms);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!check_is_inited_())) {
    ret = OB_NOT_INIT;
    MDS_LOG_GET(WARN, "not inited");
  } else if (OB_UNLIKELY(!snapshot.is_max())) {
    ret = OB_NOT_SUPPORTED;
    MDS_LOG_GET(WARN, "only support read latest data currently");
  } else {
    if (CLICK_FAIL((get_snapshot<mds::DummyKey, share::ObTabletAutoincSeq>(mds::DummyKey(),
        ReadAutoIncSeqOp(allocator, data), snapshot, timeout)))) {
      if (OB_EMPTY_RESULT != ret) {
        MDS_LOG_GET(WARN, "fail to get snapshot", K(lbt()));
      } else {
        data.reset(); // use default value
        ret = OB_SUCCESS;
      }
    }
  }

  return ret;
  #undef PRINT_WRAPPER
}

int ObITabletMdsInterface::read_raw_data(
    common::ObIAllocator &allocator,
    const uint8_t mds_unit_id,
    const common::ObString &udf_key,
    const share::SCN &snapshot,
    const int64_t timeout_us,
    mds::MdsDumpKV &kv) const
{
  int ret = OB_SUCCESS;
  const common::ObTabletID &tablet_id = get_tablet_meta_().tablet_id_;
  const int64_t abs_timeout = timeout_us + ObClockGenerator::getClock();
  ObMdsReadInfoCollector placeholder_collector;
  SMART_VARS_3((ObTableScanParam, scan_param), (ObStoreCtx, store_ctx), (ObMdsRowIterator, iter)) {
    if (OB_FAIL(ObMdsScanParamHelper::build_scan_param(
        allocator,
        tablet_id,
        ObMdsSchemaHelper::MDS_TABLE_ID,
        mds_unit_id,
        udf_key,
        true/*is_get*/,
        abs_timeout,
        ObVersionRange(0/*base_version*/, snapshot.get_val_for_tx()/*snapshot_version*/),
        placeholder_collector,
        scan_param))) {
      MDS_LOG(WARN, "fail to build scan param", K(ret));
    } else if (OB_FAIL(mds_table_scan(scan_param, store_ctx, iter))) {
      MDS_LOG(WARN, "fail to do mds table scan", K(ret), K(snapshot), K(scan_param));
    } else {
      int tmp_ret = OB_SUCCESS;
      if (OB_FAIL(iter.get_next_mds_kv(allocator, kv))) {
        if (OB_ITER_END != ret) {
          MDS_LOG(WARN, "fail to get next row", K(ret));
        }
      } else if (OB_UNLIKELY(OB_ITER_END != (tmp_ret = iter.get_next_row()))) {
        ret = OB_ERR_UNEXPECTED;
        MDS_LOG(WARN, "iter should reach the end", K(ret), K(tmp_ret), K(iter));
      }
    }

    if (OB_FAIL(ret)) {
      kv.reset();
    }
  }

  return ret;
}

int ObITabletMdsInterface::mds_table_scan(
    ObTableScanParam &scan_param,
    ObStoreCtx &store_ctx,
    ObMdsRowIterator &iter) const
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;

  if (OB_FAIL(get_tablet_handle_from_this(tablet_handle))) {
    MDS_LOG(WARN, "fail to build tablet handle", K(ret));
  } else if (OB_FAIL(iter.init(scan_param, tablet_handle, store_ctx))) {
    MDS_LOG(WARN, "fail to init mds row iter", K(ret), KPC(tablet_handle.get_obj()), K(scan_param));
  }

  return ret;
}

int ObITabletMdsInterface::get_tablet_handle_from_this(
  ObTabletHandle &tablet_handle) const
{
  int ret = OB_SUCCESS;
  const ObTablet *tablet = static_cast<const ObTablet*>(this);
  const common::ObTabletID &tablet_id = get_tablet_meta_().tablet_id_;
  ObStorageMetaMemMgr *t3m = share::g_mp->storage_meta_mem_mgr();
  if (OB_FAIL(t3m->build_tablet_handle_for_mds_scan(const_cast<ObTablet*>(tablet), tablet_handle))) {
    MDS_LOG(WARN, "fail to build tablet handle", K(ret), K(tablet_id));
  } 
  return ret;
}

} // namespace storage
} // namespace oceanbase
