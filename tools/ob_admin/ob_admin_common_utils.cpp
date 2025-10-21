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

#include "ob_admin_common_utils.h"
#include "storage/blocksstable/ob_storage_cache_suite.h"

using namespace oceanbase::common;
using namespace oceanbase::blocksstable;
namespace oceanbase 
{
namespace tools 
{
int ObAdminCommonUtils::dump_single_macro_block(
    const ObDumpMacroBlockContext &macro_context, 
    const char* buf, 
    const int64_t size) 
{
  int ret = OB_SUCCESS;
  ObSSTableDataBlockReader macro_reader;
  if (OB_ISNULL(buf) || size <= 0) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(ERROR, "invalid argument", K(ret), KP(buf), K(size));
  } else if (OB_FAIL(macro_reader.init(buf, size, false))) {
    STORAGE_LOG(ERROR, "failed to init macro reader", K(ret), KP(buf), K(size));
  } else if (OB_FAIL(macro_reader.dump(macro_context.tablet_id_, macro_context.scn_))) {
    STORAGE_LOG(ERROR, "failed dump macro block", K(ret), KP(buf), K(size));
  }

  return ret;
}

int ObAdminCommonUtils::dump_shared_macro_block(
    const ObDumpMacroBlockContext &macro_context,
    const char* buf, 
    const int64_t size) 
{
  int ret = OB_SUCCESS;
  const int64_t aligned_size = 4096;
  int64_t current_page_offset = aligned_size;
  if (OB_ISNULL(buf) || size <= 0) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(ERROR, "invalid argument", K(ret), KP(buf), K(size));
  } else {
    while (OB_SUCC(ret) && current_page_offset < size) {
      ObMacroBlockCommonHeader common_header;
      int64_t pos = 0;
      const char* cur_buf = buf + current_page_offset;
      const int64_t cur_size = size - current_page_offset;
      if (OB_FAIL(common_header.deserialize(cur_buf, cur_size, pos))) {
        if (OB_DESERIALIZE_ERROR != ret) {
          STORAGE_LOG(ERROR, "deserialize common header fail", K(ret), K(pos));
        } else {
          ret = OB_SUCCESS;
          break;
        }
      } else if (OB_FAIL(common_header.check_integrity())) {
        STORAGE_LOG(ERROR, "invalid common header", K(ret), K(common_header));
      } else if (OB_FAIL(dump_single_macro_block(macro_context, cur_buf,
          common_header.get_header_size() + common_header.get_payload_size()))) {
        STORAGE_LOG(ERROR, "dump single block fail", K(ret), K(common_header));
      } else {
        current_page_offset = upper_align(
            current_page_offset + common_header.get_header_size() + common_header.get_payload_size(),
            aligned_size);
      }
    }
  }
  STORAGE_LOG(INFO, "dump shared block finish", K(ret), K(current_page_offset));
  return ret;
}

}  // namespace tools
}  // namespace oceanbase
