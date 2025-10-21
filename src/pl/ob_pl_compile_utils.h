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

#ifndef OCEANBASE_SRC_PL_OB_PL_COMPILE_UTILS_H_
#define OCEANBASE_SRC_PL_OB_PL_COMPILE_UTILS_H_

#include "share/schema/ob_routine_info.h"
#include "share/schema/ob_package_info.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObRoutineInfo;
class ObPackageInfo;
}
}
namespace pl
{

class ObPLCompilerUtils
{
public:

enum CompileType {
  COMPILE_INVALID = -1,
  COMPILE_PROCEDURE,
  COMPILE_FUNCTION,
  COMPILE_PACKAGE_SPEC,
  COMPILE_PACKAGE_BODY,
  COMPILE_TRIGGER,
};

  static inline CompileType get_compile_type(share::schema::ObRoutineType routine_type) {
    CompileType type = COMPILE_INVALID;
    if (share::schema::ROUTINE_PROCEDURE_TYPE == routine_type) {
      type = COMPILE_PROCEDURE;
    } else if (share::schema::ROUTINE_FUNCTION_TYPE == routine_type) {
      type = COMPILE_FUNCTION;
    }
    return type;
  }

  static inline CompileType get_compile_type(share::schema::ObPackageType package_type) {
    CompileType type = COMPILE_INVALID;
    if (share::schema::PACKAGE_TYPE == package_type) {
      type = COMPILE_PACKAGE_SPEC;
    } else if (share::schema::PACKAGE_BODY_TYPE == package_type) {
      type = COMPILE_PACKAGE_BODY;
    }
    return type;
  }

  static inline CompileType get_compile_type(ObString &object_type) {
    CompileType type = COMPILE_INVALID;
    if (0 == object_type.compare("PROCEDURE")) {
      type = COMPILE_PROCEDURE;
    } else if (0 == object_type.compare("FUNCTION")) {
      type = COMPILE_FUNCTION;
    } else if (0 == object_type.compare("TRIGGER")) {
      type = COMPILE_TRIGGER;
    } else if (0 == object_type.compare("PACKAGE")) {
      type = COMPILE_PACKAGE_SPEC;
    } else if (0 == object_type.compare("PACKAGE BODY")) {
      type = COMPILE_PACKAGE_BODY;
    }
    return type;
  }

  static int compile(sql::ObExecContext &ctx,
                     uint64_t tenant_id,
                     uint64_t database_id,
                     const ObString &object_name,
                     CompileType object_type,
                     int64_t schema_version = OB_INVALID_VERSION,
                     bool is_recompile = false);

  static int compile(sql::ObExecContext &ctx,
                     uint64_t tenant_id,
                     const ObString &database_name,
                     const ObString &object_name,
                     CompileType object_type,
                     int64_t schema_version = OB_INVALID_VERSION,
                     bool is_recompile = false);

private:
  static int compile_routine(sql::ObExecContext &ctx,
                             uint64_t tenant_id,
                             uint64_t database_id,
                             const ObString &routine_name,
                             share::schema::ObRoutineType routine_type,
                             int64_t schema_version,
                             bool is_recompile);
  static int compile_package(sql::ObExecContext &ctx,
                             uint64_t tenant_id,
                             uint64_t database_id,
                             const ObString &package_name,
                             share::schema::ObPackageType package_type,
                             int64_t schema_version,
                             bool is_recompile);
  static int compile_trigger(sql::ObExecContext &ctx,
                             uint64_t tenant_id,
                             uint64_t database_id,
                             const ObString &trigger_name,
                             int64_t schema_version,
                             bool is_recompile);
};

}
}

#endif /* OCEANBASE_SRC_PL_OB_PL_COMPILE_UTILS_H_ */
