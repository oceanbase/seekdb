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

#ifndef __SQL_OB_ODPS_TABLE_UTILS_H__
#define __SQL_OB_ODPS_TABLE_UTILS_H__
#include "common/object/ob_object.h"
#include "lib/udt/ob_array_type.h"
#include "sql/engine/expr/ob_expr.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase {
namespace sql {

/*
  ObArrayHelper contain element information used for decode odps array record.
*/
struct ObODPSArrayHelper {
  ObODPSArrayHelper(ObIAllocator &allocator)
  : allocator_(allocator),
    array_(nullptr),
    child_helper_(nullptr)
  {}
  ~ObODPSArrayHelper()
  {
    if (array_ != nullptr) {
      array_->clear();
      allocator_.free(array_);
      array_ = nullptr;
    }
    if (child_helper_ != nullptr) {
      child_helper_->~ObODPSArrayHelper();
      allocator_.free(child_helper_);
      child_helper_ = nullptr;
    }
  }
  TO_STRING_KV(K(element_type_), K(element_precision_), K(element_scale_),
               K(element_collation_), K(element_length_));
  
  ObIAllocator &allocator_;
  // used to hold child element
  ObIArrayType *array_;
  // child array helper if array element is array
  ObODPSArrayHelper* child_helper_;
  // child element type
  ObObjType element_type_;
  // child element precision
  ObPrecision element_precision_;
  // child element scale
  ObScale element_scale_;
  // child element collation
  ObCollationType element_collation_;
  // child element length
  int32_t element_length_;
};

class ObODPSTableUtils {
public:
  static int create_array_helper(ObExecContext &exec_ctx,
                                 ObIAllocator &allocator,
                                 const ObExpr &cur_expr,
                                 ObODPSArrayHelper *&array_helper);
  static int recursive_create_array_helper(ObIAllocator &allocator,
                                           const ObCollectionTypeBase *coll_meta,
                                           ObODPSArrayHelper *&array_helper);
private:
  //disallow construct
  ObODPSTableUtils();
  ~ObODPSTableUtils();
};


} // sql
} // oceanbase

#endif // __SQL_OB_ODPS_TABLE_UTILS_H__
