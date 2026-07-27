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

#ifndef OCEANBASE_OBSERVER_OB_VERSION_DEF_H_
#define OCEANBASE_OBSERVER_OB_VERSION_DEF_H_

#include "lib/ob_define.h"
#include "lib/allocator/page_arena.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace common
{
#define OB_VSN_MAJOR_SHIFT 32
#define OB_VSN_MINOR_SHIFT 16
#define OB_VSN_MAJOR_PATCH_SHIFT 8
#define OB_VSN_MINOR_PATCH_SHIFT 0
#define OB_VSN_MAJOR_MASK 0xffffffff
#define OB_VSN_MINOR_MASK 0xffff
#define OB_VSN_MAJOR_PATCH_MASK 0xff
#define OB_VSN_MINOR_PATCH_MASK 0xff
#define OB_VSN_MAJOR(version) (static_cast<const uint32_t>((version >> OB_VSN_MAJOR_SHIFT) & OB_VSN_MAJOR_MASK))
#define OB_VSN_MINOR(version) (static_cast<const uint16_t>((version >> OB_VSN_MINOR_SHIFT) & OB_VSN_MINOR_MASK))
#define OB_VSN_MAJOR_PATCH(version) (static_cast<const uint8_t>((version >> OB_VSN_MAJOR_PATCH_SHIFT) & OB_VSN_MAJOR_PATCH_MASK))
#define OB_VSN_MINOR_PATCH(version) (static_cast<const uint8_t>(version & OB_VSN_MINOR_PATCH_MASK))

#define CALC_VERSION(major, minor, major_patch, minor_patch) \
        (((major) << OB_VSN_MAJOR_SHIFT) + \
         ((minor) << OB_VSN_MINOR_SHIFT) + \
         ((major_patch) << OB_VSN_MAJOR_PATCH_SHIFT) + \
         ((minor_patch)))
constexpr static inline uint64_t
cal_version(const uint64_t major, const uint64_t minor, const uint64_t major_patch, const uint64_t minor_patch)
{
  return CALC_VERSION(major, minor, major_patch, minor_patch);
}

#define SERVER_CURRENT_VERSION (oceanbase::common::cal_version(1, 3, 0, 0))
#define DATA_CURRENT_VERSION (oceanbase::common::cal_version(1, 3, 0, 0))

#define PROXY_VERSION_4_2_3_0 (oceanbase::common::cal_version(4, 2, 3, 0))
#define PROXY_VERSION_4_3_0_0 (oceanbase::common::cal_version(4, 3, 0, 0))
#define PROXY_VERSION_4_3_3_0 (oceanbase::common::cal_version(4, 3, 3, 0))

class VersionUtil
{
public:
  static int64_t print_version_str(char *buf, const int64_t buf_len, uint64_t version);
};

class ObVersionPrinter
{
public:
  ObVersionPrinter(const uint64_t version);
  TO_STRING_KV(K_(version_str), K_(version_val));
private:
  uint64_t version_val_;
  char version_str_[OB_SERVER_VERSION_LENGTH];
};

} // namespace common
} // namespace oceanbase

#define VP(version) (::oceanbase::common::ObVersionPrinter(version))
#define DVP(version) VP(version)
// print data version in human readable way
#define KDV(x) #x, DVP(x)
#define KDV_(x) #x, DVP(x##_)
#endif
