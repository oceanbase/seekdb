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

#include "storage/multi_data_source/ob_mds_table_merge_dag_param.h"

namespace oceanbase
{
namespace storage
{
namespace mds
{
ObMdsTableMergeDagParam::ObMdsTableMergeDagParam()
  : ObTabletMergeDagParam(),
    flush_scn_(share::SCN::invalid_scn()),
    generate_ts_(0),
    mds_construct_sequence_(-1)
{
  // Mds dump should not access mds data to avoid potential dead lock
  // between mds table lock on ObTabletPointer and other mds component
  // inner locks.
  skip_get_tablet_ = true;
}
} // namespace mds
} // namespace storage
} // namespace oceanbase
