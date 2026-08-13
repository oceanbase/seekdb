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

#ifndef OCEANBASE_SQL_RESOLVER_DDL_OB_TRIGGER_SOURCE_BUILDER_H_
#define OCEANBASE_SQL_RESOLVER_DDL_OB_TRIGGER_SOURCE_BUILDER_H_

#include "lib/string/ob_string.h"

struct _ParseNode;
typedef struct _ParseNode ParseNode;

namespace oceanbase
{
namespace common
{
class ObDataTypeCastParams;
class ObIAllocator;
}
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
class ObTriggerInfo;
}
}
namespace sql
{

class ObTriggerSourceBuilder
{
public:
  static int generate_package_source(
      uint64_t trigger_package_id,
      common::ObString &source,
      bool is_header,
      share::schema::ObSchemaGetterGuard &schema_guard,
      common::ObIAllocator &allocator);
  static int build_package_source(
      share::schema::ObTriggerInfo &trigger_info,
      const common::ObString &base_object_database,
      const common::ObString &base_object_name,
      const ParseNode &parse_node,
      const common::ObDataTypeCastParams &dtc_params);
  static int build_procedure_source(
      const share::schema::ObTriggerInfo &trigger_info,
      common::ObIAllocator &allocator,
      const common::ObString &base_object_database,
      const common::ObString &base_object_name,
      const ParseNode &parse_node,
      const common::ObDataTypeCastParams &dtc_params,
      common::ObString &procedure_source);
  static int replace_table_name_in_body(
      share::schema::ObTriggerInfo &trigger_info,
      common::ObIAllocator &allocator,
      const common::ObString &base_object_database,
      const common::ObString &base_object_name);

private:
  enum PackageSourceType
  {
    SPEC_AND_BODY = 0,
    SPEC_ONLY,
    BODY_ONLY,
  };
  struct TriggerContext
  {
    void select_simple_sections(
        const share::schema::ObTriggerInfo &trigger_info,
        common::ObString *&declaration,
        common::ObString *&execution,
        common::ObString *&trigger_body);
    common::ObString before_row_declare_;
    common::ObString before_row_execute_;
    common::ObString after_row_declare_;
    common::ObString after_row_execute_;
    common::ObString trigger_body_;
  };

  static int generate_simple(
      const share::schema::ObTriggerInfo &trigger_info,
      const common::ObString &base_object_database,
      const common::ObString &base_object_name,
      const ParseNode &parse_node,
      const common::ObDataTypeCastParams &dtc_params,
      common::ObString &spec_source,
      common::ObString &body_source,
      common::ObIAllocator &allocator,
      PackageSourceType type = SPEC_AND_BODY);
  static void calculate_source_sizes(
      const share::schema::ObTriggerInfo &trigger_info,
      const common::ObString &base_object_database,
      const common::ObString &base_object_name,
      int64_t &spec_size,
      int64_t &body_size);
  static int fill_package_spec(
      const share::schema::ObTriggerInfo &trigger_info,
      const common::ObString &base_object_database,
      const common::ObString &base_object_name,
      int64_t spec_size,
      common::ObString &spec_source,
      common::ObIAllocator &allocator);
  static int fill_package_body(
      const share::schema::ObTriggerInfo &trigger_info,
      const common::ObString &base_object_database,
      const common::ObString &base_object_name,
      int64_t body_size,
      const TriggerContext &trigger_context,
      common::ObString &body_source,
      common::ObIAllocator &allocator);
  static int fill_row_routine_spec(
      const char *format,
      const share::schema::ObTriggerInfo &trigger_info,
      const common::ObString &base_object_database,
      const common::ObString &base_object_name,
      char *buffer,
      int64_t buffer_length,
      int64_t &position,
      bool is_before_row);
  static int fill_row_routine_body(
      const share::schema::ObTriggerInfo &trigger_info,
      const common::ObString &base_object_database,
      const common::ObString &base_object_name,
      const TriggerContext &trigger_context,
      char *buffer,
      int64_t buffer_length,
      int64_t &position,
      bool is_before_row);
};

}  // namespace sql
}  // namespace oceanbase

#endif  // OCEANBASE_SQL_RESOLVER_DDL_OB_TRIGGER_SOURCE_BUILDER_H_
