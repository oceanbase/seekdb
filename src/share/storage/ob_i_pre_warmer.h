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
#ifndef OB_SHARE_STORAGE_I_PRE_WARMER_H_
#define OB_SHARE_STORAGE_I_PRE_WARMER_H_
#include "/usr/include/stdint.h"
#include "deps/oblib/src/lib/utility/ob_print_utils.h"
#include "share/schema/ob_table_param.h"
namespace oceanbase
{
namespace storage
{
class ObITableReadInfo;
}
namespace blocksstable
{
struct ObMicroBlockDesc;
}
namespace common
{
class ObTabletID;
}
namespace share
{
class ObLSID;
class ObIPreWarmer
{
public:
  ObIPreWarmer() : is_inited_(false) {}
  virtual ~ObIPreWarmer() {}
  virtual int init(const storage::ObITableReadInfo *table_read_info) = 0;
  virtual void reuse() = 0;
  virtual int reserve(const blocksstable::ObMicroBlockDesc &micro_block_desc,
                      bool &reserve_succ_flag,
                      const int64_t level = 0) = 0;
  virtual int add(const blocksstable::ObMicroBlockDesc &micro_block_desc, const bool reserve_succ_flag) = 0;
  virtual int close() = 0;
  DECLARE_PURE_VIRTUAL_TO_STRING;

protected:
  bool is_inited_;
};

enum ObPreWarmerType : uint8_t
{
  PRE_WARM_TYPE_NONE = 0,
  MEM_PRE_WARM, // ObDataBlockCachePreWarmer
  MEM_AND_FILE_PRE_WARM, // for SS
  PRE_WARM_TYPE_MAX
};

struct ObPreWarmerParam
{
public:
  ObPreWarmerParam()
    : type_(PRE_WARM_TYPE_MAX), fixed_percentage_(0)
  {}
  ObPreWarmerParam(const ObPreWarmerType type)
    : type_(type)
  {}
  virtual ~ObPreWarmerParam() { type_ = PRE_WARM_TYPE_MAX; }
  virtual bool is_valid() const { return type_ >= PRE_WARM_TYPE_NONE && type_ < PRE_WARM_TYPE_MAX; }
  virtual int init(const share::ObLSID &ls_id, const common::ObTabletID &tablet_id, const bool use_fixed_percentage = false);
  VIRTUAL_TO_STRING_KV(K_(type), K_(fixed_percentage));
  ObPreWarmerType type_;
  int64_t fixed_percentage_;
};

} // namespace share
} // namespace oceanbase

#endif // OB_SHARE_STORAGE_I_PRE_WARMER_H_
