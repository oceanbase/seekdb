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

 #ifndef OCEANBASE_STORAGE_OB_GC_UPPER_TRANS_HELPER
 #define OCEANBASE_STORAGE_OB_GC_UPPER_TRANS_HELPER

#include "storage/ls/ob_ls.h"
#include "storage/tablet/ob_tablet.h"

namespace oceanbase
{
namespace storage
{

class ObGCUpperTransHelper
{
public:
static int try_get_sstable_upper_trans_version(
    ObLS &ls,
    const blocksstable::ObSSTable &sstable, 
    int64_t &new_upper_trans_version);

static int check_need_gc_or_update_upper_trans_version(
    ObLS &ls,
    const ObTablet &tablet,
    int64_t &multi_version_start,
    UpdateUpperTransParam &upper_trans_param,
    bool &need_update);
};

} // namespace storage
} // namespace oceanbase

 #endif
