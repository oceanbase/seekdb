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

#ifdef  ARCH_DEF
ARCH_DEF(OB_X86_ARCH_TYPE, "X86")
ARCH_DEF(OB_ARM_ARCH_TYPE, "ARM")
#endif


#ifndef OCEANBASE_PL_ROUTINE_STORAGE_H_
#define OCEANBASE_PL_ROUTINE_STORAGE_H_

#include "share/ob_define.h"
#include "lib/mysqlclient/ob_isql_client.h"
#include "ob_pl_stmt.h"
#include "pl/ob_pl_allocator.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "sql/resolver/ob_stmt_resolver.h"
namespace oceanbase
{

namespace common
{
class ObIAllocator;
class ObISQLClient;
class ObMySQLTransaction;
}

namespace share
{
class ObDMLSqlSplicer;
}

namespace pl
{

enum ObPLArchType
{
  OB_INVALID_ARCH_TYPE = -1,
#define ARCH_DEF(type, name) type,
#include "pl/ob_pl_persistent.h"
#undef ARCH_DEF
  ARCH_TYPE_IDX_NUM
};

static constexpr const char* ARCH_TYPE_DEF[ARCH_TYPE_IDX_NUM] =
{
#define ARCH_DEF(type, name) name,
#include "pl/ob_pl_persistent.h"
#undef ARCH_DEF
};

class ObRoutinePersistentInfo
{
public:
  enum ObPLOperation
  {
    NONE = 0,
    INSERT,
    UPDATE,
    SUCC,
  };

  ObRoutinePersistentInfo()
  : tenant_id_(OB_INVALID_ID),
    database_id_(OB_INVALID_ID),
    compile_db_id_(OB_INVALID_ID),
    key_id_(OB_INVALID_ID),
#if defined(__aarch64__)
    arch_type_(ARCH_TYPE_DEF[ObPLArchType::OB_ARM_ARCH_TYPE]),
#else
    arch_type_(ARCH_TYPE_DEF[ObPLArchType::OB_X86_ARCH_TYPE]),
#endif
    allocator_(ObMemAttr(MTL_ID() == OB_INVALID_TENANT_ID ? OB_SYS_TENANT_ID : MTL_ID(), GET_PL_MOD_STRING(OB_PL_JIT))),
    tenant_id_belongs_(OB_INVALID_ID)
  {}
  ObRoutinePersistentInfo(uint64_t tenant_id,
                      uint64_t database_id,
                      uint64_t compile_db_id,
                      uint64_t key_id,
                      uint64_t tenant_id_belongs)
  : tenant_id_(tenant_id),
    database_id_(database_id),
    compile_db_id_(compile_db_id),
    key_id_(key_id),
#if defined(__aarch64__)
    arch_type_(ARCH_TYPE_DEF[ObPLArchType::OB_ARM_ARCH_TYPE]),
#else
    arch_type_(ARCH_TYPE_DEF[ObPLArchType::OB_X86_ARCH_TYPE]),
#endif
    allocator_(ObMemAttr(tenant_id_, GET_PL_MOD_STRING(OB_PL_JIT))),
    tenant_id_belongs_(tenant_id_belongs)
  {}

  int64_t get_head_size() { return 1 + 1 + 2 + 2;/* 8bit flags + 8bit level + 8bit id + 8bit nums*/ }
  int get_total_size(ObPLCompileUnit &unit, int32_t &total_len);

  int encode_header(char *buf, const int64_t len, int64_t &pos,
                        int8_t flags, const int8_t level, const int16_t id, const int16_t nums);

  int encode_payload(char *buf, int64_t len, int64_t &pos, const ObString &binary);

  int encode_dll(ObPLCompileUnit &unit, ObString &dll, int64_t &pos,
                    const int8_t level, const int16_t id);

  int decode_header(char *buf, const int64_t len, int64_t &pos,
                        int8_t &flags, int8_t &level, int16_t &id, int16_t &nums);

  int decode_payload(char *buf, int64_t len, int64_t &pos, ObString &binary);

  int decode_dll(ObSQLSessionInfo &session_info,
                  schema::ObSchemaGetterGuard &schema_guard,
                  ObExecEnv &exec_env,
                  ObPLCompileUnitAST &unit_ast, ObPLCompileUnit &unit,
                  char *buf, const int64_t len, int64_t &pos,
                  int8_t &level, int16_t &id);

  int gen_routine_storage_dml(const uint64_t exec_tenant_id,
                              ObDMLSqlSplicer &dml,
                              int64_t merge_version,
                              const ObString &binary,
                              const ObString &stack_sizes);

  template<typename DependencyTable>
  static int check_dep_schema(ObSchemaGetterGuard &schema_guard,
                              const DependencyTable &dep_schema_objs,
                              int64_t merge_version,
                              bool &match);

  int read_dll_from_disk(ObSQLSessionInfo *session_info,
                          schema::ObSchemaGetterGuard &schema_guard,
                          ObExecEnv &exec_env,
                          ObPLCompileUnitAST &unit_ast,
                          ObPLCompileUnit &unit,
                          ObRoutinePersistentInfo::ObPLOperation &op);

  int insert_or_update_dll_to_disk(schema::ObSchemaGetterGuard &schema_guard,
                                   const ObString &binary,
                                   const ObString &stack_sizes,
                                   const ObRoutinePersistentInfo::ObPLOperation op);

  int process_storage_dll(ObIAllocator &alloc,
                            schema::ObSchemaGetterGuard &schema_guard,
                            ObPLCompileUnit &unit,
                            const ObRoutinePersistentInfo::ObPLOperation op);

  static int has_same_name_dependency_with_public_synonym(
                            schema::ObSchemaGetterGuard &schema_guard,
                            const ObPLDependencyTable &dep_schema_objs,
                            bool& exist,
                            ObSQLSessionInfo &session_info);                  

  static int delete_dll_from_disk(common::ObISQLClient &trans,
                                  uint64_t tenant_id,
                                  uint64_t key_id,
                                  uint64_t database_id);

  static int get_stack_size_length(const ObPLCompileUnit &unit, int64_t &stack_size_length);
  static int encode_stack_sizes(char *buf, const int64_t len, int64_t &pos, const ObPLCompileUnit &unit);
  static int decode_stack_sizes(ObPLCompileUnit &unit, char *buf, const int64_t len, int64_t &pos);
  
private:
  uint64_t tenant_id_;
  uint64_t database_id_;
  uint64_t compile_db_id_;
  uint64_t key_id_;
  ObString arch_type_;

  ObArenaAllocator allocator_;
  uint64_t tenant_id_belongs_;
};

}

}
#endif
