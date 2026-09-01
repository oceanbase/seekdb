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
#include "share/rc/ob_server_runtime.h"
#include "observer/change_stream/ob_change_stream_mgr.h"
#include "observer/change_stream/ob_change_stream_fetcher.h"
#include "lib/oblog/ob_log_module.h"

using namespace oceanbase::common;
using namespace oceanbase::share;

namespace oceanbase
{
namespace observer
{

ObAllVirtualChangeStreamRefreshStat::ObAllVirtualChangeStreamRefreshStat()
  : ObVirtualTableScannerIterator(),
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
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualChangeStreamRefreshStat::inner_get_next_row(ObNewRow *&row)
{
  LOG_INFO("select from dba_ob_change_stream_refresh_stat");
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

    // Get Change Stream watermarks and Fetcher stats from in-memory state.
    ObChangeStreamMgr *cs_mgr = ::oceanbase::share::server_service<::oceanbase::share::ObChangeStreamMgr>();
    if (OB_NOT_NULL(cs_mgr) && cs_mgr->is_inited()) {
      refresh_scn_val = cs_mgr->get_refresh_scn();
      palf::LSN min_dep_lsn;
      const int min_dep_ret = cs_mgr->get_min_dep_lsn(min_dep_lsn);
      if (OB_SUCCESS == min_dep_ret) {
        min_dep_lsn_val = static_cast<int64_t>(min_dep_lsn.val_);
      } else if (OB_EAGAIN != min_dep_ret && OB_NOT_INIT != min_dep_ret) {
        SERVER_LOG(WARN, "fail to get in-memory change stream min_dep_lsn",
                   KR(min_dep_ret));
      }
      ObCSFetcher &fetcher = cs_mgr->get_fetcher();
      pending_tx_count = fetcher.get_current_processing_tx_count();
      fetch_tx = fetcher.get_current_processing_tx_id().get_id();
      fetch_lsn = fetcher.get_current_lsn().val_;
      fetch_scn = fetcher.get_current_scn().get_val_for_inner_table_field();
    }

    // Fill row data
    if (OB_SUCC(ret)) {
      for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
        uint64_t col_id = output_column_ids_.at(i);
        switch (col_id) {
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
