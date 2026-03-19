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
#ifndef OCEANBASE_LOG_SERVICE_OB_REMOTE_LOG_SOURCE_ALLOCATOR_H_
#define OCEANBASE_LOG_SERVICE_OB_REMOTE_LOG_SOURCE_ALLOCATOR_H_

#include "share/restore/ob_log_restore_source.h"
#include "ob_remote_log_source.h"
namespace oceanbase
{
namespace logservice
{
class ObResSrcAlloctor
{
public:
    static ObRemoteLogParent *alloc(const share::ObLogRestoreSourceType &type, const share::ObLSID &ls_id);
    static void free(ObRemoteLogParent *source);
};
} // namespace logservice
} // namespace oceanbase


#endif /* OCEANBASE_LOG_SERVICE_OB_REMOTE_LOG_SOURCE_ALLOCATOR_H_ */
