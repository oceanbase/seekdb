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
#define USING_LOG_PREFIX STORAGE

#include "storage/meta_store/ob_storage_meta_io_util.h"
namespace oceanbase
{
namespace storage
{
#ifdef OB_BUILD_SHARED_STORAGE
int ObStorageMetaIOUtil::check_meta_existence(
    const blocksstable::ObStorageObjectOpt &opt, const int64_t ls_epoch, bool &is_exist)
{
  int ret = OB_SUCCESS;
  blocksstable::MacroBlockId object_id;
  if (OB_FAIL(OB_STORAGE_OBJECT_MGR.ss_get_object_id(opt, object_id))) {
    LOG_WARN("fail to get object id", K(ret), K(opt));
  } else if (OB_FAIL(OB_STORAGE_OBJECT_MGR.ss_is_exist_object(object_id, ls_epoch, is_exist))) {
    LOG_WARN("fail to check existence", K(ret), K(object_id));
  }
  return ret;
}

#endif

} // namespace storage
} // namespace oceanbase
