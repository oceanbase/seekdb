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

#ifndef OCEANBASE_SQL_PRIVILEGE_CHECK_OB_AI_MODEL_PRIV_UTIL_
#define OCEANBASE_SQL_PRIVILEGE_CHECK_OB_AI_MODEL_PRIV_UTIL_

#include "sql/resolver/ob_schema_checker.h"
#include "share/schema/ob_priv_type.h"

namespace oceanbase
{
namespace sql
{


class ObAIServiceEndpointPrivUtil
{
public:
  explicit ObAIServiceEndpointPrivUtil(share::schema::ObSchemaGetterGuard &schema_guard);
  ~ObAIServiceEndpointPrivUtil() {}

  int check_create_ai_model_priv(
    ObIAllocator &allocator,
    const share::schema::ObSessionPrivInfo &session_priv,
    bool &has_priv);

  int check_alter_ai_model_priv(
    ObIAllocator &allocator,
    const share::schema::ObSessionPrivInfo &session_priv,
    bool &has_priv);

  int check_drop_ai_model_priv(
    ObIAllocator &allocator,
    const share::schema::ObSessionPrivInfo &session_priv,
    bool &has_priv);

  int check_access_ai_model_priv(
    ObIAllocator &allocator,
    const share::schema::ObSessionPrivInfo &session_priv,
    bool &has_priv);

private:
  enum ObAIServiceEndpointPrivType {
    AI_PRIV_CREATE = OB_PRIV_CREATE_AI_MODEL,
    AI_PRIV_ALTER  = OB_PRIV_ALTER_AI_MODEL,
    AI_PRIV_DROP   = OB_PRIV_DROP_AI_MODEL,
    AI_PRIV_ACCESS = OB_PRIV_ACCESS_AI_MODEL
  };

  int check_ai_model_priv(
    ObIAllocator &allocator,
    const share::schema::ObSessionPrivInfo &session_priv,
    ObPrivSet ai_priv_type,
    bool &has_priv);

private:
  share::schema::ObSchemaGetterGuard &schema_guard_;
};


} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_PRIVILEGE_CHECK_OB_AI_MODEL_PRIV_UTIL_
