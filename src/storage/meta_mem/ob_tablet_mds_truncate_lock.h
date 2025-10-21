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

#ifndef OCEANBASE_STORAGE_OB_TABLET_MDS_TRUNCATE_LOCK_H
#define OCEANBASE_STORAGE_OB_TABLET_MDS_TRUNCATE_LOCK_H

#include "deps/oblib/src/lib/lock/ob_spin_rwlock.h"
#include "storage/ls/ob_ls_meta.h"

namespace oceanbase
{
namespace storage
{

using ObTabletMDSTruncateLock = common::ObLatch;
using ObTabletMdsSharedLockGuard = ObLSMeta::ObReentrantRLockGuard;
using ObTabletMdsExclusiveLockGuard = ObLSMeta::ObReentrantWLockGuard;

}
}

#endif
