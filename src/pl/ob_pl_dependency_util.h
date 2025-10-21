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

#ifndef OCEANBASE_SRC_PL_OB_PL_DEPENDENCY_MANAGER_H_
#define OCEANBASE_SRC_PL_OB_PL_DEPENDENCY_MANAGER_H_

#include "share/ob_define.h"
#include "sql/resolver/ob_stmt_resolver.h"
#include "pl/ob_pl_stmt.h"

namespace oceanbase
{

namespace pl
{

class ObPLDependencyUtil
{
public:
  ObPLDependencyUtil() {}
  virtual ~ObPLDependencyUtil() {}
  static int add_dependency_objects(const ObPLDependencyTable *dep_tbl,
                                    const ObIArray<ObSchemaObjVersion> &dependency_objects);
  static int add_dependency_objects(ObPLDependencyTable &dep_tbl,
                                    const ObPLResolveCtx &resolve_ctx,
                                    const ObPLDataType &type);
  static int add_dependency_object_impl(const ObPLDependencyTable *dep_tbl,
                                        const share::schema::ObSchemaObjVersion &obj_version);
  static int add_dependency_object_impl(ObPLDependencyTable &dep_tbl,
                             const share::schema::ObSchemaObjVersion &obj_version);
};

class ObPLDependencyGuard
{
public:
  ObPLDependencyGuard(const ObPLExternalNS *src_external_ns, const ObPLExternalNS *dst_external_ns)
  : old_external_ns_(nullptr), old_dependency_table_(nullptr) {
    if (OB_NOT_NULL(src_external_ns)
    && OB_NOT_NULL(dst_external_ns)
    && dst_external_ns->get_dependency_table() != src_external_ns->get_dependency_table()) {
      old_external_ns_ = const_cast<ObPLExternalNS *>(dst_external_ns);
      old_dependency_table_ = old_external_ns_->get_dependency_table();
      old_external_ns_->set_dependency_table(const_cast<ObPLExternalNS *>(src_external_ns)->get_dependency_table());
    }
  }
  ~ObPLDependencyGuard() {
    if (OB_NOT_NULL(old_external_ns_)) {
      old_external_ns_->set_dependency_table(old_dependency_table_);
    }
  }
private:
  ObPLExternalNS *old_external_ns_;
  ObPLDependencyTable *old_dependency_table_;
};

}
}

#endif
