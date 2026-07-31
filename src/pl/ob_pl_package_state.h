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

#ifndef SRC_PL_OB_PL_PACKAGE_STATE_H_
#define SRC_PL_OB_PL_PACKAGE_STATE_H_

#include <cstdint>
#include "share/ob_define.h"
#include "common/object/ob_object.h"
#include "lib/container/ob_array.h"
#include "ob_pl_type.h"
#include "ob_pl_allocator.h"

namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
}
namespace pl
{
class ObPLPackage;

struct ObPackageStateVersion
{
public:
  int64_t package_version_;
  int64_t package_body_version_;
  int64_t header_merge_version_;
  int64_t body_merge_version_;
  // Reserved serialized slots retained for compatibility with existing session state.
  int64_t reserved_header_count_;
  int64_t reserved_body_count_;

  ObPackageStateVersion(int64_t package_version, int64_t package_body_version)
      : package_version_(package_version),
        package_body_version_(package_body_version),
        header_merge_version_(common::OB_INVALID_VERSION),
        body_merge_version_(common::OB_INVALID_VERSION),
        reserved_header_count_(0),
        reserved_body_count_(0) {}
  virtual ~ObPackageStateVersion()
  {
    package_version_ = common::OB_INVALID_VERSION;
    package_body_version_ = common::OB_INVALID_VERSION;
    header_merge_version_ = common::OB_INVALID_VERSION;
    body_merge_version_ = common::OB_INVALID_VERSION;
    reserved_header_count_ = 0;
    reserved_body_count_ = 0;
  }
  void set_merge_versions(const ObPLPackage &head, const ObPLPackage *body);
  ObPackageStateVersion(const ObPackageStateVersion &other);
  bool is_valid() const { return package_version_ != common::OB_INVALID_VERSION; }
  ObPackageStateVersion &operator =(const ObPackageStateVersion &other);
  bool equal(const ObPackageStateVersion &other);
  bool operator ==(const ObPackageStateVersion &other) const;

  TO_STRING_KV(K(package_version_), K(package_body_version_),
               K(header_merge_version_), K(body_merge_version_),
               K(reserved_header_count_), K(reserved_body_count_));
};

class ObPLPackageState
{
public:
  ObPLPackageState(uint64_t package_id,
                   const ObPackageStateVersion &state_version)
      : parent_alloc_(lib::ObMemAttr("PLPkgSymbol"), OB_MALLOC_NORMAL_BLOCK_SIZE),
        inner_allocator_(PL_MOD_IDX::OB_PL_PACKAGE_SYMBOL, &parent_alloc_),
        cursor_allocator_(lib::ObMemAttr("PLPkgCursor"), OB_MALLOC_NORMAL_BLOCK_SIZE),
        package_id_(package_id),
        state_version_(state_version),
        types_(),
        vars_(),
        has_instantiated_(false)
  {
    types_.set_attr(lib::ObMemAttr("PLPkgTypes"));
    vars_.set_attr(lib::ObMemAttr("PLPkgVars"));
  }
  virtual ~ObPLPackageState()
  {
    package_id_ = common::OB_INVALID_ID;
    types_.reset();
    vars_.reset();
    inner_allocator_.reset();
    cursor_allocator_.reset();
    has_instantiated_ = false;
  }
  int init();
  void reset(sql::ObSQLSessionInfo *session_info);
  common::ObIAllocator &get_pkg_allocator() { return inner_allocator_; }
  common::ObIAllocator &get_pkg_cursor_allocator() { return cursor_allocator_; }
  int add_package_var_val(const common::ObObj &value, ObPLType type);
  int set_package_var_val(int64_t var_idx,
                          const common::ObObj &value,
                          bool deep_copy_complex = true);
  int get_package_var_val(int64_t var_idx, common::ObObj &value);
  inline bool check_version(const ObPackageStateVersion &state_version)
  {
    return state_version_.equal(state_version);
  }

  static int check_version(const ObPackageStateVersion &state_version,
                           const ObPackageStateVersion &cur_state_version,
                           ObSchemaGetterGuard &schema_guard,
                           const ObPLPackage &spec,
                           const ObPLPackage *body,
                           bool &match);

  uint64_t get_package_id() { return package_id_; }
  ObIArray<ObObj> &get_vars() { return vars_; }
  ObPackageStateVersion &get_state_version() { return state_version_; }

  void set_has_instantiated(bool instantiated) { has_instantiated_ = instantiated; }
  bool has_instantiated() const { return has_instantiated_; }

  TO_STRING_KV(K(package_id_), K(state_version_));

private:
  DISALLOW_COPY_AND_ASSIGN(ObPLPackageState);
  ObArenaAllocator parent_alloc_;
  ObPLAllocator1 inner_allocator_;
  ObArenaAllocator cursor_allocator_;
  uint64_t package_id_;
  ObPackageStateVersion state_version_;
  common::ObSEArray<ObPLType, 64> types_;
  common::ObSEArray<ObObj, 64> vars_;
  bool has_instantiated_;
};
} // namespace pl
} // namespace oceanbase
#endif /* SRC_PL_OB_PL_PACKAGE_STATE_H_ */
