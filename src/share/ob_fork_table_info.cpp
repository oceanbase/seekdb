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
#include "share/ob_fork_table_info.h"

namespace oceanbase
{
namespace share
{
OB_SERIALIZE_MEMBER(ObForkTableInfo, fork_src_table_id_, fork_snapshot_version_);
OB_SERIALIZE_MEMBER(ObForkTabletInfo, fork_info_, fork_snapshot_version_, fork_src_tablet_id_);
}  // namespace share
}  // namespace oceanbase
