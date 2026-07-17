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

#ifndef OB_ALL_VIRTUAL_TRANSACTION_FREEZE_CHECKPOINT_H_
#define OB_ALL_VIRTUAL_TRANSACTION_FREEZE_CHECKPOINT_H_

#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"

namespace oceanbase
{
namespace storage
{
class ObLS;
}
namespace observer
{
static constexpr const char OB_FREEZE_CHECKPOINT[] = "ob_freeze_checkpoint";
typedef common::ObSimpleIterator<checkpoint::ObFreezeCheckpointVTInfo,
  OB_FREEZE_CHECKPOINT, 20> ObFreezeCheckpointVTIterator;


class ObAllVirtualFreezeCheckpointInfo : public common::ObVirtualTableScannerIterator
{
 public:
  explicit ObAllVirtualFreezeCheckpointInfo();
  virtual ~ObAllVirtualFreezeCheckpointInfo();
 public:
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
 private:
  int prepare_to_read_();
  int get_next_(storage::checkpoint::ObFreezeCheckpointVTInfo &freeze_checkpoint);
 private:
  char freeze_checkpoint_location_buf_[common::MAX_FREEZE_CHECKPOINT_LOCATION_BUF_LENGTH];

  storage::ObLS *ls_;
  ObFreezeCheckpointVTIterator ob_freeze_checkpoint_iter_;
  
 private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualFreezeCheckpointInfo);
};
} // observer
} // oceanbase
#endif
