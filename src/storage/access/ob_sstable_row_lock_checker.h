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

#ifndef OB_SSTABLE_ROW_LOCK_CHECKER_H_
#define OB_SSTABLE_ROW_LOCK_CHECKER_H_

#include "ob_sstable_row_getter.h"
#include "storage/access/ob_sstable_row_multi_scanner.h"
#include "storage/access/ob_sstable_row_scanner.h"
#include "storage/blocksstable/ob_micro_block_row_lock_checker.h"

namespace oceanbase {
using namespace transaction;

namespace storage {

class ObSSTableRowLockChecker : public ObSSTableRowScanner<ObIndexTreeMultiPassPrefetcher<2, 2>>
{
public:
  ObSSTableRowLockChecker();
  virtual ~ObSSTableRowLockChecker();
  virtual void reset() override;
  int check_row_locked(
    const bool check_exist,
    const share::SCN &snapshot_version,
    ObStoreRowLockState &lock_state);
  inline void set_iter_type(bool check_exist)
  {
    if (check_exist) {
      type_ = ObStoreRowIterator::IteratorRowLockAndDuplicationCheck;
    } else {
      type_ = ObStoreRowIterator::IteratorRowLockCheck;
    }
  }
protected:
  virtual int inner_open(
      const ObTableIterParam &param,
      ObTableAccessContext &context,
      ObITable *table,
      const void *query_range) override;
private:
  int init_micro_scanner();
private:
  const blocksstable::ObDatumRowkey *base_rowkey_;
  blocksstable::ObDatumRange multi_version_range_;
};

class ObSSTableRowLockMultiChecker : public ObSSTableRowScanner<ObIndexTreeMultiPassPrefetcher<32, 8>>
{
public:
  ObSSTableRowLockMultiChecker();
  virtual ~ObSSTableRowLockMultiChecker();
  virtual int init(
      const ObTableIterParam &iter_param,
      ObTableAccessContext &access_ctx,
      ObITable *table,
      const void *query_range) override;
  int check_row_locked(
      const bool check_exist,
      const share::SCN &snapshot_version);
protected:
   int fetch_row(ObSSTableReadHandle &read_handle);
private:
  int init_micro_scanner();
  int open_cur_data_block(ObSSTableReadHandle &read_handle);
};

}
}
#endif /* OB_SSTABLE_ROW_LOCK_CHECKER_H_ */
