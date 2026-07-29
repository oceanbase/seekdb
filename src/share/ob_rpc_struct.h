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

#ifndef OCEANBASE_RPC_OB_RPC_STRUCT_H_
#define OCEANBASE_RPC_OB_RPC_STRUCT_H_

#include "share/ob_fork_table_info.h"
#include "common/ob_role.h"
#include "common/ob_role_mgr.h"
#include "common/ob_common_types.h"
#include "common/ob_store_format.h"
#include "common/ob_tablet_id.h"
#include "share/ob_ddl_common.h"
#include "sql/resolver/ob_stmt_type.h"  // pure enum X-macro header, conf L2(base_stmt_type)
#include "share/ob_debug_sync.h"
#include "share/ob_simple_batch.h"
#include "share/ob_schema_version_info.h"
#include "share/session/ob_local_session_var.h"
#include "share/ob_version_parser.h"
#include "share/schema/ob_error_info.h"
#include "share/schema/ob_constraint.h"
#include "share/schema/ob_schema_service.h"
#include "share/schema/ob_objpriv_mysql_schema_struct.h"
#include "share/schema/ob_dependency_info.h"
#include "share/schema/ob_trigger_info.h"
#include "share/io/ob_io_calibration.h"
#include "sql/executor/ob_task_id.h"
#include "sql/plan_cache/ob_lib_cache_register.h"
#include "common/ob_item_type.h"
#include "ob_i_tablet_scan.h"
#include "storage/tablet/ob_tablet_create_delete_mds_user_data.h"  // ObTabletMdsUserDataType
#include "storage/tablelock/ob_table_lock_priority.h"  // conf L2
#include "storage/tx/ob_trans_id.h"  // conf L2
#include "share/ob_tablet_autoincrement_param.h"
#include "logservice/palf/log_define.h"//INVALID_PROPOSAL_ID
#include "share/config/ob_config.h" // ObConfigArray
#include "share/scn.h"//SCN
#include "share/resource_limit_calculator/ob_resource_limit_calculator.h"//ObUserResourceCalculateArg
#include "ob_ddl_args.h"
#include "share/inner_table/ob_load_inner_table_schema.h"
#include "share/ai_service/ob_ai_model_info.h"

namespace oceanbase
{
namespace storage
{
class ObCreateTabletSchema;
}
namespace obcall
{
// restore bare names explicitly after the include chain was cut(enum values only, avoid polluting the obrpc lexical scope with a using-directive)
using share::schema::CST_FK_VALIDATED;
using share::schema::GENERATED_TYPE_UNKNOWN;
using share::schema::ObCstFkValidateFlag;
using share::schema::ObNameGeneratedType;
using share::schema::ObTableSchema;
using share::schema::ObPartitionLevel;
using share::schema::ObSimpleTableSchemaV2;
using share::schema::PARTITION_LEVEL_MAX;
typedef common::ObSArray<common::ObAddr> ObServerList;
static const int64_t MAX_COUNT = 128;
static const int64_t OB_DEFAULT_ARRAY_SIZE = 8;
typedef common::ObFixedLengthString<common::OB_MAX_CLUSTER_NAME_LENGTH> ObClusterName;

enum ObDefaultRoleFlag
{
  OB_DEFUALT_NONE = 0,
  OB_DEFAULT_ROLE_LIST = 1,
  OB_DEFAULT_ROLE_ALL = 2,
  OB_DEFAULT_ROLE_ALL_EXCEPT = 3,
  OB_DEFAULT_ROLE_NONE = 4,
  OB_DEFAULT_ROLE_DEFAULT = 5,
  OB_DEFAULT_ROLE_MAX,
};
struct Bool
{
  OB_UNIS_VERSION(1);

public:
  Bool(bool v = false)
      : v_(v) {}

  operator bool () { return v_; }
  operator bool () const { return v_; }
  DEFINE_TO_STRING(BUF_PRINTO(v_));

private:
  bool v_;
};

struct Int64
{
  OB_UNIS_VERSION(1);

public:
  Int64(int64_t v = common::OB_INVALID_ID)
      : v_(v) {}

  inline void reset();
  bool is_valid() const { return true; }
  operator int64_t () { return v_; }
  operator int64_t () const { return v_; }
  DEFINE_TO_STRING(BUF_PRINTO(v_));

private:
  int64_t v_;
};

struct UInt64
{
  OB_UNIS_VERSION(1);

public:
  UInt64(uint64_t v = common::OB_INVALID_ID)
      : v_(v) {}

  operator uint64_t () { return v_; }
  operator uint64_t () const { return v_; }
  DEFINE_TO_STRING(BUF_PRINTO(v_));

private:
  uint64_t v_;
};

struct ObPartitionId
{
  OB_UNIS_VERSION(1);

public:
  int64_t table_id_;
  int64_t partition_id_;

  ObPartitionId() : table_id_(common::OB_INVALID_ID), partition_id_(common::OB_INVALID_INDEX) {}

  DECLARE_TO_STRING;
};

class ObLoadRuntimeTableSchemaArg
{
  OB_UNIS_VERSION(1);
public:
  int init(const uint64_t table_id, const ObIArray<share::ObLoadInnerTableSchemaInfo> *schema_infos,
      const ObIArray<int64_t> &insert_idx, const uint64_t data_version);
  int assign(const ObLoadRuntimeTableSchemaArg &arg);
  bool is_valid() const;
  ObLoadRuntimeTableSchemaArg() : table_id_(OB_INVALID_ID),
    data_version_(OB_INVALID_VERSION), insert_idx_() {}
  TO_STRING_KV(K_(table_id), KDV_(data_version), K_(insert_idx));

  uint64_t get_table_id() const { return table_id_; }
  const ObIArray<int64_t>& get_insert_idx() const { return insert_idx_; }
  uint64_t get_data_version() const { return data_version_; }
  const ObIArray<share::ObLoadInnerTableSchemaInfo> *get_infos() const
  {
    return reinterpret_cast<ObIArray<share::ObLoadInnerTableSchemaInfo> *>(schema_infos_);
  }
private:

  uint64_t table_id_;
  uint64_t data_version_;
  uint64_t schema_infos_;
  ObSArray<int64_t> insert_idx_;
};


struct ObModifySysVarArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObModifySysVarArg() : ObDDLArg(), is_inner_(false)
    { }
  DECLARE_TO_STRING;
  bool is_valid() const;
  virtual bool is_allow_when_disable_ddl() const { return true; }

  common::ObSArray<share::schema::ObSysVarSchema> sys_var_list_;
  bool is_inner_;
};

struct ObCreateDatabaseArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);

public:
  ObCreateDatabaseArg():
    ObDDLArg(),
    if_not_exist_(false)
  {}
  bool is_valid() const;
  DECLARE_TO_STRING;

  share::schema::ObDatabaseSchema database_schema_;
  //used to mark alter database options
  common::ObBitSet<> alter_option_bitset_;
  bool if_not_exist_;
};

struct ObAlterDatabaseArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  enum ModifiableOptions {
       CHARSET_TYPE = 1,
       COLLATION_TYPE,
       READ_ONLY,
       MAX_OPTION
  };

public:
  ObAlterDatabaseArg() : ObDDLArg()
    { }
  bool is_valid() const;
  DECLARE_TO_STRING;

  share::schema::ObDatabaseSchema database_schema_;
  //used to mark alter database options
  common::ObBitSet<> alter_option_bitset_;
};

struct ObDropDatabaseArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);

public:
  ObDropDatabaseArg():
    ObDDLArg(),
    if_exist_(false),
    to_recyclebin_(false),
    is_add_to_scheduler_(false)
  {}

  ObDropDatabaseArg &operator=(const ObDropDatabaseArg &other) = delete;
  ObDropDatabaseArg(const ObDropDatabaseArg &other) = delete;
  virtual ~ObDropDatabaseArg() = default;
  bool is_valid() const;
  DECLARE_TO_STRING;


  common::ObString database_name_;
  bool if_exist_;
  bool to_recyclebin_;
  bool is_add_to_scheduler_;
};

struct ObCreateVertialPartitionArg : ObDDLArg
{
  OB_UNIS_VERSION(1);

public:
  ObCreateVertialPartitionArg() :
      ObDDLArg(),
      vertical_partition_columns_()
  {}
  virtual ~ObCreateVertialPartitionArg()
  {}
  void reset()
  {
    vertical_partition_columns_.reset();
  }
  DECLARE_TO_STRING;

public:
  common::ObSEArray<common::ObString, 8> vertical_partition_columns_;
};


struct ObCheckFrozenScnArg
{
  OB_UNIS_VERSION(1);
public:
  ObCheckFrozenScnArg();
  virtual ~ObCheckFrozenScnArg() {}

  bool is_valid() const;
  TO_STRING_KV(K_(frozen_scn));
public:
  share::SCN frozen_scn_;
};

struct ObCreateIndexArg;//Forward declaration
struct ObCreateForeignKeyArg;//Forward declaration

struct ObCreateTableRes
{
  OB_UNIS_VERSION(1);

public:
  ObCreateTableRes() :
      table_id_(OB_INVALID_ID),
      schema_version_(OB_INVALID_VERSION),
      task_id_(0),
      do_nothing_(false)
  {}
  int assign(const ObCreateTableRes &other) {
    table_id_ = other.table_id_;
    schema_version_ = other.schema_version_;
    task_id_ = other.task_id_;
    do_nothing_ = other.do_nothing_;
    return common::OB_SUCCESS;
  }
  TO_STRING_KV(K_(table_id), K_(schema_version), K_(task_id), K_(do_nothing));
  uint64_t table_id_;
  int64_t schema_version_;
  int64_t task_id_;
  bool do_nothing_;
};

struct ObDropTableRes
{
  OB_UNIS_VERSION(1);

public:
  ObDropTableRes() :
    schema_version_(OB_INVALID_VERSION),
    task_id_(OB_INVALID_ID),
    do_nothing_(false)
  {}
  int assign(const ObDropTableRes &other) {
    schema_version_ = other.schema_version_;
    task_id_ = other.task_id_;
    do_nothing_ = other.do_nothing_;
    return common::OB_SUCCESS;
  }
  TO_STRING_KV(K_(schema_version), K_(task_id), K_(do_nothing));
  int64_t schema_version_;
  int64_t task_id_;
  bool do_nothing_;
};

struct ObCreateTableLikeArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObCreateTableLikeArg():
      ObDDLArg(),
      if_not_exist_(false),
      table_type_(share::schema::USER_TABLE),
      origin_db_name_(),
      origin_table_name_(),
      new_db_name_(),
      new_table_name_(),
      session_id_(0),
      define_user_id_(common::OB_INVALID_ID)
  {}
  bool is_valid() const;
  DECLARE_TO_STRING;

  bool if_not_exist_;

  share::schema::ObTableType table_type_;
  common::ObString origin_db_name_;
  common::ObString origin_table_name_;
  common::ObString new_db_name_;
  common::ObString new_table_name_;
  int64_t session_id_;
  uint64_t define_user_id_;
};

struct ObIndexArg : public ObDDLArg
{
  OB_UNIS_VERSION_V(1);
public:
  enum IndexActionType
  {
    INVALID_ACTION = 1,
    ADD_INDEX,
    DROP_INDEX,
    ALTER_INDEX,
    DROP_FOREIGN_KEY, // The foreign key is a 1.4 function, and rename_index needs to be placed at the back in consideration of compatibility
    RENAME_INDEX,
    ALTER_INDEX_PARALLEL,
    REBUILD_INDEX,
    ALTER_PRIMARY_KEY,
    ADD_PRIMARY_KEY,
    DROP_PRIMARY_KEY
  };

  static const char *to_type_str(const IndexActionType type)
  {
    const char *str = "";
    if (ADD_INDEX == type) {
      str = "add index";
    } else if (DROP_INDEX == type) {
      str = "drop index";
    } else if (ALTER_INDEX == type) {
      str = "alter index";
    } else if (DROP_FOREIGN_KEY == type) {
      str = "drop foreign key";
    } else if (RENAME_INDEX == type) {
      str = "rename index";
    } else if (ALTER_INDEX_PARALLEL == type) {
      str = "alter index parallel";
    } else if (REBUILD_INDEX == type) {
      str = "rebuild index";
    } else if (ALTER_PRIMARY_KEY == type) {
      str = "alter primary key";
    } else if (ADD_PRIMARY_KEY == type) {
      str = "add primary key";
    } else if (DROP_PRIMARY_KEY == type) {
      str = "drop primary key";
    }
    return str;
  }


  uint64_t session_id_; //The session id is passed in when building the index, and the table schema is searched by rs according to the temporary table and then the ordinary table.
  common::ObString index_name_;
  common::ObString table_name_;
  common::ObString database_name_;
  IndexActionType index_action_type_;
  share::SortCompactLevel compact_level_;
  ObIndexArg():
      ObDDLArg(),
      session_id_(common::OB_INVALID_ID),
      index_name_(),
      table_name_(),
      database_name_(),
      index_action_type_(INVALID_ACTION),
      compact_level_(share::SORT_COMPACT_LEVEL)
  {}
  virtual ~ObIndexArg() {}
  void reset()
  {

    session_id_ = common::OB_INVALID_ID;
    index_name_.reset();
    table_name_.reset();
    database_name_.reset();
    index_action_type_ = INVALID_ACTION;
    compact_level_ = share::SORT_COMPACT_LEVEL;
    ObDDLArg::reset();
  }
  bool is_valid() const;
  int assign(const ObIndexArg &other) {
    int ret = common::OB_SUCCESS;
    if (OB_FAIL(ObDDLArg::assign(other))) {
      SHARE_LOG(WARN, "assign ddl arg failed", K(ret));
    } else {

      session_id_ = other.session_id_;
      index_name_ = other.index_name_;
      table_name_ = other.table_name_;
      database_name_ = other.database_name_;
      index_action_type_ = other.index_action_type_;
      compact_level_ = other.compact_level_;
    }
    return ret;
  }

  DECLARE_VIRTUAL_TO_STRING;
};

struct ObUpdateStatCacheArg : public ObDDLArg
{
  OB_UNIS_VERSION_V(1);
public:
  ObUpdateStatCacheArg()
    : table_id_(common::OB_INVALID_ID),
      partition_ids_(),
      column_ids_(),
      no_invalidate_(false),
      update_system_stats_only_(false)
  {}
  virtual ~ObUpdateStatCacheArg() {}
  void rest()
  {
    table_id_ = common::OB_INVALID_ID,
    partition_ids_.reset();
    column_ids_.reset();
    no_invalidate_ = false;
    update_system_stats_only_ = false;
  }
  bool is_valid() const;
  int assign(const ObUpdateStatCacheArg &other) {
    int ret = common::OB_SUCCESS;

    table_id_ = other.table_id_;
    no_invalidate_ = other.no_invalidate_;
    update_system_stats_only_ = other.update_system_stats_only_;
    if (OB_FAIL(ObDDLArg::assign(other))) {
      SHARE_LOG(WARN, "fail to assign ddl arg", KR(ret));
    } else if (OB_FAIL(partition_ids_.assign(other.partition_ids_))) {
      SHARE_LOG(WARN, "fail to assign partition ids", KR(ret));
    } else if (OB_FAIL(column_ids_.assign(other.column_ids_))) {
      SHARE_LOG(WARN, "fail to assign column ids", KR(ret));
    } else { /*do nothing*/ }
    return ret;
  }

  uint64_t table_id_;
  common::ObSArray<int64_t> partition_ids_;
  common::ObSArray<uint64_t> column_ids_;
  bool no_invalidate_;
  bool update_system_stats_only_;

  DECLARE_VIRTUAL_TO_STRING;
};

struct ObDropLobArg: public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObDropLobArg():
      ObDDLArg(),
      session_id_(common::OB_INVALID_ID),
      data_table_id_(common::OB_INVALID_ID),
      aux_lob_meta_table_id_(common::OB_INVALID_ID)
  {}
  virtual ~ObDropLobArg() {}
  void reset()
  {
    ObDDLArg::reset();

    session_id_ = common::OB_INVALID_ID;
    data_table_id_ = common::OB_INVALID_ID;
    aux_lob_meta_table_id_ = common::OB_INVALID_ID;
  }
  bool is_valid() const
  {
    return common::OB_INVALID_ID != data_table_id_
      && common::OB_INVALID_ID != aux_lob_meta_table_id_;
  }
  int assign(const ObDropLobArg &other)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(ObDDLArg::assign(other))) {
      SHARE_LOG(WARN, "fail to assign ddl arg", KR(ret));
    } else {

      session_id_ = other.session_id_;
      data_table_id_ = other.data_table_id_;
      aux_lob_meta_table_id_ = other.aux_lob_meta_table_id_;
    }
    return ret;
  }
public:

  uint64_t session_id_;
  uint64_t data_table_id_;
  uint64_t aux_lob_meta_table_id_;
  INHERIT_TO_STRING_KV("ObDDLArg", ObDDLArg, K_(session_id), K_(data_table_id), K_(aux_lob_meta_table_id));
};

struct ObDropIndexArg: public ObIndexArg
{
  OB_UNIS_VERSION(1);
  //if add new member,should add to_string and serialize function
public:
  ObDropIndexArg():
      ObIndexArg()
  {
    index_action_type_ = DROP_INDEX;
    index_table_id_ = common::OB_INVALID_ID;
    is_add_to_scheduler_ = false;
    is_hidden_ = false;
    is_in_recyclebin_ = false;
    is_inner_ = false;
    is_vec_inner_drop_ = false;
    is_parent_task_dropping_fts_index_ = false;
    is_parent_task_dropping_multivalue_index_ = false;
    is_parent_task_dropping_spiv_index_ = false;
    only_set_status_ = false;
    index_ids_.reset();
    table_id_ = common::OB_INVALID_ID;
    is_drop_in_rebuild_task_ = false;
  }
  virtual ~ObDropIndexArg() {}
  int assign(const ObDropIndexArg &other);
  void reset()
  {
    ObIndexArg::reset();
    index_action_type_ = DROP_INDEX;
    is_add_to_scheduler_ = false;
    is_hidden_ = false;
    is_in_recyclebin_ = false;
    is_inner_ = false;
    is_vec_inner_drop_ = false;
    is_parent_task_dropping_fts_index_ = false;
    is_parent_task_dropping_multivalue_index_ = false;
    is_parent_task_dropping_spiv_index_ = false;
    only_set_status_ = false;
    index_ids_.reset();
    table_id_ = common::OB_INVALID_ID;
    is_drop_in_rebuild_task_ = false;
  }
  bool is_valid() const { return ObIndexArg::is_valid(); }
  uint64_t index_table_id_;
  bool is_add_to_scheduler_;
  bool is_hidden_;
  bool is_in_recyclebin_;
  bool is_inner_;
  bool is_vec_inner_drop_;
  bool is_parent_task_dropping_fts_index_;
  bool is_parent_task_dropping_multivalue_index_;
  bool is_parent_task_dropping_spiv_index_;
  bool only_set_status_;
  common::ObSEArray<int64_t, 5> index_ids_;
  uint64_t table_id_;
  bool is_drop_in_rebuild_task_;

  DECLARE_VIRTUAL_TO_STRING;
};

struct ObDropIndexRes final
{
  OB_UNIS_VERSION(1);
public:
  ObDropIndexRes()
    : index_table_id_(common::OB_INVALID_ID), schema_version_(0), task_id_(0)
  {}
  ~ObDropIndexRes() = default;
public:

  uint64_t index_table_id_;
  int64_t schema_version_;
  int64_t task_id_;
};

struct ObRebuildIndexArg: public ObIndexArg
{
  OB_UNIS_VERSION(1);
  //if add new member,should add to_string and serialize function
public:
  ObRebuildIndexArg() : ObIndexArg(),
    vidx_refresh_info_()
  {
    index_action_type_ = REBUILD_INDEX;
    index_table_id_ = common::OB_INVALID_ID;
  }
  virtual ~ObRebuildIndexArg() {}

  int assign(const ObRebuildIndexArg &other) {
    int ret = common::OB_SUCCESS;
    if (OB_FAIL(ObIndexArg::assign(other))) {
      SHARE_LOG(WARN, "fail to assign base", K(ret));
    } else {
      index_table_id_ = other.index_table_id_;
      vidx_refresh_info_ = other.vidx_refresh_info_;
    }
    return ret;
  }

  void reset()
  {
    ObIndexArg::reset();
    index_action_type_ = REBUILD_INDEX;
    vidx_refresh_info_.reset();
  }
  bool is_valid() const { return ObIndexArg::is_valid(); }
  uint64_t index_table_id_;
  share::schema::ObVectorIndexRefreshInfo vidx_refresh_info_;

  DECLARE_VIRTUAL_TO_STRING;
};

struct ObAlterIndexParallelArg: public ObIndexArg
{
  OB_UNIS_VERSION_V(1);
public:
  ObAlterIndexParallelArg() : ObIndexArg(), new_parallel_(common::OB_DEFAULT_TABLE_DOP)
  {
    index_action_type_ = ALTER_INDEX_PARALLEL;
  }
  virtual ~ObAlterIndexParallelArg()  {}
  void reset()
  {
    ObIndexArg::reset();
    index_action_type_ = ALTER_INDEX_PARALLEL;
    new_parallel_ = common::OB_DEFAULT_TABLE_DOP;
  }
  bool is_valid() const
  {
    // parallel must be greater than 0
    return new_parallel_ > 0;
  }

  int64_t new_parallel_;

  DECLARE_VIRTUAL_TO_STRING;
};

struct ObRenameIndexArg: public ObIndexArg
{
  OB_UNIS_VERSION_V(1);
public:
  ObRenameIndexArg() : ObIndexArg(), origin_index_name_(), new_index_name_()
  {
    index_action_type_ = RENAME_INDEX;
  }
  virtual ~ObRenameIndexArg()  {}
  void reset()
  {
    ObIndexArg::reset();
    index_action_type_ = RENAME_INDEX;
    origin_index_name_.reset();
    new_index_name_.reset();
  }
  common::ObString origin_index_name_;
  common::ObString new_index_name_;

  DECLARE_VIRTUAL_TO_STRING;
};

struct ObAlterIndexArg: public ObIndexArg
{
  OB_UNIS_VERSION_V(1);
public:
  ObAlterIndexArg() : ObIndexArg(), index_visibility_(common::OB_DEFAULT_INDEX_VISIBILITY)
  {
    index_action_type_ = ALTER_INDEX;
  }
  virtual ~ObAlterIndexArg() {}
  void reset()
  {
    ObIndexArg::reset();
    index_action_type_ = ALTER_INDEX;
    index_visibility_ = common::OB_DEFAULT_INDEX_VISIBILITY;
  }
  uint64_t index_visibility_;

  DECLARE_VIRTUAL_TO_STRING;
};

struct ObTruncateTableArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObTruncateTableArg():
      ObDDLArg(),
      session_id_(common::OB_INVALID_ID),
      database_name_(),
      table_name_(),
      is_add_to_scheduler_(false),
      foreign_key_checks_(false)
  {}

  ObTruncateTableArg &operator=(const ObTruncateTableArg &other) = delete;
  ObTruncateTableArg(const ObTruncateTableArg &other) = delete;
  virtual ~ObTruncateTableArg() = default;
  bool is_valid() const;
  DECLARE_TO_STRING;


  uint64_t session_id_; //Pass in session id when truncate table
  common::ObString database_name_;
  common::ObString table_name_;
  bool is_add_to_scheduler_;
  bool foreign_key_checks_;
};

struct ObRenameTableItem
{
  OB_UNIS_VERSION(1);
public:
  ObRenameTableItem():
      origin_db_name_(),
      new_db_name_(),
      origin_table_name_(),
      new_table_name_(),
      origin_table_id_(common::OB_INVALID_ID)
  {}
  bool is_valid() const;
  DECLARE_TO_STRING;

  common::ObString origin_db_name_;
  common::ObString new_db_name_;
  common::ObString origin_table_name_;
  common::ObString new_table_name_;
  uint64_t origin_table_id_;//only used in work thread, no need add to SERIALIZE now
};

struct ObRenameTableArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObRenameTableArg() :
      ObDDLArg(),
      rename_table_items_(),
      lock_session_id_(0),
      lock_session_create_ts_(0),
      lock_priority_(transaction::tablelock::ObTableLockPriority::NORMAL)
  {}
  bool is_valid() const;
  DECLARE_TO_STRING;


  common::ObSArray<ObRenameTableItem> rename_table_items_;
  uint32_t lock_session_id_;
  int64_t lock_session_create_ts_;
  transaction::tablelock::ObTableLockPriority lock_priority_;
};
struct ObStartRedefTableArg final
{
  OB_UNIS_VERSION(1);
public:
  TO_STRING_KV(
               K_(orig_table_id),

               K_(target_table_id),
               K_(session_id),
               K_(parallelism),
               K_(ddl_type),
               K_(ddl_stmt_str),
               K_(trace_id),
               K_(sql_mode),
               K_(tz_info_wrap),
               "nls_formats", common::ObArrayWrap<common::ObString>(nls_formats_, common::ObNLSFormatEnum::NLS_MAX),
               K_(foreign_key_checks));
  ObStartRedefTableArg():
    orig_table_id_(common::OB_INVALID_ID),
    target_table_id_(common::OB_INVALID_ID),
    session_id_(common::OB_INVALID_ID),
    ddl_type_(share::DDL_INVALID),
    ddl_stmt_str_(),
    trace_id_(),
    sql_mode_(0),
    tz_info_wrap_(),
    nls_formats_{},
    foreign_key_checks_(true)
  {}

  ~ObStartRedefTableArg()
  {
    allocator_.clear();
  }

  void reset()
  {

    orig_table_id_ = common::OB_INVALID_ID;

    target_table_id_ = common::OB_INVALID_ID;
    session_id_ = common::OB_INVALID_ID;
    ddl_type_ = share::DDL_INVALID;
    ddl_stmt_str_.reset();
    sql_mode_ = 0;
    foreign_key_checks_ = true;
  }

  inline void set_tz_info_map(const common::ObTZInfoMap *tz_info_map)
  {
    tz_info_wrap_.set_tz_info_map(tz_info_map);
  }
  int set_nls_formats(const common::ObString *nls_formats);
  int set_nls_formats(const common::ObString &nls_date_format,
                      const common::ObString &nls_timestamp_format,
                      const common::ObString &nls_timestamp_tz_format)
  {
    ObString tmp_str[ObNLSFormatEnum::NLS_MAX] = {nls_date_format, nls_timestamp_format,
                                                  nls_timestamp_tz_format};
    return set_nls_formats(tmp_str);
  }
  bool is_valid() const;
public:

  uint64_t orig_table_id_;

  uint64_t target_table_id_;
  uint64_t session_id_;
  uint64_t parallelism_;
  share::ObDDLType ddl_type_;
  common::ObString ddl_stmt_str_;
  share::ObTaskId trace_id_;
  ObSQLMode sql_mode_;
  common::ObArenaAllocator allocator_;
  common::ObTimeZoneInfoWrap tz_info_wrap_;
  common::ObString nls_formats_[common::ObNLSFormatEnum::NLS_MAX];
  bool foreign_key_checks_;
};

struct ObStartRedefTableRes final
{
  OB_UNIS_VERSION(1);
public:
  TO_STRING_KV(K_(task_id),

               K_(schema_version));
  ObStartRedefTableRes() : task_id_(0), schema_version_(0){}
  ~ObStartRedefTableRes() = default;
  void reset()
  {
    task_id_ = 0;

    schema_version_ = 0;
  }
public:
  int64_t task_id_;

  int64_t schema_version_;
};

struct ObCopyTableDependentsArg final
{
  OB_UNIS_VERSION(1);
public:
  TO_STRING_KV(K_(task_id),

               K_(copy_indexes),
               K_(copy_triggers),
               K_(copy_constraints),
               K_(copy_foreign_keys),
               K_(ignore_errors));
  ObCopyTableDependentsArg() : task_id_(0), copy_indexes_(true), copy_triggers_(true),
                               copy_constraints_(true), copy_foreign_keys_(true), ignore_errors_(false) {}

  ~ObCopyTableDependentsArg() = default;
  bool is_valid() const;
  void reset()
  {
    task_id_ = 0;

    copy_indexes_ = false;
    copy_triggers_ = false;
    copy_constraints_ = false;
    copy_foreign_keys_ = false;
    ignore_errors_ = false;
  }
public:
  int64_t task_id_;

  bool copy_indexes_;
  bool copy_triggers_;
  bool copy_constraints_;
  bool copy_foreign_keys_;
  bool ignore_errors_;
};

struct ObFinishRedefTableArg final
{
  OB_UNIS_VERSION(1);
public:
  TO_STRING_KV(K_(task_id));
  ObFinishRedefTableArg() :
    task_id_(0) {}
  ~ObFinishRedefTableArg() = default;
  bool is_valid() const;
  void reset()
  {
    task_id_ = 0;

  }
public:
  int64_t task_id_;

};


struct ObAbortRedefTableArg final
{
  OB_UNIS_VERSION(1);
public:
  TO_STRING_KV(K_(task_id));
  ObAbortRedefTableArg() : task_id_(0) {}
  ~ObAbortRedefTableArg() = default;
  bool is_valid() const;
  void reset()
  {
    task_id_ = 0;

  }
public:
  int64_t task_id_;

};
struct ObUpdateDDLTaskActiveTimeArg final
{
  OB_UNIS_VERSION(1);
public:
  TO_STRING_KV(K_(task_id));
  ObUpdateDDLTaskActiveTimeArg() : task_id_(0) {}
  ~ObUpdateDDLTaskActiveTimeArg() = default;
  bool is_valid() const;
  void reset()
  {
    task_id_ = 0;

  }
public:
  int64_t task_id_;

};


struct ObCreateForeignKeyArg : public ObIndexArg
{
  OB_UNIS_VERSION_V(1);
public:
  ObCreateForeignKeyArg()
  : ObIndexArg(),
    parent_database_(),
    parent_table_(),
    child_columns_(),
    parent_columns_(),
    update_action_(share::schema::ACTION_INVALID),
    delete_action_(share::schema::ACTION_INVALID),
    foreign_key_name_(),
    enable_flag_(true),
    is_modify_enable_flag_(false),
    fk_ref_type_(share::schema::FK_REF_TYPE_INVALID),
    ref_cst_id_(common::OB_INVALID_ID),
    validate_flag_(CST_FK_VALIDATED),
    is_modify_validate_flag_(false),
    rely_flag_(false),
    is_modify_rely_flag_(false),
    is_modify_fk_state_(false),
    need_validate_data_(true),
    is_parent_table_mock_(false),
    parent_database_id_(common::OB_INVALID_ID),
    parent_table_id_(common::OB_INVALID_ID),
    name_generated_type_(GENERATED_TYPE_UNKNOWN)
  {}
  virtual ~ObCreateForeignKeyArg()
  {}

  void reset()
  {
    ObIndexArg::reset();
    parent_database_.reset();
    parent_table_.reset();
    child_columns_.reset();
    parent_columns_.reset();
    update_action_ = share::schema::ACTION_INVALID;
    delete_action_ = share::schema::ACTION_INVALID;
    foreign_key_name_.reset();
    enable_flag_ = true;
    is_modify_enable_flag_ = false;
    fk_ref_type_ = share::schema::FK_REF_TYPE_INVALID;
    ref_cst_id_ = common::OB_INVALID_ID;
    validate_flag_ = CST_FK_VALIDATED;
    is_modify_validate_flag_ = false;
    rely_flag_ = false;
    is_modify_rely_flag_ = false;
    is_modify_fk_state_ = false;
    need_validate_data_ = true;
    is_parent_table_mock_ = false;
    parent_database_id_ = common::OB_INVALID_ID;
    parent_table_id_ = common::OB_INVALID_ID;
    name_generated_type_ = GENERATED_TYPE_UNKNOWN;
  }
  int assign(const ObCreateForeignKeyArg &other) {
    int ret = common::OB_SUCCESS;
    if (OB_FAIL(ObIndexArg::assign(other))) {
      SHARE_LOG(WARN, "assign index arg failed", K(ret), K(other));
    } else if (FALSE_IT(parent_database_ = other.parent_database_)) {
    } else if (FALSE_IT(parent_table_ = other.parent_table_)) {
    } else if (OB_FAIL(child_columns_.assign(other.child_columns_))) {
      SHARE_LOG(WARN, "assign child columns failed", K(ret), K(other.child_columns_));
    } else if (OB_FAIL(parent_columns_.assign(other.parent_columns_))) {
      SHARE_LOG(WARN, "assign parent columns failed", K(ret), K(other.parent_columns_));
    } else {
      update_action_ = other.update_action_;
      delete_action_ = other.delete_action_;
      foreign_key_name_ = other.foreign_key_name_;
      enable_flag_ = other.enable_flag_;
      is_modify_enable_flag_ = other.is_modify_enable_flag_;
      fk_ref_type_ = other.fk_ref_type_;
      ref_cst_id_ = other.ref_cst_id_;
      validate_flag_ = other.validate_flag_;
      is_modify_validate_flag_ = other.is_modify_validate_flag_;
      rely_flag_ = other.rely_flag_;
      is_modify_rely_flag_ = other.is_modify_rely_flag_;
      is_modify_fk_state_ = other.is_modify_fk_state_;
      need_validate_data_ = other.need_validate_data_;
      is_parent_table_mock_ = other.is_parent_table_mock_;
      parent_database_id_ = other.parent_database_id_;
      parent_table_id_ = other.parent_table_id_;
      name_generated_type_ = other.name_generated_type_;
    }
    return ret;
  }
  DECLARE_VIRTUAL_TO_STRING;

public:
  common::ObString parent_database_;
  common::ObString parent_table_;
  common::ObSEArray<common::ObString, 8> child_columns_;
  common::ObSEArray<common::ObString, 8> parent_columns_;
  share::schema::ObReferenceAction update_action_;
  share::schema::ObReferenceAction delete_action_;
  common::ObString foreign_key_name_;
  bool enable_flag_;
  bool is_modify_enable_flag_;
  // foreign key type (ref primary key/unique key/non-unique key)
  share::schema::ObForeignKeyRefType fk_ref_type_; // FARM COMPAT WHITELIST for ref_cst_type_
  uint64_t ref_cst_id_; // the id of index referenced by foreign key
  ObCstFkValidateFlag validate_flag_;
  bool is_modify_validate_flag_;
  bool rely_flag_;
  bool is_modify_rely_flag_;
  bool is_modify_fk_state_;
  bool need_validate_data_;
  bool is_parent_table_mock_;
  uint64_t parent_database_id_;  // used in ddl_service to store related object_id
  uint64_t parent_table_id_;     // used in ddl_service to store related object_id
  ObNameGeneratedType name_generated_type_;
};


struct ObSetCommentArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  enum OP_TYPE {
    MIN_OP_TYPE = 1,
    COMMENT_TABLE,
    COMMENT_COLUMN,
    MAX_OP_TYPE = 1000
  };
  ObSetCommentArg():
    ObDDLArg(),
    session_id_(common::OB_INVALID_ID),
    database_name_(),
    table_name_(),
    table_comment_(),
    column_name_list_(),
    column_comment_list_(),
    op_type_(MIN_OP_TYPE)
  {
  }
  virtual ~ObSetCommentArg() {
  }
  bool is_valid() const;
  TO_STRING_KV(K(ObDDLArg()),
               K_(session_id),
               K_(database_name),
               K_(table_name),
               K_(column_name_list),
               K_(column_comment_list),
               K_(table_comment),
               K_(op_type));
public:
  uint64_t session_id_;
  common::ObString database_name_;
  common::ObString table_name_;
  common::ObString table_comment_;
  common::ObSArray<common::ObString> column_name_list_;
  common::ObSArray<common::ObString> column_comment_list_;
  OP_TYPE op_type_;
private:
   DISALLOW_COPY_AND_ASSIGN(ObSetCommentArg);
};

struct ObAlterTableArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  enum ModifiableTableColumns {
       AUTO_INCREMENT = 1,
       BLOCK_SIZE,
       CHARSET_TYPE,
       COLLATION_TYPE,
       COMPRESS_METHOD,
       COMMENT,
       TABLET_SIZE,
       PCTFREE,
       PROGRESSIVE_MERGE_NUM,
       TABLE_NAME,
       SEQUENCE_COLUMN_ID,
       READ_ONLY,
       SESSION_ID,
       STORE_FORMAT,
       ENABLE_ROW_MOVEMENT,
       PROGRESSIVE_MERGE_ROUND,
       TABLE_MODE,
       TABLE_DOP,
       LOB_INROW_THRESHOLD,
       INCREMENT_CACHE_SIZE,
       SEMISTRUCT_ENCODING_TYPE,
       MAX_OPTION = 1000
  };
  enum AlterPartitionType
  {
    ADD_PARTITION = -1,
    DROP_PARTITION,
    TRUNCATE_PARTITION,
    ADD_SUB_PARTITION,
    DROP_SUB_PARTITION,
    TRUNCATE_SUB_PARTITION,
    REPARTITION_TABLE,
    // 1. convert range to interval in range part table
    // 2. modify interval range in interval part table
    SET_INTERVAL,
    // cnovert interval to range
    INTERVAL_TO_RANGE,
    RENAME_PARTITION,
    RENAME_SUB_PARTITION,
    EXCHANGE_PARTITION,
    EXCHANGE_SUBPARTITION = 18,
    NO_OPERATION = 1000
  };
  enum AlterConstraintType
  {
    ADD_CONSTRAINT = -1,
    DROP_CONSTRAINT,
    ALTER_CONSTRAINT_STATE,
    CONSTRAINT_NO_OPERATION = 1000
  };
  enum AlterAlgorithm
  {
    DEFAULT = 0, // empty
    INSTANT = 1,
    INPLACE = 2,
  };
  ObAlterTableArg():
      ObDDLArg(),
      session_id_(common::OB_INVALID_ID),
      alter_part_type_(NO_OPERATION),
      alter_constraint_type_(CONSTRAINT_NO_OPERATION),
      index_arg_list_(),
      foreign_key_arg_list_(),
      allocator_(),
      alter_table_schema_(&allocator_),
      tz_info_wrap_(),
      nls_formats_{},
      sql_mode_(0),
      ddl_task_type_(share::INVALID_TASK),
      table_id_(common::OB_INVALID_ID),
      hidden_table_id_(common::OB_INVALID_ID),
      is_alter_columns_(false),
      is_alter_indexs_(false),
      is_alter_options_(false),
      is_alter_partitions_(false),
      is_inner_(false),
      is_update_global_indexes_(false),
      is_convert_to_character_(false),
      skip_sys_table_check_(false),
      need_rebuild_trigger_(false),
      foreign_key_checks_(true),
      is_add_to_scheduler_(false),
      local_session_var_(&allocator_),
      alter_algorithm_(INPLACE),
      rebuild_index_arg_list_(),
      lock_session_id_(0),
      lock_session_create_ts_(0),
      lock_priority_(transaction::tablelock::ObTableLockPriority::NORMAL),
      data_version_(0)
  {
  }
  virtual ~ObAlterTableArg()
  {
    for (int64_t i = 0; i < index_arg_list_.size(); ++i) {
      ObIndexArg *index_arg = index_arg_list_.at(i);
      if (OB_NOT_NULL(index_arg)) {
        index_arg->~ObIndexArg();
      }
    }
  }
  bool is_valid() const;
  bool has_rename_action() const
  { return alter_table_schema_.alter_option_bitset_.has_member(TABLE_NAME); }
  bool need_progressive_merge() const {
    return alter_table_schema_.alter_option_bitset_.has_member(BLOCK_SIZE)
        || alter_table_schema_.alter_option_bitset_.has_member(COMPRESS_METHOD)
        || alter_table_schema_.alter_option_bitset_.has_member(PCTFREE)
        || alter_table_schema_.alter_option_bitset_.has_member(STORE_FORMAT)
        || alter_table_schema_.alter_option_bitset_.has_member(PROGRESSIVE_MERGE_ROUND)
        || alter_table_schema_.alter_option_bitset_.has_member(PROGRESSIVE_MERGE_NUM);
  }
  bool is_only_alter_column() const {
    return is_alter_columns_ && foreign_key_checks_
            && !is_alter_indexs_ && !is_alter_options_ && !is_alter_partitions_
            && !is_inner_ && !is_update_global_indexes_ && !is_convert_to_character_
            && !skip_sys_table_check_ && !need_rebuild_trigger_ && !is_add_to_scheduler_;
  }
  ObAlterTableArg &operator=(const ObAlterTableArg &other) = delete;
  ObAlterTableArg(const ObAlterTableArg &other) = delete;
  virtual bool is_allow_when_disable_ddl() const;
  inline void set_tz_info_map(const common::ObTZInfoMap *tz_info_map)
  {
    tz_info_wrap_.set_tz_info_map(tz_info_map);
  }
  int is_alter_comment(bool &is_alter_comment) const;
  int set_nls_formats(const common::ObString *nls_formats);
  int set_nls_formats(const common::ObString &nls_date_format,
                      const common::ObString &nls_timestamp_format,
                      const common::ObString &nls_timestamp_tz_format)
  {
    ObString tmp_str[ObNLSFormatEnum::NLS_MAX] = {nls_date_format, nls_timestamp_format,
                                                  nls_timestamp_tz_format};
    return set_nls_formats(tmp_str);
  }

  inline bool is_only_alter_index() const
  {
    return is_alter_indexs_
       && !is_alter_columns_
       && !is_alter_options_
       && !is_alter_partitions_
       && !is_convert_to_character_
       && alter_constraint_type_ ==  CONSTRAINT_NO_OPERATION;
  }

  TO_STRING_KV(K_(session_id),
               K_(alter_part_type),
               K_(index_arg_list),
               K_(foreign_key_arg_list),
               K_(alter_table_schema),
               K_(alter_constraint_type),
               "nls_formats", common::ObArrayWrap<common::ObString>(nls_formats_, common::ObNLSFormatEnum::NLS_MAX),
               K_(ddl_task_type),
               K_(is_alter_columns),
               K_(is_alter_indexs),
               K_(is_alter_options),
               K_(is_alter_partitions),
               K_(is_inner),
               K_(is_update_global_indexes),
               K_(is_convert_to_character),
               K_(skip_sys_table_check),
               K_(need_rebuild_trigger),
               K_(foreign_key_checks),
               K_(is_add_to_scheduler),
               K_(table_id),
               K_(hidden_table_id),
               K_(local_session_var),
               K_(alter_algorithm),
               K_(rebuild_index_arg_list),
               K_(lock_session_id),
               K_(lock_session_create_ts),
               K_(lock_priority),
               K_(data_version));
private:
  int alloc_index_arg(const ObIndexArg::IndexActionType index_action_type, ObIndexArg *&index_arg);
public:
  uint64_t session_id_; //Only used to update the last active time of the temporary table. At this time, the session id used to create the temporary table is passed in
  AlterPartitionType alter_part_type_;
  AlterConstraintType alter_constraint_type_;
  common::ObSArray<ObIndexArg *> index_arg_list_;
  common::ObSArray<ObCreateForeignKeyArg> foreign_key_arg_list_;
  common::ObArenaAllocator allocator_;
  share::schema::AlterTableSchema alter_table_schema_;
  common::ObTimeZoneInfoWrap tz_info_wrap_;
  common::ObString nls_formats_[common::ObNLSFormatEnum::NLS_MAX];
  ObSQLMode sql_mode_;
  share::ObDDLTaskType ddl_task_type_;
  int64_t table_id_; // to check if the table we get is correct
  int64_t hidden_table_id_; // to check if the hidden table we get is correct
  bool is_alter_columns_;
  bool is_alter_indexs_;
  bool is_alter_options_;
  bool is_alter_partitions_;
  bool is_inner_;
  bool is_update_global_indexes_;
  bool is_convert_to_character_;
  bool skip_sys_table_check_;
  bool need_rebuild_trigger_;
  bool foreign_key_checks_;
  bool is_add_to_scheduler_;
  share::ObLocalSessionVar local_session_var_;
  AlterAlgorithm alter_algorithm_;
  common::ObSArray<ObTableSchema> rebuild_index_arg_list_;
  uint32_t lock_session_id_;
  int64_t lock_session_create_ts_;
  transaction::tablelock::ObTableLockPriority lock_priority_;
  uint64_t data_version_;
  int serialize_index_args(char *buf, const int64_t data_len, int64_t &pos) const;
  int deserialize_index_args(const char *buf, const int64_t data_len, int64_t &pos);
  int64_t get_index_args_serialize_size() const;
};

struct ObExchangePartitionArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObExchangePartitionArg():
      ObDDLArg(),
      session_id_(common::OB_INVALID_ID),
      exchange_partition_level_(PARTITION_LEVEL_MAX),
      base_table_id_(common::OB_INVALID_ID),
      base_table_part_name_(),
      inc_table_id_(common::OB_INVALID_ID),
      including_indexes_(true),
      without_validation_(true),
      update_global_indexes_(false)
  {
  }
  virtual ~ObExchangePartitionArg()
  {
  }
  bool is_valid() const;
  int assign(const ObExchangePartitionArg& other);
public:
  DECLARE_TO_STRING;
  uint64_t session_id_;

  ObPartitionLevel exchange_partition_level_;
  uint64_t base_table_id_; // PT table, always contains large amount pf data.
  ObString base_table_part_name_;
  uint64_t inc_table_id_; // NT table, always contains incremental data.
  bool including_indexes_; // default true.
  bool without_validation_; // default true.
  bool update_global_indexes_; // default false.
};

struct ObTableItem
{
  OB_UNIS_VERSION(1);
public:
  ObTableItem():
      mode_(common::OB_NAME_CASE_INVALID), //for compare
      database_name_(),
      table_name_(),
      is_hidden_(false),
      table_id_(OB_INVALID_ID)
  {}
  bool operator==(const ObTableItem &table_item) const;
  inline uint64_t hash() const;
  inline int hash(uint64_t &hash_val, uint64_t seed = 0) const;
  void reset() {
    mode_ = common::OB_NAME_CASE_INVALID;
    database_name_.reset();
    table_name_.reset();
    is_hidden_ = false;
    table_id_ = OB_INVALID_ID;
  }
  DECLARE_TO_STRING;

  common::ObNameCaseMode mode_;
  common::ObString database_name_;
  common::ObString table_name_;
  bool is_hidden_;
  uint64_t table_id_;
};

inline uint64_t ObTableItem::hash() const
{
  uint64_t val = 0;
  if (!database_name_.empty() && !table_name_.empty()) {
    val = common::murmurhash(database_name_.ptr(), database_name_.length(), val);
    val = common::murmurhash(table_name_.ptr(), table_name_.length(), val);
  }
  return val;
}

inline int ObTableItem::hash(uint64_t &hash_val, uint64_t seed) const
{
  hash_val = seed;
  if (!database_name_.empty() && !table_name_.empty()) {
    hash_val = common::murmurhash(database_name_.ptr(), database_name_.length(), hash_val);
    hash_val = common::murmurhash(table_name_.ptr(), table_name_.length(), hash_val);
  }
  return OB_SUCCESS;
}

struct ObDropTableArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObDropTableArg():
      ObDDLArg(),
      session_id_(common::OB_INVALID_ID),
      sess_create_time_(0),
      table_type_(share::schema::MAX_TABLE_TYPE),
      tables_(),
      if_exist_(false),
      to_recyclebin_(false),
      foreign_key_checks_(true),
      is_add_to_scheduler_(false),
      force_drop_(false)
  {}
  bool is_valid() const;
  ObDropTableArg &operator=(const ObDropTableArg &other) = delete;
  ObDropTableArg(const ObDropTableArg &other) = delete;
  virtual ~ObDropTableArg() { tables_.reset(); }

  DECLARE_TO_STRING;


  uint64_t session_id_; //Pass in session id when deleting table
  int64_t sess_create_time_; //Pass session creation time when deleting temporary table data
  share::schema::ObTableType table_type_;
  common::ObSArray<ObTableItem> tables_;
  bool if_exist_;
  bool to_recyclebin_;
  bool foreign_key_checks_;
  bool is_add_to_scheduler_;
  bool force_drop_;
  common::ObArenaAllocator allocator_;
};

struct ObForkTableArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObForkTableArg():
      ObDDLArg(),
      src_database_name_(),
      src_table_name_(),
      dst_database_name_(),
      dst_table_name_(),
      if_not_exist_(false),
      session_id_(0)
  {}
  bool is_valid() const;
  int assign(const ObForkTableArg &other);
  DECLARE_TO_STRING;


  common::ObString src_database_name_;
  common::ObString src_table_name_;
  common::ObString dst_database_name_;
  common::ObString dst_table_name_;
  bool if_not_exist_;
  int64_t session_id_;
};

struct ObForkDatabaseArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObForkDatabaseArg():
      ObDDLArg(),
      src_database_name_(),
      dst_database_name_(),
      if_not_exist_(false),
      session_id_(0)
  {}
  bool is_valid() const;
  int assign(const ObForkDatabaseArg &other);
  DECLARE_TO_STRING;


  common::ObString src_database_name_;
  common::ObString dst_database_name_;
  bool if_not_exist_;
  int64_t session_id_;
};

struct ObOptimizeTableArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObOptimizeTableArg():
    ObDDLArg(),
    tables_()
  {}
  DECLARE_TO_STRING;
  bool is_valid() const;

  common::ObSArray<ObTableItem> tables_;
};

struct ObColumnSortItem final
{
  OB_UNIS_VERSION(1);
public:
  ObColumnSortItem() : column_name_(),
                       prefix_len_(0),
                       order_type_(common::ObOrderType::ASC),
                       column_id_(common::OB_INVALID_ID),
                       is_func_index_(false)
  {}
  void reset()
  {
    column_name_.reset();
    prefix_len_ = 0;
    order_type_ = common::ObOrderType::ASC;
    column_id_ = common::OB_INVALID_ID;
    is_func_index_ = false;
  }
  inline uint64_t get_column_id() const { return column_id_; }

  DECLARE_TO_STRING;

  common::ObString column_name_;
  int32_t prefix_len_;
  common::ObOrderType order_type_;
  uint64_t column_id_;
  bool is_func_index_;   //Whether the mark is a function index, the default is false.
};

struct ObTableOption
{
  OB_UNIS_VERSION_V(1);
public:
  ObTableOption() :
    block_size_(-1),
    index_status_(share::schema::INDEX_STATUS_UNAVAILABLE),
    compress_method_("none"),
    comment_(),
    progressive_merge_num_(common::OB_DEFAULT_PROGRESSIVE_MERGE_NUM),
    row_store_type_(common::MAX_ROW_STORE),
    store_format_(common::OB_STORE_FORMAT_INVALID),
    progressive_merge_round_(0)
  {}
  virtual void reset()
  {
    block_size_ = common::OB_DEFAULT_SSTABLE_BLOCK_SIZE;
    index_status_ = share::schema::INDEX_STATUS_UNAVAILABLE;
    compress_method_ = common::ObString::make_string("none");
    comment_.reset();
    progressive_merge_num_ = common::OB_DEFAULT_PROGRESSIVE_MERGE_NUM;
    row_store_type_ = common::MAX_ROW_STORE;
    store_format_ = common::OB_STORE_FORMAT_INVALID;
    progressive_merge_round_ = 0;
  }
  DECLARE_TO_STRING;

  int64_t block_size_;
  share::schema::ObIndexStatus index_status_;
  common::ObString compress_method_;
  common::ObString comment_;
  int64_t progressive_merge_num_;
  common::ObRowStoreType row_store_type_;
  common::ObStoreFormatType  store_format_;
  int64_t progressive_merge_round_;
};

struct ObIndexOption : public ObTableOption
{
  OB_UNIS_VERSION(1);
public:
  ObIndexOption() :
    ObTableOption(),
    parser_name_(),
    parser_properties_(),
    index_attributes_set_(common::OB_DEFAULT_INDEX_ATTRIBUTES_SET)
  { }

  bool is_valid() const;
  void reset()
  {
    ObTableOption::reset();
    parser_name_.reset();
    parser_properties_.reset();
  }
  DECLARE_TO_STRING;

  common::ObString parser_name_;
  common::ObString parser_properties_;
  uint64_t index_attributes_set_;//flags, one bit for one attribute
};

struct ObCreateIndexArg : public ObIndexArg
{
  OB_UNIS_VERSION_V(1);
public:
  ObCreateIndexArg()
      : index_type_(share::schema::INDEX_TYPE_IS_NOT),
        index_columns_(),
        store_columns_(),
        hidden_store_columns_(),
        index_option_(),
        data_table_id_(common::OB_INVALID_ID),
        index_table_id_(common::OB_INVALID_ID),
        if_not_exist_(false),
        with_rowid_(false),
        index_schema_(&allocator_),
        is_inner_(false),
        nls_date_format_(),
        nls_timestamp_format_(),
        nls_timestamp_tz_format_(),
        sql_mode_(0),
        allocator_(),
        local_session_var_(&allocator_),
        vidx_refresh_info_(),
        is_rebuild_index_(false),
        is_index_scope_specified_(false),
        is_offline_rebuild_(false),
        index_key_(-1),
        data_version_(0)
  {
    index_action_type_ = ADD_INDEX;
    index_using_type_ = share::schema::USING_BTREE;
  }
  virtual ~ObCreateIndexArg() {}
  void reset()
  {
    ObIndexArg::reset();
    index_action_type_ = ADD_INDEX;
    index_type_ = share::schema::INDEX_TYPE_IS_NOT;
    index_columns_.reset();
    store_columns_.reset();
    hidden_store_columns_.reset();
    index_option_.reset();
    index_using_type_ = share::schema::USING_BTREE;
    data_table_id_ = common::OB_INVALID_ID;
    index_table_id_ = common::OB_INVALID_ID;
    if_not_exist_ = false;
    with_rowid_ = false;
    index_schema_.reset();
    is_inner_ = false;
    nls_date_format_.reset();
    nls_timestamp_format_.reset();
    nls_timestamp_tz_format_.reset();
    sql_mode_ = 0;
    local_session_var_.reset();
    allocator_.reset();
    vidx_refresh_info_.reset();
    is_rebuild_index_ = false;
    is_index_scope_specified_ = false;
    is_offline_rebuild_ = false;
    index_key_ = -1;
    data_version_ = 0;
  }
  void set_index_action_type(const IndexActionType type) { index_action_type_  = type; }
  bool is_valid() const;
  int assign(const ObCreateIndexArg &other) {
    int ret = common::OB_SUCCESS;
    if (OB_FAIL(ObIndexArg::assign(other))) {
      SHARE_LOG(WARN, "fail to assign base", K(ret));
    } else if (OB_FAIL(index_columns_.assign(other.index_columns_))) {
      SHARE_LOG(WARN, "fail to assign index columns", K(ret));
    } else if (OB_FAIL(store_columns_.assign(other.store_columns_))) {
      SHARE_LOG(WARN, "fail to assign store columns", K(ret));
    } else if (OB_FAIL(hidden_store_columns_.assign(other.hidden_store_columns_))) {
      SHARE_LOG(WARN, "fail to assign hidden store columns", K(ret));
    } else if (OB_FAIL(index_schema_.assign(other.index_schema_))) {
      SHARE_LOG(WARN, "fail to assign index schema", K(ret));
    } else if (OB_FAIL(local_session_var_.deep_copy(other.local_session_var_))){
      SHARE_LOG(WARN, "fail to copy local session vars", K(ret));
    } else {
      index_type_ = other.index_type_;
      index_option_ = other.index_option_;
      index_using_type_ = other.index_using_type_;
      data_table_id_ = other.data_table_id_;
      index_table_id_ = other.index_table_id_;
      if_not_exist_ = other.if_not_exist_;
      with_rowid_ = other.with_rowid_;
      is_inner_ = other.is_inner_;
      nls_date_format_ = other.nls_date_format_;
      nls_timestamp_format_ = other.nls_timestamp_format_;
      nls_timestamp_tz_format_ = other.nls_timestamp_tz_format_;
      sql_mode_ = other.sql_mode_;
      vidx_refresh_info_ = other.vidx_refresh_info_;
      is_rebuild_index_ = other.is_rebuild_index_;
      is_index_scope_specified_ = other.is_index_scope_specified_;
      is_offline_rebuild_ = other.is_offline_rebuild_;
      index_key_ = other.index_key_;
      data_version_ = other.data_version_;
    }
    return ret;
  }
  inline bool is_unique_primary_index() const
  {
    return ObSimpleTableSchemaV2::is_unique_index(index_type_)
            || share::schema::INDEX_TYPE_PRIMARY == index_type_;
  }
  DECLARE_VIRTUAL_TO_STRING;
  inline bool is_spatial_index() const { return ObSimpleTableSchemaV2::is_spatial_index(index_type_); }
  inline bool is_multivalue_index() const { return is_multivalue_index_aux(index_type_); }
  inline bool is_vec_index() const { return ObSimpleTableSchemaV2::is_vec_index(index_type_); }

public:
  share::schema::ObIndexType index_type_;
  common::ObSEArray<ObColumnSortItem, common::OB_PREALLOCATED_NUM> index_columns_;
  common::ObSEArray<common::ObString, common::OB_PREALLOCATED_NUM> store_columns_;
  common::ObSEArray<common::ObString, common::OB_PREALLOCATED_NUM> hidden_store_columns_;
  ObIndexOption index_option_;
  share::schema::ObIndexUsingType index_using_type_;
  uint64_t data_table_id_;
  uint64_t index_table_id_; // Data_table_id and index_table_id will be given in SQL during recovery
  bool if_not_exist_;
  bool with_rowid_;
  share::schema::ObTableSchema index_schema_; // Index table schema
  bool is_inner_;
  //Nls_xx_format is required when creating a functional index
  common::ObString nls_date_format_;
  common::ObString nls_timestamp_format_;
  common::ObString nls_timestamp_tz_format_;
  ObSQLMode sql_mode_;
  common::ObArenaAllocator allocator_;
  share::ObLocalSessionVar local_session_var_;
  share::schema::ObVectorIndexRefreshInfo vidx_refresh_info_;
  bool is_rebuild_index_;
  bool is_index_scope_specified_;
  bool is_offline_rebuild_;
  int64_t index_key_;
  uint64_t data_version_;
};


struct ObCreateAuxIndexArg : public ObDDLArg
{
  OB_UNIS_VERSION_V(1);
public:
  ObCreateAuxIndexArg()
    : data_table_id_(OB_INVALID_ID),
      snapshot_version_(0)
  {}
  ~ObCreateAuxIndexArg() {}
  bool is_valid() const
  {
    return data_table_id_ != OB_INVALID_ID &&
           create_index_arg_.is_valid();
  }
  void reset()
  {

    data_table_id_ = OB_INVALID_ID;
    create_index_arg_.reset();
    snapshot_version_ = 0;
  }
  TO_STRING_KV(K(data_table_id_), K(create_index_arg_), K(snapshot_version_));

public:

  uint64_t data_table_id_;
  ObCreateIndexArg create_index_arg_;
  int64_t snapshot_version_;
};

struct ObCreateAuxIndexRes final
{
  OB_UNIS_VERSION_V(1);
public:
  ObCreateAuxIndexRes()
    : aux_table_id_(OB_INVALID_ID),
      ddl_task_id_(OB_INVALID_ID),
      schema_generated_(false)
  {}
  ~ObCreateAuxIndexRes() {}
  int assign(const ObCreateAuxIndexRes &other)
  {
    int ret = OB_SUCCESS;
    aux_table_id_ = other.aux_table_id_;
    ddl_task_id_ = other.ddl_task_id_;
    schema_generated_ = other.schema_generated_;
    return ret;
  }
  void reset()
  {
    aux_table_id_ = OB_INVALID_ID;
    ddl_task_id_ = OB_INVALID_ID;
    schema_generated_ = false;
  }
  TO_STRING_KV(K(aux_table_id_), K(ddl_task_id_), K(schema_generated_));

public:
  uint64_t aux_table_id_;
  int64_t ddl_task_id_;
  bool schema_generated_;
};

typedef ObCreateIndexArg ObAlterPrimaryArg;

struct ObDropForeignKeyArg : public ObIndexArg
{
  OB_UNIS_VERSION_V(1);
public:
  ObDropForeignKeyArg()
  : ObIndexArg(),
    foreign_key_name_()
  {
    index_action_type_ = DROP_FOREIGN_KEY;
  }
  virtual ~ObDropForeignKeyArg()
  {}

  void reset()
  {
    ObIndexArg::reset();
    foreign_key_name_.reset();
  }
  DECLARE_VIRTUAL_TO_STRING;

public:
  common::ObString foreign_key_name_;
};

struct ObRecyclebinRestoreTableArg: public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObRecyclebinRestoreTableArg():
    ObDDLArg(),
    origin_db_name_(),
    origin_table_name_(),
    new_db_name_(),
    new_table_name_(),
    origin_table_id_(common::OB_INVALID_ID)
  {}
  bool is_valid() const;

  common::ObString origin_db_name_;
  common::ObString origin_table_name_;
  common::ObString new_db_name_;
  common::ObString new_table_name_;
  uint64_t origin_table_id_;//only used in work thread, no need add to SERIALIZE now

  TO_STRING_KV(
               K_(origin_db_name),
               K_(origin_table_name),
               K_(new_db_name),
               K_(new_table_name),
               K_(origin_table_id));
};

struct ObRecyclebinRestoreDatabaseArg: public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObRecyclebinRestoreDatabaseArg():
    ObDDLArg(),
    origin_db_name_(),
    new_db_name_()
  {}
  bool is_valid() const;

  common::ObString origin_db_name_;
  common::ObString new_db_name_;

  TO_STRING_KV(
               K_(origin_db_name),
               K_(new_db_name));
};

struct ObPurgeTableArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObPurgeTableArg():
    ObDDLArg(),
    database_id_(common::OB_INVALID_ID),
    table_name_()
  {}
  bool is_valid() const;

  uint64_t database_id_;
  common::ObString table_name_;
  TO_STRING_KV(
               K_(database_id),
               K_(table_name));
};

struct ObPurgeIndexArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObPurgeIndexArg():
    ObDDLArg(),
    database_id_(common::OB_INVALID_ID),
    table_name_(),
    table_id_(common::OB_INVALID_ID)
  {}
  bool is_valid() const;
  void reset()
  {

    table_id_ = common::OB_INVALID_ID;
    table_name_.reset();
    ObDDLArg::reset();
  }

  uint64_t database_id_;
  common::ObString table_name_;
  uint64_t table_id_;//only used in work thread, no need add to SERIALIZE now

  TO_STRING_KV(
               K_(database_id),
               K_(table_name),
               K_(table_id));
};

struct ObPurgeDatabaseArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObPurgeDatabaseArg():
    ObDDLArg(),
    db_name_()
  {}
  bool is_valid() const;

  common::ObString db_name_;
  TO_STRING_KV(
               K_(db_name));
};



struct ObPurgeRecycleBinArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  static const int DEFAULT_PURGE_EACH_TIME = 10;
  ObPurgeRecycleBinArg():
    ObDDLArg(),
    purge_num_(0),
    expire_time_(0),
    auto_purge_(false)
  {}
  virtual ~ObPurgeRecycleBinArg()
  {}

  int64_t purge_num_;
  int64_t expire_time_;
  bool auto_purge_;
  TO_STRING_KV(K_(purge_num), K_(expire_time), K_(auto_purge));
};

struct ObCreateTableArg : ObDDLArg
{
  OB_UNIS_VERSION(1);

public:
  ObCreateTableArg() :
      ObDDLArg(),
      if_not_exist_(false),
      last_replay_log_id_(0),
      is_inner_(false),
      error_info_(),
      is_alter_view_(false),
      dep_infos_()
  {}
  bool is_valid() const;
  int assign(const ObCreateTableArg &other);
  DECLARE_TO_STRING;

  bool if_not_exist_;
  share::schema::ObTableSchema schema_;
  common::ObSArray<ObCreateIndexArg> index_arg_list_;
  common::ObSArray<ObCreateForeignKeyArg> foreign_key_arg_list_;
  common::ObSEArray<share::schema::ObConstraint, 4> constraint_list_;
  common::ObString db_name_;
  uint64_t last_replay_log_id_;
  bool is_inner_;
  common::ObSArray<ObCreateVertialPartitionArg> vertical_partition_arg_list_;
  share::schema::ObErrorInfo error_info_;
  bool is_alter_view_;
  common::ObSArray<oceanbase::share::schema::ObDependencyInfo> dep_infos_;
};






struct ObCreateTabletInfo
{
  OB_UNIS_VERSION(1);
public:
  ObCreateTabletInfo() { reset(); }
  ~ObCreateTabletInfo() {}
  bool is_valid() const;
  void reset();
  int assign(const ObCreateTabletInfo &info);
  int init(const ObIArray<common::ObTabletID> &tablet_ids,
           const common::ObTabletID data_tablet_id,
           const common::ObIArray<int64_t> &table_schema_index,
           const bool is_create_bind_hidden_tablets,
           const ObIArray<int64_t> &create_commit_versions);
  int init(const ObIArray<common::ObTabletID> &tablet_ids,
           const common::ObTabletID data_tablet_id,
           const common::ObIArray<int64_t> &table_schema_index,
           const bool is_create_bind_hidden_tablets,
           const ObIArray<int64_t> &create_commit_versions,
           const ObIArray<share::ObForkTabletInfo> &fork_tablet_infos);
  common::ObTabletID get_data_tablet_id() const { return data_tablet_id_; }
  int64_t get_tablet_count() const { return tablet_ids_.count(); }
  // Get fork tablet info at index, return default ObForkTabletInfo if fork_tablet_infos_ is empty
  int get_fork_tablet_info(const int64_t idx, share::ObForkTabletInfo &fork_tablet_info) const;
  DECLARE_TO_STRING;

  common::ObSArray<common::ObTabletID> tablet_ids_;
  common::ObTabletID data_tablet_id_; // or orig tablet id if is create hidden tablets
  //the index of table_schemas_ in ObBatchCreateTabletArg
  common::ObSArray<int64_t> table_schema_index_;
  bool is_create_bind_hidden_tablets_;
  ObSArray<int64_t> create_commit_versions_;
  common::ObSArray<share::ObForkTabletInfo> fork_tablet_infos_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreateTabletInfo);
};

struct ObCreateTabletExtraInfo final
{
  OB_UNIS_VERSION(1);
public:
  ObCreateTabletExtraInfo() { reset(); }
  ~ObCreateTabletExtraInfo() { reset(); }
  int init(const uint64_t data_format_version,
           const bool need_create_empty_major,
           const bool micro_index_clustered);
  void reset();
  int assign(const ObCreateTabletExtraInfo &other);
public:
  uint64_t data_format_version_;
  bool need_create_empty_major_;
  bool micro_index_clustered_;
  TO_STRING_KV(K_(data_format_version),
               K_(need_create_empty_major),
               K_(micro_index_clustered));
};

// ObBatchCreateTabletArg moved definition to storage/tablet/ob_batch_create_tablet_arg.h
// (ObSArray<ObCreateTabletSchema*> virtual to_string requires a complete type, share must not depend upward on storage)
struct ObBatchCreateTabletArg;

struct ObBatchRemoveTabletArg
{
  OB_UNIS_VERSION(2);
public:
  ObBatchRemoveTabletArg() { reset(); }
  ~ObBatchRemoveTabletArg() {}
  bool is_valid() const;
  void reset();
  int assign (const ObBatchRemoveTabletArg &arg);
  int init(const ObIArray<common::ObTabletID> &tablet_ids);
  DECLARE_TO_STRING;

public:
  common::ObSArray<common::ObTabletID> tablet_ids_;
};



// ObCreateTabletBatchInTransRes/ObRemoveTabletsInTransRes moved to storage/tx/ob_tx_result_struct.h(holds ObTxExecResult by value, share must not depend upward on storage/tx)

struct ObFetchTabletSeqArg
{
  OB_UNIS_VERSION(2);
public:
  ObFetchTabletSeqArg() : cache_size_(0), tablet_id_() {}
  ~ObFetchTabletSeqArg() {}
  bool is_valid() const;
public:
  DECLARE_TO_STRING;
public:

  uint64_t cache_size_;
  common::ObTabletID tablet_id_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObFetchTabletSeqArg);

};

struct ObFetchTabletSeqRes
{
  OB_UNIS_VERSION(1);
public:
  ObFetchTabletSeqRes() : cache_interval_() {}
  ~ObFetchTabletSeqRes() {}
  bool is_valid() const;
  void reset();
  DECLARE_TO_STRING;
public:

  share::ObTabletAutoincInterval cache_interval_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObFetchTabletSeqRes);
};

using ObClearTabletAutoincSeqCacheArg = ObBatchRemoveTabletArg;

struct ObCalcColumnChecksumRequestArg final
{
  OB_UNIS_VERSION(1);
public:
  ObCalcColumnChecksumRequestArg() { reset(); }
  ~ObCalcColumnChecksumRequestArg() = default;
  bool is_valid() const;
  void reset();
  int assign(const ObCalcColumnChecksumRequestArg &other);
  TO_STRING_KV(K_(target_table_id), K_(schema_version), K_(execution_id),
      K_(snapshot_version), K_(source_table_id), K_(task_id), K_(calc_items), K_(user_parallelism));
  struct SingleItem final
  {
    OB_UNIS_VERSION(2);
  public:
    SingleItem() { reset(); }
    ~SingleItem() = default;
    bool is_valid() const;
    void reset();
    int assign(const SingleItem &other);
    TO_STRING_KV(K_(tablet_id), K_(calc_table_id));
    common::ObTabletID tablet_id_;
    int64_t calc_table_id_;
  };
public:

  uint64_t target_table_id_;
  int64_t schema_version_;
  int64_t execution_id_;
  int64_t snapshot_version_;
  int64_t source_table_id_;
  int64_t task_id_;
  common::ObSEArray<SingleItem, 10> calc_items_;
  int64_t user_parallelism_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCalcColumnChecksumRequestArg);
};

struct ObCalcColumnChecksumRequestRes final
{
  OB_UNIS_VERSION(1);
public:
  common::ObSEArray<int, 10> ret_codes_;
};

struct ObCalcColumnChecksumResponseArg
{
  OB_UNIS_VERSION(2);
public:
  ObCalcColumnChecksumResponseArg() { reset(); }
  ~ObCalcColumnChecksumResponseArg() = default;
  bool is_valid() const;
  void reset();
  int assign(const ObCalcColumnChecksumResponseArg &other) {
    int ret = common::OB_SUCCESS;
    tablet_id_ = other.tablet_id_;
    target_table_id_ = other.target_table_id_;
    ret_code_ = other.ret_code_;
    source_table_id_ = other.source_table_id_;
    schema_version_ = other.schema_version_;
    task_id_ = other.task_id_;

    return ret;
  }
  TO_STRING_KV(K_(task_id), K_(tablet_id), K_(target_table_id), K_(ret_code), K_(source_table_id), K_(schema_version));
public:
  common::ObTabletID tablet_id_;
  uint64_t target_table_id_;
  int ret_code_;
  int64_t source_table_id_;
  int64_t schema_version_;
  int64_t task_id_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObCalcColumnChecksumResponseArg);
};









//----Structs for managing privileges----


struct ObSwitchSchemaArg
{
  OB_UNIS_VERSION(1);
public:
  explicit ObSwitchSchemaArg()
    : schema_info_(),
      force_refresh_(false),
      is_async_(false) {}
  explicit ObSwitchSchemaArg(
       const share::schema::ObRefreshSchemaInfo &schema_info,
       const bool force_refresh,
       const bool is_async)
    : schema_info_(schema_info),
      force_refresh_(force_refresh),
      is_async_(is_async) {}
  ~ObSwitchSchemaArg() {}
  bool is_valid() const { return schema_info_.is_valid(); }

  DECLARE_TO_STRING;

  share::schema::ObRefreshSchemaInfo schema_info_;
  bool force_refresh_;
  bool is_async_;
};

struct ObTabletPair final
{
  OB_UNIS_VERSION(1);
public:
  bool is_valid() const { return tablet_id_.is_valid(); }
  uint64_t hash() const { return tablet_id_.hash(); }
  int hash(uint64_t &hash_val) const { hash_val = hash(); return OB_SUCCESS; }
  bool operator == (const ObTabletPair &other) const { return tablet_id_ == other.tablet_id_; }
  bool operator < (const ObTabletPair &other) const { return tablet_id_ < other.tablet_id_; }
  TO_STRING_KV(K_(tablet_id));
  common::ObTabletID tablet_id_;
};

struct ObCheckSchemaVersionElapsedArg final
{
  OB_UNIS_VERSION(1);
public:
  ObCheckSchemaVersionElapsedArg()
    : schema_version_(0), need_wait_trans_end_(true), ddl_task_id_(0)
  {}
  bool is_valid() const;
  TO_STRING_KV(K_(schema_version), K_(need_wait_trans_end), K_(tablets), K_(ddl_task_id));


  int64_t schema_version_;
  bool need_wait_trans_end_;
  ObSEArray<ObTabletPair, 10> tablets_;
  int64_t ddl_task_id_;
};

struct ObDDLCheckTabletMergeStatusArg final
{
  OB_UNIS_VERSION(2);
public:
  ObDDLCheckTabletMergeStatusArg()
    : tablet_ids_(), snapshot_version_()
  {}
  ~ObDDLCheckTabletMergeStatusArg() = default;
  bool is_valid() const {
    return common::OB_INVALID_TIMESTAMP != snapshot_version_ &&
      tablet_ids_.count() > 0;
  }
  int assign(const ObDDLCheckTabletMergeStatusArg &other);
  void reset() {
    tablet_ids_.reset();
  }
public:
  TO_STRING_KV(K_(tablet_ids), K_(snapshot_version));

  common::ObSArray<common::ObTabletID> tablet_ids_;
  int64_t snapshot_version_;
};

struct ObCheckModifyTimeElapsedArg final
{
  OB_UNIS_VERSION(1);
public:
  ObCheckModifyTimeElapsedArg() : sstable_exist_ts_(0), ddl_task_id_(0) {}
  bool is_valid() const;
  TO_STRING_KV(K_(sstable_exist_ts), K_(tablets), K_(ddl_task_id));

  int64_t sstable_exist_ts_;
  ObSEArray<ObTabletPair, 10> tablets_;
  int64_t ddl_task_id_;
};

struct ObCheckTransElapsedResult final
{
  OB_UNIS_VERSION(1);
public:
  ObCheckTransElapsedResult()
    : snapshot_(common::OB_INVALID_TIMESTAMP), pending_tx_id_(), ret_code_(OB_SUCCESS) {}
  bool is_valid() const { return snapshot_ != common::OB_INVALID_TIMESTAMP; }
  void reuse() { snapshot_ = common::OB_INVALID_TIMESTAMP; pending_tx_id_.reset(); }
  TO_STRING_KV(K_(snapshot), K_(pending_tx_id), K_(ret_code));
  int64_t snapshot_;
  transaction::ObTransID pending_tx_id_;
  int ret_code_;
};

struct ObDDLCheckTabletMergeStatusResult
{
  OB_UNIS_VERSION(1);
public:
  ObDDLCheckTabletMergeStatusResult()
    : merge_status_() {}
  void reset() { merge_status_.reset(); }
public:
  TO_STRING_KV(K_(merge_status));
  common::ObSArray<bool> merge_status_;
};

struct ObCheckSchemaVersionElapsedResult
{
  OB_UNIS_VERSION(1);
public:
  ObCheckSchemaVersionElapsedResult() {}
  void reuse() { results_.reuse(); }
  TO_STRING_KV(K_(results));
  ObSEArray<ObCheckTransElapsedResult, 10> results_;
};

typedef ObCheckSchemaVersionElapsedResult ObCheckModifyTimeElapsedResult;

class CandidateStatus
{
  OB_UNIS_VERSION(1);
public:
  CandidateStatus() : candidate_status_(0) {}
  virtual ~CandidateStatus() {}
public:
  void set_in_black_list(const bool in_black_list) {
    if (in_black_list) {
      in_black_list_ = 1;
    } else {
      in_black_list_ = 0;
    }
  }
  bool get_in_black_list() const {
    bool ret_in_black_list = false;
    if (0 == in_black_list_) { // false, do nothing
    } else {
      ret_in_black_list = true;
    }
    return ret_in_black_list;
  }
  TO_STRING_KV("in_black_list", get_in_black_list());
private:
  union {
    uint64_t candidate_status_;
    struct {
      uint64_t in_black_list_ : 1; // Boolean
      uint64_t reserved_ : 63;
    };
  };
};

typedef common::ObSArray<CandidateStatus> CandidateStatusList;

//----Structs for managing privileges----

struct ObAccountArg
{
  OB_UNIS_VERSION(1);

public:
  ObAccountArg() : user_name_(), host_name_() , is_role_(false) {}
  ObAccountArg(const common::ObString &user_name, const common::ObString &host_name)
    : user_name_(user_name), host_name_(host_name), is_role_(false)  {}
  ObAccountArg(const char *user_name, const  char *host_name)
    : user_name_(user_name), host_name_(host_name), is_role_(false)  {}
  ObAccountArg(const common::ObString &user_name, const common::ObString &host_name, const bool is_role)
    : user_name_(user_name), host_name_(host_name), is_role_(is_role)  {}
  ObAccountArg(const char *user_name, const  char *host_name, const bool is_role)
    : user_name_(user_name), host_name_(host_name), is_role_(is_role)  {}
  bool is_valid() const { return !user_name_.empty(); }
  bool is_default_host_name() const { return 0 == host_name_.compare(common::OB_DEFAULT_HOST_NAME); }
  TO_STRING_KV(K_(user_name), K_(host_name), K_(is_role));

  common::ObString user_name_;
  common::ObString host_name_;
  bool is_role_;
};

struct ObSchemaReviseArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  enum SchemaReviseType
  {
    INVALID_SCHEMA_REVISE_TYPE = 0,
    REVISE_CONSTRAINT_COLUMN_INFO = 1,
    REVISE_NOT_NULL_CONSTRAINT = 2,
    MAX_SCHEMA_REVISE_TYPE = 1000
  };
  ObSchemaReviseArg() :
    ObDDLArg(),
    type_(INVALID_SCHEMA_REVISE_TYPE),
    table_id_(common::OB_INVALID_ID),
    csts_array_()
  {}
  virtual ~ObSchemaReviseArg()
  {}
  bool is_valid() const;
  TO_STRING_KV(K_(type), K_(table_id));
  SchemaReviseType type_;

  uint64_t table_id_;
  common::ObSArray<share::schema::ObConstraint> csts_array_;
};

struct ObCreateUserArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);

public:
  ObCreateUserArg() : ObDDLArg(), if_not_exist_(false),
                      creator_id_(common::OB_INVALID_ID), is_create_role_(false)
  {}
  virtual ~ObCreateUserArg()
  {}
  bool is_valid() const;
  virtual bool contain_sensitive_data() const { return true; }
  TO_STRING_KV(K_(user_infos));


  bool if_not_exist_;
  common::ObSArray<share::schema::ObUserInfo> user_infos_;
  uint64_t creator_id_;
  bool is_create_role_;
};

struct ObDropUserArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);

public:
  ObDropUserArg() : ObDDLArg(), is_role_(false)
  { }
  virtual ~ObDropUserArg() {}
  bool is_valid() const;
  TO_STRING_KV(K_(users), K_(hosts), K_(is_role));


  common::ObSArray<common::ObString> users_;
  common::ObSArray<common::ObString> hosts_;//can not use ObAccountArg for compatibility
  bool is_role_;
};

struct ObAlterRoleArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);

public:
  ObAlterRoleArg() : ObDDLArg()
  {}
  virtual ~ObAlterRoleArg()
  {}
  bool is_valid() const;
  TO_STRING_KV(K_(role_name), K_(host_name), K_(pwd_enc));


  ObString role_name_;
  ObString host_name_;
  ObString pwd_enc_;
};

struct ObRenameUserArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);

public:
  ObRenameUserArg() : ObDDLArg()
  { }
  virtual ~ObRenameUserArg() {}
  bool is_valid() const;
  TO_STRING_KV(K_(old_users), K_(old_hosts), K_(new_users), K_(new_hosts));


  common::ObSArray<common::ObString> old_users_;
  common::ObSArray<common::ObString> new_users_;
  common::ObSArray<common::ObString> old_hosts_;
  common::ObSArray<common::ObString> new_hosts_;
};

struct ObSetPasswdArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);

public:
  ObSetPasswdArg() : ObDDLArg(),
  ssl_type_(share::schema::ObSSLType::SSL_TYPE_NOT_SPECIFIED),
  modify_max_connections_(false),
  max_connections_per_hour_(OB_INVALID_ID), max_user_connections_(OB_INVALID_ID)
  { }
  virtual ~ObSetPasswdArg() {}
  bool is_valid() const;
  virtual bool contain_sensitive_data() const { return true; }
  TO_STRING_KV(K_(user), K_(host), K_(passwd), K_(ssl_type),
               K_(ssl_cipher), K_(x509_issuer), K_(x509_subject),
               K_(max_connections_per_hour), K_(max_user_connections));


  common::ObString user_;
  common::ObString passwd_;
  common::ObString host_;
  share::schema::ObSSLType ssl_type_;
  common::ObString ssl_cipher_;
  common::ObString x509_issuer_;
  common::ObString x509_subject_;
  bool modify_max_connections_;
  uint64_t max_connections_per_hour_;
  uint64_t max_user_connections_;
};

struct ObLockUserArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);

public:
  ObLockUserArg() : ObDDLArg(), locked_(false)
  { }
  virtual ~ObLockUserArg() {}
  bool is_valid() const;
  TO_STRING_KV(K_(users), K_(hosts), K_(locked));


  common::ObSArray<common::ObString> users_;
  common::ObSArray<common::ObString> hosts_;
  bool locked_;
};

struct ObAlterUserProfileArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);

public:
  ObAlterUserProfileArg() : ObDDLArg(),
    user_name_(), host_name_(), user_id_(common::OB_INVALID_ID),
    default_role_flag_(common::OB_INVALID_ID), role_id_array_(), user_ids_()
  { }
  virtual ~ObAlterUserProfileArg() {}
  TO_STRING_KV(K_(user_name), K_(host_name));


  common::ObString user_name_;
  common::ObString host_name_;
  uint64_t user_id_;
  uint64_t default_role_flag_;
  common::ObSEArray<uint64_t, 4> role_id_array_;
  common::ObSEArray<uint64_t, 4> user_ids_; //for set default role to multiple users
};

struct ObGrantArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);

public:
  ObGrantArg() : ObDDLArg(),
                 priv_level_(share::schema::OB_PRIV_INVALID_LEVEL),
                 priv_set_(0), users_passwd_(), hosts_(), need_create_user_(false),
                 has_create_user_priv_(false), roles_(), option_(0),
                 sys_priv_array_(), obj_priv_array_(),
                 object_type_(share::schema::ObObjectType::INVALID),
                 object_id_(common::OB_INVALID_ID), ins_col_ids_(),
                 upd_col_ids_(), ref_col_ids_(),
                 grantor_id_(common::OB_INVALID_ID), remain_roles_(), is_inner_(false),
		             sel_col_ids_(), column_names_priv_(), grantor_(), grantor_host_()
  { }
  virtual ~ObGrantArg() {}
  bool is_valid() const;
  virtual bool is_allow_when_disable_ddl() const;
  virtual bool contain_sensitive_data() const { return true; }

  TO_STRING_KV(K_(priv_level), K_(db), K_(table), K_(priv_set),
               K_(users_passwd), K_(hosts), K_(need_create_user), K_(has_create_user_priv),
               K_(option), K_(object_type), K_(object_id), K_(grantor_id), K_(ins_col_ids),
               K_(upd_col_ids), K_(ref_col_ids), K_(grantor_id), K_(column_names_priv),
               K_(grantor), K_(grantor_host));


  share::schema::ObPrivLevel priv_level_;
  common::ObString db_;
  common::ObString table_;
  ObPrivSet priv_set_;
  common::ObSArray<common::ObString> users_passwd_;//user_name1, pwd1; user_name2, pwd2
  common::ObSArray<common::ObString> hosts_;//hostname1, hostname2, ..
  bool need_create_user_;
  bool has_create_user_priv_;
  common::ObSArray<common::ObString> roles_;
  uint64_t option_;
  share::ObRawPrivArray sys_priv_array_;
  share::ObRawObjPrivArray obj_priv_array_;
  share::schema::ObObjectType object_type_;
  uint64_t object_id_;
  common::ObSEArray<uint64_t, 4> ins_col_ids_;
  common::ObSEArray<uint64_t, 4> upd_col_ids_;
  common::ObSEArray<uint64_t, 4> ref_col_ids_;
  uint64_t grantor_id_;
  // used to save the user_name and host_name that cannot be stored in role[0] and role[1]
  // Used to support grant xxx to multiple users.
  common::ObSArray<common::ObString> remain_roles_;
  bool is_inner_;
  common::ObSEArray<uint64_t, 4> sel_col_ids_;
  common::ObSEArray<std::pair<ObString, ObPrivType>, 4> column_names_priv_;
  common::ObString grantor_;
  common::ObString grantor_host_;
};


struct ObRevokeUserArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);

public:
  ObRevokeUserArg() : ObDDLArg(), user_id_(common::OB_INVALID_ID),
                      priv_set_(0), revoke_all_(false), role_ids_()
  { }
  bool is_valid() const;
  TO_STRING_KV(
               K_(user_id),
               "priv_set", share::schema::ObPrintPrivSet(priv_set_),
               K_(revoke_all),
               K_(role_ids));


  uint64_t user_id_;
  ObPrivSet priv_set_;
  bool revoke_all_;
  common::ObSArray<uint64_t> role_ids_;
};

struct ObRevokeDBArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);

public:
  ObRevokeDBArg() : ObDDLArg(), user_id_(common::OB_INVALID_ID),
                         priv_set_(0)
  { }
  bool is_valid() const;
  TO_STRING_KV(
               K_(user_id),
               K_(db),
               "priv_set", share::schema::ObPrintPrivSet(priv_set_));


  uint64_t user_id_;
  common::ObString db_;
  ObPrivSet priv_set_;
};

struct ObRevokeTableArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);

public:
  ObRevokeTableArg() : ObDDLArg(), user_id_(common::OB_INVALID_ID),
                            priv_set_(0), grant_(true), obj_id_(common::OB_INVALID_ID),
                            obj_type_(common::OB_INVALID_ID), grantor_id_(common::OB_INVALID_ID),
                            obj_priv_array_(), revoke_all_ora_(false), sel_col_ids_(), ins_col_ids_(),
                            upd_col_ids_(), ref_col_ids_(), column_names_priv_(),
                            grantor_(), grantor_host_()
  { }

  bool is_valid() const;

  TO_STRING_KV(
               K_(user_id),
               K_(db),
               K_(table),
               "priv_set", share::schema::ObPrintPrivSet(priv_set_),
               K_(grant),
               K_(obj_id),
               K_(obj_type),
               K_(grantor_id),
               K_(obj_priv_array),
               K_(column_names_priv),
               K_(grantor),
               K_(grantor_host));


  uint64_t user_id_;
  common::ObString  db_;
  common::ObString table_;
  ObPrivSet priv_set_;
  bool grant_;
  uint64_t obj_id_;
  uint64_t obj_type_;
  uint64_t grantor_id_;
  share::ObRawObjPrivArray obj_priv_array_;
  bool revoke_all_ora_;
  common::ObSEArray<uint64_t, 4> sel_col_ids_;
  common::ObSEArray<uint64_t, 4> ins_col_ids_;
  common::ObSEArray<uint64_t, 4> upd_col_ids_;
  common::ObSEArray<uint64_t, 4> ref_col_ids_;
  common::ObSEArray<std::pair<ObString, ObPrivType>, 4> column_names_priv_;
  common::ObString grantor_;
  common::ObString grantor_host_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObRevokeTableArg);
};

struct ObRevokeRoutineArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);

public:
  ObRevokeRoutineArg() : ObDDLArg(), user_id_(common::OB_INVALID_ID),
                            priv_set_(0), grant_(true), obj_id_(common::OB_INVALID_ID),
                            obj_type_(common::OB_INVALID_ID), grantor_id_(common::OB_INVALID_ID),
                            obj_priv_array_(), revoke_all_ora_(false), grantor_(), grantor_host_()
  { }
  bool is_valid() const;
  TO_STRING_KV(
               K_(user_id),
               K_(db),
               K_(routine),
               "priv_set", share::schema::ObPrintPrivSet(priv_set_),
               K_(grant),
               K_(obj_id),
               K_(obj_type),
               K_(grantor_id),
               K_(obj_priv_array),
               K_(grantor),
               K_(grantor_host));


  uint64_t user_id_;
  common::ObString db_;
  common::ObString routine_;
  ObPrivSet priv_set_;
  bool grant_;
  uint64_t obj_id_;
  uint64_t obj_type_;
  uint64_t grantor_id_;
  share::ObRawObjPrivArray obj_priv_array_;
  bool revoke_all_ora_;
  common::ObString grantor_;
  common::ObString grantor_host_;
};

struct ObRevokeSysPrivArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);

public:
  ObRevokeSysPrivArg() : ObDDLArg(),
                      grantee_id_(common::OB_INVALID_ID),
                      sys_priv_array_(), role_ids_()
  { }
  virtual ~ObRevokeSysPrivArg() {}
  bool is_valid() const;
  TO_STRING_KV(
               K_(grantee_id),
               K_(sys_priv_array),
               K_(role_ids));


  uint64_t grantee_id_;
  share::ObRawPrivArray sys_priv_array_;
  common::ObSArray<uint64_t> role_ids_;
};

struct ObCreateRoleArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);

public:
  ObCreateRoleArg() : ObDDLArg()
  {}
  virtual ~ObCreateRoleArg(){}
  bool is_valid() const;
  TO_STRING_KV(K_(user_infos));


  // role and user share the same user schema structure
  common::ObSArray<share::schema::ObUserInfo> user_infos_;
};

//----End of structs for managing privileges----

// system admin (alter system ...) rpc argument define

struct ObAdminSetConfigItem
{
  OB_UNIS_VERSION(1);
public:
  ObAdminSetConfigItem() : name_(), value_(), comment_() {}
  TO_STRING_KV(K_(name), K_(value), K_(comment));

  common::ObFixedLengthString<common::OB_MAX_CONFIG_NAME_LEN> name_;
  common::ObFixedLengthString<common::OB_MAX_CONFIG_VALUE_LEN> value_;
  common::ObFixedLengthString<common::OB_MAX_CONFIG_INFO_LEN> comment_;
};

struct ObAdminSetConfigArg
{
  OB_UNIS_VERSION(1);
public:
  ObAdminSetConfigArg() : items_(), is_inner_(false) {}
  ~ObAdminSetConfigArg() {}

  bool is_valid() const { return items_.count() > 0; }


  TO_STRING_KV(K_(items), K_(is_inner));

  common::ObSArray<ObAdminSetConfigItem> items_;
  bool is_inner_;
};

struct ObUpdateIndexStatusArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObUpdateIndexStatusArg():
    ObDDLArg(),
    index_table_id_(common::OB_INVALID_ID),
    status_(share::schema::INDEX_STATUS_MAX),
    convert_status_(true),
    in_offline_ddl_white_list_(false),
    data_table_id_(common::OB_INVALID_ID),
    database_name_(),
    task_id_(0),
    error_code_(OB_SUCCESS)
  {}
  bool is_valid() const;
  virtual bool is_allow_when_disable_ddl() const;
  virtual bool is_in_offline_ddl_white_list() const { return in_offline_ddl_white_list_; }
  TO_STRING_KV(K_(index_table_id), K_(status), K_(convert_status), K_(in_offline_ddl_white_list), K_(task_id), K_(error_code), K_(data_table_id), K_(database_name));

  uint64_t index_table_id_;
  share::schema::ObIndexStatus status_;
  bool convert_status_;
  bool in_offline_ddl_white_list_;
  uint64_t data_table_id_;
  ObString database_name_;
  int64_t task_id_;
  int error_code_;
};

struct ObMergeFinishArg
{
  OB_UNIS_VERSION(1);
public:
  ObMergeFinishArg():
    frozen_version_(0)
  {}

  bool is_valid() const { return server_.is_valid() && frozen_version_ > 0; }
  TO_STRING_KV(K_(server), K_(frozen_version));

  common::ObAddr server_;
  int64_t frozen_version_;
};

struct ObDebugSyncActionArg
{
  OB_UNIS_VERSION(1);
public:
  ObDebugSyncActionArg():
    reset_(false),
    clear_(false)
  {}

  bool is_valid() const { return reset_ || clear_ || action_.is_valid(); }
  TO_STRING_KV(K_(reset), K_(clear), K_(action));

  bool reset_;
  bool clear_;
  common::ObDebugSyncAction action_;
};


struct ObMinorFreezeArg
{
  OB_UNIS_VERSION(1);
public:
  ObMinorFreezeArg() {}
  int assign(const ObMinorFreezeArg &other);
  void reset()
  {
    tablet_id_.reset();
  }

  bool is_valid() const
  {
    return true;
  }

  TO_STRING_KV(K_(tablet_id));

  common::ObTabletID tablet_id_;
};

struct ObRootMinorFreezeArg
{
  OB_UNIS_VERSION(2);
public:
  ObRootMinorFreezeArg()
  {}
  void reset()
  {
    tablet_id_.reset();
    ls_id_.reset();
  }

  bool is_valid() const
  {
    return true;
  }

  TO_STRING_KV(K_(tablet_id), K_(ls_id));

  common::ObTabletID tablet_id_;
  share::ObLSID ls_id_;
};

struct ObTabletMajorFreezeArg
{
  OB_UNIS_VERSION(2);
public:
  ObTabletMajorFreezeArg()
    : ls_id_(),
      tablet_id_()
    {}
  ~ObTabletMajorFreezeArg() = default;
  bool is_valid() const
  {
    return ls_id_.is_valid() && tablet_id_.is_valid();
  }
  TO_STRING_KV(K_(ls_id), K_(tablet_id));

  share::ObLSID ls_id_;
  common::ObTabletID tablet_id_;
};


struct ObGetPartitionCountResult
{
  OB_UNIS_VERSION(1);

public:
  ObGetPartitionCountResult() : partition_count_(0) {}
  void reset() { partition_count_ = 0; }
  TO_STRING_KV(K_(partition_count));

  int64_t partition_count_;
};

inline void Int64::reset()
{
  v_ = common::OB_INVALID_ID;
}



struct ObCreateOutlineArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObCreateOutlineArg(): ObDDLArg(), or_replace_(false), outline_info_(), db_name_() {}
  virtual ~ObCreateOutlineArg() {}
  bool is_valid() const;
  TO_STRING_KV(K_(or_replace), K_(outline_info), K_(db_name));

  bool or_replace_;
  share::schema::ObOutlineInfo outline_info_;
  common::ObString db_name_;
};

struct ObAlterOutlineArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  enum AlterOutlineOptions {
    ADD_OUTLINE_CONTENT = 1,
    MAX_OPTION
  };
  ObAlterOutlineArg(): ObDDLArg(), alter_outline_info_(), db_name_() {}
  virtual ~ObAlterOutlineArg() {}
  bool is_valid() const
  {
    return (!db_name_.empty() && !alter_outline_info_.get_signature_str().empty()
            && !alter_outline_info_.get_outline_content_str().empty());
  }
  TO_STRING_KV(K_(alter_outline_info), K_(db_name));

  share::schema::ObAlterOutlineInfo alter_outline_info_;
  common::ObString db_name_;
};

struct ObDropOutlineArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObDropOutlineArg(): ObDDLArg(), db_name_(), outline_name_(), is_format_(false) {}
  virtual ~ObDropOutlineArg() {}
  bool is_valid() const;
  TO_STRING_KV(K_(db_name), K_(outline_name), K_(is_format));


  common::ObString db_name_;
  common::ObString outline_name_;
  bool is_format_;
};

struct ObUseDatabaseArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObUseDatabaseArg() : ObDDLArg()
  { }
  virtual ~ObUseDatabaseArg() {}
};

struct ObSetTracepointParam
{
public:
  ObSetTracepointParam()
    : event_no_(0),
      event_name_(),
      occur_(0),
      trigger_freq_(1),
      error_code_(0),
      cond_(0)
  {}

  bool is_valid() const
  {
    return error_code_ <= 0 && trigger_freq_ >= 0;
  }

   TO_STRING_KV(K_(event_no),
                K_(event_name),
                K_(occur),
                K_(trigger_freq),
                K_(error_code),
                K_(cond));

  int64_t event_no_;                 // tracepoint no
  common::ObString event_name_;      // tracepoint name
  int64_t occur_;                    // number of occurrences
  int64_t trigger_freq_;             // trigger frequency
  int64_t error_code_;               // error code to return
  int64_t cond_;                     // condition to match
};

struct ObCreateRoutineArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObCreateRoutineArg()
    : routine_info_(),
    db_name_(),
    is_or_replace_(false),
    is_need_alter_(false),
    error_info_(),
    dependency_infos_(),
    with_if_not_exist_(false) {}
  virtual ~ObCreateRoutineArg() {}
  bool is_valid() const;
  TO_STRING_KV(K_(routine_info),
               K_(db_name),
               K_(is_or_replace),
               K_(is_need_alter),
               K_(error_info),
               K_(dependency_infos),
               K_(with_if_not_exist));

  share::schema::ObRoutineInfo routine_info_;
  common::ObString db_name_;
  bool is_or_replace_;
  bool is_need_alter_; // used in mysql mode
  share::schema::ObErrorInfo error_info_;
  common::ObSArray<oceanbase::share::schema::ObDependencyInfo> dependency_infos_;
  bool with_if_not_exist_;
};

struct ObDropRoutineArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObDropRoutineArg()
    : db_name_(),
      routine_name_(),
      routine_type_(share::schema::INVALID_ROUTINE_TYPE),
      if_exist_(false),
      error_info_() {}
  virtual ~ObDropRoutineArg() {}
  bool is_valid() const;
  TO_STRING_KV(
               K_(db_name),
               K_(routine_name),
               K_(routine_type),
               K_(if_exist),
               K_(error_info));


  common::ObString db_name_;
  common::ObString routine_name_;
  share::schema::ObRoutineType routine_type_;
  bool if_exist_;
  share::schema::ObErrorInfo error_info_;
};

struct ObCreatePackageArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObCreatePackageArg()
      : is_replace_(false),
        is_editionable_(false),
        db_name_(),
        package_info_(),
        error_info_() {}
  virtual ~ObCreatePackageArg() {}
  bool is_valid() const;
  TO_STRING_KV(K_(is_replace), K_(is_editionable), K_(db_name),
               K_(package_info), K_(public_routine_infos), K(error_info_),
               K_(dependency_infos));

  bool is_replace_;
  bool is_editionable_;
  common::ObString db_name_;
  share::schema::ObPackageInfo package_info_;
  common::ObSArray<share::schema::ObRoutineInfo> public_routine_infos_;
  share::schema::ObErrorInfo error_info_;
  common::ObSArray<oceanbase::share::schema::ObDependencyInfo> dependency_infos_;
};

struct ObDropPackageArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObDropPackageArg()
    : db_name_(),
      package_name_(),
      package_type_(share::schema::INVALID_PACKAGE_TYPE),
      error_info_() {}
  virtual ~ObDropPackageArg() {}
  bool is_valid() const;
  TO_STRING_KV(K_(db_name), K_(package_name), K_(package_type), K_(error_info));


  common::ObString db_name_;
  common::ObString package_name_;
  share::schema::ObPackageType package_type_;
  share::schema::ObErrorInfo error_info_;
};

struct ObCreateTriggerArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObCreateTriggerArg()
    : ObDDLArg(),
      trigger_info_(),
      flags_(0),
      error_info_()
  {}
  virtual ~ObCreateTriggerArg() {}
  bool is_valid() const;
  TO_STRING_KV(K(trigger_database_),
               K(base_object_database_),
               K(base_object_name_),
               K(trigger_info_),
               K(with_replace_),
               K(in_second_stage_),
               K(with_if_not_exist_),
               K(error_info_),
               K(dependency_infos_));
public:
  common::ObString trigger_database_;
  common::ObString base_object_database_;
  common::ObString base_object_name_;
  share::schema::ObTriggerInfo trigger_info_;
  union
  {
    uint32_t flags_;
    struct
    {
      uint32_t with_replace_:1;
      uint32_t in_second_stage_:1; // is second create trigger stage
      uint32_t with_if_not_exist_:1;
      uint32_t reserved_:29;
    };
  };
  share::schema::ObErrorInfo error_info_;
  common::ObSArray<share::schema::ObDependencyInfo> dependency_infos_;
};

struct ObRoutineDDLRes
{
  OB_UNIS_VERSION(1);

public:
  ObRoutineDDLRes() :
    store_routine_schema_version_(OB_INVALID_VERSION)
  {}
  int assign(const ObRoutineDDLRes &other) {
    store_routine_schema_version_ = other.store_routine_schema_version_;
    return common::OB_SUCCESS;
  }
  TO_STRING_KV(K_(store_routine_schema_version));
  int64_t store_routine_schema_version_;
};

struct ObCreateTriggerRes
{
  OB_UNIS_VERSION(1);

public:
  ObCreateTriggerRes() :
    table_schema_version_(OB_INVALID_VERSION),
    trigger_schema_version_(OB_INVALID_VERSION)
  {}
  int assign(const ObCreateTriggerRes &other) {
    table_schema_version_ = other.table_schema_version_;
    trigger_schema_version_ = other.trigger_schema_version_;
    return common::OB_SUCCESS;
  }
  TO_STRING_KV(K_(table_schema_version), K_(trigger_schema_version));
  int64_t table_schema_version_;
  int64_t trigger_schema_version_;
};

struct ObDropTriggerArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObDropTriggerArg()
    : trigger_database_(),
      trigger_name_(),
      if_exist_(false)
  {}
  virtual ~ObDropTriggerArg() {}
  bool is_valid() const;
  TO_STRING_KV(K(1UL),
               K(trigger_database_),
               K(trigger_name_),
               K(if_exist_));


  common::ObString trigger_database_;
  common::ObString trigger_name_;
  bool if_exist_;
};

struct ObAlterTriggerArg: public ObDDLArg
{
OB_UNIS_VERSION(1);
public:
  ObAlterTriggerArg()
      :
      ObDDLArg(), trigger_database_(), trigger_info_(), trigger_infos_(),
      is_set_status_(false)
  {}
  virtual ~ObAlterTriggerArg()
  {}
  bool is_valid() const;
  TO_STRING_KV(K(trigger_database_),
      K(trigger_info_),
      K(trigger_infos_),
      K(is_set_status_))
  ;
public:
  common::ObString trigger_database_;           // deprecated
  share::schema::ObTriggerInfo trigger_info_;   // deprecated
  common::ObSArray<share::schema::ObTriggerInfo> trigger_infos_;
  bool is_set_status_;
};



struct ObCancelTaskArg
{
  OB_UNIS_VERSION(2);
public:
  ObCancelTaskArg() : task_id_()
  {}
  TO_STRING_KV(K_(task_id));
  share::ObTaskId task_id_;
};

struct ObForceCreateSysTableArg
{
  OB_UNIS_VERSION(1);
public:
  ObForceCreateSysTableArg() :
          table_id_(common::OB_INVALID_ID),
          last_replay_log_id_(common::OB_INVALID_ID) {}
  ~ObForceCreateSysTableArg() {}

  DECLARE_TO_STRING;

  uint64_t table_id_;
  uint64_t last_replay_log_id_;
};




struct ObDDLNopOpreatorArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObDDLNopOpreatorArg(): schema_operation_() {}
  ~ObDDLNopOpreatorArg() {}
public:
  share::schema::ObSchemaOperation schema_operation_;
  bool is_valid() const {
    return schema_operation_.is_valid();
  }
  void reset() {
    schema_operation_.reset();
  }
  TO_STRING_KV(K_(schema_operation));
private:
  DISALLOW_COPY_AND_ASSIGN(ObDDLNopOpreatorArg);
};

// end for ddl arg
//////////////////////////////////////////////////

struct ObEstPartArgElement
{
  ObEstPartArgElement() : batch_(), scan_flag_(),
    index_id_(common::OB_INVALID_ID), range_columns_count_(0), tablet_id_(), tx_id_()
  {}
  // Essentially, we can use ObIArray<ObNewRange> here
  // For compatibility reason, we still use ObSimpleBatch
  common::ObSimpleBatch batch_;
  common::ObQueryFlag scan_flag_;
  int64_t index_id_;
  int64_t range_columns_count_;
  ObTabletID tablet_id_;

  transaction::ObTransID tx_id_;

  TO_STRING_KV(
      K(scan_flag_),
      K(index_id_),
      K(batch_),
      K(range_columns_count_),
      K(tablet_id_),
      K(tx_id_));
  int64_t get_serialize_size(void) const;
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int deserialize(common::ObIAllocator &allocator,
                  const char *buf,
                  const int64_t data_len,
                  int64_t &pos);
};

struct ObEstPartArg
{
  //Deserialization use
  common::ObArenaAllocator allocator_;

  int64_t schema_version_;
  common::ObSEArray<ObEstPartArgElement, 4, common::ModulePageAllocator, true> index_params_;

  ObEstPartArg()
      : allocator_(common::ObModIds::OB_SQL_QUERY_RANGE),
        schema_version_(0),
        index_params_()
  {}
  ~ObEstPartArg() { reset(); }

  void reset();

  TO_STRING_KV(K_(schema_version),
               K_(index_params));

  OB_UNIS_VERSION(1);
};

struct ObEstPartResElement
{
  int64_t logical_row_count_;
  int64_t physical_row_count_;
  /**
   * @brief reliable_
   * storage estimation is not successfully called,
   * we use ndv to estimate row count in the following
   */
  bool reliable_;
  common::ObSEArray<common::ObEstRowCountRecord, 2, common::ModulePageAllocator, true> est_records_;

  ObEstPartResElement() {
    reset();
  }

  void reset()
  {
    logical_row_count_ = common::OB_INVALID_COUNT;
    physical_row_count_ = common::OB_INVALID_COUNT;
    reliable_ = false;
    est_records_.reset();
  }

  TO_STRING_KV(K(logical_row_count_), K(physical_row_count_), K(reliable_), K(est_records_));
  OB_UNIS_VERSION(1);
};

struct ObEstPartRes
{
  common::ObSEArray<ObEstPartResElement, 4, common::ModulePageAllocator, true> index_param_res_;

  ObEstPartRes() : index_param_res_()
  {}

  TO_STRING_KV(K(index_param_res_));

  OB_UNIS_VERSION(1);
};

struct ObGetWRSArg
{
  OB_UNIS_VERSION(1);
public:
  enum Scope
  {
    INVALID_RANGE = 0,
    INNER_TABLE,
    USER_TABLE,
    ALL_TABLE
  };
  TO_STRING_KV(K_(scope), K_(need_filter));

  Scope scope_; //The machine-readable timestamp can be calculated separately for the timestamp of the system table or user table, or collectively calculated together
  bool need_filter_;

  ObGetWRSArg() :
      scope_(ALL_TABLE),  // Statistics of all types of tables by default
      need_filter_(false) // Unreadable partitions are not filtered by default
  {}

};

struct ObGetWRSResult
{
  OB_UNIS_VERSION(1);

public:
  ObGetWRSResult() : self_addr_(), err_code_(0)
  {}

  void reset()
  {
    self_addr_.reset();
    err_code_ = 0;
  }

public:
  common::ObAddr  self_addr_;
  int err_code_;
  TO_STRING_KV(K_(err_code),
      K_(self_addr));
};

struct ObAlterTableResArg
{
  OB_UNIS_VERSION(1);
public:
  ObAlterTableResArg() :
  schema_type_(share::schema::OB_MAX_SCHEMA),
  schema_id_(common::OB_INVALID_ID),
  schema_version_(common::OB_INVALID_VERSION),
  part_object_id_(common::OB_INVALID_ID)
  {}
  ObAlterTableResArg(
      const share::schema::ObSchemaType schema_type,
      const uint64_t schema_id,
      const int64_t schema_version)
      : schema_type_(schema_type),
        schema_id_(schema_id),
        schema_version_(schema_version)
  {}
    ObAlterTableResArg(
      const share::schema::ObSchemaType schema_type,
      const uint64_t schema_id,
      const int64_t schema_version,
      const int64_t part_object_id)
      : schema_type_(schema_type),
        schema_id_(schema_id),
        schema_version_(schema_version),
        part_object_id_(part_object_id)
  {}
public:
  TO_STRING_KV(K_(schema_type), K_(schema_id), K_(schema_version), K_(part_object_id));
  share::schema::ObSchemaType schema_type_;
  uint64_t schema_id_;
  int64_t schema_version_;
  int64_t part_object_id_;
};

struct ObDDLRes final
{
  OB_UNIS_VERSION(1);
public:
  ObDDLRes()
    : schema_id_(common::OB_INVALID_ID), task_id_(0)
  {}
  ~ObDDLRes() = default;
  int assign(const ObDDLRes &other);
  void reset() {

    schema_id_ = common::OB_INVALID_ID;
    task_id_ = 0;
  }
  bool is_valid() {
    return common::OB_INVALID_ID != schema_id_
        && task_id_ > 0;
  }
  TO_STRING_KV(K_(schema_id), K_(task_id));
public:

  uint64_t schema_id_;
  int64_t task_id_;
};

struct ObParallelDDLRes
{
  OB_UNIS_VERSION(1);
public:
  ObParallelDDLRes():
  schema_version_(common::OB_INVALID_VERSION)
  {}
  int assign(const ObParallelDDLRes &other) {
    int ret = common::OB_SUCCESS;
    schema_version_ = other.schema_version_;
    return ret;
  }
public:
  TO_STRING_KV(K_(schema_version));
  int64_t schema_version_;
};

struct ObAlterTableRes
{
  OB_UNIS_VERSION(1);
public:
  ObAlterTableRes() :
  index_table_id_(common::OB_INVALID_ID),
  constriant_id_(common::OB_INVALID_ID),
  schema_version_(common::OB_INVALID_VERSION),
  res_arg_array_(),
  ddl_type_(share::DDL_INVALID),
  task_id_(0),
  ddl_res_array_(),
  ddl_need_retry_at_executor_(false)
  {}
  void reset();
  int assign(const ObAlterTableRes &other) {
    int ret = common::OB_SUCCESS;
    index_table_id_ = other.index_table_id_;
    constriant_id_ = other.constriant_id_;
    schema_version_ = other.schema_version_;
    if (OB_FAIL(res_arg_array_.assign(other.res_arg_array_))) {
      SHARE_LOG(WARN, "assign res_arg_array failed", K(ret), K(other.res_arg_array_));
    } else if (OB_FAIL(ddl_res_array_.assign(other.ddl_res_array_))) {
      SHARE_LOG(WARN, "assign ddl res array failed", K(ret));
    } else {
      ddl_type_ = other.ddl_type_;
      task_id_ = other.task_id_;
      ddl_need_retry_at_executor_ = other.ddl_need_retry_at_executor_;
    }
    return ret;
  }
public:
  TO_STRING_KV(K_(index_table_id), K_(constriant_id), K_(schema_version),
  K_(res_arg_array), K_(ddl_type), K_(task_id), K_(ddl_need_retry_at_executor));
  uint64_t index_table_id_;
  uint64_t constriant_id_;
  int64_t schema_version_;
  common::ObSArray<ObAlterTableResArg> res_arg_array_;
  share::ObDDLType ddl_type_;
  int64_t task_id_;
  common::ObSArray<ObDDLRes> ddl_res_array_;
  bool ddl_need_retry_at_executor_;
};

struct ObDropDatabaseRes final
{
  OB_UNIS_VERSION(1);
public:
  ObDropDatabaseRes()
    : ddl_res_(),
    affected_row_(0)
  {}
  bool is_valid() {
    return ddl_res_.is_valid();
  }
public:
  TO_STRING_KV(K_(ddl_res), K_(affected_row));
  ObDDLRes ddl_res_;
  UInt64 affected_row_;
};
struct ObGetRuntimeSchemaVersionArg
{
  OB_UNIS_VERSION(1);
public:
  ObGetRuntimeSchemaVersionArg() {}
  bool is_valid() const { return true; }

  TO_STRING_EMPTY();

};

struct ObGetRuntimeSchemaVersionResult
{
  OB_UNIS_VERSION(1);
public:
  ObGetRuntimeSchemaVersionResult() : schema_version_(common::OB_INVALID_VERSION) {}
  bool is_valid() const { return schema_version_ > 0; }

  TO_STRING_KV(K_(schema_version));
  int64_t schema_version_;
};


struct ObDependencyObjDDLArg: public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObDependencyObjDDLArg()
    : reset_view_column_infos_(false)
  {
  }
  bool is_valid() const { return 1UL != OB_INVALID_ID; }
  TO_STRING_KV(
               K_(insert_dep_objs),
               K_(update_dep_objs),
               K_(delete_dep_objs),
               K_(reset_view_column_infos));


  share::schema::ObReferenceObjTable::DependencyObjKeyItemPairs insert_dep_objs_;
  share::schema::ObReferenceObjTable::DependencyObjKeyItemPairs update_dep_objs_;
  share::schema::ObReferenceObjTable::DependencyObjKeyItemPairs delete_dep_objs_;
  share::schema::ObTableSchema schema_;
  bool reset_view_column_infos_;
};

struct CheckLeaderRpcIndex
{
  OB_UNIS_VERSION(1);
public:
  int64_t switchover_timestamp_; //Switch logo
  int64_t epoch_;  //(switchover_timestamp, epoch) uniquely identifies a statistical information during a switching process

  int64_t ml_pk_index_;  //Position the coordinates of pkey
  int64_t pkey_info_start_index_; //Position the coordinates of pkey
  CheckLeaderRpcIndex()
    : switchover_timestamp_(0), epoch_(0),
      ml_pk_index_(0), pkey_info_start_index_(0) {};
  ~CheckLeaderRpcIndex() {reset();}
  bool is_valid() const;
  void reset();
  TO_STRING_KV(K_(switchover_timestamp), K_(epoch),
               K_(ml_pk_index), K_(pkey_info_start_index));
};





enum TransToolCmd
{
  MODIFY = 0,
  DUMP = 1,
  KILL = 2
};



struct ObDDLLocalBuildResponse final
{
  OB_UNIS_VERSION(2);
public:
  ObDDLLocalBuildResponse()
    : tablet_id_(),
      source_table_id_(OB_INVALID_ID),
      dest_schema_id_(OB_INVALID_ID),
      ret_code_(OB_SUCCESS),
      snapshot_version_(0),
      schema_version_(0),
      task_id_(0),
      execution_id_(-1),
      row_scanned_(0),
      row_inserted_(0),
      dest_schema_version_(0),
      server_addr_(),
      physical_row_count_(0)
  {}
  ~ObDDLLocalBuildResponse() = default;
  bool is_valid() const {
    return tablet_id_.is_valid() &&
           OB_INVALID_ID != source_table_id_ &&
           OB_INVALID_ID != dest_schema_id_ &&
           snapshot_version_ > 0 &&
           schema_version_ > 0 &&
           dest_schema_version_ > 0 &&
           task_id_ > 0 &&
           execution_id_ >= 0 &&
           server_addr_.is_valid();
  }
  TO_STRING_KV(K_(tablet_id), K_(source_table_id), K_(dest_schema_id), K_(ret_code),
               K_(snapshot_version), K_(schema_version), K_(dest_schema_version), K_(task_id),
               K_(execution_id), K_(row_scanned), K_(row_inserted), K_(server_addr), K_(physical_row_count));
public:

  ObTabletID tablet_id_;
  int64_t source_table_id_;
  int64_t dest_schema_id_;
  int ret_code_;
  int64_t snapshot_version_;
  int64_t schema_version_;
  int64_t task_id_;
  int64_t execution_id_;
  int64_t row_scanned_;
  int64_t row_inserted_;

  int64_t dest_schema_version_;
  common::ObAddr server_addr_;
  int64_t physical_row_count_;
};























struct ObSwitchSchemaResult
{
  OB_UNIS_VERSION(1);
public:
  ObSwitchSchemaResult() : ret_(common::OB_SUCCESS) {}
  ~ObSwitchSchemaResult() {}
  int assign(const ObSwitchSchemaResult &other)
  {
    ret_ = other.ret_;
    return common::OB_SUCCESS;
  }
  void reset() { ret_ = common::OB_SUCCESS; }
  void set_ret(int ret) { ret_ = ret; }
  int get_ret() const { return ret_; }
  TO_STRING_KV(K_(ret));
private:
  int ret_;
};

struct ObRuntimeConfigArg
{
  OB_UNIS_VERSION(1);
public:
  ObRuntimeConfigArg() : config_str_() {}
  bool is_valid() const { return !config_str_.empty(); }
  int assign(const ObRuntimeConfigArg &other);

  common::ObString config_str_;
  TO_STRING_KV(K_(config_str));
};


struct ObFlushOptStatArg
{
  OB_UNIS_VERSION(1);
public:
  ObFlushOptStatArg() : is_flush_col_usage_(false), is_flush_dml_stat_(false) {}
  ObFlushOptStatArg(const bool is_flush_col_usage,
                    const bool is_flush_dml_stat) :
    is_flush_col_usage_(is_flush_col_usage),
    is_flush_dml_stat_(is_flush_dml_stat)
  {}
  bool is_valid() const { return 1UL > 0; }

  bool is_flush_col_usage_;
  bool is_flush_dml_stat_;
  TO_STRING_KV(K_(is_flush_col_usage), K_(is_flush_dml_stat));
};

struct ObCancelDDLTaskArg final
{
  OB_UNIS_VERSION(1);
public:
  ObCancelDDLTaskArg();
  explicit ObCancelDDLTaskArg(const ObCurTraceId::TraceId &task_id);
  ~ObCancelDDLTaskArg() = default;
  bool is_valid() const { return !task_id_.is_invalid(); }
  const ObCurTraceId::TraceId &get_task_id() const{ return task_id_; }
  TO_STRING_KV(K_(task_id));
private:
  ObCurTraceId::TraceId task_id_;
};

struct ObEstBlockArgElement
{
  OB_UNIS_VERSION(1);
public:
  ObEstBlockArgElement() : tablet_id_() {}
  bool is_valid() const { return tablet_id_.is_valid(); }
  int assign(const ObEstBlockArgElement &other);

  ObTabletID tablet_id_;
  TO_STRING_KV(K_(tablet_id));
};

struct ObEstBlockArg
{
  OB_UNIS_VERSION(1);
public:
  common::ObSEArray<ObEstBlockArgElement, 4> tablet_params_arg_;
  bool is_valid() const;
  ObEstBlockArg() : tablet_params_arg_() {}
  TO_STRING_KV(K(tablet_params_arg_));
};

struct ObEstBlockResElement
{
  OB_UNIS_VERSION(1);
public:
  int64_t macro_block_count_;
  int64_t micro_block_count_;
  int64_t sstable_row_count_;
  int64_t memtable_row_count_;
  bool is_valid() const { return true; }
  int assign(const ObEstBlockResElement &other);
  ObEstBlockResElement() : macro_block_count_(0), micro_block_count_(0), sstable_row_count_(0), memtable_row_count_(0) {}
  TO_STRING_KV(K(macro_block_count_), K(micro_block_count_),
      K(sstable_row_count_), K(memtable_row_count_));
};

struct ObEstBlockRes
{
  OB_UNIS_VERSION(1);
public:
  common::ObSEArray<ObEstBlockResElement, 4> tablet_params_res_;
  bool is_valid() const { return true; }
  ObEstBlockRes() : tablet_params_res_() {}
  TO_STRING_KV(K(tablet_params_res_));
};

struct ObBatchGetTabletAutoincSeqArg final
{
  OB_UNIS_VERSION(2);
public:
  ObBatchGetTabletAutoincSeqArg()
    : src_tablet_ids_(), dest_tablet_ids_()
  {}
  ~ObBatchGetTabletAutoincSeqArg() {}
public:
  int assign(const ObBatchGetTabletAutoincSeqArg &other);
  bool is_valid() const
  {
    return src_tablet_ids_.count() > 0
            && src_tablet_ids_.count() == dest_tablet_ids_.count();
  }
  int init(const ObIArray<share::ObTabletAutoincSeqCopyParam> &params);
  TO_STRING_KV(K_(src_tablet_ids), K_(dest_tablet_ids));
public:
  common::ObSEArray<common::ObTabletID, 1> src_tablet_ids_;
  common::ObSEArray<common::ObTabletID, 1> dest_tablet_ids_;
};

struct ObBatchGetTabletAutoincSeqRes final
{
  OB_UNIS_VERSION(1);
public:
  ObBatchGetTabletAutoincSeqRes() : autoinc_params_() {}
  ~ObBatchGetTabletAutoincSeqRes() {}
public:
  bool is_valid() const { return autoinc_params_.count() > 0; }
  TO_STRING_KV(K_(autoinc_params));
public:
  common::ObSEArray<share::ObTabletAutoincSeqCopyParam, 1> autoinc_params_;
};

struct ObBatchSetTabletAutoincSeqArg final
{
  OB_UNIS_VERSION(2);
public:
  ObBatchSetTabletAutoincSeqArg()
    : autoinc_params_(), is_tablet_creating_(false)
  {}
  ~ObBatchSetTabletAutoincSeqArg() {}
public:
  int assign(const ObBatchSetTabletAutoincSeqArg &other);
  bool is_valid() const { return autoinc_params_.count() > 0; }
  int init(const ObIArray<share::ObTabletAutoincSeqCopyParam> &params);
  void reset();
  TO_STRING_KV(K_(autoinc_params), K_(is_tablet_creating));
public:
  common::ObSEArray<share::ObTabletAutoincSeqCopyParam, 1> autoinc_params_;
  bool is_tablet_creating_;
};

struct ObBatchSetTabletAutoincSeqRes final
{
  OB_UNIS_VERSION(1);
public:
  ObBatchSetTabletAutoincSeqRes() : autoinc_params_() {}
  ~ObBatchSetTabletAutoincSeqRes() {}
public:
  bool is_valid() const { return autoinc_params_.count() > 0; }
  TO_STRING_KV(K_(autoinc_params));
public:
  common::ObSEArray<share::ObTabletAutoincSeqCopyParam, 1> autoinc_params_;
};

struct ObBatchGetTabletBindingArg final
{
  OB_UNIS_VERSION(2);
public:
  ObBatchGetTabletBindingArg()
    : tablet_ids_(), check_committed_(false)
  {}
  ~ObBatchGetTabletBindingArg() {}
public:
  bool is_valid() const { return tablet_ids_.count() > 0; }
  int init(const common::ObIArray<common::ObTabletID> &tablet_ids, const bool check_committed);
  TO_STRING_KV(K_(tablet_ids), K_(check_committed));
public:
  common::ObSArray<common::ObTabletID> tablet_ids_;
  bool check_committed_;
};

struct ObInitRuntimeConfigArg
{
  OB_UNIS_VERSION(1);
public:
  ObInitRuntimeConfigArg() : configs_() {}
  ~ObInitRuntimeConfigArg() {}
  bool is_valid() const { return configs_.count() > 0; }
  int assign(const ObInitRuntimeConfigArg &other);
  int add_config(const ObRuntimeConfigArg &arg);
  const common::ObSArray<ObRuntimeConfigArg> &get_configs() const { return configs_; }
  TO_STRING_KV(K_(configs));
private:
  common::ObSArray<ObRuntimeConfigArg> configs_;
};

struct ObInitRuntimeConfigRes
{
  OB_UNIS_VERSION(1);
public:
  ObInitRuntimeConfigRes() : ret_(common::OB_ERROR) {}
  ~ObInitRuntimeConfigRes() {}
  void set_ret(int ret) { ret_ = ret; }
  int64_t get_ret() const { return ret_; }
  TO_STRING_KV(K_(ret));
private:
  int ret_;
};

struct ObRecompileAllViewsBatchArg: public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObRecompileAllViewsBatchArg()
    : view_ids_()
  {
  }
  bool is_valid() const { return 1UL != OB_INVALID_ID && !view_ids_.empty(); }
  TO_STRING_KV(K_(view_ids));


  ObSArray<uint64_t> view_ids_;
};

struct ObCancelGatherStatsArg
{
  OB_UNIS_VERSION(1);
public:
  ObCancelGatherStatsArg() : task_id_() {}
  bool is_valid() const { return true; }

  common::ObString task_id_;
  TO_STRING_KV(K(task_id_));
};








struct ObCreateTableGroupRes : ObParallelDDLRes
{
  OB_UNIS_VERSION(1);
public:
  ObCreateTableGroupRes()
    : ObParallelDDLRes(),
      tablegroup_id_(OB_INVALID_ID)
  {}
  ~ObCreateTableGroupRes() = default;
  uint64_t tablegroup_id_;
};

struct ObCreateAiModelArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObCreateAiModelArg() : ObDDLArg(), model_info_() {}
  ObCreateAiModelArg(const share::ObAiServiceModelInfo &model_info)
  : ObDDLArg(), model_info_(model_info)
  {

  }
  ~ObCreateAiModelArg() {}
  int check_valid() const;
  int assign(const ObCreateAiModelArg &other);
  const share::ObAiServiceModelInfo &get_model_info() const { return model_info_; }
  TO_STRING_KV(K_(model_info));
  share::ObAiServiceModelInfo model_info_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreateAiModelArg);
};

struct ObDropAiModelArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObDropAiModelArg() : ObDDLArg(), ai_model_name_() {}
  ObDropAiModelArg(const ObString &ai_model_name)
  : ObDDLArg(),
    ai_model_name_(ai_model_name)
  {

  }
  ~ObDropAiModelArg() {}
  bool is_valid() const { return !ai_model_name_.empty(); }
  const ObString &get_ai_model_name() const { return ai_model_name_; }
  int assign(const ObDropAiModelArg &other);
  TO_STRING_KV(K_(ai_model_name));
  ObString ai_model_name_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObDropAiModelArg);
};

struct ObRevokeObjMysqlArg : public ObDDLArg
{
  OB_UNIS_VERSION(1);

public:
  ObRevokeObjMysqlArg() : ObDDLArg(), user_id_(common::OB_INVALID_ID),
                            obj_name_(), obj_type_(common::OB_INVALID_ID),
                            priv_set_(0), grant_(true),
                            grantor_(), grantor_host_()
  { }
  bool is_valid() const;
  int assign(const ObRevokeObjMysqlArg &other);
  TO_STRING_KV(K_(user_id),
               K_(obj_name),
               "priv_set", share::schema::ObPrintPrivSet(priv_set_),
               K_(grant),
               K_(obj_type),
               K_(grantor),
               K_(grantor_host));


  uint64_t user_id_;
  common::ObString obj_name_;
  uint64_t obj_type_;
  ObPrivSet priv_set_;
  bool grant_;
  common::ObString grantor_;
  common::ObString grantor_host_;
};

}//end namespace obcall
}//end namespace oceanbase
#endif
