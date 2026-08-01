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

#ifndef OCEANBASE_SRC_PL_OB_PL_H_

#define OCEANBASE_SRC_PL_OB_PL_H_
#include "lib/container/ob_bit_set.h"
#include "lib/container/ob_array.h"
#include "lib/container/ob_se_array.h"
#include "lib/container/ob_2d_array.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/rc/context.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/oblog/ob_log_module.h"
#include "sql/engine/expr/ob_expr_res_type.h"
#include "share/ob_errno.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/session/ob_basic_session_info.h"
#include "sql/parser/parse_node.h"
#include "pl/parser/parse_stmt_node.h"
#include "pl/ob_pl_type.h"
#include "pl/ob_pl_package_manager.h"
#include "pl/ob_pl_interface_pragma.h"
#include "sql/plan_cache/ob_cache_object_factory.h"
#include "pl/pl_cache/ob_pl_cache.h"
#include "pl/pl_cache/ob_pl_cache_object.h"
#include "pl/ob_pl_allocator.h"

namespace test
{
class MockCacheObjectFactory;
}
namespace oceanbase
{

namespace sql
{
class ObPsCache;
class ObSQLSessionInfo;
class ObAlterRoutineResolver;
}
using common::ObPsStmtId;
namespace pl
{
typedef common::ObFixedBitSet<128> ObPLFlag;
typedef void* ObPointer;
typedef common::ParamStore ParamStore;

class ObPLCacheCtx;
class ObPLAllocator1;


enum ObPLObjectType
{
  INVALID_OBJECT_TYPE = -1,
  TABLE,
  PL,
  PACKAGE_SPEC
};

class ObPLINS
{
public:
  ObPLINS() {}
  virtual ~ObPLINS() {}

  virtual int get_user_type(uint64_t type_id,
                            const ObUserDefinedType *&user_type,
                            ObIAllocator *allocator = NULL) const = 0;

  virtual int get_size(ObPLTypeSize type,
                       const ObPLDataType &pl_type,
                       int64_t &size,
                       ObIAllocator *allocator = NULL) const;
  virtual int get_element_data_type(const ObPLDataType &pl_type,
                        ObDataType &elem_type,
                        ObIAllocator *allocator = NULL) const;
  virtual int get_not_null(const ObPLDataType &pl_type,
                                     bool &not_null,
                                     ObIAllocator *allocator = NULL) const;
  virtual int init_complex_obj(ObIAllocator &allocator,
                               ObIAllocator &expr_allocator,
                               const ObPLDataType &pl_type,
                               common::ObObjParam &obj,
                               bool set_allocator = false,
                               bool set_null = true);

  virtual int calc_expr(uint64_t package_id, int64_t expr_idx, ObObjParam &result);
};

class ObPLFunctionBase
{
  static const int64_t CONTAINS_DYNAMIC_SQL = 2; // PS
  static const int64_t MULTI_RESULTS = 3; // SELECT
  static const int64_t HAS_COMMIT_OR_ROLLBACK = 4; // DDL
  static const int64_t HAS_SET_AUTOCOMMIT_STMT = 5; // SET AUTOCOMMIT
  static const int64_t RESERVED_LEGACY_FLAG_6 = 6;
  static const int64_t RESERVED_LEGACY_FLAG_7 = 7;
  static const int64_t RESERVED_LEGACY_FLAG_8 = 8;
  static const int64_t RESERVED_LEGACY_FLAG_9 = 9;

public:
  ObPLFunctionBase()
  : proc_type_(INVALID_PROC_TYPE),
    routine_id_(common::OB_INVALID_ID),
    package_id_(common::OB_INVALID_ID),
    database_id_(common::OB_INVALID_ID),
    package_version_(common::OB_INVALID_VERSION),
    owner_(common::OB_INVALID_ID),
    priv_user_(common::OB_INVALID_ID),
    gmt_create_(0),
    ret_type_(common::ObNullType),
    arg_count_(0),
    flag_() {}
  virtual ~ObPLFunctionBase() {}

public:
  inline ObProcType get_proc_type() const { return proc_type_; }
  inline void set_proc_type(ObProcType type) { proc_type_ = type; }
  inline uint64_t get_routine_id() const { return routine_id_; }
  inline uint64_t get_package_id() const { return package_id_; }
  inline void set_routine_id(uint64_t routine_id) { routine_id_ = routine_id; }
  inline void set_package_id(uint64_t package_id) { package_id_ = package_id; }
  inline uint64_t get_database_id() const { return database_id_; }
  inline void set_database_id(uint64_t database_id) { database_id_ = database_id; }
  inline uint64_t get_package_version() const { return package_version_; }
  inline void set_package_version(uint64_t package_version) { package_version_ = package_version; }
  inline uint64_t get_owner() const { return owner_; }
  inline void set_owner(uint64_t owner) { owner_ = owner; }
  inline uint64_t get_gmt_create() const { return gmt_create_; }
  inline void set_gmt_create(uint64_t gmt_create) { gmt_create_ = gmt_create; }
  inline int64_t get_arg_count() const { return arg_count_; }
  inline void set_arg_count(int64_t arg_count) { arg_count_ = arg_count; }
  inline const ObPLFlag &get_flag() const { return flag_; }
  inline int add_members(const ObPLFlag &flag) { return flag_.add_members(flag); }

  inline void add_flag(int64_t index) { flag_.add_member(index); }
  inline bool has_flag(int64_t index) const { return flag_.has_member(index); }

  inline void set_contain_dynamic_sql() { flag_.add_member(CONTAINS_DYNAMIC_SQL); }
  inline bool get_contain_dynamic_sql() const { return flag_.has_member(CONTAINS_DYNAMIC_SQL); }
  inline void set_multi_results() { flag_.add_member(MULTI_RESULTS); }
  inline bool get_multi_results() const { return flag_.has_member(MULTI_RESULTS); }
  inline void set_has_commit_or_rollback() { flag_.add_member(HAS_COMMIT_OR_ROLLBACK); }
  inline bool get_has_commit_or_rollback() const { return flag_.has_member(HAS_COMMIT_OR_ROLLBACK); }
  inline void set_has_set_autocommit_stmt() { flag_.add_member(HAS_SET_AUTOCOMMIT_STMT); }
  inline bool get_has_set_autocommit_stmt() const { return flag_.has_member(HAS_SET_AUTOCOMMIT_STMT); }
  inline const ObPLDataType &get_ret_type() const { return ret_type_; }
  inline void set_ret_type(const ObPLDataType &ret_type) { ret_type_ = ret_type; }
  inline int set_ret_type_info(const common::ObIArray<common::ObString>& type_info, ObPLEnumSetCtx *enum_set_ctx)
  {
    ret_type_.set_enum_set_ctx(enum_set_ctx);
    return ret_type_.set_type_info(type_info);
  }
  inline bool is_function()
  {
    return STANDALONE_FUNCTION == proc_type_
          || PACKAGE_FUNCTION == proc_type_
          || NESTED_FUNCTION == proc_type_;
  }
private:
  //Basic information
  ObProcType proc_type_;
  uint64_t routine_id_;
  uint64_t package_id_;
  uint64_t database_id_;
  uint64_t package_version_;
  uint64_t owner_;
  uint64_t priv_user_;
  int64_t gmt_create_;
  ObPLDataType ret_type_;
  int64_t arg_count_; //Parameters must be at the very front of the symbol table
  ObPLFlag flag_;

  DISALLOW_COPY_AND_ASSIGN(ObPLFunctionBase);
};

class ObPLExecutableUnit : public ObPLCacheObject
{
  friend class ::test::MockCacheObjectFactory;
public:
  ObPLExecutableUnit(sql::ObLibCacheNameSpace ns, lib::MemoryContext &mem_context);
  virtual ~ObPLExecutableUnit();

  inline bool get_can_cached() { return can_cached_; }
  inline void set_can_cached(bool can_cached) { can_cached_ = can_cached; }
  inline bool has_incomplete_rt_dep_error() { return has_incomplete_rt_dep_error_; }
  inline void set_has_incomplete_rt_dep_error(bool has_incomplete_rt_dep_error) { has_incomplete_rt_dep_error_ = has_incomplete_rt_dep_error; }
  inline const ObIArray<ObPLFunction*> &get_routine_table() const { return routine_table_; }
  inline ObIArray<ObPLFunction*> &get_routine_table() { return routine_table_; }
  inline int set_routine_table(ObIArray<ObPLFunction*> &table)
  {
    return routine_table_.assign(table);
  }
  int add_routine(ObPLFunction *routine);
  int get_routine(int64_t routine_idx, ObPLFunction *&routine) const;
  void init_routine_table(int64_t count) { routine_table_.set_capacity(static_cast<uint32_t>(count)); }
  inline const ObIArray<ObUserDefinedType *> &get_type_table() const { return type_table_; }

  inline ObPLEnumSetCtx & get_enum_set_ctx() { return enum_set_ctx_; }

  inline const sql::ObExecEnv &get_exec_env() const { return exec_env_; }
  inline sql::ObExecEnv &get_exec_env() { return exec_env_; }
  inline void set_exec_env(const sql::ObExecEnv &env) { exec_env_ = env; }

  virtual void reset();
  virtual void dump_deleted_log_info(const bool is_debug_log = true) const;
  virtual int check_need_add_cache_obj_stat(ObILibCacheCtx &ctx, bool &need_real_add);

  TO_STRING_KV(K_(routine_table),
               K_(can_cached),
               K_(runtime_schema_version),
               K_(sys_schema_version),
               K_(stat));

protected:

  common::ObFixedArray<ObPLFunction*, common::ObIAllocator> routine_table_;
  common::ObArray<ObUserDefinedType *> type_table_;

  pl::ObPLEnumSetCtx enum_set_ctx_;

  bool can_cached_;
  bool has_incomplete_rt_dep_error_;
  sql::ObExecEnv exec_env_;

  DISALLOW_COPY_AND_ASSIGN(ObPLExecutableUnit);
};

class ObPLSymbolTable;
class ObPLFunctionAST;

class ObPLFunction : public ObPLFunctionBase, public ObPLExecutableUnit
{
public:
  ObPLFunction(lib::MemoryContext &mem_context)
  : ObPLFunctionBase(), ObPLExecutableUnit(sql::ObLibCacheNameSpace::NS_PRCR, mem_context),
    variables_(allocator_),
    default_idxs_(allocator_),
    in_args_(),
    out_args_(),
    ast_(NULL),
    is_all_sql_stmt_(true),
    is_invoker_right_(false),
    function_name_(),
    has_parallel_affect_factor_(false) { }
  virtual ~ObPLFunction() = default;

  inline const common::ObIArray<ObPLDataType> &get_variables() const { return variables_; }
  inline const common::ObIArray<int64_t> &get_default_idxs() const { return default_idxs_; }
  inline sql::ObSqlExpression* get_default_expr(int64_t param)
  {
    int64_t idx = param >= 0 && param < default_idxs_.count() ? default_idxs_.at(param) : -1;
    return idx >= 0 && idx < expressions_.count() ? expressions_.at(idx) : NULL;
  }
  int set_variables(const ObPLSymbolTable &symbol_table);
  int set_types(const ObPLUserTypeTable &type_table);
  inline const common::ObBitSet<common::OB_DEFAULT_BITSET_SIZE> &get_in_args() const { return in_args_; }
  inline void set_in_args(const common::ObBitSet<common::OB_DEFAULT_BITSET_SIZE> &out_idx) { in_args_ = out_idx; }
  inline int add_in_arg(int64_t i) { return in_args_.add_member(i); }
  inline const common::ObBitSet<common::OB_DEFAULT_BITSET_SIZE> &get_out_args() const { return out_args_; }
  inline void set_out_args(const common::ObBitSet<common::OB_DEFAULT_BITSET_SIZE> &out_idx) { out_args_ = out_idx; }
  inline int add_out_arg(int64_t i) { return out_args_.add_member(i); }
  // Resolved AST retained for the tree-walking interpreter (non-owning: lives on
  // this func's allocator / the test's scope).
  inline ObPLFunctionAST *get_ast() const { return ast_; }
  inline void set_ast(ObPLFunctionAST *ast) { ast_ = ast; }

  inline bool get_is_all_sql_stmt() const { return is_all_sql_stmt_; }
  inline void set_is_all_sql_stmt(bool is_all_sql_stmt) { is_all_sql_stmt_ = is_all_sql_stmt; }

  inline bool is_invoker_right() const { return is_invoker_right_; }
  inline void set_invoker_right() { is_invoker_right_ = true; }

  inline bool get_has_parallel_affect_factor() const { return has_parallel_affect_factor_; }
  inline void set_has_parallel_affect_factor(bool value) { has_parallel_affect_factor_ = value; }

  int get_subprogram(const ObIArray<int64_t> &path, ObPLFunction *&routine) const;

  inline const common::ObString &get_function_name() const { return function_name_; }
  inline const common::ObString &get_package_name() const { return package_name_; }
  inline const common::ObString &get_database_name() const { return database_name_; }
  inline const common::ObString &get_priv_user() const { return priv_user_; }
  int set_function_name(const ObString &function_name)
  {
    return ob_write_string(get_allocator(), function_name, function_name_);
  }
  int set_package_name(const ObString &package_name)
  {
    return ob_write_string(get_allocator(), package_name, package_name_);
  }
  int set_database_name(const ObString &database_name)
  {
    return ob_write_string(get_allocator(), database_name, database_name_);
  }
  int set_priv_user(const ObString &priv_user)
  {
    return ob_write_string(get_allocator(), priv_user, priv_user_);
  }

  TO_STRING_KV(K_(ns),
               K_(ref_count),
               K_(runtime_schema_version),
               K_(sys_schema_version),
               K_(object_id),
               K_(dependency_tables),
               K_(params_info),
               K_(variables),
               K_(default_idxs),
               K_(function_name),
               K_(priv_user),
               K_(stat));

private:
  //symbol table information
  common::ObFixedArray<ObPLDataType, common::ObIAllocator> variables_; //Generated from the global symbol table of ObPLSymbolTable, all input and output parameters and all variables used within the PL body
  common::ObFixedArray<int64_t, common::ObIAllocator> default_idxs_;
  common::ObBitSet<common::OB_DEFAULT_BITSET_SIZE> in_args_;
  common::ObBitSet<common::OB_DEFAULT_BITSET_SIZE> out_args_;
  ObPLFunctionAST *ast_;  // retained resolved AST for the interpreter (non-owning)
  bool is_all_sql_stmt_;
  bool is_invoker_right_;

  common::ObString function_name_;
  common::ObString package_name_;
  common::ObString database_name_;
  common::ObString priv_user_;
  bool has_parallel_affect_factor_;

  DISALLOW_COPY_AND_ASSIGN(ObPLFunction);
};

struct ObPLExecRecursionCtx
{
public:
  ObPLExecRecursionCtx() : init_(false), max_recursion_depth_(0), recursion_depth_map_() {}
  ~ObPLExecRecursionCtx() {}
  int init(sql::ObSQLSessionInfo &session_info);
  int inc_and_check_depth(uint64_t package_id, uint64_t proc_id, bool is_function);
  int dec_and_check_depth(uint64_t package_id, uint64_t proc_id);
private:
  static const int64_t RECURSION_ARRAY_SIZE = 4;
  static const int64_t RECURSION_MAP_SIZE = 10;
private:
  bool init_;
  int64_t max_recursion_depth_;
  // To avoid hash map initialize cost, we store recursion depth in array
  // if routines less than RECURSION_ARRAY_SIZE.
  common::ObSEArray<std::pair<std::pair<uint64_t, uint64_t>, int64_t>, RECURSION_MAP_SIZE> recursion_depth_array_;
  common::hash::ObHashMap<std::pair<uint64_t, uint64_t>, int64_t> recursion_depth_map_;
};

struct ObPLSqlCodeInfo
{
public:
  ObPLSqlCodeInfo() : sqlcode_(OB_SUCCESS), sqlmsg_(), is_caught_error_(false) { sqlstate_[0] = '\0'; }
  inline void set_sqlcode(int sqlcode, const ObString &sqlmsg = ObString(""))
  {
    sqlcode_ = sqlcode;
    sqlmsg_ = sqlmsg;
  }
  inline void reset()
  {
    sqlcode_ = OB_SUCCESS;
    sqlmsg_ = ObString("");
    sqlstate_[0] = '\0';
    is_caught_error_ = false;
    stakced_warning_buff_.reset();
  }
  inline void set_sqlmsg(const ObString &sqlmsg) { sqlmsg_ = sqlmsg; }
  inline int get_sqlcode() const { return sqlcode_; }
  inline const ObString& get_sqlmsg() const { return sqlmsg_; }
  // The diagnostic-area sqlstate. Persisted here (alongside sqlcode_/sqlmsg_) because the TSI
  // warning buffer's sqlstate is wiped during error propagation while errno+message survive,
  // so a bare RESIGNAL / GET DIAGNOSTICS must still recover the originally raised sqlstate.
  inline void set_sqlstate(const char *ss)
  {
    if (OB_NOT_NULL(ss)) { snprintf(sqlstate_, sizeof(sqlstate_), "%s", ss); }
    else { sqlstate_[0] = '\0'; }
  }
  inline const char *get_sqlstate() const { return sqlstate_; }
  // Severity of the condition the currently-running handler caught: true if it was an error
  // (entered via the error-handler search), false if a warning (the raised-warning search). A
  // bare RESIGNAL re-raises with the original severity -- a warning-class sqlstate (01000) on an
  // error-severity condition (strict-mode 1265) must re-raise as an error, not downgrade.
  inline void set_caught_error(bool v) { is_caught_error_ = v; }
  inline bool is_caught_error() const { return is_caught_error_; }
  inline common::ObIArray<ObWarningBuffer>& get_stack_warning_buf()
  {
    return stakced_warning_buff_;
  }
private:
  int sqlcode_;
  ObString sqlmsg_;
  char sqlstate_[8];
  bool is_caught_error_;
  common::ObSEArray<ObWarningBuffer, 4> stakced_warning_buff_;
};

struct ObPLException;
struct ObPLCtx
{
public:
  ObPLCtx() {}
  ~ObPLCtx();
  int add(ObObj &obj) {
    return objects_.push_back(obj);
  }
  void clear() {
    objects_.reset();
  }
  void reset_obj();
  void reset_obj_range_to_end(int64_t index);
  common::ObIArray<ObObj>& get_objects() { return objects_; }
private:
  // Used to collect Allocators used during PL execution,
  // Some data has a lifecycle for the entire execution period, so it cannot be released immediately after PL execution is complete, such as OUT parameters
  // The Allocator here needs to ensure allocation by ObExecContext->allocator,
  // This is to ensure that the pointer is valid when the allocator here is released
  ObSEArray<ObObj, 32> objects_;
};

class ObPLPackageGuard;

#define IDX_PLEXECCTX_VTABLE 0
#define IDX_PLEXECCTX_ALLOCATOR 1
#define IDX_PLEXECCTX_CTX 2
#define IDX_PLEXECCTX_PARAMS 3
#define IDX_PLEXECCTX_RESULT 4
#define IDX_PLEXECCTX_STATUS 5
#define IDX_PLEXECCTX_FUNC 6
#define IDX_PLEXECCTX_IN 7
#define IDX_PLEXECCTX_PL_CTX 8

struct ObPLExecCtx : public ObPLINS
{
  ObPLExecCtx(common::ObIAllocator *allocator,
              sql::ObExecContext *exec_ctx,
              ParamStore *params,
              common::ObObj *result,
              int *status,
              ObPLFunction *func,
              bool in_function = false,
              ObPLPackageGuard *guard = NULL) :
      allocator_(allocator), exec_ctx_(exec_ctx), params_(params),
      result_(result), status_(status), func_(func),
      in_function_(in_function), pl_ctx_(NULL), guard_(guard),
      local_expr_alloc_("PLBlockExpr", OB_MALLOC_NORMAL_BLOCK_SIZE)
  {
    if (NULL != exec_ctx && NULL != exec_ctx_->get_my_session()) {
      pl_ctx_ = exec_ctx_->get_my_session()->get_pl_context();
    }
  }

  static uint32_t allocator_offset_bits() { return offsetof(ObPLExecCtx, allocator_) * 8; }
  static uint32_t exec_ctx_offset_bits() { return offsetof(ObPLExecCtx, exec_ctx_) * 8; }
  static uint32_t params_offset_bits() { return offsetof(ObPLExecCtx, params_) * 8; }
  static uint32_t result_offset_bits() { return offsetof(ObPLExecCtx, result_) * 8; }
  static uint32_t pl_ctx_offset_bits() { return offsetof(ObPLExecCtx, pl_ctx_) * 8; }

  bool valid();

  ObIAllocator *get_top_expr_allocator() { return &local_expr_alloc_; }

  virtual int get_user_type(uint64_t type_id,
                            const ObUserDefinedType *&user_type,
                            ObIAllocator *allocator = NULL) const;
  virtual int calc_expr(uint64_t package_id, int64_t expr_idx, ObObjParam &result);
  void set_is_sensitive(bool is_sensitive) const
  {
    if (OB_NOT_NULL(exec_ctx_) && OB_NOT_NULL(exec_ctx_->get_sql_ctx())) {
      exec_ctx_->get_sql_ctx()->is_sensitive_ = is_sensitive;
    }
  }

  common::ObIAllocator *allocator_; // Symbol Allocator
  sql::ObExecContext *exec_ctx_;
  ParamStore *params_; // param store, corresponding to the symbol table of PL Function
  common::ObObj *result_;
  int *status_; // SQL errors in PL will directly throw exceptions and will not reach the normal return value of the function to return an error code, so the error code needs to be recorded here
  ObPLFunction *func_; // corresponds to the func_ of the context to be executed
  bool in_function_; // Record whether the current state is inside a function
  ObPLContext *pl_ctx_; // for error stack
  ObPLPackageGuard *guard_; //corresponding package_guard for this execution
  ObArenaAllocator local_expr_alloc_;
};

// backup and restore ObExecContext attributes
struct ExecCtxBak
{
#define PL_EXEC_CTX_BAK_ATTRS phy_plan_ctx_,expr_op_ctx_store_,expr_op_size_,has_non_trivial_expr_op_ctx_,frames_,frame_cnt_,pl_expr_allocator_

#define DEF_BACKUP_ATTR(x) typeof(sql::ObExecContext::x) x = 0
  LST_DO_CODE(DEF_BACKUP_ATTR, EXPAND(PL_EXEC_CTX_BAK_ATTRS));
#undef DEF_BACKUP_ATTR

  void backup(sql::ObExecContext &ctx);
  void restore(sql::ObExecContext &ctx);
};

class ObPLContext;
class ObPLExecState
{
public:
  ObPLExecState(common::ObIAllocator &in_allocator,
                common::ObIAllocator &allocator,
                sql::ObExecContext &ctx,
                ObPLPackageGuard &guard,
                ObPLFunction &func,
                common::ObObj &result,
                int &status,
                bool top_call = false,
                bool inner_call = false,
                bool in_function = false,
                uint64_t loc = 0,
                bool is_called_from_sql = false) :
    func_(func),
    phy_plan_ctx_(in_allocator),
    eval_ctx_(ctx),
    result_(result),
    ctx_(&allocator,
         &ctx,
         &phy_plan_ctx_.get_param_store_for_update(),
         &result,
         &status,
         &func_,
         in_function,
         &guard),
    inner_call_(inner_call),
    top_call_(top_call),
    need_reset_physical_plan_(false),
    top_context_(NULL),
    loc_(loc),
    is_called_from_sql_(is_called_from_sql),
    pure_sql_exec_time_(0),
    pure_plsql_exec_time_(0),
    pure_sub_plsql_exec_time_(0),
    need_free_()
  { }
  virtual ~ObPLExecState();

  int init(const ParamStore *params = NULL, bool is_anonymous = false);
  int defend_stored_routine_change(const ObObjParam &actual_param, const ObPLDataType &formal_param_type);
  int check_routine_param_legal(ParamStore *params = NULL);
  int check_anonymous_collection_compatible(const ObPLComposite &composite, const ObPLDataType &dest_type, bool &need_cast);
  int convert_composite(ObObjParam &param, const ObPLDataType &dest_type);
  int init_params(const ParamStore *params = NULL, bool is_anonymous = false);
  int execute();
  int final(int ret);
  int deep_copy_result_if_need(common::ObIAllocator &allocator);
  int init_complex_obj(common::ObIAllocator &allocator, const ObPLDataType &pl_type, common::ObObjParam &obj, bool set_null = true);
  inline const common::ObObj &get_result() const { return result_; }
  inline common::ObIAllocator *get_allocator() { return ctx_.allocator_; }
  inline const sql::ObPhysicalPlanCtx &get_physical_plan_ctx() const { return phy_plan_ctx_; }
  inline sql::ObPhysicalPlanCtx &get_physical_plan_ctx() { return phy_plan_ctx_; }
  inline const ParamStore &get_params() const { return phy_plan_ctx_.get_param_store(); }
  inline ParamStore &get_params() { return phy_plan_ctx_.get_param_store_for_update(); }
  ObPLFunction &get_function() { return func_; }
  int get_var(int64_t var_idx, ObObjParam& result);
  int set_var(int64_t var_idx, const ObObjParam& value);
  ObPLExecCtx& get_exec_ctx() { return ctx_; }
  int check_pl_execute_priv(ObSchemaGetterGuard &guard,
                                          const uint64_t user_id,
                                          const ObSchemaObjVersion &schema_obj,
                                          const ObIArray<uint64_t> &role_id_array);

  inline bool is_top_call() const { return top_call_; }
  inline uint64_t get_loc() const { return loc_; }
  inline void set_loc(uint64_t loc) { loc_ = loc; }

  inline uint64_t get_current_line() { return (get_loc() >> 32) + 1; }

  inline void set_current_line(int64_t current_line)
  {
    current_line = (current_line > 0) ? (current_line - 1) : current_line;
    set_loc((current_line) << 32 | (get_loc() & 0x00000000ffffffff));
  }

  inline bool is_called_from_sql() const { return is_called_from_sql_; }
  inline void set_is_called_from_sql(bool flag) { is_called_from_sql_ = flag; }
  inline bool is_for_trigger() const { return ObTriggerInfo::is_trigger_package_id(func_.get_package_id());}

  inline void add_pure_sql_exec_time(int64_t sql_exec_time)
  {
    pure_sql_exec_time_ += sql_exec_time;
  }
  inline void reset_pure_sql_exec_time() { pure_sql_exec_time_ = 0; }

  int64_t get_pure_sql_exec_time() { return pure_sql_exec_time_; }

  int add_pl_exec_time(int64_t pl_exec_time, bool is_called_from_sql);

  void reset_plsql_exec_time() { pure_plsql_exec_time_ = 0; }
  void add_plsql_exec_time(int64_t plsql_exec_time) { pure_plsql_exec_time_ = plsql_exec_time; }
  int64_t get_plsql_exec_time() { return pure_plsql_exec_time_; }
  void add_sub_plsql_exec_time(int64_t sub_plsql_exec_time) { pure_sub_plsql_exec_time_ += sub_plsql_exec_time; }
  int64_t get_sub_plsql_exec_time() { return pure_sub_plsql_exec_time_; }
  void reset_sub_plsql_exec_time() { pure_sub_plsql_exec_time_ = 0; }

  bool need_free_arg(int64_t i)
  {
    return need_free_.count() > i ? need_free_.at(i) : false;
  }
  ObPLContext *get_top_pl_context() { return top_context_; }
  ExecCtxBak &get_exec_ctx_bak() { return self_exec_ctx_bak_; }

  TO_STRING_KV(K_(inner_call),
               K_(top_call),
               K_(need_reset_physical_plan),
               K_(loc),
               K_(is_called_from_sql),
               K_(pure_sql_exec_time),
               K_(pure_plsql_exec_time),
               K_(pure_sub_plsql_exec_time));
private:
  ObPLFunction &func_;
  sql::ObPhysicalPlanCtx phy_plan_ctx_; //The runtime param values are stored here, corresponding one-to-one with variables_ in ObPLFunction, default values need to be set during initialization
  sql::ObEvalCtx eval_ctx_;
  common::ObObj &result_;
  ObPLExecCtx ctx_;
  bool inner_call_;
  bool top_call_;

  ExecCtxBak exec_ctx_bak_;
  ExecCtxBak self_exec_ctx_bak_;
  bool need_reset_physical_plan_;

  ObPLContext *top_context_;
  uint64_t loc_; // combine of line and column number
  bool is_called_from_sql_;
  int64_t pure_sql_exec_time_;
  int64_t pure_plsql_exec_time_;
  int64_t pure_sub_plsql_exec_time_;
  common::ObSEArray<bool,8> need_free_;
};

class ObPLCallStackTrace;
class ObPLContext
{
  friend class LinkPLStackGuard;
public:
  ObPLContext() 
      { reset(); }
  virtual ~ObPLContext() { reset(); }
  void reset()
  {
    inc_recursion_depth_ = false;
    reset_autocommit_ = false;
    has_stash_savepoint_ = false;
    has_implicit_savepoint_ = false;
    is_top_stack_ = false;
    exception_handler_illegal_ = false;
    need_reset_exec_env_ = false;
    database_id_ = OB_INVALID_ID;
    need_reset_default_database_ = false;
    session_info_ = NULL;
    exec_stack_.reset();
    need_reset_role_id_array_ = false;
    old_role_id_array_.reset();
    old_priv_user_id_ = OB_INVALID_ID;
    old_in_definer_ = false;
    has_output_arguments_ = false;
    old_worker_timeout_ts_ = 0;
    old_phy_plan_timeout_ts_ = 0;
    parent_stack_ctx_ = nullptr;
    top_stack_ctx_ = nullptr;
    my_exec_ctx_ = nullptr;
    cur_query_.reset();
    is_function_or_trigger_ = false;
    last_insert_id_ = 0;
    trace_id_.reset();
    old_user_priv_set_ = OB_PRIV_SET_EMPTY;
    old_db_priv_set_ = OB_PRIV_SET_EMPTY;
    is_inner_mock_ = false;
  }

  int is_inited() { return session_info_ != NULL; }

  int init(sql::ObSQLSessionInfo &session_info,
           sql::ObExecContext &ctx,
           ObPLFunction *routine,
           bool is_function_or_trigger,
           ObIAllocator *allocator = NULL);
  void destory(sql::ObSQLSessionInfo &session_info, sql::ObExecContext &ctx, int &ret);

  inline ObPLCursorInfo& get_cursor_info() { return cursor_info_; }
  inline ObPLSqlCodeInfo& get_sqlcode_info() { return sqlcode_info_; }
  inline bool has_implicit_savepoint() { return has_implicit_savepoint_; }
  inline void clear_implicit_savepoint() { has_implicit_savepoint_ = false; }
  inline void set_has_implicit_savepoint(bool v) { has_implicit_savepoint_ = v; }

  bool is_top_stack() { return is_top_stack_; }

  inline bool is_exception_handler_illegal() const { return exception_handler_illegal_; }
  inline void set_exception_handler_illegal() { exception_handler_illegal_ = true; }
  inline void set_reset_autocommit() { reset_autocommit_ = true; }
  inline bool get_reset_autocommit() const { return reset_autocommit_; }

  static int valid_execute_context(sql::ObExecContext &ctx);
  static int check_routine_legal(ObPLFunction &routine, bool in_function, bool in_tg);

  static int get_exec_state_from_local(sql::ObSQLSessionInfo &session_info,
                                    int64_t package_id,
                                    int64_t routine_id,
                                    ObPLExecState *&plstate);
  static int get_param_store_from_local(ObSQLSessionInfo &session_info,
                                        int64_t package_id,
                                        int64_t routine_id,
                                        ParamStore *&params);
  static int get_routine_from_local(sql::ObSQLSessionInfo &session_info,
                                    int64_t package_id,
                                    int64_t routine_id,
                                    ObPLFunction *&routine);
  static int get_subprogram_var_from_local(sql::ObSQLSessionInfo &session_info,
                                    int64_t package_id,
                                    int64_t routine_id,
                                    int64_t var_idx,
                                    ObObjParam &result);
  static int set_subprogram_var_from_local(sql::ObSQLSessionInfo &session_info,
                                    int64_t package_id,
                                    int64_t routine_id,
                                    int64_t var_idx,
                                    const ObObjParam &value);
  int inc_and_check_depth(int64_t package_id, int64_t routine_id, bool is_function);
  void dec_and_check_depth(int64_t package_id, int64_t routine_id, int &ret, bool inner_call);

  int set_exec_env(ObPLFunction &routine);
  int set_default_database(ObPLFunction &routine, share::schema::ObSchemaGetterGuard &guard);
  void reset_exec_env(int &ret);
  void reset_default_database(int &ret);

  int set_role_id_array(ObPLFunction &routine, share::schema::ObSchemaGetterGuard &guard);
  void reset_role_id_array(int &ret);

  ObIArray<ObPLExecState *> &get_exec_stack() { return exec_stack_; }

  ObPLExecState *get_current_state()
  {
    return exec_stack_.empty() ? NULL : exec_stack_.at(exec_stack_.count() - 1);
  }
  ObPLFunction *get_current_routine()
  {
    return NULL == get_current_state() ? NULL : &get_current_state()->get_function();
  }
  ObPLExecCtx *get_current_ctx()
  {
    return NULL == get_current_state() ? NULL : &get_current_state()->get_exec_ctx();
  }
  bool has_output_arguments() { return has_output_arguments_; }
  void set_has_output_arguments(bool has_output_arguments)
  {
    has_output_arguments_ = has_output_arguments;
  }
  static int implicit_end_trans(
    sql::ObSQLSessionInfo &session, sql::ObExecContext &ctx, bool is_rollback, bool can_async = false);

  inline ObString get_database_name() const { return database_name_.string(); }
  inline uint64_t get_database_id() const { return database_id_; }
  inline bool is_function_or_trigger() const { return is_function_or_trigger_; }
  bool in_nested_sql_ctrl() const
  { return ObStmt::is_dml_stmt(my_exec_ctx_->get_sql_ctx()->stmt_type_); }
  pl::ObPLContext *get_parent_stack_ctx() { return parent_stack_ctx_; }
  pl::ObPLContext *get_top_stack_ctx() { return top_stack_ctx_; }
  sql::ObExecContext *get_my_exec_ctx() { return my_exec_ctx_; }
  ObCurTraceId::TraceId get_trace_id() const { return trace_id_; }
  void set_is_inner_mock(bool is_inner_mock) { is_inner_mock_ = is_inner_mock; }
  bool get_is_inner_mock() const { return is_inner_mock_; }


private:
  ObPLContext* get_stack_pl_ctx();
  void set_parent_stack_ctx(pl::ObPLContext *parent_stack_ctx)
  {
    parent_stack_ctx_ = parent_stack_ctx;
    top_stack_ctx_ = (parent_stack_ctx == nullptr) ? this : parent_stack_ctx->get_top_stack_ctx();
  }
  void set_my_exec_ctx(sql::ObExecContext *my_exec_ctx) { my_exec_ctx_ = my_exec_ctx; }
private:
  ObPLCursorInfo cursor_info_;
  ObPLSqlCodeInfo sqlcode_info_;
  ObPLExecRecursionCtx recursion_ctx_;
  bool inc_recursion_depth_;
  bool reset_autocommit_;
  bool has_stash_savepoint_;
  bool has_implicit_savepoint_;
  bool is_top_stack_;
  bool exception_handler_illegal_;

  sql::ObExecEnv exec_env_;
  bool need_reset_exec_env_;
  ObSqlString database_name_;
  uint64_t database_id_;
  common::ObSEArray<uint64_t, 8> old_role_id_array_;
  uint64_t old_priv_user_id_;
  ObPrivSet old_user_priv_set_;
  ObPrivSet old_db_priv_set_;
  bool old_in_definer_;
  bool need_reset_default_database_;
  bool need_reset_role_id_array_;

  bool has_output_arguments_;

  int64_t old_worker_timeout_ts_;
  int64_t old_phy_plan_timeout_ts_;

  sql::ObSQLSessionInfo *session_info_;

  common::ObString cur_query_;

  common::ObSEArray<ObPLExecState*, 4> exec_stack_;
  ObPLContext *parent_stack_ctx_;
  ObPLContext *top_stack_ctx_;
  sql::ObExecContext *my_exec_ctx_; //my exec context
  bool is_function_or_trigger_;
  uint64_t last_insert_id_;
  ObCurTraceId::TraceId trace_id_;
  bool is_inner_mock_;
};

class ObPL
{
  friend class sql::ObAlterRoutineResolver;
public:
  ObPL() :
    sql_proxy_(NULL),
    package_manager_(),
    interface_service_() {}
  virtual ~ObPL() {}

  int init(common::ObMySQLProxy &sql_proxy);
  void destory();

public:
  // for anonymous + ps
  int execute(sql::ObExecContext &ctx,
              ParamStore &params,
              uint64_t stmt_id,
              const common::ObString &sql,
              ObBitSet<OB_DEFAULT_BITSET_SIZE> &out_args);
  // for anonymous
  int execute(sql::ObExecContext &ctx,
              ParamStore &params,
              const ObStmtNodeTree *block);

  // for normal routine or package routine
  int execute(sql::ObExecContext &ctx,
              ObIAllocator &allocator,
              uint64_t package_id,
              uint64_t routine_id,
              const ObIArray<int64_t> &subprogram_path,
              ParamStore &params,
              common::ObObj &result,
              int *status = NULL,
              bool inner_call = false,
              bool in_function = false,
              uint64_t loc = 0,
              bool is_called_from_sql = false);
  int check_exec_priv(sql::ObExecContext &ctx,
                      const ObString &database_name,
                      ObPLFunction *routine);

private:
  // for normal routine
  int get_pl_function(sql::ObExecContext &ctx,
                      ObPLPackageGuard &package_guard,
                      int64_t package_id,
                      int64_t routine_id,
                      const ObIArray<int64_t> &subprogram_path,
                      ObCacheObjGuard& cacheobj_guard,
                      ObPLFunction *&local_routine);

  // for anonymous + ps
  int get_pl_function(sql::ObExecContext &ctx,
                      ParamStore &params,
                      uint64_t stmt_id,
                      const ObString &sql,
                      ObCacheObjGuard& cacheobj_guard);

  // for anonymous + ps
  int generate_pl_function(sql::ObExecContext &ctx,
                           const ObString &anonymouse_sql,
                           ParamStore &params,
                           ParseNode &node,
                           ObCacheObjGuard& cacheobj_guard,
                           const uint64_t stmt_id,
                           bool is_anonymous_text = false);

  // for inner common execute
  int execute(sql::ObExecContext &ctx,
              ObIAllocator &allocator,
              ObPLPackageGuard &package_guard,
              ObPLFunction &routine,
              ParamStore *params,
              ObObj *result,
              int *status = NULL,
              bool is_top_stack = false,
              bool is_inner_call = false,
              bool is_in_function = false,
              bool is_anonymous = false,
              uint64_t loc = 0,
              bool is_called_from_sql = false);

public:
  // for normal routine
  static int generate_pl_function(sql::ObExecContext &ctx,
                           uint64_t proc_id,
                           ObCacheObjGuard& cacheobj_guard);
  // add pl to cache
  static int add_pl_lib_cache(ObPLFunction *pl_func, ObPLCacheCtx &pc_ctx);
  static int execute_proc(ObPLExecCtx &ctx,
                          uint64_t package_id,
                          uint64_t proc_id,
                          int64_t *subprogram_path,
                          int64_t path_length,
                          uint64_t line_num, /* call position line number, for call_stack info*/
                          int64_t argc,
                          common::ObObjParam **argv);

  inline ObPLPackageManager &get_package_manager() { return package_manager_; }
  inline common::ObMySQLProxy *get_sql_proxy() { return sql_proxy_; }
  inline const ObPLInterfaceService &get_interface_service() const { return interface_service_; }
  static int insert_error_msg(int errcode);

  static int check_trigger_arg(ParamStore &params, const ObPLFunction &func);

  std::pair<common::ObBucketLock, common::ObBucketLock>& get_build_lock() { return build_lock_; }

  static int check_session_alive(const ObBasicSessionInfo &session);

private:
  common::ObMySQLProxy *sql_proxy_;
  ObPLPackageManager package_manager_;
  ObPLInterfaceService interface_service_;

  // first bucket is for deduplication, second bucket is for concurrency control
  std::pair<common::ObBucketLock, common::ObBucketLock> build_lock_;
};

class LinkPLStackGuard
{
public:
  LinkPLStackGuard(sql::ObExecContext &exec_ctx, ObPLContext &pl_stack)
    : exec_ctx_(exec_ctx),
      parent_stack_(nullptr)
  {
    //last_pl_stack means the last pl stack in all execution stack
    //such as: pl_stack1->sql_stack->pl_stack2, pl_stack2.last_pl_stack is pl_stack1
    ObPLContext *last_pl_stack = nullptr;
    //parent_stack means the direct parent pl stack in current execution stack
    //such as: pl_stack1->sql_stack->pl_stack2, pl_stack2.parent_stack is nullptr
    ObExecContext *cur_exec_ctx = &exec_ctx;
    while (cur_exec_ctx != nullptr && last_pl_stack == nullptr) {
      if (cur_exec_ctx->get_pl_stack_ctx() != nullptr) {
        last_pl_stack = cur_exec_ctx->get_pl_stack_ctx();
        if (cur_exec_ctx == &exec_ctx) {
          parent_stack_ = cur_exec_ctx->get_pl_stack_ctx();
        }
      } else {
        //find the parent execution stack
        cur_exec_ctx = cur_exec_ctx->get_parent_ctx();
      }
    }
    pl_stack.set_parent_stack_ctx(last_pl_stack);
    pl_stack.set_my_exec_ctx(&exec_ctx_);
    exec_ctx_.set_pl_stack_ctx(&pl_stack);
  }
  ~LinkPLStackGuard()
  {
    //pop the pl stack from current execution stack
    exec_ctx_.set_pl_stack_ctx(parent_stack_);
  }

private:
  sql::ObExecContext &exec_ctx_;
  ObPLContext *parent_stack_;
};

class ObPLASHGuard
{
public:
  enum ObPLASHStatus {
    INVALID_ASH_STATUS,
    IS_PLSQL_COMPILATION,
    IS_PLSQL_EXECUTION,
    IS_SQL_EXECUTION,
  };
  ObPLASHGuard(ObPLASHStatus status);
  ObPLASHGuard(int64_t package_id, int64_t routine_id);
  ObPLASHGuard(int64_t package_id, int64_t routine_id, const ObString &routine_name);
  ~ObPLASHGuard();
private:
  char plsql_current_subprogram_name_[common::OB_MAX_ASH_PL_NAME_LENGTH + 1];
  bool in_plsql_compilation_;
  bool in_plsql_execution_;
  int64_t plsql_entry_object_id_;
  int64_t plsql_entry_subprogram_id_;
  int64_t plsql_current_object_id_;
  int64_t plsql_current_subprogram_id_;
  bool set_entry_info_;
  bool set_entry_name_;
  bool set_current_name_;
  ObPLASHStatus pl_ash_status_;
public:
  inline bool is_set_entry_info() const {return set_entry_info_;}
};
}
}
#endif /* OCEANBASE_SRC_PL_OB_PL_H_ */
