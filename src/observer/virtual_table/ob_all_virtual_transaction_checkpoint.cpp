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

#include "observer/virtual_table/ob_all_virtual_transaction_checkpoint.h"
#include "share/rc/ob_module_provider.h"
#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"

using namespace oceanbase::common;
using namespace oceanbase::storage;
using namespace oceanbase::storage::checkpoint;
namespace oceanbase
{
namespace observer
{

ObAllVirtualTransCheckpointInfo::ObAllVirtualTransCheckpointInfo()
    : ObVirtualTableScannerIterator(),
      ls_(nullptr)
{
}

ObAllVirtualTransCheckpointInfo::~ObAllVirtualTransCheckpointInfo()
{
  reset();
}

void ObAllVirtualTransCheckpointInfo::reset()
{
  ls_ = nullptr;
  ob_common_checkpoint_iter_.reset();
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualTransCheckpointInfo::prepare_to_read_()
{
  int ret = OB_SUCCESS;
  ObArray<ObCommonCheckpointVTInfo> infos;
  ob_common_checkpoint_iter_.reset();
  ObLSService *ls_service = share::g_mp->ls_service();
  if (OB_ISNULL(ls_service)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "ls service is null", K(ret));
  } else if (OB_FAIL(ls_service->get_ls(ls_))) {
    SERVER_LOG(WARN, "get log stream failed", K(ret));
  } else if (FALSE_IT(infos.reset())) {
  } else if (OB_FAIL(ls_->get_common_checkpoint_info(infos))) {
    SERVER_LOG(WARN, "get commoncheckpoint info failed", K(ret), KPC(ls_));
  } else {
    int64_t idx = 0;
    for (; idx < infos.count() && OB_SUCC(ret); ++idx) {
      if (OB_FAIL(ob_common_checkpoint_iter_.push(infos.at(idx)))) {
        SERVER_LOG(ERROR, "ob_common_checkpoint_iter push failed", K(ret), KPC(ls_));
      }
    }
  }

  if (OB_SUCC(ret) && OB_FAIL(ob_common_checkpoint_iter_.set_ready())) {
    SERVER_LOG(WARN, "iterate common_checkpoint info begin error", K(ret));
  }

  if (OB_FAIL(ret)) {
    ob_common_checkpoint_iter_.reset();
  }

  return ret;
}

int ObAllVirtualTransCheckpointInfo::get_next_(ObCommonCheckpointVTInfo &common_checkpoint)
{
  int ret = OB_SUCCESS;
  if (!ob_common_checkpoint_iter_.is_ready() && OB_FAIL(prepare_to_read_())) {
    SERVER_LOG(WARN, "prepare data failed", K(ret));
  } else if (OB_FAIL(ob_common_checkpoint_iter_.get_next(common_checkpoint))) {
    if (OB_ITER_END != ret) {
      SERVER_LOG(WARN, "get next commoncheckpoint info error.", K(ret));
    }
  }
  return ret;
}

int ObAllVirtualTransCheckpointInfo::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  ObCommonCheckpointVTInfo common_checkpoint;
  if (NULL == allocator_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "allocator_ shouldn't be NULL", K(allocator_), K(ret));
  } else if (OB_FAIL(get_next_(common_checkpoint))) {
    if (OB_ITER_END != ret) {
      SERVER_LOG(WARN, "get_next_common_checkpoint failed", K(ret));
    }
  } else {
    const int64_t col_count = output_column_ids_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case OB_APP_MIN_COLUMN_ID: {
          cur_row_.cells_[i].set_int(common_checkpoint.tablet_id.id());
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 1: {
          cur_row_.cells_[i].set_uint64((common_checkpoint.rec_scn.is_valid() ? common_checkpoint.rec_scn.get_val_for_inner_table_field() : 0));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 2:
          if (OB_FAIL(common_checkpoint_type_to_string(ObCommonCheckpointType(common_checkpoint.checkpoint_type),
                                                       checkpoint_type_buf_,
                                                       sizeof(checkpoint_type_buf_)))) {
            SERVER_LOG(WARN, "get common_checkpoint type buf failed", K(ret), K(common_checkpoint));
          } else {
            checkpoint_type_buf_[MAX_CHECKPOINT_TYPE_BUF_LENGTH - 1] = '\0';
            cur_row_.cells_[i].set_varchar(checkpoint_type_buf_);
            cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          }
          break;
        case OB_APP_MIN_COLUMN_ID + 3:
          cur_row_.cells_[i].set_int(common_checkpoint.is_flushing ? 1 : 0);
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid col_id", K(ret), K(col_id));
          break;
      }
    }
  }
  if (OB_SUCC(ret)) {
    row = &cur_row_;
  }

  return ret;
}

} // observer
} // oceanbase
