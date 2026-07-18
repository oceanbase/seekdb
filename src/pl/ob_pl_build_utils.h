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

#ifndef OCEANBASE_SRC_PL_OB_PL_BUILD_UTILS_H_
#define OCEANBASE_SRC_PL_OB_PL_BUILD_UTILS_H_

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

class ObPLBuildUtils
{
public:

enum PLUnitType {
  PL_UNIT_INVALID = -1,
  PL_UNIT_PROCEDURE,
  PL_UNIT_FUNCTION,
  PL_UNIT_PACKAGE_SPEC,
  PL_UNIT_PACKAGE_BODY,
  PL_UNIT_TRIGGER,
};

  static inline PLUnitType get_pl_unit_type(share::schema::ObRoutineType routine_type) {
    PLUnitType type = PL_UNIT_INVALID;
    if (share::schema::ROUTINE_PROCEDURE_TYPE == routine_type) {
      type = PL_UNIT_PROCEDURE;
    } else if (share::schema::ROUTINE_FUNCTION_TYPE == routine_type) {
      type = PL_UNIT_FUNCTION;
    }
    return type;
  }

  static inline PLUnitType get_pl_unit_type(share::schema::ObPackageType package_type) {
    PLUnitType type = PL_UNIT_INVALID;
    if (share::schema::PACKAGE_TYPE == package_type) {
      type = PL_UNIT_PACKAGE_SPEC;
    } else if (share::schema::PACKAGE_BODY_TYPE == package_type) {
      type = PL_UNIT_PACKAGE_BODY;
    }
    return type;
  }

  static inline PLUnitType get_pl_unit_type(ObString &object_type) {
    PLUnitType type = PL_UNIT_INVALID;
    if (0 == object_type.compare("PROCEDURE")) {
      type = PL_UNIT_PROCEDURE;
    } else if (0 == object_type.compare("FUNCTION")) {
      type = PL_UNIT_FUNCTION;
    } else if (0 == object_type.compare("TRIGGER")) {
      type = PL_UNIT_TRIGGER;
    } else if (0 == object_type.compare("PACKAGE")) {
      type = PL_UNIT_PACKAGE_SPEC;
    } else if (0 == object_type.compare("PACKAGE BODY")) {
      type = PL_UNIT_PACKAGE_BODY;
    }
    return type;
  }

  static int build(sql::ObExecContext &ctx,
                   uint64_t database_id,
                   const ObString &object_name,
                   PLUnitType unit_type,
                   int64_t schema_version = OB_INVALID_VERSION,
                   bool is_rebuild = false);

  static int build(sql::ObExecContext &ctx,
                   const ObString &database_name,
                   const ObString &object_name,
                   PLUnitType unit_type,
                   int64_t schema_version = OB_INVALID_VERSION,
                   bool is_rebuild = false);

private:
  static int build_routine(sql::ObExecContext &ctx,
                           uint64_t database_id,
                           const ObString &routine_name,
                           share::schema::ObRoutineType routine_type,
                           int64_t schema_version,
                           bool is_rebuild);
  static int build_package(sql::ObExecContext &ctx,
                           uint64_t database_id,
                           const ObString &package_name,
                           share::schema::ObPackageType package_type,
                           int64_t schema_version,
                           bool is_rebuild);
  static int build_trigger(sql::ObExecContext &ctx,
                           uint64_t database_id,
                           const ObString &trigger_name,
                           int64_t schema_version,
                           bool is_rebuild);
};

}
}

#endif /* OCEANBASE_SRC_PL_OB_PL_BUILD_UTILS_H_ */
