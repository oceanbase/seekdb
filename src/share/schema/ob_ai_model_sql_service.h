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

 #ifndef OCEANBASE_SHARE_SCHEMA_OB_AI_MODEL_SQL_SERVICE_H_
 #define OCEANBASE_SHARE_SCHEMA_OB_AI_MODEL_SQL_SERVICE_H_

 #include "ob_ddl_sql_service.h"

 namespace oceanbase
 {

 namespace common
 {

 class ObString;
 class ObISQLClient;

 }

 namespace obrpc
 {

 class ObCreateAiModelArg;

 }
 namespace share
 {

 namespace schema
 {

 class ObAiModelSchema;

 class ObAiModelSqlService final: public ObDDLSqlService
 {
 public:
   ObAiModelSqlService(ObSchemaService &schema_service)
     : ObDDLSqlService(schema_service)
   {  }

 virtual ~ObAiModelSqlService() = default;

 int create_ai_model(const ObAiModelSchema &new_schema,
                     const ObString &ddl_stmt,
                     common::ObISQLClient &sql_client);

 int drop_ai_model(const ObAiModelSchema &schema,
                   const int64_t new_schema_version,
                   const ObString &ddl_stmt,
                   common::ObISQLClient &sql_client);

 private:
   DISALLOW_COPY_AND_ASSIGN(ObAiModelSqlService);
 };

 } // namespace schema
 } // namespace share
 } // namespace oceanbase

 #endif // OCEANBASE_SHARE_SCHEMA_OB_AI_MODEL_SQL_SERVICE_H_
