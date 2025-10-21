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

#define USING_LOG_PREFIX SQL_RESV

#include "sql/resolver/cmd/ob_create_restore_point_resolver.h"
#include "sql/resolver/cmd/ob_create_restore_point_stmt.h"
#include "src/sql/resolver/ob_resolver_utils.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

/**
 *  CREATE RESTORE POINT restore_point_name
 *
 */

ObCreateRestorePointResolver::ObCreateRestorePointResolver(ObResolverParams &params)
  : ObSystemCmdResolver(params)
{
}

ObCreateRestorePointResolver::~ObCreateRestorePointResolver()
{
}

int ObCreateRestorePointResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObCreateRestorePointStmt *mystmt = NULL;

  if (OB_UNLIKELY(T_CREATE_RESTORE_POINT != parse_tree.type_)
      || OB_ISNULL(parse_tree.children_)
      || OB_UNLIKELY(1 != parse_tree.num_child_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid param", K(parse_tree.type_), K(parse_tree.num_child_),
        K(parse_tree.children_), K(ret));
  }

  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(NULL == (mystmt = create_stmt<ObCreateRestorePointStmt>()))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("failed to create select stmt");
    } else {
      stmt_ = mystmt;
    }
  }

  /* tenant name */
  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(T_IDENT != parse_tree.children_[0]->type_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("invalid parse_tree", K(ret));
    } else {
      ObString restore_point_name;
      restore_point_name.assign_ptr((char *)(parse_tree.children_[0]->str_value_),
                             static_cast<int32_t>(parse_tree.children_[0]->str_len_));
      if (restore_point_name.length() >= OB_MAX_RESERVED_POINT_NAME_LENGTH) {
        ret = OB_ERR_TOO_LONG_IDENT;
        LOG_USER_ERROR(OB_ERR_TOO_LONG_IDENT, restore_point_name.length(),
            restore_point_name.ptr());
      } else {
        mystmt->set_restore_point_name(restore_point_name);
      }
    }
  }

  // TODO: yanyuan.cxf will be supported later.
  ret = OB_NOT_SUPPORTED;
  return ret;
}


} /* sql */
} /* oceanbase */
