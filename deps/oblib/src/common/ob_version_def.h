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

#define DEF_MAJOR_VERSION 1
#define DEF_MINOR_VERSION 0
#define DEF_MAJOR_PATCH_VERSION 0
#define DEF_MINOR_PATCH_VERSION 0

/*
 * FIXME: cluster_version is currently up to 4 digits, this definition needs to be consistent with the definitions in CMakeLists.txt, tools/upgrade, src/share/parameter/ob_parameter_seed.ipp
 *        When the last digit is not 0, attention is needed. For example, version 2.2.2 actually represents 2.2.02, but we actually want to define it as 2.2.20, which does not match our intention.
 *        However, versions 2.2.1 and earlier have already been released, to avoid introducing compatibility issues, the cluster_version definition of historical versions will not be changed.
 */
// ATTENSION!!!!!!!!!!!!!!!!!
//
// Cluster Version which is less than "3.2.3":
// - 1. It's composed by 3 parts(major, minor, minor_patch)
// - 2. String: cluster version will be format as "major.minor.minor_patch[.0]", and string like "major.minor.x.minor_patch" is invalid.
// - 3. Integer: for compatibility, cluster version will be encoded into "major|minor|x|minor_patch". "x" must be 0, otherwise, it's invalid.
// - 4. Print: cluster version str will be still printed as 3 parts.
//
// Cluster Version which is not less than "3.2.3":
// - 1. It's composed by 4 parts(major, minor, major_patch, minor_patch)
// - 2. String: cluster version will be format as "major.minor.major_patch.minor_patch".
// - 3. Integer: cluster version will be encoded into "major|minor|major_patch|minor_patch".
// - 4. Print: cluster version str will be printed as 4 parts.

#define CLUSTER_VERSION_1_0_0_0 (oceanbase::common::cal_version(1, 0, 0, 0))
#define CLUSTER_VERSION_1_0_1_0 (oceanbase::common::cal_version(1, 0, 1, 0))
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//TODO: If you update the above version, please update CLUSTER_CURRENT_VERSION.
#define CLUSTER_CURRENT_VERSION CLUSTER_VERSION_1_0_1_0

// ATTENSION !!!!!!!!!!!!!!!!!!!!!!!!!!!
// 1. each cluster_version is corresponed to a data version.
// 2. cluster_version and data_version is not compariable.
// 3. TODO: If you update data_version below, please update DATA_CURRENT_VERSION & ObUpgradeChecker too.
#define DEFAULT_MIN_DATA_VERSION (oceanbase::common::cal_version(0, 0, 0, 1))

#define DATA_VERSION_1_0_0_0 (oceanbase::common::cal_version(1, 0, 0, 0))
#define DATA_VERSION_1_0_1_0 (oceanbase::common::cal_version(1, 0, 1, 0))
#define DATA_CURRENT_VERSION DATA_VERSION_1_0_1_0
// ATTENSION !!!!!!!!!!!!!!!!!!!!!!!!!!!
// LAST_BARRIER_DATA_VERSION should be the latest barrier data version before DATA_CURRENT_VERSION
#define LAST_BARRIER_DATA_VERSION DATA_VERSION_1_0_0_0

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
#define CVP(version) VP(version)
// print data version in human readable way
#define KDV(x) #x, DVP(x)
#define KDV_(x) #x, DVP(x##_)
// print cluster version in human readable way
#define KCV(x) #x, CVP(x)
#define KCV_(x) #x, CVP(x##_)
#endif
