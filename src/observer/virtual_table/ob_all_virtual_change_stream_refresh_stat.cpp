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

#define USING_LOG_PREFIX SERVER

#include "observer/virtual_table/ob_all_virtual_change_stream_refresh_stat.h"
#include "share/ob_global_stat_proxy.h"
#include "share/change_stream/ob_change_stream_mgr.h"
#include "share/change_stream/ob_change_stream_fetcher.h"
#include "share/rc/ob_tenant_base.h"
#include "lib/oblog/ob_log_module.h"

using namespace oceanbase::common;
using namespace oceanbase::share;

namespace oceanbase
{
namespace observer
{

ObAllVirtualChangeStreamRefreshStat::ObAllVirtualChangeStreamRefreshStat()
  : ObVirtualTableScannerIterator(),
    ObMultiTenantOperator(),
    row_produced_(false)
{
}

ObAllVirtualChangeStreamRefreshStat::~ObAllVirtualChangeStreamRefreshStat()
{
  reset();
}

void ObAllVirtualChangeStreamRefreshStat::reset()
{
  row_produced_ = false;
  ObMultiTenantOperator::reset();
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualChangeStreamRefreshStat::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(execute(row))) {
    SERVER_LOG(WARN, "execute fail", K(ret));
  }
  return ret;
}

bool ObAllVirtualChangeStreamRefreshStat::is_need_process(uint64_t tenant_id)
{
  if (!is_virtual_tenant_id(tenant_id) &&
      (is_sys_tenant(effective_tenant_id_) || tenant_id == effective_tenant_id_)) {
    return true;
  }
  return false;
}

void ObAllVirtualChangeStreamRefreshStat::release_last_tenant()
{
  row_produced_ = false;
}

int ObAllVirtualChangeStreamRefreshStat::process_curr_tenant(ObNewRow *&row)
{
  LOG_INFO("select from dba_ob_change_stream_refresh_stat", K(MTL_ID()));
  int ret = OB_SUCCESS;

  if (row_produced_) {
    ret = OB_ITER_END;
  } else if (OB_ISNULL(cur_row_.cells_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "cur row cell is NULL", K(ret));
  } else {
    const int64_t col_count = output_column_ids_.count();
    ObObj *cells = cur_row_.cells_;
    int64_t refresh_scn_val = 0;
    int64_t min_dep_lsn_val = 0;
    int64_t pending_tx_count = 0;
    int64_t fetch_tx = 0;
    int64_t fetch_lsn = 0;
    int64_t fetch_scn = 0;

    // Get refresh_scn from global_stat
    SCN refresh_scn;
    if (OB_FAIL(ObGlobalStatProxy::get_change_stream_refresh_scn(
            *GCTX.sql_proxy_, MTL_ID(), false, refresh_scn))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        // Change stream not initialized yet, use default value
        ret = OB_SUCCESS;
        refresh_scn_val = 0;
      } else {
        SERVER_LOG(WARN, "fail to get change_stream_refresh_scn", K(ret), K(MTL_ID()));
      }
    } else {
      refresh_scn_val = refresh_scn.get_val_for_inner_table_field();
    }

    // Get min_dep_lsn from global_stat
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ObGlobalStatProxy::get_change_stream_min_dep_lsn(
              *GCTX.sql_proxy_, MTL_ID(), false, min_dep_lsn_val))) {
        if (OB_ENTRY_NOT_EXIST == ret) {
          // Change stream not initialized yet, use default value
          ret = OB_SUCCESS;
          min_dep_lsn_val = 0;
        } else {
          SERVER_LOG(WARN, "fail to get change_stream_min_dep_lsn", K(ret), K(MTL_ID()));
        }
      }
    }

    // Get stats from ObCSFetcher
    if (OB_SUCC(ret)) {
      ObChangeStreamMgr *cs_mgr = MTL(ObChangeStreamMgr*);
      if (OB_NOT_NULL(cs_mgr) && cs_mgr->is_inited()) {
        ObCSFetcher &fetcher = cs_mgr->get_fetcher();
        pending_tx_count = fetcher.get_current_processing_tx_count();
        fetch_tx = fetcher.get_current_processing_tx_id().get_id();
        fetch_lsn = fetcher.get_current_lsn().val_;
        fetch_scn = fetcher.get_current_scn().get_val_for_inner_table_field();
      }
    }

    // Fill row data
    if (OB_SUCC(ret)) {
      for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
        uint64_t col_id = output_column_ids_.at(i);
        switch (col_id) {
          case TENANT_ID: {
            cells[i].set_int(MTL_ID());
            break;
          }
          case CHANGE_STREAM_REFRESH_SCN: {
            cells[i].set_int(refresh_scn_val);
            break;
          }
          case CHANGE_STREAM_MIN_DEP_LSN: {
            cells[i].set_int(min_dep_lsn_val);
            break;
          }
          case CHANGE_STREAM_PENDING_TX_COUNT: {
            cells[i].set_int(pending_tx_count);
            break;
          }
          case CHANGE_STREAM_FETCH_TX: {
            cells[i].set_int(fetch_tx);
            break;
          }
          case CHANGE_STREAM_FETCH_LSN: {
            cells[i].set_int(fetch_lsn);
            break;
          }
          case CHANGE_STREAM_FETCH_SCN: {
            cells[i].set_int(fetch_scn);
            break;
          }
          default: {
            ret = OB_ERR_UNEXPECTED;
            SERVER_LOG(WARN, "unexpected column id", K(ret), K(col_id));
            break;
          }
        }
      }
    }

    if (OB_SUCC(ret)) {
      row = &cur_row_;
      row_produced_ = true;
    }
  }

  return ret;
}

} // end namespace observer
} // end namespace oceanbase
