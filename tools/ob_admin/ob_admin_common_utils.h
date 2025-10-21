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

#ifndef _OB_ADMIN_COMMON_UTILS_H_
#define _OB_ADMIN_COMMON_UTILS_H_
#include "lib/restore/ob_storage_info.h"


namespace oceanbase
{
namespace tools
{

struct ObDumpMacroBlockContext final
{
public:
  ObDumpMacroBlockContext()
    : first_id_(-1), second_id_(-1), micro_id_(-1), tablet_id_(0), scn_(-1), offset_(0)
  {}
  ~ObDumpMacroBlockContext() = default;
  bool is_valid() const
  {
    return second_id_ >= 0 || STRLEN(object_file_path_) > 0 || (STRLEN(uri_) > 0 && STRLEN(storage_info_str_) > 0);
  }
  TO_STRING_KV(K(first_id_), K(second_id_), K(micro_id_), K_(tablet_id), K_(scn), K_(object_file_path), K_(uri), K_(prewarm_index));
  uint64_t first_id_;
  int64_t second_id_;
  int64_t micro_id_;
  uint64_t tablet_id_;
  int64_t scn_;
  int64_t offset_;
  char object_file_path_[common::OB_MAX_FILE_NAME_LENGTH] = {0};
  char uri_[common::OB_MAX_URI_LENGTH] = {0};
  char storage_info_str_[common::OB_MAX_BACKUP_STORAGE_INFO_LENGTH] = {0};
  char prewarm_index_[common::OB_MAX_FILE_NAME_LENGTH] = {0};
};

class ObAdminCommonUtils {
public:
  static int dump_single_macro_block(const ObDumpMacroBlockContext &macro_context, const char *buf, const int64_t size);
  static int dump_shared_macro_block(const ObDumpMacroBlockContext &macro_context, const char *buf, const int64_t size);
};

} //namespace tools
} //namespace oceanbase
#endif  // _OB_ADMIN_COMMON_UTILS_H_
