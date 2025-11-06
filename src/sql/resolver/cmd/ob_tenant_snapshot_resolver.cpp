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

#include "sql/resolver/cmd/ob_tenant_snapshot_stmt.h"
#include "sql/resolver/cmd/ob_tenant_snapshot_resolver.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

ObCreateTenantSnapshotResolver::ObCreateTenantSnapshotResolver(ObResolverParams &params)
  : ObCMDResolver(params)
{
}

ObCreateTenantSnapshotResolver::~ObCreateTenantSnapshotResolver()
{
}

int ObCreateTenantSnapshotResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ParseNode *node = const_cast<ParseNode*>(&parse_tree);
  ObCreateTenantSnapshotStmt *mystmt = NULL;
  uint64_t tenant_id = OB_INVALID_TENANT_ID;

  if (OB_ISNULL(node)
      || OB_UNLIKELY(T_CREATE_TENANT_SNAPSHOT != node->type_)
      || OB_UNLIKELY(2 != node->num_child_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid param", KR(ret));
  } else if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session info should not be null", KR(ret));
  }

  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(NULL == (mystmt = create_stmt<ObCreateTenantSnapshotStmt>()))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("failed to create stmt");
    } else {
      stmt_ = mystmt;
    }
  }

  /* snapshot name */
  if (OB_SUCC(ret)) {
    if (NULL != node->children_[0]) {
      if (OB_UNLIKELY(T_IDENT != node->children_[0]->type_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("invalid node", KR(ret));
      } else {
        ObString snapshot_name;
        snapshot_name.assign_ptr((char *)(node->children_[0]->str_value_),
                              static_cast<int32_t>(node->children_[0]->str_len_));
        if (OB_FAIL(mystmt->set_tenant_snapshot_name(snapshot_name))) {
          LOG_WARN("fail to set tenant snapshot name", KR(ret), K(snapshot_name));
        }
      }
    } else {
      //create snapshot name later
    }
  }

   /* tenant name */
  if (OB_SUCC(ret)) {
    if (NULL != node->children_[1]) {
      if (OB_SYS_TENANT_ID != tenant_id) {
        ret = OB_ERR_NO_PRIVILEGE;
        LOG_WARN("Only sys tenant can add suffix opt(for tenant name)", KR(ret), K(tenant_id));
      } else if (OB_UNLIKELY(T_IDENT != node->children_[1]->type_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("invalid node", KR(ret));
      } else {
        ObString tenant_name;
        tenant_name.assign_ptr((char *)(node->children_[1]->str_value_),
                              static_cast<int32_t>(node->children_[1]->str_len_));
        if (OB_FAIL(mystmt->set_tenant_name(tenant_name))) {
          LOG_WARN("fail to set tenant name", KR(ret), K(tenant_name));
        }
      }
    }
  }
  
  return ret;
}

ObDropTenantSnapshotResolver::ObDropTenantSnapshotResolver(ObResolverParams &params)
  : ObCMDResolver(params)
{
}

ObDropTenantSnapshotResolver::~ObDropTenantSnapshotResolver()
{
}

int ObDropTenantSnapshotResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ParseNode *node = const_cast<ParseNode*>(&parse_tree);
  ObDropTenantSnapshotStmt *mystmt = NULL;
  uint64_t tenant_id = OB_INVALID_TENANT_ID;

  if (OB_ISNULL(node)
      || OB_UNLIKELY(T_DROP_TENANT_SNAPSHOT != node->type_)
      || OB_UNLIKELY(2 != node->num_child_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid param", KR(ret));
  } else if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session info should not be null", KR(ret));
  }

  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(NULL == (mystmt = create_stmt<ObDropTenantSnapshotStmt>()))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("failed to create stmt");
    } else {
      stmt_ = mystmt;
    }
  }

  /* snapshot name */
  if (OB_SUCC(ret)) {
    if (NULL != node->children_[0]) {
      if (OB_UNLIKELY(T_IDENT != node->children_[0]->type_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("invalid node", KR(ret));
      } else {
        ObString snapshot_name;
        snapshot_name.assign_ptr((char *)(node->children_[0]->str_value_),
                              static_cast<int32_t>(node->children_[0]->str_len_));
        if (OB_FAIL(mystmt->set_tenant_snapshot_name(snapshot_name))) {
          LOG_WARN("fail to set tenant snapshot name", KR(ret), K(snapshot_name));
        }
      }
    }
  }

   /* tenant name */
  if (OB_SUCC(ret)) {
    if (NULL != node->children_[1]) {
      if (OB_SYS_TENANT_ID != tenant_id) {
        ret = OB_ERR_NO_PRIVILEGE;
        LOG_WARN("Only sys tenant can add suffix opt(for tenant name)", KR(ret), K(tenant_id));
      } else if (OB_UNLIKELY(T_IDENT != node->children_[1]->type_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("invalid node", KR(ret));
      } else {
        ObString tenant_name;
        tenant_name.assign_ptr((char *)(node->children_[1]->str_value_),
                              static_cast<int32_t>(node->children_[1]->str_len_));
        if (OB_FAIL(mystmt->set_tenant_name(tenant_name))) {
          LOG_WARN("fail to set tenant name", KR(ret), K(tenant_name));
        }
      }
    }
  }
  
  return ret;
}

}
}
