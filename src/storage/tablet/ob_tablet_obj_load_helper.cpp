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

#include "storage/tablet/ob_tablet_obj_load_helper.h"
#include "storage/blockstore/ob_shared_object_reader_writer.h"

#define USING_LOG_PREFIX STORAGE

using namespace oceanbase::common;

namespace oceanbase
{
namespace storage
{
int ObTabletObjLoadHelper::read_from_addr(
    common::ObArenaAllocator &allocator,
    const ObMetaDiskAddr &meta_addr,
    char *&buf,
    int64_t &buf_len)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!meta_addr.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(meta_addr));
  } else if (OB_UNLIKELY(!meta_addr.is_block())) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("the meta disk address type is not supported", K(ret), K(meta_addr));
  } else {
    ObSharedObjectReadInfo read_info;
    read_info.addr_ = meta_addr;
    read_info.io_desc_.set_mode(ObIOMode::READ);
    read_info.io_desc_.set_wait_event(ObWaitEventIds::DB_FILE_DATA_READ);
    read_info.ls_epoch_ = 0; /* ls_epoch for share storage */
    read_info.io_timeout_ms_ = GCONF._data_storage_io_timeout / 1000;
    ObSharedObjectReadHandle io_handle(allocator);
    if (OB_FAIL(ObSharedObjectReaderWriter::async_read(read_info, io_handle))) {
      LOG_WARN("fail to async read", K(ret), K(read_info));
    } else if (OB_FAIL(io_handle.wait())) {
      LOG_WARN("fail to wait io_hanlde", K(ret), K(read_info));
    } else if (OB_FAIL(io_handle.get_data(allocator, buf, buf_len))) {
      LOG_WARN("fail to get data", K(ret), K(read_info));
    }
  }
  return ret;
}
} // namespace storage
} // namespace oceanbase
