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

#ifndef OCEANBASE_SQL_ENGINE_TABLE_OB_VIRTUAL_TABLE_CTX_
#define OCEANBASE_SQL_ENGINE_TABLE_OB_VIRTUAL_TABLE_CTX_

#include "share/ob_define.h"

namespace oceanbase
{

namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
}
}

namespace sql
{
class ObIVirtualTableIteratorFactory;
class ObSQLSessionInfo;
class ObVirtualTableCtx
{
public:
  ObVirtualTableCtx()
      : vt_iter_factory_(NULL),
        schema_guard_(NULL),
        session_(NULL)
  {}
  ~ObVirtualTableCtx() {}

  void reset()
  {
    vt_iter_factory_ = NULL;
    schema_guard_ = NULL;
    session_ = NULL;
  }

  ObIVirtualTableIteratorFactory *vt_iter_factory_;
  share::schema::ObSchemaGetterGuard *schema_guard_;
  ObSQLSessionInfo *session_;
};
}
}
#endif /* OCEANBASE_SQL_ENGINE_TABLE_OB_VIRTUAL_TABLE_CTX_ */
