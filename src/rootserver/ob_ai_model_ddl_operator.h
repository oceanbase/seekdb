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

 #ifndef OCEANBASE_ROOTSERVER_OB_AI_MODEL_DDL_OPERATOR_H_
 #define OCEANBASE_ROOTSERVER_OB_AI_MODEL_DDL_OPERATOR_H_

 #include "share/ob_rpc_struct.h"
 #include "share/schema/ob_schema_service.h"

namespace oceanbase
{

namespace rootserver
{
class ObAiModelDDLOperator
{
public:
  ObAiModelDDLOperator(share::schema::ObMultiVersionSchemaService &schema_service)
      : schema_service_(schema_service) {}
  virtual ~ObAiModelDDLOperator() {}

  int create_ai_model(ObAiModelSchema &ai_model_schema,
                      const ObString &ddl_stmt,
                      common::ObMySQLTransaction &trans);
  int drop_ai_model(const ObAiModelSchema &ai_model_schema,
                    const ObString &ddl_stmt,
                    common::ObMySQLTransaction &trans);

private:
  share::schema::ObMultiVersionSchemaService &schema_service_;
};

} // end namespace rootserver
} // end namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_OB_AI_MODEL_DDL_OPERATOR_H_
