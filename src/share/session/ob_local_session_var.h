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

#ifndef OCEANBASE_SHARE_LOCAL_SESSION_VAR_H_
#define OCEANBASE_SHARE_LOCAL_SESSION_VAR_H_

#include "share/system_variable/ob_sys_var_class_type.h"
#include "common/object/ob_object.h"
#include "lib/container/ob_fixed_array.h"
#include "lib/allocator/page_arena.h"

namespace oceanbase
{
namespace share
{

struct ObSessionSysVar {
  OB_UNIS_VERSION(1);
public:
  TO_STRING_KV(K_(type), K_(val));
  bool is_equal(const common::ObObj &other_val) const;
  int64_t get_deep_copy_size() const;

  share::ObSysVarClassType type_;
  common::ObObj val_;
};

class ObLocalSessionVar {
  OB_UNIS_VERSION(1);
public:
  ObLocalSessionVar(common::ObIAllocator *alloc)
    : alloc_(alloc),
      local_session_vars_(alloc)
  {}
  ObLocalSessionVar()
    : alloc_(NULL)
  {}
  ~ObLocalSessionVar() { reset(); }
  void set_allocator(common::ObIAllocator *allocator)
  {
    alloc_ = allocator;
    local_session_vars_.set_allocator(allocator);
  }
  void reset();
  int set_local_var_capacity(int64_t sz);
  int add_local_var(share::ObSysVarClassType var_type, const common::ObObj &value);
  int add_local_var(const ObSessionSysVar *var);
  int get_local_var(share::ObSysVarClassType var_type, ObSessionSysVar *&sys_var) const;
  int get_local_vars(common::ObIArray<const ObSessionSysVar *> &var_array) const;
  int deep_copy(const ObLocalSessionVar &other);
  int assign(const ObLocalSessionVar &other);
  bool operator == (const ObLocalSessionVar& other) const;
  int64_t get_deep_copy_size() const;
  int64_t get_var_count() const { return local_session_vars_.count(); }
  int gen_local_session_var_str(common::ObIAllocator &allocator, common::ObString &local_session_var_str) const;
  int fill_local_session_var_from_str(const common::ObString &local_session_var_str);
  DECLARE_TO_STRING;

private:
  common::ObIAllocator *alloc_;
  common::ObFixedArray<ObSessionSysVar *, common::ObIAllocator> local_session_vars_;
};

} // namespace share
} // namespace oceanbase

#endif /* OCEANBASE_SHARE_LOCAL_SESSION_VAR_H_ */
