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

#ifndef OCEANBASE_STORAGE_MEMTABLE_OB_CONCURRENT_CONTROL
#define OCEANBASE_STORAGE_MEMTABLE_OB_CONCURRENT_CONTROL

#include "data_plane/memtable/ob_write_flag.h"
#include "storage/blocksstable/ob_datum_row.h"
#include "storage/tx/ob_trans_define.h"

namespace oceanbase
{
namespace concurrent_control
{

int check_sequence_set_violation(
    const concurrent_control::ObWriteFlag write_flag,
    const transaction::ObTxSEQ reader_seq_no,
    const transaction::ObTransID checker_tx_id,
    const blocksstable::ObDmlFlag checker_dml_flag,
    const transaction::ObTxSEQ checker_seq_no,
    const transaction::ObTransID locker_tx_id,
    const blocksstable::ObDmlFlag locker_dml_flag,
    const transaction::ObTxSEQ locker_seq_no);

} // namespace concurrent_control
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_MEMTABLE_OB_CONCURRENT_CONTROL
