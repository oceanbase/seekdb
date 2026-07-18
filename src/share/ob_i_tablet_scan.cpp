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

#define USING_LOG_PREFIX SHARE
#include "share/ob_i_tablet_scan.h"

namespace oceanbase
{
namespace common
{

OB_SERIALIZE_MEMBER(ObLimitParam, offset_, limit_);
OB_SERIALIZE_MEMBER(SampleInfo, table_id_, method_, scope_, percent_, seed_, force_block_);
OB_SERIALIZE_MEMBER(ObEstRowCountRecord, table_id_, table_type_, version_range_, logical_row_count_, physical_row_count_);
OB_SERIALIZE_MEMBER(ObTableScanOption, io_read_batch_size_, io_read_gap_size_, storage_rowsets_size_);


// ObVTableScanParam::to_string moved definition to sql/engine/table/ob_table_scan_op.cpp(printing ObFixedArray<sql::ObExpr*> needs ObExpr complete type)
// Note: master removed external_file_format_/external_file_location_ printing of two fields, must be synced in HOST(ob_table_scan_op.cpp) (see routing item)
}
}
