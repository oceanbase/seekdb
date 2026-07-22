# -*- coding: utf-8 -*-
# Copyright 2010 - 2020 Alibaba Inc. All Rights Reserved.
# Author:
#
# OB use disjoint table_id ranges to define different kinds of tables:
# - (0, 100)         : Core Table
# - (0, 10000)       : System Table
# - (10000, 15000)   : MySQL Virtual Table
# - (15000, 20000)   : Extended Virtual Table
# - (20000, 25000)   : MySQL System View
# - (25000, 30000)   : Extended System View
# - (50000, 60000)   : Lob meta table
# - (60000, 70000)   : Lob piece table
# - (100000, 200000) : System table Index
# - (15305, <20000)  : Real Agent table Index
# - (500000, ~)      : User Table
#
# Here are some table_name definition principles.
# 1. Be defined by simple present tense, first person.
# 2. Be active and singular.
# 3. System table's table_name should be started with '__all_'.
# 4. Virtual table's table_name should be started with '__all_virtual' or '__tenant_virtual'.
#    Virtual table started with '__all_virtual' can be directly queried by SQL.
#    Virtual table started with '__tenant_virtual' is used for special cmd(such as show cmd), which can't be queried by SQL.
# 5. System view's table_name should follow system view naming conventions.
# 6. Definitions in extended virtual table/system view ranges can be referred from document:
#
# 7. Difference between REAL_AGENT and SYS_AGENT:
#     sys_agent access tables belong to sys tenant only
#     real_agent access tables belong to current tenant
# 8. Virtual table system design summary:
#
# 9. For compatibility, when add new column for system table, new column's definition should be "not null + default value" or "nullable".
#    Specially, when column types are as follows:
#    1. double、number：default value is not supported, so new column definition should be "nullable".
#    2. longtext、timestamp：mysql can't cast default value to specified column type, so new column definition should be "nullable".
#
# Add internal table encoding guidelines see:
################################################################################

################################################################################
# Column definition:
# - Use [column_name, column_type, nullable, default_value] to specify column definition.
# - Use lowercase to define column names.
# - Define primary keys in rowkey_columns, and define other columns in normal_columns.
#
# Partition definition:
# - Defined by partition_expr and partition_columns.
# - Use [partition_type, expr, partition_num] to define partition_expr.
# - Use [col1, col2, ...] to define partition_columns.
# - Two different partition_type are supported: hash/key
#   - hash: expr means expression.
#   - key: expr means list of partition columns.
# - All virtual tables use local routing policy (svr_ip/svr_port removed).
# - rowkey_columns must contains columns defined in partition_columns.
################################################################################
################################### Placeholder Notice ###################################
# Placeholder example: Write comments at the beginning of the line to indicate which TABLE_ID is to be occupied and what the corresponding name is
# TABLE_ID: TABLE_NAME
#
# FARM will base the placeholder validation development branch TABLE_ID and TABLE_NAME match check, if they do not match, FARM will intercept and report an error
#
# Note:
# 0. Placeholder before 'reserved position'
# 1. Always start by occupying the master, ensuring that the master branch is a superset of all other branches to avoid NAME and ID conflicts
# 2. After the master placeholder is set, do not change NAME on the development branch, otherwise FARM will consider it an ID placeholder conflict. If this scenario occurs, you need to modify the master placeholder first
# 3. It is recommended to use the accurate TABLE_NAME for placeholder, TABLE_ID and TABLE_NAME are one-to-one corresponding within the system
# 4. Some tables are defined based on the schema of other base tables (e.g., gen_xx_table_def()), their actual table names are relatively complex, to facilitate placeholder usage, it is recommended to use the base table name for placeholders
#    - Example 1: def_table_schema(**gen_mysql_sys_agent_virtual_table_def('12393', all_def_keywords['__all_virtual_long_ops_status']))
#      * Base table name placeholder: # 12393: __all_virtual_long_ops_status
#      * Real table name placeholder: # 12393: __all_virtual_virtual_long_ops_status_mysql_sys_agent
#      * Base table name placeholder: # 15009: __all_virtual_sql_audit
#      * Real table name placeholder: # 15009: ALL_VIRTUAL_SQL_AUDIT
#    - Example 3: def_table_schema(**gen_sys_agent_virtual_table_def('15111', all_def_keywords['__all_routine_param']))
#      * Base table name placeholder: # 15111: __all_routine_param
#      * Real table name placeholder: # 15111: ALL_VIRTUAL_ROUTINE_PARAM_SYS_AGENT
# 5. Index table placeholder requirements TABLE_NAME should be used as follows: base table (data table) name, index name (index_name), actual index table name
#    For example: 100001 The placeholder method for the index table can be:
#       * # 100001: __idx_3_idx_data_table_id
#       * # 100001: idx_data_table_id
#       * # 100001: __all_table
################################################################################

################################################################################
# SQLite Table Definitions
# These tables are stored in SQLite and do not have table_id (they are not
# OceanBase system tables). They are defined here for code generation purposes.
# SQLite tables are defined before OceanBase system tables to ensure they are
# available when virtual tables reference them.
################################################################################

# __all_merge_info: SQLite table for global merge info
gen_sqlite_table_def(
  table_name = '__all_merge_info',
  columns = [
      ('id', 'INTEGER', 'NOT NULL DEFAULT 0', None),
      ('frozen_scn', 'INTEGER', 'NOT NULL', None),
      ('global_broadcast_scn', 'INTEGER', 'NOT NULL', None),
      ('is_merge_error', 'INTEGER', 'NOT NULL', None),
      ('last_merged_scn', 'INTEGER', 'NOT NULL', None),
      ('merge_status', 'INTEGER', 'NOT NULL', None),
      ('error_type', 'INTEGER', 'NOT NULL', None),
      ('suspend_merging', 'INTEGER', 'NOT NULL', None),
      ('merge_start_time', 'INTEGER', 'NOT NULL', None),
      ('last_merged_time', 'INTEGER', 'NOT NULL', None)
  ],
  primary_key = ['id']
  )

# __all_column_checksum_error_info: SQLite table for column checksum error info
gen_sqlite_table_def(
  table_name = '__all_column_checksum_error_info',
  columns = [
      ('frozen_scn', 'INTEGER', 'NOT NULL', None),
      ('index_type', 'INTEGER', 'NOT NULL', None),
      ('data_table_id', 'INTEGER', 'NOT NULL', None),
      ('index_table_id', 'INTEGER', 'NOT NULL', None),
      ('data_tablet_id', 'INTEGER', 'NOT NULL', None),
      ('index_tablet_id', 'INTEGER', 'NOT NULL', None),
      ('column_id', 'INTEGER', 'NOT NULL', None),
      ('data_column_checksum', 'INTEGER', 'NOT NULL', None),
      ('index_column_checksum', 'INTEGER', 'NOT NULL', None)
  ],
  primary_key = ['frozen_scn', 'index_type', 'data_table_id', 'index_table_id', 'data_tablet_id', 'index_tablet_id']
  )

# __all_deadlock_event_history: SQLite table for deadlock event history
gen_sqlite_table_def(
  table_name = '__all_deadlock_event_history',
  columns = [
      ('event_id', 'INTEGER', 'NOT NULL', None),
      ('detector_id', 'INTEGER', 'NOT NULL', None),
      ('report_time', 'INTEGER', 'NOT NULL', None),
      ('cycle_idx', 'INTEGER', 'NOT NULL', None),
      ('cycle_size', 'INTEGER', 'NOT NULL', None),
      ('role', 'TEXT', 'NULL', None),
      ('priority_level', 'TEXT', 'NULL', None),
      ('priority', 'INTEGER', 'NOT NULL', None),
      ('create_time', 'INTEGER', 'NOT NULL', None),
      ('start_delay', 'INTEGER', 'NOT NULL', None),
      ('module', 'TEXT', 'NULL', None),
      ('visitor', 'TEXT', 'NULL', None),
      ('object', 'TEXT', 'NULL', None),
      ('extra_name1', 'TEXT', 'NULL', None),
      ('extra_value1', 'TEXT', 'NULL', None),
      ('extra_name2', 'TEXT', 'NULL', None),
      ('extra_value2', 'TEXT', 'NULL', None),
      ('extra_name3', 'TEXT', 'NULL', None),
      ('extra_value3', 'TEXT', 'NULL', None)
  ],
  primary_key = ['event_id', 'detector_id']
  )

# __all_tablet_meta_table: SQLite table for tablet meta
gen_sqlite_table_def(
  table_name = '__all_tablet_meta_table',
  columns = [
      ('gmt_create', 'INTEGER', 'NULL', None),
      ('gmt_modified', 'INTEGER', 'NULL', None),
      ('tablet_id', 'INTEGER', 'NOT NULL', None),
      ('compaction_scn', 'INTEGER', 'NOT NULL', None),
      ('data_size', 'INTEGER', 'NOT NULL', None),
      ('required_size', 'INTEGER', 'NOT NULL', '0'),
      ('report_scn', 'INTEGER', 'NOT NULL', '0'),
      ('status', 'INTEGER', 'NOT NULL', '0')
  ],
  primary_key = ['tablet_id']
  )

# __all_tablet_replica_checksum: SQLite table for tablet replica checksum
gen_sqlite_table_def(
  table_name = '__all_tablet_replica_checksum',
  columns = [
      ('tablet_id', 'INTEGER', 'NOT NULL', None),
      ('compaction_scn', 'INTEGER', 'NOT NULL', None),
      ('row_count', 'INTEGER', 'NOT NULL', None),
      ('data_checksum', 'INTEGER', 'NOT NULL', None),
      ('column_checksums', 'TEXT', 'NULL', None),
      ('b_column_checksums', 'BLOB', 'NULL', None),
      ('data_checksum_type', 'INTEGER', 'NOT NULL', '0')
  ],
  primary_key = ['tablet_id']
  )

# __all_sys_parameter: SQLite table for sys parameter
gen_sqlite_table_def(
  table_name = '__all_sys_parameter',
  columns = [
      ('gmt_create', 'INTEGER', 'NULL', None),
      ('gmt_modified', 'INTEGER', 'NULL', None),
      ('name', 'TEXT', 'NOT NULL', None),
      ('data_type', 'TEXT', 'NULL', None),
      ('value', 'TEXT', 'NOT NULL', None),
      ('value_strict', 'TEXT', 'NULL', None),
      ('info', 'TEXT', 'NULL', None),
      ('need_reboot', 'INTEGER', 'NULL', None),
      ('section', 'TEXT', 'NULL', None),
      ('visible_level', 'TEXT', 'NULL', None),
      ('config_version', 'INTEGER', 'NOT NULL', None),
      ('scope', 'TEXT', 'NULL', None),
      ('source', 'TEXT', 'NULL', None),
      ('edit_level', 'TEXT', 'NULL', None)
  ],
  primary_key = ['name']
  )

gen_sqlite_table_def(
    table_name = '__all_rootservice_job',
    columns = [
        ('job_id', 'INTEGER', 'NOT NULL', '0'),
        ('gmt_create', 'INTEGER', 'NULL', None),
        ('gmt_modified', 'INTEGER', 'NULL', None),
        ('job_type', 'TEXT', 'NOT NULL', "''"),
        ('job_status', 'TEXT', 'NOT NULL', "''"),
        ('result_code', 'INTEGER', 'NULL', None)
  ],
    primary_key = ['job_id']
  )

################################################################################
# OceanBase System Table Definitions
################################################################################

global fields
fields = [
    'tablegroup_id',
    'database_id',
    'table_id',
    'rowkey_split_pos',
    'is_use_bloomfilter',
    'progressive_merge_num',
    'rowkey_column_num', # This field will be calculated by rowkey_columns automatically.
    'load_type',
    'table_type',
    'index_type',
    'def_type',
    'table_name',
    'compress_func_name',
    'part_level',
    'charset_type',
    'collation_type',
    'gm_columns',
    'rowkey_columns',
    'normal_columns',
    'partition_columns',
    'in_tenant_space',
    'view_definition',
    'partition_expr',
    'index',
    'index_using_type',
    'name_postfix',
    'row_store_type',
    'store_format',
    'progressive_merge_round',
    'storage_format_version',
    'is_cluster_private',
    'is_real_virtual_table',
    'owner',
    'vtable_route_policy', # value: only_rs, distributed, local(default)
    'tablet_id',
    'micro_index_clustered'
]

global index_only_fields
index_only_fields = ['index_name', 'index_columns', 'index_status', 'index_type', 'data_table_id', 'storing_columns']

global lob_fields
lob_fields = ['data_table_id']

global default_filed_values
default_filed_values = {
    'tablegroup_id' : 'OB_SYS_TABLEGROUP_ID',
    'database_id' : 'OB_SYS_DATABASE_ID',
    'table_type' : 'MAX_TABLE_TYPE',
    'index_type' : 'INDEX_TYPE_IS_NOT',
    'load_type' : 'TABLE_LOAD_TYPE_IN_DISK',
    'def_type' : 'TABLE_DEF_TYPE_INTERNAL',
    'rowkey_split_pos' : '0',
    'compress_func_name' : 'OB_DEFAULT_COMPRESS_FUNC_NAME',
    'part_level' : 'PARTITION_LEVEL_ZERO',
    'is_use_bloomfilter' : 'false',
    'progressive_merge_num' : '0',
    'charset_type' : 'ObCharset::get_default_charset()',
    'collation_type' : 'ObCharset::get_default_collation(ObCharset::get_default_charset())',
    'in_tenant_space' : False,
    'view_definition' : '',
    'partition_expr' : [],
    'partition_columns' : [],
    'index' : [],
    'index_using_type' : 'USING_BTREE',
    'name_postfix': '',
    'row_store_type': 'ENCODING_ROW_STORE',
    'store_format': 'OB_STORE_FORMAT_DYNAMIC_MYSQL',
    'progressive_merge_round' : '1',
    'storage_format_version' : '3',
    'is_cluster_private': False,
    'is_real_virtual_table': False,
    'owner' : '',
    'vtable_route_policy' : 'local',
    'tablet_id' : '0',
    'micro_index_clustered' : 'false'
}

################################################################################
# System Table(0,10000]
################################################################################

global lob_aux_data_def
lob_aux_data_def = dict (
  owner = 'luohongdi.lhd',
  gm_columns = [],
  rowkey_columns = [
    ('piece_id', 'uint')
  ],
  normal_columns = [
    ('data_len', 'uint32'),
    ('lob_data', 'varbinary:32')
  ]
  )

global lob_aux_meta_def
lob_aux_meta_def = dict (
  owner = 'luohongdi.lhd',
  gm_columns = [],
  rowkey_columns = [
    ('lob_id', 'varbinary:16'),
    ('seq_id', 'varbinary:8192')
  ],

  normal_columns = [
    ('binary_len', 'uint32'),
    ('char_len', 'uint32'),
    ('piece_id', 'uint'),
    ('lob_data', 'varbinary:262144')
  ]
  )

#
# Core Table (0, 100]
#
def_table_schema(
    owner = 'yanmu.ztl',
    table_name    = '__all_core_table',
    table_id      = '1',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('table_name', 'varchar:OB_MAX_CORE_TALBE_NAME_LENGTH'),
        ('row_id', 'int'),
        ('column_name', 'varchar:OB_MAX_COLUMN_NAME_LENGTH')
  ],
    in_tenant_space = True,
    is_core_related = True,

  normal_columns = [
      ('column_value', 'varchar:OB_OLD_MAX_VARCHAR_LENGTH', 'true')
  ]
  )

# 2: __all_root_table # abandoned in 4.0.

all_table_def = dict(
    owner = 'yanmu.ztl',
    table_name    = '__all_table',
    table_id      = '3',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('table_id', 'int')
  ],
    in_tenant_space = True,
    is_core_related = True,

    normal_columns = [
      ('table_name', 'varchar:OB_MAX_TABLE_NAME_LENGTH', 'false', ''),
      ('database_id', 'int'),
      ('table_type', 'int'),
      ('load_type', 'int'),
      ('def_type', 'int'),
      ('rowkey_column_num', 'int'),
      ('index_column_num', 'int'),
      ('max_used_column_id', 'int'),
      ('autoinc_column_id', 'int'),
      ('auto_increment', 'uint', 'true', '1'),
      ('read_only', 'int'),
      ('rowkey_split_pos', 'int'),
      ('compress_func_name', 'varchar:OB_MAX_COMPRESSOR_NAME_LENGTH'),
      ('expire_condition', 'varchar:OB_MAX_EXPIRE_INFO_STRING_LENGTH'),
      ('is_use_bloomfilter', 'int'),
      ('comment', 'varchar:MAX_TABLE_COMMENT_LENGTH', 'false', ''),
      ('block_size', 'int'),
      ('collation_type', 'int'),
      ('data_table_id', 'int', 'true'),
      ('index_status', 'int'),
      ('tablegroup_id', 'int'),
      ('progressive_merge_num', 'int'),
      ('index_type', 'int'),
      ('part_level', 'int'),
      ('part_func_type', 'int'),
      ('part_func_expr', 'varchar:OB_MAX_PART_FUNC_EXPR_LENGTH'),
      ('part_num', 'int'),
      ('sub_part_func_type', 'int'),
      ('sub_part_func_expr', 'varchar:OB_MAX_PART_FUNC_EXPR_LENGTH'),
      ('sub_part_num', 'int'),
      ('schema_version', 'int'),
      ('view_definition', 'longtext'),
      ('view_check_option', 'int'),
      ('view_is_updatable', 'int'),
      ('index_using_type', 'int', 'false', 'USING_BTREE'),
      ('parser_name', 'varchar:OB_MAX_PARSER_NAME_LENGTH', 'true'),
      ('index_attributes_set', 'int', 'true', 0),
      ('tablet_size', 'int', 'false', 'OB_DEFAULT_TABLET_SIZE'),
      ('pctfree', 'int', 'false', 'OB_DEFAULT_PCTFREE'),
      ('partition_status', 'int', 'true', '0'),
      ('partition_schema_version', 'int', 'true', '0'),
      ('session_id', 'int', 'true', '0'),
      ('pk_comment', 'varchar:MAX_TABLE_COMMENT_LENGTH', 'false', ''),
      ('row_store_type', 'varchar:OB_MAX_STORE_FORMAT_NAME_LENGTH', 'true', 'encoding_row_store'),
      ('store_format', 'varchar:OB_MAX_STORE_FORMAT_NAME_LENGTH', 'true', ''),
      ('progressive_merge_round', 'int', 'true', '0'),
      ('storage_format_version', 'int', 'true', '2'),
      ('table_mode', 'int', 'false', '0'),
      ('encryption', 'varchar:OB_MAX_ENCRYPTION_NAME_LENGTH', 'true', ''),
      ('tablespace_id', 'int', 'false', '-1'),
      ('sub_part_template_flags', 'int', 'false', '0'),
      ("dop", 'int', 'false', '1'),
      ('character_set_client', 'int', 'false', '0'),
      ('collation_connection', 'int', 'false', '0'),
      ('association_table_id', 'int', 'false', '-1'),
      ('tablet_id', 'bigint', 'false', 'ObTabletID::INVALID_TABLET_ID'),
      ('max_dependency_version', 'int', 'false', '-1'),
      ('define_user_id', 'int', 'false', '-1'),
      ('transition_point', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('b_transition_point', 'varchar:OB_MAX_B_HIGH_BOUND_VAL_LENGTH', 'true'),
      ('interval_range', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('b_interval_range', 'varchar:OB_MAX_B_HIGH_BOUND_VAL_LENGTH', 'true'),
      ('object_status', 'int', 'false', '1'),
      ('table_flags', 'int', 'false', '0'),
      ('truncate_version', 'int', 'false', '-1'),
      ('external_file_location', 'varbinary:OB_MAX_VARCHAR_LENGTH', 'true'),
      ('external_file_location_access_info', 'varbinary:OB_MAX_VARCHAR_LENGTH', 'true'),
      ('external_file_format', 'varbinary:OB_MAX_VARCHAR_LENGTH', 'true'),
      ('external_file_pattern', 'varbinary:OB_MAX_VARCHAR_LENGTH', 'true'),
      ('ttl_definition', 'varchar:OB_MAX_DEFAULT_VALUE_LENGTH', 'false', ''),
      ('kv_attributes', 'varchar:OB_MAX_DEFAULT_VALUE_LENGTH', 'false', ''),
      ('name_generated_type', 'int', 'false', '0'),
      ('lob_inrow_threshold', 'int', 'false', 'OB_DEFAULT_LOB_INROW_THRESHOLD'),
      ('auto_increment_cache_size', 'int', 'false', '0'),
      ('external_properties', 'varbinary:OB_MAX_VARCHAR_LENGTH', 'true'),
      ('index_params', 'varchar:OB_MAX_INDEX_PARAMS_LENGTH', 'false', ''),
      ('micro_index_clustered', 'bool', 'false', 'false'),
      ('parser_properties', 'longtext', 'false', ''),
      ('enable_macro_block_bloom_filter', 'bool', 'false', 'false'),
      ('storage_cache_policy', 'varchar:OB_MAX_VARCHAR_LENGTH', 'false', r'{\"GLOBAL\":\"AUTO\"}'),
      ('merge_engine_type', 'int', 'false', '0'),
      ('semistruct_encoding_type', 'int', 'false', '0'),
      ('external_location_id', 'int', 'false', 'OB_INVALID_ID'),
      ('external_sub_path', 'varbinary:OB_MAX_VARCHAR_LENGTH', 'true')
  ]
  )

def_table_schema(**all_table_def)

all_column_def = dict(
    owner = 'bin.lb',
    table_name    = '__all_column',
    table_id      = '4',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('table_id', 'int'),
        ('column_id', 'int')
  ],
    in_tenant_space = True,
    is_core_related = True,

    normal_columns = [
      ('column_name', 'varchar:OB_MAX_COLUMN_NAME_LENGTH', 'false', ''),
      ('rowkey_position', 'int', 'false', '0'),
      ('index_position', 'int'),
      ('order_in_rowkey', 'int'),
      ('partition_key_position', 'int'),
      ('data_type', 'int'),
      ('data_length', 'int'),
      ('data_precision', 'int', 'true'),
      ('data_scale', 'int', 'true'),
      ('zero_fill', 'int'),
      ('nullable', 'int'),
      ('on_update_current_timestamp', 'int'),
      ('autoincrement', 'int'),
      ('is_hidden', 'int', 'false', '0'),
      ('collation_type', 'int'),
      ('orig_default_value', 'varchar:OB_MAX_DEFAULT_VALUE_LENGTH', 'true'),
      ('cur_default_value', 'varchar:OB_MAX_DEFAULT_VALUE_LENGTH', 'true'),
      ('comment', 'longtext', 'true'),
      ('schema_version', 'int'),
      ('column_flags', 'int', 'false', '0'),
      ('prev_column_id', 'int', 'false', '-1'),
      ('extended_type_info', 'varbinary:OB_MAX_VARBINARY_LENGTH', 'true'),
      ('orig_default_value_v2', 'varbinary:OB_MAX_DEFAULT_VALUE_LENGTH', 'true'),
      ('cur_default_value_v2', 'varbinary:OB_MAX_DEFAULT_VALUE_LENGTH', 'true'),
      ('srs_id', 'int', 'false', 'OB_DEFAULT_COLUMN_SRS_ID'),
      ('udt_set_id', 'int', 'false', '0'),
      ('sub_data_type', 'int', 'false', '0'),
      ('skip_index_attr', 'int', 'false', '0'),
      ('lob_chunk_size', 'int', 'false', 'OB_DEFAULT_LOB_CHUNK_SIZE'),
      ('local_session_vars', 'longtext', 'true')
  ]
  )

def_table_schema(**all_column_def)

def_table_schema(
    owner = 'yanmu.ztl',
    table_name    = '__all_ddl_operation',
    table_id      = '5',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('schema_version', 'int')
  ],
    in_tenant_space = True,
    is_core_related = True,

    normal_columns = [
      ('user_id', 'int'),
      ('database_id', 'int'),
      ('database_name', 'varchar:OB_MAX_DATABASE_NAME_LENGTH'),
      ('tablegroup_id', 'int'),
      ('table_id', 'int'),
      ('table_name', 'varchar:OB_MAX_CORE_TALBE_NAME_LENGTH'),
      ('operation_type', 'int'),
      ('ddl_stmt_str', 'longtext'),
  ]
  )

# 6: __all_freeze_info  # abandoned in 4.0
# 7: __all_table_v2 # abandoned in 4.0

#
# System Table (100, 1000]
#

# 101: __all_meta_table # abandoned in 4.0

all_user_def = dict(
    owner = 'sean.yyj',
    table_name    = '__all_user',
    table_id      = '102',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('user_id', 'int')
  ],
    in_tenant_space = True,
    normal_columns = [
      ('user_name', 'varchar:OB_MAX_USER_NAME_LENGTH_STORE'),
      ('host', 'varchar:OB_MAX_HOST_NAME_LENGTH', 'false', '%'),
      ('passwd', 'varchar:OB_MAX_PASSWORD_LENGTH'),
      ('info', 'varchar:OB_MAX_USER_INFO_LENGTH'),
      ('priv_alter', 'int', 'false', '0'),
      ('priv_create', 'int', 'false', '0'),
      ('priv_delete', 'int', 'false', '0'),
      ('priv_drop', 'int', 'false', '0'),
      ('priv_grant_option', 'int', 'false', '0'),
      ('priv_insert', 'int', 'false', '0'),
      ('priv_update', 'int', 'false', '0'),
      ('priv_select', 'int', 'false', '0'),
      ('priv_index', 'int', 'false', '0'),
      ('priv_create_view', 'int', 'false', '0'),
      ('priv_show_view', 'int', 'false', '0'),
      ('priv_show_db', 'int', 'false', '0'),
      ('priv_create_user', 'int', 'false', '0'),
      ('priv_super', 'int', 'false', '0'),
      ('is_locked', 'int'),
      ('priv_process', 'int', 'false', '0'),
      ('priv_create_synonym', 'int', 'false', '0'),
      ('ssl_type', 'int', 'false', '0'),
      ('ssl_cipher', 'varchar:1024', 'false', ''),
      ('x509_issuer', 'varchar:1024', 'false', ''),
      ('x509_subject', 'varchar:1024', 'false', ''),
      ('type', 'int', 'true', 0), #0: user; 1: role
      ('profile_id', 'int', 'false', 'OB_INVALID_ID'),
      ('password_last_changed', 'timestamp', 'true'),
      ('priv_file', 'int', 'false', '0'),
      ('priv_alter_tenant', 'int', 'false', '0'),
      ('priv_alter_system', 'int', 'false', '0'),
      ('priv_create_resource_pool', 'int', 'false', '0'),
      ('priv_create_resource_unit', 'int', 'false', '0'),
      ('max_connections', 'int', 'false', '0'),
      ('max_user_connections', 'int', 'false', '0'),
      ('priv_repl_slave', 'int', 'false', '0'),
      ('priv_repl_client', 'int', 'false', '0'),
      ('priv_drop_database_link', 'int', 'false', '0'),
      ('priv_create_database_link', 'int', 'false', '0'),
      ('priv_others', 'int', 'false', '0')
  ]
  )

def_table_schema(**all_user_def)

def_table_schema(**gen_history_table_def(103, all_user_def))

all_database_def = dict(
    owner = 'yanmu.ztl',
    table_name    = '__all_database',
    table_id      = '104',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('database_id', 'int')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('database_name', 'varchar:OB_MAX_DATABASE_NAME_LENGTH', 'false', ''),
      ('collation_type', 'int'),
      ('comment', 'varchar:MAX_DATABASE_COMMENT_LENGTH'),
      ('read_only', 'int'),
      ('default_tablegroup_id', 'int', 'false', 'OB_INVALID_ID'),
      ('in_recyclebin', 'int', 'false', '0')
  ]
  )

def_table_schema(**all_database_def)

def_table_schema(**gen_history_table_def(105, all_database_def))

all_tablegroup_def = dict(
    owner = 'yanmu.ztl',
    table_name    = '__all_tablegroup',
    table_id      = '106',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('tablegroup_id', 'int')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('tablegroup_name', 'varchar:OB_MAX_TABLEGROUP_NAME_LENGTH'),
      ('comment', 'varchar:MAX_TABLEGROUP_COMMENT_LENGTH'),
      ('part_level', 'int', 'false', '0'),
      ('part_func_type', 'int', 'false', '0'),
      ('part_func_expr_num', 'int', 'false', '0'),
      ('part_num', 'int', 'false', '0'),
      ('sub_part_func_type', 'int', 'false', '0'),
      ('sub_part_func_expr_num', 'int', 'false', '0'),
      ('sub_part_num', 'int', 'false', '0'),
      ('schema_version', 'int'),
      ('partition_status', 'int', 'true', '0'),
      ('partition_schema_version', 'int', 'true', '0'),
      ('sub_part_template_flags', 'int', 'false', '0'),
      ('sharding', 'varchar:OB_MAX_PARTITION_SHARDING_LENGTH', 'false', 'ADAPTIVE')
  ]
  )

def_table_schema(**all_tablegroup_def)

def_table_schema(**gen_history_table_def(107, all_tablegroup_def))

# 108: __all_tenant (abandoned)
# 109: __all_tenant_history (abandoned)

all_table_privilege_def = dict(
    owner = 'sean.yyj',
    table_name    = '__all_table_privilege',
    table_id      = '110',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('user_id', 'int'),
        ('database_name', 'varchar:OB_MAX_DATABASE_NAME_LENGTH'),
        ('table_name', 'varchar:OB_MAX_CORE_TALBE_NAME_LENGTH')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('priv_alter', 'int', 'false', '0'),
      ('priv_create', 'int', 'false', '0'),
      ('priv_delete', 'int', 'false', '0'),
      ('priv_drop', 'int', 'false', '0'),
      ('priv_grant_option', 'int', 'false', '0'),
      ('priv_insert', 'int', 'false', '0'),
      ('priv_update', 'int', 'false', '0'),
      ('priv_select', 'int', 'false', '0'),
      ('priv_index', 'int', 'false', '0'),
      ('priv_create_view', 'int', 'false', '0'),
      ('priv_show_view', 'int', 'false', '0'),
      ('priv_others', 'int', 'false', '0'),
      ('grantor', 'varchar:OB_MAX_USER_NAME_LENGTH_STORE', 'true'),
      ('grantor_host', 'varchar:OB_MAX_HOST_NAME_LENGTH', 'true')
  ]
  )

def_table_schema(**all_table_privilege_def)

def_table_schema(**gen_history_table_def(111, all_table_privilege_def))

all_database_privilege_def = dict(
    owner = 'sean.yyj',
    table_name    = '__all_database_privilege',
    table_id      = '112',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('user_id', 'int'),
        ('database_name', 'varchar:OB_MAX_DATABASE_NAME_LENGTH')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('priv_alter', 'int', 'false', '0'),
      ('priv_create', 'int', 'false', '0'),
      ('priv_delete', 'int', 'false', '0'),
      ('priv_drop', 'int', 'false', '0'),
      ('priv_grant_option', 'int', 'false', '0'),
      ('priv_insert', 'int', 'false', '0'),
      ('priv_update', 'int', 'false', '0'),
      ('priv_select', 'int', 'false', '0'),
      ('priv_index', 'int', 'false', '0'),
      ('priv_create_view', 'int', 'false', '0'),
      ('priv_show_view', 'int', 'false', '0'),
      ('priv_others', 'int', 'false', '0')
  ]
  )

def_table_schema(**all_database_privilege_def)

def_table_schema(**gen_history_table_def(113, all_database_privilege_def))

def_table_schema(**gen_history_table_def(114, all_table_def))

def_table_schema(**gen_history_table_def(115, all_column_def))

# 116: __all_zone (abandoned)
# 117: __all_server (abandoned)
# 118: __all_sys_parameter # migrated to SQLite, see gen_sqlite_table_def above
# Placeholder - original definition removed, using SQLite version

# 119: __tenant_parameter (abandoned)

all_sys_variable_def= dict(
    owner = 'xiaochu.yh',
    table_name     = '__all_sys_variable',
    table_id       = '120',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('name', 'varchar:OB_MAX_CONFIG_NAME_LEN', 'false', '')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('data_type', 'int'),
      ('value', 'varchar:OB_MAX_CONFIG_VALUE_LEN', 'true'),
      ('info', 'varchar:OB_MAX_CONFIG_INFO_LEN'),
      ('flags', 'int'),
      ('min_val', 'varchar:OB_MAX_CONFIG_VALUE_LEN', 'false', ''),
      ('max_val', 'varchar:OB_MAX_CONFIG_VALUE_LEN', 'false', '')
  ]
  )
def_table_schema(**all_sys_variable_def)

def_table_schema(
    owner = 'yanmu.ztl',
    table_name     = '__all_sys_stat',
    table_id       = '121',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('name', 'varchar:OB_MAX_CONFIG_NAME_LEN')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('data_type', 'int'),
      ('value', 'varchar:OB_MAX_CONFIG_VALUE_LEN'),
      ('info', 'varchar:OB_MAX_CONFIG_INFO_LEN')
  ]
  )

# 122: __all_column_statistic # abandoned in 4.0

# 123: __all_unit (abandoned)
# 124: __all_unit_config (abandoned)
# 125: __all_resource_pool (abandoned)

# 128: __all_charset (abandoned)
# 129: __all_collation (abandoned)

# 137: __all_clog_history_info # abandoned in 4.0

# 139: __all_clog_history_info_v2 # abandoned in 4.0

# 141: __all_privilege (abandoned)

all_outline_def = dict(
    owner = 'xiaoyi.xy',
    table_name    = '__all_outline',
    table_id      = '142',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('outline_id', 'int')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('database_id', 'int'),
      ('schema_version', 'int'),
      ('name', 'varchar:OB_MAX_OUTLINE_NAME_LENGTH', 'false', ''),
      ('signature', 'varbinary:OB_MAX_OUTLINE_SIGNATURE_LENGTH', 'false', ''),
      ('outline_content', 'longtext', 'false'),
      ('sql_text', 'longtext', 'false'),
      ('owner', 'varchar:OB_MAX_USERNAME_LENGTH', 'false', ''),
      ('used', 'int', 'false', '0'),
      ('version', 'varchar:OB_SERVER_VERSION_LENGTH', 'false', ''),
      ('compatible', 'int', 'false', '1'),
      ('enabled', 'int', 'false', '1'),
      ('format', 'int', 'false', '0'),
      ('outline_params', 'varbinary:OB_MAX_OUTLINE_PARAMS_LENGTH', 'false', ''),
      ('outline_target', 'longtext', 'false'),
      ('sql_id', 'varbinary:OB_MAX_SQL_ID_LENGTH', 'false', ''),
      ('owner_id', 'int', 'true'),
      ('format_sql_text', 'longtext', 'true'),
      ('format_sql_id', 'varbinary:OB_MAX_SQL_ID_LENGTH', 'false', ''),
      ('format_outline', 'int', 'false', '0')
    ]
  )

def_table_schema(**all_outline_def)

def_table_schema(**gen_history_table_def(143, all_outline_def))

# 144: __all_election_event_history # abandoned in 4.0

def_table_schema(
  owner = 'bin.lb',
  table_name = '__all_recyclebin',
  table_id = '145',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create'],
  in_tenant_space = True,
  rowkey_columns = [
    ('object_name', 'varchar:OB_MAX_OBJECT_NAME_LENGTH'),
    ('type', 'int')
  ],

  normal_columns = [
    ('database_id', 'int'),
    ('table_id', 'int'),
    ('tablegroup_id', 'int'),
    ('original_name', 'varchar:OB_MAX_ORIGINAL_NANE_LENGTH')
  ]
  )

all_part_def = dict(
    owner = 'yanmu.ztl',
    table_name    = '__all_part',
    table_id      = '146',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('table_id', 'int'),
        ('part_id', 'int')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('part_name', 'varchar:OB_MAX_PARTITION_NAME_LENGTH', 'false', ''),
      ('schema_version', 'int'),
      ('high_bound_val', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('b_high_bound_val', 'varchar:OB_MAX_B_HIGH_BOUND_VAL_LENGTH', 'true'),
      ('sub_part_num', 'int', 'true'),
      ('sub_part_space', 'int', 'true'),
      ('new_sub_part_space', 'int', 'true'),
      ('sub_part_interval', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('sub_interval_start', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('new_sub_part_interval', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('new_sub_interval_start', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('block_size', 'int', 'true'),
      ('compress_func_name', 'varchar:OB_MAX_COMPRESSOR_NAME_LENGTH', 'true'),
      ('status', 'int', 'true'),
      ('spare1', 'int', 'true'),
      ('spare2', 'int', 'true'),
      ('spare3', 'varchar:OB_OLD_MAX_VARCHAR_LENGTH', 'true'),
      ('comment', 'varchar:OB_MAX_PARTITION_COMMENT_LENGTH', 'true'),
      ('list_val', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('b_list_val', 'varchar:OB_MAX_B_PARTITION_EXPR_LENGTH', 'true'),
      ('part_idx', 'int', 'true'),
      ('source_partition_id', 'varchar:MAX_VALUE_LENGTH', 'true', ''),
      ('tablespace_id', 'int', 'false', '-1'),
      ('partition_type', 'int', 'false', '0'),
      ('tablet_id', 'bigint', 'false', 'ObTabletID::INVALID_TABLET_ID'),
      ('external_location', 'varbinary:OB_MAX_VARBINARY_LENGTH', 'true'),
      ('storage_cache_policy', 'varchar:OB_MAX_VARCHAR_LENGTH', 'false', 'NONE')
  ]
  )

def_table_schema(**all_part_def)

def_table_schema(**gen_history_table_def(147, all_part_def))

all_sub_part_def = dict(
    owner = 'yanmu.ztl',
    table_name    = '__all_sub_part',
    table_id      = '148',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('table_id', 'int'),
        ('part_id', 'int'),
        ('sub_part_id', 'int')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('sub_part_name', 'varchar:OB_MAX_PARTITION_NAME_LENGTH', 'false', ''),
      ('schema_version', 'int'),
      ('high_bound_val', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('b_high_bound_val', 'varchar:OB_MAX_B_HIGH_BOUND_VAL_LENGTH', 'true'),
      ('block_size', 'int', 'true'),
      ('compress_func_name', 'varchar:OB_MAX_COMPRESSOR_NAME_LENGTH', 'true'),
      ('status', 'int', 'true'),
      ('spare1', 'int', 'true'),
      ('spare2', 'int', 'true'),
      ('spare3', 'varchar:OB_OLD_MAX_VARCHAR_LENGTH', 'true'),
      ('comment', 'varchar:OB_MAX_PARTITION_COMMENT_LENGTH', 'true'),
      ('list_val', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('b_list_val', 'varchar:OB_MAX_B_PARTITION_EXPR_LENGTH', 'true'),
      ('tablespace_id', 'int', 'false', '-1'),
      ('sub_part_idx', 'int', 'false', '-1'),
      ('source_partition_id', 'varchar:MAX_VALUE_LENGTH', 'false', ''),
      ('partition_type', 'int', 'false', '0'),
      ('tablet_id', 'bigint', 'false', 'ObTabletID::INVALID_TABLET_ID'),
      ('storage_cache_policy', 'varchar:OB_MAX_VARCHAR_LENGTH', 'false', 'NONE')
  ]
  )

def_table_schema(**all_sub_part_def)

def_table_schema(**gen_history_table_def(149, all_sub_part_def))

all_part_info_def = dict(
    owner = 'yanmu.ztl',
    table_name    = '__all_part_info',
    table_id      = '150',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('table_id', 'int')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('part_type', 'int', 'false'),
      ('schema_version', 'int'),
      ('part_num', 'int', 'false'),
      ('part_space', 'int', 'false'),
      ('new_part_space', 'int', 'true'),
      ('sub_part_type', 'int', 'true'),
      ('def_sub_part_num', 'int', 'true'),
      ('part_expr', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('sub_part_expr', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('part_interval', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('interval_start', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('new_part_interval', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('new_interval_start', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('def_sub_part_interval', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('def_sub_interval_start', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('new_def_sub_part_interval', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('new_def_sub_interval_start', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('block_size', 'int', 'true'),
      ('compress_func_name', 'varchar:OB_MAX_COMPRESSOR_NAME_LENGTH', 'true'),
      ('spare1', 'int', 'true'),
      ('spare2', 'int', 'true'),
      ('spare3', 'varchar:OB_OLD_MAX_VARCHAR_LENGTH', 'true')
  ]
  )

def_table_schema(**all_part_info_def)

def_table_schema(**gen_history_table_def(151, all_part_info_def))

# TODO: abandoned
all_def_sub_part_def = dict(
    owner = 'yanmu.ztl',
    table_name    = '__all_def_sub_part',
    table_id      = '152',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('table_id', 'int'),
        ('sub_part_id', 'int')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('sub_part_name', 'varchar:OB_MAX_PARTITION_NAME_LENGTH', 'false', ''),
      ('schema_version', 'int'),
      ('high_bound_val', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('b_high_bound_val', 'varchar:OB_MAX_B_HIGH_BOUND_VAL_LENGTH', 'true'),
      ('block_size', 'int', 'true'),
      ('compress_func_name', 'varchar:OB_MAX_COMPRESSOR_NAME_LENGTH', 'true'),
      ('spare1', 'int', 'true'),
      ('spare2', 'int', 'true'),
      ('spare3', 'varchar:OB_OLD_MAX_VARCHAR_LENGTH', 'true'),
      ('comment', 'varchar:OB_MAX_PARTITION_COMMENT_LENGTH', 'true'),
      ('list_val', 'varchar:OB_MAX_PARTITION_EXPR_LENGTH', 'true'),
      ('b_list_val', 'varchar:OB_MAX_B_PARTITION_EXPR_LENGTH', 'true'),
      ('sub_part_idx', 'int', 'true'),
      ('source_partition_id', 'varchar:MAX_VALUE_LENGTH', 'true', ''),
      ('tablespace_id', 'int', 'false', '-1')
    ]
  )

def_table_schema(**all_def_sub_part_def)

def_table_schema(**gen_history_table_def(153, all_def_sub_part_def))

# 155: __all_rootservice_job # migrated to SQLite, see gen_sqlite_table_def above
# Placeholder - original definition removed, using SQLite version

# 156: __all_unit_load_history # abandoned in 4.0.

all_sys_variable_history_def= dict(
    owner = 'xiaochu.yh',
    table_name     = '__all_sys_variable_history',
    table_id       = '157',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('name', 'varchar:OB_MAX_CONFIG_NAME_LEN', 'false', ''),
        ('schema_version', 'int')
    ],
    in_tenant_space = True,
    normal_columns = [
      ('is_deleted', 'int', 'false'),
      ('data_type', 'int'),
      ('value', 'varchar:OB_MAX_CONFIG_VALUE_LEN', 'true'),
      ('info', 'varchar:OB_MAX_CONFIG_INFO_LEN'),
      ('flags', 'int'),
      ('min_val', 'varchar:OB_MAX_CONFIG_VALUE_LEN', 'false', ''),
      ('max_val', 'varchar:OB_MAX_CONFIG_VALUE_LEN', 'false', '')
  ]
  )
def_table_schema(**all_sys_variable_history_def)

# 158: __all_restore_job (abandoned)
# 159: __all_restore_task # abandoned in 4.0

# __all_restore_job_history
# 160: __all_restore_job_history (abandoned)

all_foreign_key_def = dict(
  owner = 'webber.wb',
  table_name    = '__all_foreign_key',
  table_id      = '166',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
    ('foreign_key_id', 'int')
  ],
  in_tenant_space = True,

  normal_columns = [
    ('foreign_key_name', 'varchar:OB_MAX_EXTENDED_CONSTRAINT_NAME_LENGTH', 'false', ''),
    ('child_table_id', 'int'),
    ('parent_table_id', 'int'),
    ('update_action', 'int'),
    ('delete_action', 'int'),
    ('ref_cst_type', 'int', 'false', '0'),
    ('ref_cst_id', 'int', 'false', '-1'),
    ('rely_flag', 'bool', 'false', 'false'),
    ('enable_flag', 'bool', 'false', 'true'),
    ('validate_flag', 'int', 'false', '1'),
    ('is_parent_table_mock', 'bool', 'false', 'false'),
    ('name_generated_type', 'int', 'false', '0')
  ]
  )

def_table_schema(**all_foreign_key_def)

def_table_schema(**gen_history_table_def(167, all_foreign_key_def))

all_foreign_key_column_def = dict(
  owner = 'webber.wb',
  table_name    = '__all_foreign_key_column',
  table_id      = '168',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
    ('foreign_key_id', 'int'),
    ('child_column_id', 'int'),
    ('parent_column_id', 'int')
  ],
  in_tenant_space = True,

  normal_columns = [
   ('position', 'int', 'false', '0')
  ]
  )

def_table_schema(**all_foreign_key_column_def)

def_table_schema(**gen_history_table_def(169, all_foreign_key_column_def))

def_table_schema(
    owner = 'xiaochu.yh',
    table_name     = '__all_auto_increment',
    table_id       = '182',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('sequence_key', 'int'),
        ('column_id', 'int')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('sequence_name', 'varchar:OB_MAX_SEQUENCE_NAME_LENGTH', 'true'),
      ('sequence_value', 'uint', 'true'),
      ('sync_value', 'uint'),
      ('truncate_version', 'int', 'false', '-1')
  ]
  )

# 183: __all_tenant_meta_table # abandoned in 4.0.

def_table_schema(
  owner = 'zhenjiang.xzj',
  table_name     = '__all_ddl_checksum',
  table_id       = '188',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
  ('table_id', 'int'),
  ('execution_id', 'int'),
  ('ddl_task_id', 'int'),
  ('column_id', 'int'),
  ('task_id', 'int')
  ],

  is_cluster_private = False,
  in_tenant_space = True,

  normal_columns = [
  ('checksum', 'int'),
  ('tablet_id', 'int', 'false', 0)
  ]
  )

all_routine_def = dict(
    owner = 'linlin.xll',
    table_name    = '__all_routine',
    table_id      = '189',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('routine_id', 'int')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('database_id', 'int', 'false'),
      ('package_id', 'int', 'false'),
      ('routine_name', 'varchar:OB_MAX_ROUTINE_NAME_LENGTH'),
      ('overload', 'int'),
      ('subprogram_id', 'int', 'false'),
      ('schema_version', 'int'),
      ('routine_type', 'int', 'false'),
      ('flag', 'int', 'false'),
      ('owner_id', 'int', 'false'),
      ('priv_user', 'varchar:OB_MAX_USER_NAME_LENGTH_STORE', 'true'),
      ('exec_env', 'varchar:OB_MAX_PROC_ENV_LENGTH', 'true'),
      ('routine_body', 'longtext', 'true'),
      ('comment', 'varchar:MAX_TENANT_COMMENT_LENGTH', 'true'),
      ('route_sql', 'longtext', 'true'),
      ('type_id', 'int', 'true', 'OB_INVALID_ID')
    ]
  )

def_table_schema(**all_routine_def)

def_table_schema(**gen_history_table_def(190, all_routine_def))

all_routine_param_def = dict(
    owner = 'linlin.xll',
    table_name    = '__all_routine_param',
    table_id      = '191',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('routine_id', 'int'),
        ('sequence', 'int')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('subprogram_id', 'int', 'false'),
      ('param_position', 'int', 'false'),
      ('param_level', 'int', 'false'),
      ('param_name', 'varchar:OB_MAX_COLUMN_NAME_LENGTH', 'true', ''),
      ('schema_version', 'int'),
      ('param_type', 'int', 'false'),
      ('param_length', 'int'),
      ('param_precision', 'int', 'true'),
      ('param_scale', 'int', 'true'),
      ('param_zero_fill', 'int'),
      ('param_charset', 'int', 'true'),
      ('param_coll_type', 'int'),
      ('flag', 'int', 'false'),
      ('default_value', 'varchar:OB_MAX_DEFAULT_VALUE_LENGTH', 'true'),
      ('type_owner', 'int', 'true'),
      ('type_name', 'varchar:OB_MAX_COLUMN_NAME_LENGTH', 'true', ''),
      ('type_subname', 'varchar:OB_MAX_COLUMN_NAME_LENGTH', 'true', ''),
      ('extended_type_info', "varbinary:OB_MAX_VARBINARY_LENGTH", 'true', '')
    ]
  )

def_table_schema(**all_routine_param_def)
def_table_schema(**gen_history_table_def(192, all_routine_param_def))

all_package_def = dict(
    owner = 'linlin.xll',
    table_name    = '__all_package',
    table_id      = '196',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('package_id', 'int')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('database_id', 'int', 'false'),
      ('package_name', 'varchar:OB_MAX_PACKAGE_NAME_LENGTH', 'false', ''),
      ('schema_version', 'int', 'false'),
      ('type', 'int', 'false'),
      ('flag', 'int', 'false'),
      ('owner_id', 'int', 'false'),
      ('exec_env', 'varchar:OB_MAX_PROC_ENV_LENGTH', 'true'),
      ('source', 'longtext', 'true'),
      ('comment', 'varchar:MAX_TENANT_COMMENT_LENGTH', 'true'),
      ('route_sql', 'longtext', 'true')
    ]
  )

def_table_schema(**all_package_def)
def_table_schema(**gen_history_table_def(197, all_package_def))

def_table_schema(
  owner = 'jingyan.kfy',
  table_name     = '__all_acquired_snapshot',
  table_id       = '202',
  table_type = 'SYSTEM_TABLE',
  gm_columns = [],
  rowkey_columns = [
    ('gmt_create', 'timestamp:6', 'false')
    ],
  in_tenant_space = True,
  is_cluster_private = False,
  normal_columns = [
    ('snapshot_type', 'int'),
    ('snapshot_scn', 'uint'),
    ('schema_version', 'int', 'true'),
    ('tablet_id', 'int', 'true'),
    ('extra_info', 'varchar:MAX_ROOTSERVICE_EVENT_EXTRA_INFO_LENGTH', 'true', '')
  ]
  )

# 205: __all_tenant_gc_partition_info # abandoned in 4.0

all_constraint_def = dict(
    owner = 'bin.lb',
    table_name    = '__all_constraint',
    table_id      = '206',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('table_id', 'int'),
        ('constraint_id', 'int')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('constraint_name', 'varchar:OB_MAX_EXTENDED_CONSTRAINT_NAME_LENGTH', 'false'),
      ('check_expr', 'varchar:OB_MAX_CONSTRAINT_EXPR_LENGTH', 'false'),
      ('schema_version', 'int'),
      ('constraint_type', 'int'),
      ('rely_flag', 'bool', 'false', 'false'),
      ('enable_flag', 'bool', 'false', 'true'),
      ('validate_flag', 'int', 'false', '1'),
      ('name_generated_type', 'int', 'false', '0')
  ]
  )

def_table_schema(**all_constraint_def)

def_table_schema(**gen_history_table_def(207, all_constraint_def))

def_table_schema(
  owner = 'yanmu.ztl',
  table_name     = '__all_ori_schema_version',
  table_id       = '208',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
      ('table_id', 'int', 'false')
  ],
  in_tenant_space = True,

  normal_columns = [
  ('ori_schema_version', 'int')
  ]
  )

all_func_def = dict(
    owner = 'bin.lb',
    table_name    = '__all_func',
    table_id      = '209',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('name', 'varchar:OB_MAX_UDF_NAME_LENGTH', 'false')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('ret', 'int'),
      ('dl', 'varchar:OB_MAX_DL_NAME_LENGTH', 'false'),
      #TODO muhang.zb the inner table python generator do not support enum at this time
      #('type', 'enum(\'function\',\'aggregate\')'),
      ('udf_id', 'int'),
      # 1 for normal function; 2 for aggregate function.
      ('type', 'int')
  ]
  )

def_table_schema(**all_func_def)

def_table_schema(**gen_history_table_def(210, all_func_def))


def_table_schema(
  owner = 'xiaochu.yh',
  table_name    = '__all_sequence_object',
  table_id      = '213',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
  ('sequence_id', 'int', 'false')
  ],
  in_tenant_space = True,

  normal_columns = [
  ('schema_version', 'int', 'false'),
  ('database_id', 'int', 'false'),
  ('sequence_name', 'varchar:OB_MAX_SEQUENCE_NAME_LENGTH', 'false'),
  ('min_value', 'number:28:0', 'false'),
  ('max_value', 'number:28:0', 'false'),
  ('increment_by', 'number:28:0', 'false'),
  ('start_with', 'number:28:0', 'false'),
  ('cache_size', 'number:28:0', 'false'),
  ('order_flag', 'bool', 'false'),
  ('cycle_flag', 'bool', 'false'),
  ('is_system_generated', 'bool', 'false', 'false'),
  ('flag', 'int', 'false', 0)
  ]
  )

def_table_schema(
  owner = 'xiaochu.yh',
  table_name    = '__all_sequence_object_history',
  table_id      = '214',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
  ('sequence_id', 'int', 'false'),
  ('schema_version', 'int', 'false')
  ],
  in_tenant_space = True,

  normal_columns = [
  ('is_deleted', 'int', 'false'),
  ('database_id', 'int', 'true'),
  ('sequence_name', 'varchar:OB_MAX_SEQUENCE_NAME_LENGTH', 'true'),
  ('min_value', 'number:28:0', 'true'),
  ('max_value', 'number:28:0', 'true'),
  ('increment_by', 'number:28:0', 'true'),
  ('start_with', 'number:28:0', 'true'),
  ('cache_size', 'number:28:0', 'true'),
  ('order_flag', 'bool', 'true'),
  ('cycle_flag', 'bool', 'true'),
  ('is_system_generated', 'bool', 'true'),
  ('flag', 'int', 'false', 0)
  ]
  )


def_table_schema(
    owner = 'xiaochu.yh',
    table_name     = '__all_sequence_value',
    table_id       = '215',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('sequence_id', 'int', 'false')
  ],
    in_tenant_space = True,

  normal_columns = [
      ('next_value', 'number:38:0', 'false')
  ]
  )

# 216: __all_tenant_plan_baseline # abandoned in 4.0
# 217: __all_tenant_plan_baseline_history

# 218: __all_ddl_helper # abandoned in 4.0

# 219: __all_freeze_schema_version (abandoned)
# 226: __all_weak_read_service (abandoned)
# 228: __all_cluster # abandoned in 4.0

# 229: __all_gts # abandoned in 4.0

# 230: __all_tenant_gts # abandoned in 4.0

# 231: __all_partition_member_list # abandoned in 4.0

# 234: __all_tenant_partition_meta_table # abandoned in 4.0.

all_tenant_role_grantee_map_def = dict(
  owner = 'sean.yyj',
  table_name = '__all_role_grantee_map',
  table_id = '235',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
    ('grantee_id', 'int', 'false'),
    ('role_id', 'int', 'false')
  ],
  in_tenant_space = True,

  normal_columns = [
    ('admin_option', 'int', 'false', '0'),
    ('disable_flag', 'int', 'false', '0')
  ]
  )
def_table_schema(**all_tenant_role_grantee_map_def)
def_table_schema(**gen_history_table_def(236, all_tenant_role_grantee_map_def))


def_table_schema(
  owner = 'jim.wjh',
  table_name    = '__all_user_failed_login_stat',
  table_id      = '249',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
    ('user_id', 'int')
  ],
  in_tenant_space = True,
  is_cluster_private = False,
  meta_record_in_sys = False,

  normal_columns = [
    ('user_name', 'varchar:OB_MAX_USER_NAME_LENGTH'),
    ('failed_login_attempts', 'int'),
    ('last_failed_login_svr_ip', 'varchar:MAX_IP_ADDR_LENGTH', 'true', '')
  ]
  )


all_trigger_def = dict(
  owner = 'webber.wb',
  table_name    = '__all_trigger',
  table_id      = '254',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
      ('trigger_id', 'int')
  ],
  in_tenant_space = True,
  normal_columns = [
    ('trigger_name', 'varchar:OB_MAX_TRIGGER_NAME_LENGTH', 'false'),
    ('database_id', 'int', 'false'),
    ('owner_id', 'int', 'false'),
    ('schema_version', 'int', 'false'),
    ('trigger_type', 'int', 'false'),
    ('trigger_events', 'int', 'false'),
    ('timing_points', 'int', 'false'),
    ('base_object_type', 'int', 'false'),
    ('base_object_id', 'int', 'false'),
    ('trigger_flags', 'int', 'false'),
    ('update_columns', 'varchar:OB_MAX_UPDATE_COLUMNS_LENGTH', 'true'),
    ('ref_old_name', 'varchar:OB_MAX_TRIGGER_NAME_LENGTH', 'false'),
    ('ref_new_name', 'varchar:OB_MAX_TRIGGER_NAME_LENGTH', 'false'),
    ('ref_parent_name', 'varchar:OB_MAX_TRIGGER_NAME_LENGTH', 'false'),
    ('when_condition', 'varchar:OB_MAX_WHEN_CONDITION_LENGTH', 'true'),
    ('trigger_body', 'varchar:OB_MAX_TRIGGER_BODY_LENGTH', 'true'),
    ('package_spec_source', 'varchar:OB_MAX_TRIGGER_BODY_LENGTH', 'true'),
    ('package_body_source', 'varchar:OB_MAX_TRIGGER_BODY_LENGTH', 'true'),
    ('package_flag', 'int', 'false'),
    ('package_exec_env', 'varchar:OB_MAX_PROC_ENV_LENGTH', 'true'),
    ('sql_mode', 'int', 'false'),
    ('trigger_priv_user', 'varchar:OB_MAX_USER_NAME_LENGTH_STORE', 'true'),
    ('order_type', 'int', 'false'),
    ('ref_trg_db_name', 'varchar:OB_MAX_TRIGGER_NAME_LENGTH', 'true'),
    ('ref_trg_name', 'varchar:OB_MAX_TRIGGER_NAME_LENGTH', 'true'),
    ('action_order', 'int', 'false'),
    ('analyze_flag', 'int', 'false', 0),
    ('trigger_body_v2', 'longtext', 'false', '')
  ]
  )

def_table_schema(**all_trigger_def)
def_table_schema(**gen_history_table_def(255, all_trigger_def))

# 256: __all_seed_parameter (abandoned)
# 257: __all_failover_scn # abandoned in 4.0

# 258: __all_tenant_sstable_column_checksum # abandoned in 4.0


all_sysauth_def = dict(
    owner = 'sean.yyj',
    table_name     = '__all_sysauth',
    table_id       = '260',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    in_tenant_space = True,

    rowkey_columns = [
        ('grantee_id', 'int', 'false'),
        ('priv_id', 'int', 'false')
  ],
    normal_columns = [
      ('priv_option', 'int', 'false')
  ]
  )

def_table_schema(**all_sysauth_def)
def_table_schema(**gen_history_table_def(261, all_sysauth_def))

all_objauth_def = dict(
    owner = 'sean.yyj',
    table_name     = '__all_objauth',
    table_id       = '262',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    in_tenant_space = True,

    rowkey_columns = [
        ('obj_id', 'int', 'false'),
        ('objtype', 'int', 'false'),
        ('col_id', 'int', 'false'),
        ('grantor_id', 'int', 'false'),
        ('grantee_id', 'int', 'false'),
        ('priv_id', 'int', 'false')
  ],
    normal_columns = [
      ('priv_option', 'int', 'false')
  ]
  )

def_table_schema(**all_objauth_def)
def_table_schema(**gen_history_table_def(263, all_objauth_def))


# 264: __all_tenant_backup_info # abandoned in 4.0
# 265: __all_restore_info (abandoned)

# 266: __all_tenant_backup_log_archive_status # abandoned in 4.0
# 267: __all_backup_log_archive_status_history # abandoned in 4.0
# 268: __all_tenant_backup_task # abandoned in 4.0
# 269: __all_backup_task_history # abandoned in 4.0
# 270: __all_tenant_pg_backup_task # abandoned in 4.0
# 271:__all_failover_info # abandoned in 4.0

all_tenant_error_def = dict(
    owner = 'lj229669',
    table_name = '__all_error',
    table_id = '272',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
                      ('obj_id', 'int', 'false'),
                      ('obj_seq', 'int', 'false'),
                      ('obj_type', 'int', 'false')
  ],
    in_tenant_space = True,
    is_cluster_private = False,
    normal_columns = [
      ('line', 'int', 'false'),
      ('position', 'int', 'false'),
      ('text_length', 'int', 'false'),
      ('text', 'varchar:MAX_COMMENT_TEXT_LENGTH'),
      ('property', 'int', 'true'),
      ('error_number', 'int', 'true'),
      ('schema_version', 'int', 'false')
    ]
  )
def_table_schema(**all_tenant_error_def)

# 273: __all_server_recovery_status # abandoned in 4.0
# 274: __all_datafile_recovery_status # abandoned in 4.0

# 276: all_tenant_backup_clean_info # abandoned in 4.0
# 277: __all_backup_clean_info_history # abandoned in 4.0
# 278: __all_backup_task_clean_history # abandoned in 4.0

# 279: __all_restore_progress (abandoned)

# 280: __all_restore_history # abandoned in 4.0
# 281: __all_tenant_restore_pg_info # abandoned in 4.0
# 282: __all_table_v2_history # abandoned in 4.0

# 285: __all_backup_validation_job # abandoned in 4.0
# 286: __all_backup_validation_job_history # abandoned in 4.0
# 287: __all_tenant_backup_validation_task # abandoned in 4.0
# 288: __all_backup_validation_task_history # abandoned in 4.0
# 289: __all_tenant_pg_backup_validation_task # abandoned in 4.0

def_table_schema(
  owner = 'dachuan.sdc',
  table_name     = '__all_time_zone_name',
  table_id       = '291',
  table_type = 'SYSTEM_TABLE',
  gm_columns = [],
  rowkey_columns = [
    ('name', 'varchar:64', 'false', 'NULL')
  ],
  in_tenant_space = True,
  is_cluster_private = False,

  normal_columns = [
  ('time_zone_id', 'int', 'false', 'NULL'),
  ('version', 'int', 'true')
  ]
  )

def_table_schema(
  owner = 'dachuan.sdc',
  table_name     = '__all_time_zone_transition',
  table_id       = '292',
  table_type = 'SYSTEM_TABLE',
  gm_columns = [],
  rowkey_columns = [
    ('time_zone_id', 'int', 'false', 'NULL'),
    ('transition_time', 'int', 'false', 'NULL')
  ],
  in_tenant_space = True,
  is_cluster_private = False,

  normal_columns = [
  ('transition_type_id', 'int', 'false', 'NULL'),
  ('version', 'int', 'true')
  ]
  )

def_table_schema(
  owner = 'dachuan.sdc',
  table_name     = '__all_time_zone_transition_type',
  table_id       = '293',
  table_type = 'SYSTEM_TABLE',
  gm_columns = [],
  rowkey_columns = [
    ('time_zone_id', 'int', 'false', 'NULL'),
    ('transition_type_id', 'int', 'false', 'NULL')
  ],
  in_tenant_space = True,
  is_cluster_private = False,

  normal_columns = [
  ('offset', 'int', 'false', '0'),
  ('is_dst', 'int', 'false', '0'),
  ('abbreviation', 'varchar:8', 'false', ''),
  ('version', 'int', 'true')
  ]
  )

all_tenant_constraint_column_def = dict(
  owner = 'bin.lb',
  table_name    = '__all_constraint_column',
  table_id      = '294',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
    ('table_id', 'int', 'false'),
    ('constraint_id', 'int', 'false'),
    ('column_id', 'int', 'false')
  ],
  in_tenant_space = True,
  normal_columns = [
    ('schema_version', 'int', 'false')
  ]
  )
def_table_schema(**all_tenant_constraint_column_def)
def_table_schema(**gen_history_table_def(295,  all_tenant_constraint_column_def))

all_tenant_dependency_def = dict(
  owner = 'lj229669',
  table_name = '__all_dependency',
  table_id = '297',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
    ('dep_obj_type', 'int'),
    ('dep_obj_id', 'int'),
    ('dep_order', 'int')
  ],
  in_tenant_space = True,

  normal_columns = [
    ('schema_version', 'int'),
    ('dep_timestamp', 'int'),
    ('ref_obj_type', 'int'),
    ('ref_obj_id', 'int'),
    ('ref_timestamp', 'int'),
    ('dep_obj_owner_id', 'int', 'true'),
    ('property', 'int'),
    ('dep_attrs', 'varbinary:OB_MAX_RAW_SQL_COL_LENGTH', 'true'),
    ('dep_reason', 'varbinary:OB_MAX_RAW_SQL_COL_LENGTH', 'true'),
    ('ref_obj_name', 'varchar:OB_MAX_TABLE_NAME_LENGTH', 'true')
  ]
  )

def_table_schema(**all_tenant_dependency_def)

# 298: __all_backup_backupset_job # abandoned in 4.0
# 299: __all_backup_backupset_job_history # abandoned in 4.0
# 300: __all_tenant_backup_backupset_task # abandoned in 4.0
# 301: __all_backup_backupset_task_history # abandoned in 4.0
# 302: __all_tenant_pg_backup_backupset_task # abandoned in 4.0
# 303: __all_tenant_backup_backup_log_archive_status # abandoned in 4.0
# 304: __all_backup_backup_log_archive_status_history # abandoned in 4.0

# 305: removed (legacy resource manager deleted)
# 306: removed (legacy resource manager deleted)
# 307: removed (legacy resource manager deleted)

def_table_schema(
    owner = 'zhenjiang.xzj',
    table_name    = '__all_ddl_error_message',
    table_id      = '308',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('task_id', 'int'),
        ('target_object_id', 'int'),
        ('object_id', 'int'),
        ('schema_version', 'int')
  ],
    is_cluster_private = False,
    in_tenant_space = True,

    normal_columns = [
      ('ret_code', 'int'),
      ('ddl_type', 'int'),
      ('affected_rows', 'int'),
      ('user_message', 'longtext', 'true'),
      ('dba_message', 'varchar:OB_MAX_ERROR_MSG_LEN', 'true'),
      ('parent_task_id', 'int', 'false', 0),
      ('trace_id', 'varchar:OB_MAX_TRACE_ID_BUFFER_SIZE', 'true'),
      ('consensus_schema_version', 'int', 'false', '-1')
    ]
  )

# 309: __all_space_usage (abandoned)
# 310: __all_backup_backuppiece_job # abandoned in 4.0
# 311: __all_backup_backuppiece_job_history # abandoned in 4.0
# 312: __all_backup_backuppiece_task # abandoned in 4.0
# 313: __all_backup_backuppiece_task_history # abandoned in 4.0
# 314: __all_backup_piece_files # abandoned in 4.0
# 315: __all_backup_set_files # abandoned

# 316: removed (legacy resource manager deleted)

# 317: __all_backup_info # abandoned

# 318: __all_backup_log_archive_status_v2 # abandoned in 4.0

def_table_schema(
  owner = 'zhenjiang.xzj',
  table_name = '__all_ddl_task_status',
  table_id = '319',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
    ('task_id', 'int')
  ],
  is_cluster_private = False,
  in_tenant_space = True,

  normal_columns = [
    ('object_id', 'int'),
    ('target_object_id', 'int'),
    ('ddl_type', 'int'),
    ('schema_version', 'int'),
    ('parent_task_id', 'int'),
    ('trace_id', 'varchar:256'),
    ('status', 'int'),
    ('snapshot_version', 'uint', 'false', '0'),
    ('task_version', 'int', 'false', '0'),
    ('execution_id', 'int', 'false', '0'),
    ('ddl_stmt_str', 'longtext', 'true'),
    ('ret_code', 'int', 'false', '0'),
    ('message', 'longtext', 'true'),
    ('consensus_schema_version', 'int', 'false', '-1'),
    ('schedule_info', 'longtext', 'true')
  ]
  )

# 320: (abandoned)
# 321: __all_backup_backup_log_archive_status_v2 # abandoned in 4.0

# 322: __all_deadlock_event_history # migrated to SQLite, see gen_sqlite_table_def above
# Placeholder - original definition removed, using SQLite version

all_column_usage_def = dict(
    owner = 'yibo.tyf',
    table_name    = '__all_column_usage',
    table_id      = '323',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('table_id', 'int'),
        ('column_id', 'int')
  ],
    in_tenant_space = True,
    is_cluster_private = False,

    normal_columns = [
      ('equality_preds', 'int', 'false', '0'),
      ('equijoin_preds', 'int', 'false', '0'),
      ('nonequijion_preds', 'int', 'false', '0'),
      ('range_preds', 'int', 'false', '0'),
      ('like_preds', 'int', 'false', '0'),
      ('null_preds', 'int', 'false', '0'),
      ('distinct_member', 'int', 'false', '0'),
      ('groupby_member', 'int', 'false', '0'),
      ('spare1', 'int', 'true'),
      ('spare2', 'int', 'true'),
      ('spare3', 'int', 'true'),
      ('spare4', 'int', 'true'),
      ('spare5', 'int', 'true'),
      ('spare6', 'int', 'true'),
      ('flags', 'int', 'false', '0')
    ]
  )

def_table_schema(**all_column_usage_def)

def_table_schema(
  owner = 'linlin.xll',
  table_name     = '__all_job',
  table_id       = '324',
  table_type     = 'SYSTEM_TABLE',
  gm_columns     = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
    ('job', 'int', 'false')
  ],
  in_tenant_space = True,
  normal_columns = [
    ('lowner', 'varchar:OB_MAX_DATABASE_NAME_LENGTH', 'false'),
    ('powner', 'varchar:OB_MAX_DATABASE_NAME_LENGTH', 'false'),
    ('cowner', 'varchar:OB_MAX_DATABASE_NAME_LENGTH', 'false'),
    ('last_date', 'timestamp', 'true'),
    ('this_date', 'timestamp', 'true'),
    ('next_date', 'timestamp', 'false'),
    ('total', 'int', 'true', '0'),
    ('interval#', 'varchar:200', 'false'),
    ('failures', 'int', 'true', '0'),
    ('flag', 'int', 'false'),
    ('what', 'varchar:4000', 'true'),
    ('nlsenv', 'varchar:4000', 'true'),
    ('charenv', 'varchar:4000', 'true'),
    ('field1', 'varchar:MAX_ZONE_LENGTH', 'true'),
    ('scheduler_flags', 'int', 'true', '0'),
    ('exec_env', 'varchar:OB_MAX_PROC_ENV_LENGTH', 'true')
  ]
  )

all_tenant_directory_def = dict(
    owner = 'jiahua.cjh',
    table_name     = '__all_directory',
    table_id       = '326',
    table_type     = 'SYSTEM_TABLE',
    gm_columns     = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('directory_id', 'int')
  ],
    normal_columns = [
        ('directory_name', 'varchar:128'),
        ('directory_path', 'varchar:4000')
  ],
    in_tenant_space = True
  )

def_table_schema(**all_tenant_directory_def)
def_table_schema(**gen_history_table_def(327, all_tenant_directory_def))

def_table_schema(
  owner = 'jiangxiu.wt',
  table_name = '__all_table_stat',
  table_id = '328',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
      ('table_id', 'int'),
      ('partition_id', 'int')
  ],
  in_tenant_space = True,
  is_cluster_private = False,

  normal_columns = [
      ('object_type', 'int'),
      ('last_analyzed', 'timestamp'),
      ('sstable_row_cnt', 'int'),
      ('sstable_avg_row_len', 'double'),
      ('macro_blk_cnt', 'int'),
      ('micro_blk_cnt', 'int'),
      ('memtable_row_cnt', 'int'),
      ('memtable_avg_row_len', 'double'),
      ('row_cnt', 'int'),
      ('avg_row_len', 'double'),
      ('global_stats', 'int', 'true', '0'),
      ('user_stats', 'int', 'true', '0'),
      ('stattype_locked', 'int', 'true', '0'),
      ('stale_stats', 'int', 'true', '0'),
      ('spare1', 'int', 'true'),
      ('spare2', 'int', 'true'),
      ('spare3', 'int', 'true'),
      ('spare4', 'varchar:MAX_VALUE_LENGTH', 'true'),
      ('spare5', 'varchar:MAX_VALUE_LENGTH', 'true'),
      ('spare6', 'varchar:MAX_VALUE_LENGTH', 'true'),
      ('index_type', 'bool')
  ]
  )

def_table_schema(
  owner = 'jiangxiu.wt',
  table_name = '__all_column_stat',
  table_id = '329',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
      ('table_id', 'int'),
      ('partition_id', 'int'),
      ('column_id', 'int')
  ],
  in_tenant_space = True,
  is_cluster_private = False,

  normal_columns = [
      ('object_type', 'int'),
      ('last_analyzed', 'timestamp'),
      ('distinct_cnt', 'int'),
      ('null_cnt', 'int'),
      ('max_value', 'varchar:MAX_VALUE_LENGTH'),
      ('b_max_value', 'varchar:MAX_VALUE_LENGTH'),
      ('min_value', 'varchar:MAX_VALUE_LENGTH'),
      ('b_min_value', 'varchar:MAX_VALUE_LENGTH'),
      ('avg_len', 'double'),
      ('distinct_cnt_synopsis','varchar:MAX_LLC_BITMAP_LENGTH'),
      ('distinct_cnt_synopsis_size', 'int'),
      ('sample_size', 'int'),
      ('density', 'double'),
      ('bucket_cnt', 'int'),
      ('histogram_type', 'int'),
      ('global_stats', 'int', 'true', '0'),
      ('user_stats', 'int', 'true', '0'),
      ('spare1', 'int', 'true'),
      ('spare2', 'int', 'true'),
      ('spare3', 'int', 'true'),
      ('spare4', 'varchar:MAX_VALUE_LENGTH', 'true'),
      ('spare5', 'varchar:MAX_VALUE_LENGTH', 'true'),
      ('spare6', 'varchar:MAX_VALUE_LENGTH', 'true')
  ]
  )

def_table_schema(
  owner = 'jiangxiu.wt',
  table_name = '__all_histogram_stat',
  table_id = '330',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
      ('table_id', 'int'),
      ('partition_id', 'int'),
      ('column_id', 'int'),
      ('endpoint_num', 'int')
  ],
  in_tenant_space = True,
  is_cluster_private = False,

  normal_columns = [
      ('object_type', 'int'),
      ('endpoint_normalized_value', 'double'),
      ('endpoint_value', 'varchar:MAX_VALUE_LENGTH'),
      ('b_endpoint_value', 'varchar:MAX_VALUE_LENGTH'),
      ('endpoint_repeat_cnt', 'int')
  ]
  )

def_table_schema(
  owner = 'yibo.tyf',
  table_name = '__all_monitor_modified',
  table_id = '331',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
      ('table_id', 'int'),
      ('tablet_id', 'int')
  ],
  in_tenant_space = True,
  is_cluster_private = False,
  normal_columns = [
      ('last_inserts', 'int', 'true', '0'),
      ('last_updates', 'int', 'true', '0'),
      ('last_deletes', 'int', 'true', '0'),
      ('inserts', 'int', 'true', '0'),
      ('updates', 'int', 'true', '0'),
      ('deletes', 'int', 'true', '0'),
      ('flags', 'int', 'true', 'NULL')
  ]
  )

def_table_schema(
  owner = 'jiangxiu.wt',
  table_name = '__all_table_stat_history',
  table_id = '332',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
      ('table_id', 'int'),
      ('partition_id', 'int'),
      ('savtime', 'timestamp')
  ],
  in_tenant_space = True,
  is_cluster_private = False,
  normal_columns = [
      ('object_type', 'int'),
      ('flags', 'int'),
      ('last_analyzed', 'timestamp'),
      ('sstable_row_cnt', 'int'),
      ('sstable_avg_row_len', 'double'),
      ('macro_blk_cnt', 'int'),
      ('micro_blk_cnt', 'int'),
      ('memtable_row_cnt', 'int'),
      ('memtable_avg_row_len', 'double'),
      ('row_cnt', 'int'),
      ('avg_row_len', 'double'),
      ('spare1', 'int', 'true'),
      ('spare2', 'int', 'true'),
      ('spare3', 'int', 'true'),
      ('spare4', 'varchar:MAX_VALUE_LENGTH', 'true'),
      ('spare5', 'varchar:MAX_VALUE_LENGTH', 'true'),
      ('spare6', 'varchar:MAX_VALUE_LENGTH', 'true'),
      ('index_type', 'bool'),
      ('stattype_locked', 'int', 'true', '0')
  ]
  )

def_table_schema(
  owner = 'jiangxiu.wt',
  table_name = '__all_column_stat_history',
  table_id = '333',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
      ('table_id', 'int'),
      ('partition_id', 'int'),
      ('column_id', 'int'),
      ('savtime', 'timestamp')
  ],
  in_tenant_space = True,
  is_cluster_private = False,
  normal_columns = [
      ('object_type', 'int'),
      ('flags', 'int'),
      ('last_analyzed', 'timestamp'),
      ('distinct_cnt', 'int'),
      ('null_cnt', 'int'),
      ('max_value', 'varchar:MAX_VALUE_LENGTH'),
      ('b_max_value', 'varchar:MAX_VALUE_LENGTH'),
      ('min_value', 'varchar:MAX_VALUE_LENGTH'),
      ('b_min_value', 'varchar:MAX_VALUE_LENGTH'),
      ('avg_len', 'double'),
      ('distinct_cnt_synopsis','varchar:MAX_LLC_BITMAP_LENGTH'),
      ('distinct_cnt_synopsis_size', 'int'),
      ('sample_size', 'int'),
      ('density', 'double'),
      ('bucket_cnt', 'int'),
      ('histogram_type', 'int'),
      ('spare1', 'int', 'true'),
      ('spare2', 'int', 'true'),
      ('spare3', 'int', 'true'),
      ('spare4', 'varchar:MAX_VALUE_LENGTH', 'true'),
      ('spare5', 'varchar:MAX_VALUE_LENGTH', 'true'),
      ('spare6', 'varchar:MAX_VALUE_LENGTH', 'true')
  ]
  )

def_table_schema(
  owner = 'jiangxiu.wt',
  table_name = '__all_histogram_stat_history',
  table_id = '334',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
      ('table_id', 'int'),
      ('partition_id', 'int'),
      ('column_id', 'int'),
      ('endpoint_num', 'int'),
      ('savtime', 'timestamp')
  ],
  in_tenant_space = True,
  is_cluster_private = False,
  normal_columns = [
      ('object_type', 'int'),
      ('endpoint_normalized_value', 'double'),
      ('endpoint_value', 'varchar:MAX_VALUE_LENGTH'),
      ('b_endpoint_value', 'varchar:MAX_VALUE_LENGTH'),
      ('endpoint_repeat_cnt', 'int'),
      ('spare1', 'int', 'true'),
      ('spare2', 'int', 'true'),
      ('spare3', 'int', 'true'),
      ('spare4', 'varchar:MAX_VALUE_LENGTH', 'true'),
      ('spare5', 'varchar:MAX_VALUE_LENGTH', 'true'),
      ('spare6', 'varchar:MAX_VALUE_LENGTH', 'true')
  ]
  )

def_table_schema(
  owner = 'jiangxiu.wt',
  table_name = '__all_optstat_global_prefs',
  table_id = '335',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
      ('sname', 'varchar:30')
  ],
  in_tenant_space = True,
  is_cluster_private = False,
  normal_columns = [
      ('sval1', 'number:38:0', 'true'),
      ('sval2', 'timestamp', 'true'),
      ('spare1', 'int', 'true'),
      ('spare2', 'int', 'true'),
      ('spare3', 'int', 'true'),
      ('spare4', 'varchar:MAX_VALUE_LENGTH', 'true'),
      ('spare5', 'varchar:MAX_VALUE_LENGTH', 'true'),
      ('spare6', 'timestamp', 'true')
  ]
  )

def_table_schema(
  owner = 'jiangxiu.wt',
  table_name = '__all_optstat_user_prefs',
  table_id = '336',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
      ('table_id', 'int'),
      ('pname', 'varchar:30')
  ],
  in_tenant_space = True,
  is_cluster_private = False,
  normal_columns = [
      ('valnum', 'int', 'true'),
      ('valchar', 'varchar:4000', 'true'),
      ('chgtime', 'timestamp', 'true'),
      ('spare1', 'int', 'true')
  ]
  )

# 342: legacy ls meta table (abandoned)

def_table_schema(
    owner = 'yanmu.ztl',
    table_name = '__all_tablet_to_table',
    table_id = '343',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('tablet_id', 'int')
  ],
    in_tenant_space = True,
    is_cluster_private = False,
    normal_columns = [
        ('table_id', 'int')
  ]
  )

# 344: __all_tablet_meta_table # migrated to SQLite, see gen_sqlite_table_def above
# Placeholder - original definition removed, using SQLite version

# 345: legacy ls status table (abandoned)
# 346: __all_zone_v2 # abandoned in 4.0

# 348: __all_log_archive_progress # abandoned
# 349: __all_log_archive_history # abandoned
# 350: __all_log_archive_piece_files # abandoned
# 351: legacy ls log archive progress table # abandoned


# 352: legacy ls table (abandoned)
# 353: abandoned
# 354: __all_backup_storage_info # abandoned
# 357: __all_backup_job # abandoned
# 358: __all_backup_job_history # abandoned
# 359: __all_backup_task # abandoned
# 360: __all_backup_task_history # abandoned
# 361: __all_backup_ls_task (abandoned)
# 362: __all_backup_ls_task_history # abandoned
# 363: __all_backup_ls_task_info# abandoned
# 364: __all_backup_skipped_tablet# abandoned
# 365: __all_backup_skipped_tablet_history
# 366: __all_tenant_info (abandoned)
# 367: __all_cluster_info # abandoned in 4.0
# 368: __all_cluster_config # abandoned in 4.0

def_table_schema(
  owner = 'yanmu.ztl',
  table_name    = '__all_tablet_to_table_history',
  table_id      = '369',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
      ('tablet_id', 'int'),
      ('schema_version', 'int')
  ],
  in_tenant_space = True,

  normal_columns = [
    ('table_id', 'int'),
    ('is_deleted', 'int')
  ]
  )

# 370: legacy ls recovery stat table (abandoned)
# 371: __all_backup_ls_task_info_history # abandoned

# 372: __all_tablet_replica_checksum # migrated to SQLite, see gen_sqlite_table_def above
# Placeholder - original definition removed, using SQLite version

# do checksum(user tenant) between primary cluster and standby cluster
# differ from __all_tablet_replica_checksum, it is tablet level
def_table_schema(
    owner = 'quanwei.wqw',
    table_name = '__all_tablet_checksum',
    table_id   = '373',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('compaction_scn', 'uint'),
        ('tablet_id', 'int')
  ],
    in_tenant_space = True,
    is_cluster_private = False,
    normal_columns = [
        ('data_checksum', 'int'),
        ('row_count', 'int'),
        ('column_checksums', 'varbinary:OB_MAX_VARBINARY_LENGTH', 'true')
  ]
  )

# 374: legacy ls replica task table (abandoned)



def_table_schema(
  owner = 'fyy280124',
  table_name     = '__all_scheduler_job',
  table_id       = '377',
  table_type     = 'SYSTEM_TABLE',
  gm_columns     = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
    ('job_name', 'varchar:128', 'false'),
    ('job', 'int', 'false')
  ],
  in_tenant_space = True,
  is_cluster_private = False,
  normal_columns = [
    ('lowner', 'varchar:OB_MAX_DATABASE_NAME_LENGTH', 'false'),
    ('powner', 'varchar:OB_MAX_DATABASE_NAME_LENGTH', 'false'),
    ('cowner', 'varchar:OB_MAX_DATABASE_NAME_LENGTH', 'false'),
    ('last_date', 'timestamp', 'true'),
    ('this_date', 'timestamp', 'true'),
    ('next_date', 'timestamp', 'false'),
    ('total', 'int', 'true', '0'),
    ('interval#', 'varchar:4000', 'false'),
    ('failures', 'int', 'true', '0'),
    ('flag', 'int', 'false'),
    ('what', 'varchar:65536', 'true'),
    ('nlsenv', 'varchar:4000', 'true'),
    ('charenv', 'varchar:4000', 'true'),
    ('field1', 'varchar:MAX_ZONE_LENGTH', 'true'),
    ('scheduler_flags', 'int', 'true', '0'),
    ('exec_env', 'varchar:OB_MAX_PROC_ENV_LENGTH', 'true'),
    ('job_style', 'varchar:128', 'true'),
    ('program_name', 'varchar:128', 'true'),
    ('job_type', 'varchar:128', 'true'),
    ('job_action', 'varchar:65536', 'true'),
    ('number_of_argument', 'int', 'true'),
    ('start_date', 'timestamp', 'true'),
    ('repeat_interval', 'varchar:4000', 'true'),
    ('end_date', 'timestamp', 'true'),
    ('job_class', 'varchar:128', 'true'),
    ('enabled', 'bool', 'true'),
    ('auto_drop', 'bool', 'true'),
    ('state', 'varchar:128', 'true'),
    ('run_count', 'int', 'true'),
    ('retry_count', 'int', 'true'),
    ('last_run_duration', 'int', 'true'),
    ('max_run_duration', 'int', 'true'),
    ('comments', 'varchar:4096', 'true'),
    ('credential_name', 'varchar:128', 'true'),
    ('destination_name', 'varchar:128', 'true'),
    ('interval_ts', 'int', 'true'),
    ('user_id', 'int', 'true', 'OB_INVALID_ID'),
    ('database_id', 'int', 'true', 'OB_INVALID_ID'),
    ('max_failures', 'int', 'true', '0'),
    ('func_type', 'int', 'true', '0'),
    ('schedule_type', 'varchar:12', 'true'),
    ('this_exec_date', 'timestamp', 'true'),
    ('this_exec_addr', 'varchar:MAX_IP_ADDR_LENGTH', 'true'),
    ('this_exec_trace_id', 'varchar:OB_MAX_TRACE_ID_BUFFER_SIZE', 'true')
  ]
  )

def_table_schema(
  owner = 'fyy280124',
  table_name     = '__all_scheduler_program',
  table_id       = '379',
  table_type     = 'SYSTEM_TABLE',
  gm_columns     = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
    ('program_name', 'varchar:30', 'false')
  ],
  in_tenant_space = True,
  is_cluster_private = False,
  normal_columns = [
    ('program_type', 'varchar:16', 'true'),
    ('program_action', 'varchar:4000', 'true'),
    ('number_of_argument', 'int', 'true'),
    ('enabled', 'varchar:5', 'true'),
    ('detached', 'varchar:5', 'true'),
    ('schedule_limit', 'varchar:200', 'true'),
    ('priority', 'int', 'true'),
    ('weight', 'int', 'true'),
    ('max_runs', 'int', 'true'),
    ('max_failures', 'int', 'true'),
    ('max_run_duration', 'varchar:200', 'true'),
    ('nls_env', 'varchar:4000', 'true'),
    ('comments', 'varchar:240', 'true'),
    ('lowner', 'varchar:OB_MAX_DATABASE_NAME_LENGTH', 'true'),
    ('powner', 'varchar:OB_MAX_DATABASE_NAME_LENGTH', 'true'),
    ('cowner', 'varchar:OB_MAX_DATABASE_NAME_LENGTH', 'true')
  ]
  )

def_table_schema(
  owner = 'fyy280124',
  table_name     = '__all_scheduler_program_argument',
  table_id       = '380',
  table_type     = 'SYSTEM_TABLE',
  gm_columns     = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
    ('program_name', 'varchar:30'),
    ('job_name', 'varchar:30'),
    ('argument_position', 'int'),
    ('is_for_default', 'bool')
  ],
  in_tenant_space = True,
  is_cluster_private = False,
  normal_columns = [
    ('argument_name', 'varchar:30', 'true'),
    ('argument_type', 'varchar:61', 'true'),
    ('metadata_attribute', 'varchar:19', 'true'),
    ('default_value', 'varchar:4000', 'true'),
    ('out_argument', 'varchar:5', 'true')
  ]
  )

all_context_def = dict(
  owner = 'peihan.dph',
  table_name    = '__all_context',
  table_id      = '381',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
  ('context_id', 'int')
  ],
  in_tenant_space = True,
  is_cluster_private = False,

  normal_columns = [
  ('namespace', 'varchar:OB_MAX_CONTEXT_STRING_LENGTH', 'false', ''),
  ('schema_version', 'int', 'false', '-1'),
  ('database_name', 'varchar:OB_MAX_CONTEXT_STRING_LENGTH', 'false', ''),
  ('package', 'varchar:OB_MAX_CONTEXT_STRING_LENGTH', 'false', ''),
  ('type', 'int', 'false', '0'),
  ('origin_con_id', 'int', 'false', '-1'),
  ('tracking', 'int', 'false', '1')
  ]
  )
def_table_schema(**all_context_def)
def_table_schema(**gen_history_table_def(382, all_context_def))

# 383: __all_global_context_value (abandoned)
# 385: legacy ls election reference info table (abandoned)

# backup clean inner table
# 386: __all_backup_delete_job # abandoned
# 387: __all_backup_delete_job_history # abandoned
# 388: __all_backup_delete_task # abandoned
# 389: __all_backup_delete_task_history # abandoned
# 390: __all_backup_delete_ls_task # abandoned
# 391: __all_backup_delete_ls_task_history # abandoned
# 393: __all_merge_info # abandoned, migrated to SQLite

def_table_schema(
  owner = 'donglou.zl',
  table_name    = '__all_freeze_info',
  table_id      = '394',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
      ('frozen_scn', 'uint')
  ],
  in_tenant_space = True,
  is_cluster_private = False,

  normal_columns = [
      ('cluster_version', 'int'),
      ('schema_version', 'int')
  ]
  )

# 395: __all_disk_io_calibration (abandoned)
# 396:__all_plan_baseline abandoned
# 397:__all_plan_baseline_item abandoned
# 398:__all_spm_config abandoned
# 399: abandoned
# 400:__all_backup_parameter abandoned
# 401: legacy ls restore progress table (abandoned)
# 402: legacy ls restore history table (abandoned)
# 403: __all_backup_storage_info_history (abandoned)
# 404: __all_backup_delete_policy (abandoned)

all_mock_fk_parent_table_def = dict(
  owner = 'bin.lb',
  table_name    = '__all_mock_fk_parent_table',
  table_id      = '405',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
    ('mock_fk_parent_table_id', 'int')
  ],
  in_tenant_space = True,
  normal_columns = [
    ('database_id', 'int'),
    ('mock_fk_parent_table_name', 'varchar:OB_MAX_TABLE_NAME_LENGTH', 'false', ''),
    ('schema_version', 'int', 'false', '-1')
  ]
  )

def_table_schema(**all_mock_fk_parent_table_def)

def_table_schema(**gen_history_table_def(406, all_mock_fk_parent_table_def))

all_mock_fk_parent_table_column_def = dict(
  owner = 'bin.lb',
  table_name    = '__all_mock_fk_parent_table_column',
  table_id      = '407',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
    ('mock_fk_parent_table_id', 'int'),
    ('parent_column_id', 'int')
  ],
  in_tenant_space = True,
  normal_columns = [
   ('parent_column_name', 'varchar:OB_MAX_COLUMN_NAME_LENGTH', 'false', ''),
   ('schema_version', 'int', 'false', '-1')
  ]
  )

def_table_schema(**all_mock_fk_parent_table_column_def)

def_table_schema(**gen_history_table_def(408, all_mock_fk_parent_table_column_def))
# 409: __all_log_restore_source abandoned

# 410: __all_kv_ttl_task (abandoned)
# 411: __all_kv_ttl_task_history (abandoned)

# 412: __all_service_epoch (abandoned)

def_table_schema(
  owner = 'tonghui.ht',
  table_name    = '__all_spatial_reference_systems',
  table_id      = '413',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
    ('srs_id', 'uint')
  ],
  in_tenant_space = True,
  is_cluster_private = False,

  normal_columns = [
    ('srs_version', 'uint', 'false'),
    ('srs_name', 'varchar:128', 'false'),
    ('organization', 'varchar:256', 'true'),
    ('organization_coordsys_id', 'uint', 'true'),
    ('definition', 'varchar:4096', 'false'),
    ('minX', 'number:38:10', 'true'),
    ('maxX', 'number:38:10', 'true'),
    ('minY', 'number:38:10', 'true'),
    ('maxY', 'number:38:10', 'true'),
    ('proj4text', 'varchar:2048', 'true'),
    ('description', 'varchar:2048', 'true')
  ]
  )

# 414 : __all_tenant_datafile
# 415 : __all_tenant_datafile_history

# 416: __all_column_checksum_error_info # migrated to SQLite, see gen_sqlite_table_def above
# Placeholder - original definition removed, using SQLite version

# 417 : abandoned
# 418 : abandoned

# 419-422: abandoned metadata tables

# 429: __all_arbitration_service (abandoned)
# 430: legacy ls arb replica task table (abandoned)

# 431: __all_data_dictionary_in_log (removed: CDC data dictionary log dumping deleted)

# 432: legacy ls arb replica task history table (abandoned)


# 445: __all_cluster_event_history # migrated to SQLite, see gen_sqlite_table_def above
# 447 : legacy ls log restore stat table

# 450: __all_external_table_file # abandoned in seekdb



# 453: __all_zone_storage (abandoned)
# 454: __all_zone_storage_operation (abandoned)

# 455 : __wr_active_session_history
# 455: __wr_active_session_history # removed

# 456 : __wr_snapshot
# __wr_snapshot # removed

# 457 : __wr_statname
# __wr_statname # removed

# 458 : __wr_sysstat
# __wr_sysstat # removed

# 460: __all_tenant_snapshot (abandoned)
# 461: __all_tenant_snapshot_ls (abandoned)
# 462: __all_tenant_snapshot_ls_replica (abandoned)

def_table_schema(
    owner = 'yangyifei.yyf',
    table_name = '__all_dbms_lock_allocated',
    table_id = '471',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
      ('name', 'varchar:128', 'false')
  ],
    in_tenant_space = True,
    is_cluster_private = False,
    meta_record_in_sys = False,
    normal_columns = [
      ('lockid', 'int'),
      ('lockhandle', 'varchar:128'),
      ('expiration', 'timestamp')
  ]
  )

# __wr_control # removed

# 473 : __all_tenant_event_history 

def_table_schema(
  table_name     = '__all_scheduler_job_class',
  owner          = 'huangrenhuang.hrh',
  table_id       = '474',
  table_type     = 'SYSTEM_TABLE',
  gm_columns     = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
    ('job_class_name', 'varchar:30', 'false')
  ],
  in_tenant_space = True,
  is_cluster_private = False,
  normal_columns = [
    ('service', 'varchar:64', 'true'),
    ('logging_level', 'varchar:11', 'true'),
    ('log_history', 'number:38:0', 'true'),
    ('comments', 'varchar:240', 'true')
  ]
  )
# 475: __all_recover_table_job # abandoned
# 476: __all_recover_table_job_history # abandoned
# 477: __all_import_table_job # abandoned
# 478: __all_import_table_job_history # abandoned
# 479: __all_import_table_task # abandoned
# 480: __all_import_table_task_history # abandoned
# 481 : __all_import_stmt_exec_history

# 485: __all_clone_job (abandoned)
# 486: __all_clone_job_history (abandoned)

# __wr_system_event # removed

# __wr_event_name # removed

# 489: __all_tenant_scheduler_running_job
all_routine_privilege_def = dict(
    owner = 'mingye.swj',
    table_name    = '__all_routine_privilege',
    table_id      = '490',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('user_id', 'int'),
        ('database_name', 'varbinary:OB_MAX_DATABASE_NAME_BINARY_LENGTH'),
        ('routine_name', 'varbinary:OB_MAX_ROUTINE_NAME_BINARY_LENGTH'),
        ('routine_type', 'int')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('all_priv', 'int', 'false', '0'),
      ('grantor', 'varchar:OB_MAX_USER_NAME_LENGTH_STORE', 'true'),
      ('grantor_host', 'varchar:OB_MAX_HOST_NAME_LENGTH', 'true')
  ]
  )

def_table_schema(**all_routine_privilege_def)
def_table_schema(**gen_history_table_def(491, all_routine_privilege_def))

# __wr_sqlstat # removed

def_table_schema(
  owner = 'zhenling.zzg',
  table_name = '__all_aux_stat',
  table_id = '494',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
      ('id', 'bigint')
  ],
  in_tenant_space = True,
  is_cluster_private = False,
  normal_columns = [
      ('last_analyzed', 'timestamp'),
      ('cpu_speed', 'bigint', 'true', '2500'),
      ('disk_seq_read_speed', 'bigint', 'true', '2000'),
      ('disk_rnd_read_speed', 'bigint', 'true', '150'),
      ('network_speed', 'bigint', '1000')
  ]
  )



# 497: __all_client_to_server_session_info (removed)

# 500: __all_tenant_snapshot_job (abandoned)

# __wr_sqltext # removed

# 502: __all_trusted_root_certificate (abandoned)

all_column_privilege_def = dict(
    owner = 'mingye.swj',
    table_name    = '__all_column_privilege',
    table_id      = '505',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('priv_id', 'int')
  ],
    in_tenant_space = True,
    normal_columns =[
        ('user_id', 'int'),
        ('database_name', 'varbinary:1024'),
        ('table_name', 'varbinary:1024'),
        ('column_name', 'varbinary:1024'),
        ('all_priv', 'int', 'false', '0')
  ]
  )

def_table_schema(**all_column_privilege_def)

def_table_schema(**gen_history_table_def(506, all_column_privilege_def))

# 507: __all_tenant_snapshot_ls_replica_history (abandoned)
# 508: legacy ls replica task history table (abandoned)
# 509 : legacy ls compaction status table
# 510 : __all_tablet_compaction_status
# 511 : __all_tablet_checksum_error_info (abandoned)
# 516 : __all_service (abandoned)
# 517: __all_storage_io_usage (abandoned)


# 520 : __all_spm_evo_result abandoned

def_table_schema(
  owner = 'yangyifei.yyf',
  table_name = '__all_detect_lock_info_v2',
  table_id = '521',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
    ('task_type', 'int'),
    ('obj_type', 'int'),
    ('obj_id', 'int'),
    ('lock_mode', 'int'),
    ('owner_type', 'int'),
    ('owner_id', 'int')
  ],
  in_tenant_space = True,
  is_cluster_private = False,
  meta_record_in_sys = False,
  normal_columns = [
    ('cnt', 'int'),
    ('detect_func_no', 'int'),
    ('detect_func_param', 'varbinary:MAX_LOCK_DETECT_PARAM_LENGTH', 'true', '')
  ]
  )

# 525: __wr_sql_plan

# __wr_sql_plan # removed

# 527: __all_kv_redis_table abandoned

all_ncomp_dll_v2 = dict(
  owner = 'hr351303',
  table_name = '__all_ncomp_dll_v2',
  table_id = '528',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
    ('database_id', 'int', 'false'),
    ('key_id', 'int'),
    ('compile_db_id', 'int'),
    ('arch_type', 'varchar:128'),
    ('build_version', 'varchar:OB_SERVER_VERSION_LENGTH')
  ],
  in_tenant_space = True,

  normal_columns = [
    ('merge_version', 'int'),
    ('dll', 'longblob', 'false'),
    ('stack_size', 'longblob', 'true')
  ]
  )
def_table_schema(**all_ncomp_dll_v2)

# __wr_sql_plan_aux_key2snapshot # removed

def_table_schema(
  owner = 'youchuan.yc',
  table_name = '__ft_dict_ik_utf8',
  table_id = '531',
  table_type = 'SYSTEM_TABLE',
    gm_columns = [],
    rowkey_columns = [
    ('word', 'varchar:2048')
  ],
  in_tenant_space = True,
  normal_columns = []
  )

def_table_schema(
  owner = 'youchuan.yc',
  table_name = '__ft_stopword_ik_utf8',
  table_id = '532',
  table_type = 'SYSTEM_TABLE',
    gm_columns = [],
    rowkey_columns = [
    ('word', 'varchar:2048')
  ],
  in_tenant_space = True,
  normal_columns = []
  )

def_table_schema(
  owner = 'youchuan.yc',
  table_name = '__ft_quantifier_ik_utf8',
  table_id = '533',
  table_type = 'SYSTEM_TABLE',
    gm_columns = [],
    rowkey_columns = [
    ('word', 'varchar:2048')
  ],
  in_tenant_space = True,
  normal_columns = []
  )

# 534: __ft_dict_ik_gbk
# 535: __ft_stopword_ik_gbk
# 536: __ft_quantifier_ik_gbk

all_catalog_def = dict(
    owner = 'linyi.cl',
    table_name    = '__all_catalog',
    table_id      = '537',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('catalog_id', 'int')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('catalog_name', 'varchar:OB_MAX_CATALOG_NAME_LENGTH', 'false', ''),
      ('catalog_properties', 'varbinary:OB_MAX_VARCHAR_LENGTH', 'true')
  ]
  )

def_table_schema(**all_catalog_def)

def_table_schema(**gen_history_table_def(538, all_catalog_def))

all_catalog_privilege_def = dict(
    owner = 'linyi.cl',
    table_name    = '__all_catalog_privilege',
    table_id      = '539',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('user_id', 'int'),
        ('catalog_name', 'varbinary:OB_MAX_CATALOG_NAME_BINARY_LENGTH')
  ],
    in_tenant_space = True,

    normal_columns = [
      ('priv_set', 'int', 'false', '0')
    ]
  )

def_table_schema(**all_catalog_privilege_def)

def_table_schema(**gen_history_table_def(540, all_catalog_privilege_def))

# 542: __sslog_table
def_table_schema(
  owner = 'jiabokai.jbk',
  table_name = '__all_pl_recompile_objinfo',
  table_id = '544',
  table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
    ('recompile_obj_id', 'int')
  ],
  in_tenant_space = True,
  normal_columns = [
    ('ref_obj_name', 'varchar:OB_MAX_CORE_TALBE_NAME_LENGTH'),
    ('schema_version', 'int'),
    ('fail_count', 'int')
  ]
  )
def_table_schema(
  owner = 'yangjiali.yjl',
  table_name = '__all_vector_index_task',
  table_id = '545',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
      ('table_id', 'int'),
      ('tablet_id', 'int'),
      ('task_id', 'int')
  ],
  in_tenant_space = True,
  normal_columns = [
    ('trigger_type', 'int'),
    ('task_type', 'int'),
    ('status', 'int'),
    ('target_scn', 'int'),
    ('ret_code', 'int'),
    ('trace_id', 'varchar:OB_MAX_ERROR_MSG_LEN')
  ]
  )

def_table_schema(
  owner = 'yangjiali.yjl',
  table_name = '__all_vector_index_task_history',
  table_id = '546',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
      ('table_id', 'int'),
      ('tablet_id', 'int'),
      ('task_id', 'int')
  ],
  in_tenant_space = True,
  normal_columns = [
    ('trigger_type', 'int'),
    ('task_type', 'int'),
    ('status', 'int'),
    ('target_scn', 'int'),
    ('ret_code', 'int'),
    ('trace_id', 'varchar:OB_MAX_ERROR_MSG_LEN')
  ]
  )

all_ccl_rule_def = dict(
  owner = 'zhl413386',
  table_name = '__all_ccl_rule',
  table_id = '547',
  table_type = 'SYSTEM_TABLE',
  gm_columns = ['gmt_create', 'gmt_modified'],
  rowkey_columns = [
    ('ccl_rule_id', 'int')
  ],
  in_tenant_space = True,

  normal_columns = [
    ('ccl_rule_name', 'varchar:OB_MAX_COLUMN_NAME_LENGTH'),
    ('affect_user_name', 'varchar:OB_MAX_USER_NAME_LENGTH'),
    ('affect_host', 'varchar:OB_MAX_HOST_NAME_LENGTH'),
    ('affect_for_all_databases', 'bool', 'false', 'true'),
    ('affect_for_all_tables', 'bool', 'false', 'true'),
    ('affect_database', 'varchar:OB_MAX_DATABASE_NAME_LENGTH', 'true', 'NULL'),
    ('affect_table', 'varchar:OB_MAX_TABLE_NAME_LENGTH', 'true', 'NULL'),
    ('affect_dml', 'int', 'false', 0),
    ('affect_scope', 'int', 'false', 0),
    ('ccl_keywords', 'varchar:OB_MAX_VARCHAR_LENGTH'),
    ('max_concurrency', 'int', 'false', 0)
  ]
  )

def_table_schema(**all_ccl_rule_def)
def_table_schema(**gen_history_table_def(548, all_ccl_rule_def))



all_ai_model_def = dict(
    owner = 'shenyunlong.syl',
    table_name = '__all_ai_model',
    table_id = '550',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
      ('model_id', 'int')
  ],

    in_tenant_space = True,
    is_cluster_private = False,
    meta_record_in_sys = False,
    normal_columns = [
        ('name', 'varchar:128', 'false'),
        ('type', 'int', 'false'),
        ('model_name', 'varchar:128', 'false')
  ]
)

def_table_schema(**all_ai_model_def)
def_table_schema(**gen_history_table_def(551, all_ai_model_def))

all_ai_model_endpoint_def = dict(
    owner = 'shenyunlong.syl',
    table_name = '__all_ai_model_endpoint',
    table_id = '552',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
      ('endpoint_id', 'int'),
      ('scope', 'varchar:128')
    ],

    in_tenant_space = True,
    is_cluster_private = False,
    meta_record_in_sys = False,
    normal_columns = [
        ('version', 'int', 'false'),
        ('endpoint_name', 'varchar:128'),
        ('ai_model_name', 'varchar:128', 'false'),
        ('url', 'varchar:2048', 'true'),
        ('access_key', 'varchar:2048', 'true'),
        ('provider', 'varchar:128', 'true'),
        ('request_model_name', 'varchar:128', 'true'),
        ('parameters', 'varchar:2048', 'true'),
        ('request_transform_fn', 'varchar:64', 'true'),
        ('response_transform_fn', 'varchar:64', 'true')
    ]
)
def_table_schema(**all_ai_model_endpoint_def)

all_tenant_location_def = dict(
    owner = 'cjl476581',
    table_name     = '__all_location',
    table_id       = '553',
    table_type     = 'SYSTEM_TABLE',
    gm_columns     = ['gmt_create', 'gmt_modified'],
    rowkey_columns = [
        ('location_id', 'int')
  ],
    normal_columns = [
        ('location_name', 'varchar:OB_MAX_LOCATION_NAME_LENGTH', 'false', ''),
        ('location_url', 'varchar:OB_MAX_LOCATION_URL_LENGTH', 'false', ''),
        ('location_access_info', 'varchar:OB_MAX_LOCATION_ACCESS_INFO_LENGTH', 'false', '')
  ],
    in_tenant_space = True
  )
def_table_schema(**all_tenant_location_def)
def_table_schema(**gen_history_table_def(554, all_tenant_location_def))

all_objauth_mysql_def = dict(
    owner = 'cjl476581',
    table_name     = '__all_objauth_mysql',
    table_id       = '555',
    table_type = 'SYSTEM_TABLE',
    gm_columns = ['gmt_create', 'gmt_modified'],
    in_tenant_space = True,
    rowkey_columns = [
        ('user_id', 'int'),
        ('obj_name', 'varchar:OB_MAX_CORE_TALBE_NAME_LENGTH'),
        ('obj_type', 'int')
    ],
    normal_columns = [
      ('all_priv', 'int', 'false', 0),
      ('grantor', 'varchar:OB_MAX_USER_NAME_LENGTH_STORE', 'false', ''),
      ('grantor_host', 'varchar:OB_MAX_HOST_NAME_LENGTH', 'false', '')
  ]
  )
def_table_schema(**all_objauth_mysql_def)
def_table_schema(**gen_history_table_def(556, all_objauth_mysql_def))


# Reserved position (placeholder before this line)
# Placeholder suggestion for this section: Use actual table names for placeholders
################################################################################
# End of System Table(0,10000]
################################################################################
################################### Placeholder Notice ###################################
# Placeholder example: Write comments at the beginning of the line to indicate which TABLE_ID is to be occupied and what the corresponding name is
# TABLE_ID: TABLE_NAME
#
# FARM will base the placeholder validation development branch TABLE_ID and TABLE_NAME matching check, if they do not match, FARM will intercept and report an error
#
# Note:
# 0. Placeholder before 'reserved position'
# 1. Always start by occupying the master, ensuring that the master branch is a superset of all other branches to avoid NAME and ID conflicts
# 2. After the master placeholder is set, do not change NAME on the development branch, otherwise FARM will consider it an ID placeholder conflict. If this scenario occurs, you need to modify the master placeholder first
# 3. It is recommended to use the accurate TABLE_NAME as a placeholder, TABLE_ID and TABLE_NAME are one-to-one corresponding within the system
# 4. Some tables are defined based on the schema of other base tables (e.g., gen_xx_table_def()), their actual table names are relatively complex, to facilitate placeholder usage, it is recommended to use the base table name for placeholders
#    - Example 1: def_table_schema(**gen_mysql_sys_agent_virtual_table_def('12393', all_def_keywords['__all_virtual_long_ops_status']))
#      * Base table name placeholder: # 12393: __all_virtual_long_ops_status
#      * Real table name placeholder: # 12393: __all_virtual_virtual_long_ops_status_mysql_sys_agent
#      * Base table name placeholder: # 15009: __all_virtual_sql_audit
#      * Real table name placeholder: # 15009: ALL_VIRTUAL_SQL_AUDIT
#    - Example 3: def_table_schema(**gen_sys_agent_virtual_table_def('15111', all_def_keywords['__all_routine_param']))
#      * Base table name placeholder: # 15111: __all_routine_param
#      * Real table name placeholder: # 15111: ALL_VIRTUAL_ROUTINE_PARAM_SYS_AGENT
# 5. Index table placeholder requirements TABLE_NAME should be used as follows: base table (data table) name, index name (index_name), actual index table name
#    For example: 100001 The placeholder method for the index table can be:
#       * # 100001: __idx_3_idx_data_table_id
#       * # 100001: idx_data_table_id
#       * # 100001: __all_table
################################################################################


################################################################################
# Reserved position
################################################################################
# Virtual Table (10000, 20000]
# Normally, virtual table's index_using_type should be USING_HASH.
################################################################################

def_table_schema(
  owner = 'bin.lb',
  table_name     = '__all_virtual_all_table',
  table_id       = '10001',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  in_tenant_space = True,

  rowkey_columns = [
  ('database_id', 'int'),
  ('table_name', 'varchar:OB_MAX_TABLE_NAME_LENGTH')
  ],
  normal_columns = [
  ('table_type', 'varchar:OB_MAX_TABLE_TYPE_LENGTH'),
  ('engine', 'varchar:MAX_ENGINE_LENGTH'),
  ('version', 'uint'),
  ('row_format', 'varchar:ROW_FORMAT_LENGTH'),
  ('rows', 'int'),
  ('avg_row_length', 'int'),
  ('data_length', 'int'),
  ('max_data_length', 'int'),
  ('index_length', 'int'),
  ('data_free', 'int'),
  ('auto_increment', 'uint'),
  ('create_time', 'timestamp'),
  ('update_time', 'timestamp'),
  ('check_time', 'timestamp'),
  ('collation', 'varchar:MAX_COLLATION_LENGTH'),
  ('checksum', 'int'),
  ('create_options', 'varchar:MAX_TABLE_STATUS_CREATE_OPTION_LENGTH'),
  ('comment', 'varchar:MAX_TABLE_COMMENT_LENGTH')
  ]
  )

def_table_schema(
  owner = 'bin.lb',
  table_name     = '__all_virtual_table_column',
  table_id       = '10002',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ('table_id', 'int'),
  ('field', 'varchar:OB_MAX_COLUMN_NAME_LENGTH')
  ],
  in_tenant_space = True,

  normal_columns = [
  ('type', 'varchar:OB_MAX_VARCHAR_LENGTH'),
  ('collation', 'varchar:MAX_COLLATION_LENGTH', 'true'),
  ('null', 'varchar:COLUMN_NULLABLE_LENGTH'),
  ('key', 'varchar:COLUMN_KEY_LENGTH'),
  ('default', 'varchar:COLUMN_DEFAULT_LENGTH', 'true'),
  ('extra', 'varchar:COLUMN_EXTRA_LENGTH'),
  ('privileges', 'varchar:MAX_COLUMN_PRIVILEGE_LENGTH'),
  ('comment', 'varchar:MAX_COLUMN_COMMENT_LENGTH'),
  ('is_hidden', 'int', 'false', '0')
  ]
  )

def_table_schema(
  owner = 'bin.lb',
  table_name     = '__all_virtual_table_index',
  table_id       = '10003',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ('table_id', 'int'),
  ('key_name', 'varchar:OB_MAX_COLUMN_NAME_LENGTH', 'false', ''),
  ('seq_in_index', 'int', 'false', '0')
  ],
  in_tenant_space = True,

  normal_columns = [
  ('table_schema', 'varchar:OB_MAX_DATABASE_NAME_LENGTH', 'false', ''),
  ('table', 'varchar:OB_MAX_TABLE_NAME_LENGTH', 'false', ''),
  ('non_unique', 'int', 'false', '0'),
  ('index_schema', 'varchar:OB_MAX_DATABASE_NAME_LENGTH', 'false', ''),
  ('column_name', 'varchar:OB_MAX_COLUMN_NAME_LENGTH', 'false', ''),
  ('collation', 'varchar:MAX_COLLATION_LENGTH', 'true'),
  ('cardinality', 'int', 'true'),
  ('sub_part', 'varchar:INDEX_SUB_PART_LENGTH', 'true'),
  ('packed', 'varchar:INDEX_PACKED_LENGTH', 'true'),
  ('null', 'varchar:INDEX_NULL_LENGTH', 'false', ''),
  ('index_type', 'varchar:INDEX_NULL_LENGTH', 'false', ''),
  ('comment', 'varchar:MAX_TABLE_COMMENT_LENGTH', 'true'),
  ('index_comment', 'varchar:MAX_TABLE_COMMENT_LENGTH', 'false', ''),
  ('is_visible', 'varchar:MAX_COLUMN_YES_NO_LENGTH', 'false', ''),
  ('expression', 'varchar:OB_MAX_DEFAULT_VALUE_LENGTH', 'true'),
  ('is_column_visible', 'int', 'false', '0')
  ]
  )

def_table_schema(
  owner = 'bin.lb',
  table_name     = '__all_virtual_show_create_database',
  table_id       = '10004',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ('database_id', 'int')
  ],
  in_tenant_space = True,

  normal_columns = [
  ('database_name', 'varchar:OB_MAX_DATABASE_NAME_LENGTH'),
  ('create_database', 'varchar:DATABASE_DEFINE_LENGTH'),
  ('create_database_with_if_not_exists', 'varchar:DATABASE_DEFINE_LENGTH')
  ]
  )

def_table_schema(
  owner = 'bin.lb',
  table_name     = '__all_virtual_show_create_table',
  table_id       = '10005',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ('table_id', 'int')
  ],
  in_tenant_space = True,

  normal_columns = [
  ('table_name', 'varchar:OB_MAX_TABLE_NAME_LENGTH'),
  ('create_table', 'longtext'),
  ('character_set_client', 'varchar:MAX_CHARSET_LENGTH'),
  ('collation_connection', 'varchar:MAX_CHARSET_LENGTH')
  ]
  )

def_table_schema(
  owner = 'xiaochu.yh',
  table_name     = '__all_virtual_session_variable',
  table_id       = '10006',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  normal_columns = [
  ('variable_name', 'varchar:OB_MAX_CONFIG_NAME_LEN', 'false', ''),
  ('value', 'varchar:OB_MAX_CONFIG_VALUE_LEN', 'true')
  ]
  )

def_table_schema(
  owner = 'sean.yyj',
  table_name     = '__all_virtual_privilege_grant',
  table_id       = '10007',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ('user_id', 'int')
  ],
  in_tenant_space = True,

  normal_columns = [
  ('grants', 'varchar:MAX_GRANT_LENGTH')
  ]
  )

def_table_schema(
  owner = 'xiaochu.yh',
  table_name     = '__all_virtual_processlist',
  table_id       = '10008',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  normal_columns = [
  ('id', 'uint', 'false', '0'),
  ('user', 'varchar:OB_MAX_USERNAME_LENGTH', 'false', ''),
  ('tenant', 'varchar:OB_MAX_TENANT_NAME_LENGTH_STORE'),
  ('host', 'varchar:OB_MAX_HOST_NAME_LENGTH', 'false', ''),
  ('db', 'varchar:OB_MAX_DATABASE_NAME_LENGTH', 'true'),
  ('command', 'varchar:OB_MAX_COMMAND_LENGTH', 'false', ''),
  ('sql_id', 'varchar:OB_MAX_SQL_ID_LENGTH', 'false', ''),
  ('time', 'double', 'false'),
  ('state', 'varchar:OB_MAX_SESSION_STATE_LENGTH', 'true'),
  ('info', 'varchar:MAX_COLUMN_VARCHAR_LENGTH', 'true'),
  ('master_sessid', 'uint', 'true'),
  ('user_client_ip', 'varchar:MAX_IP_ADDR_LENGTH', 'true'),
  ('user_host', 'varchar:OB_MAX_HOST_NAME_LENGTH', 'true'),
  ('trans_id', 'uint'),
  ('thread_id', 'uint'),
  ('ssl_cipher', 'varchar:OB_MAX_COMMAND_LENGTH', 'true'),
  ('trace_id', 'varchar:OB_MAX_TRACE_ID_BUFFER_SIZE', 'true', ''),
  ('trans_state', 'varchar:OB_MAX_TRANS_STATE_LENGTH', 'true'),
  ('total_time', 'double', 'false'),
  ('retry_cnt', 'int', 'false', '0'),
  ('retry_info', 'int', 'false', '0'),
  ('action', 'varchar:MAX_VALUE_LENGTH', 'true', ''),
  ('module', 'varchar:MAX_VALUE_LENGTH', 'true', ''),
  ('client_info', 'varchar:MAX_VALUE_LENGTH', 'true', ''),
  ('sql_trace', 'bool'),
  ('plan_id', 'int'),
  ('level', 'int'),
  ('sample_percentage', 'int'),
  ('record_policy', 'varchar:32'),
  ('lb_vid', 'bigint', 'true'),
  ('lb_vip', 'varchar:MAX_IP_ADDR_LENGTH', 'true'),
  ('lb_vport', 'int', 'true'),
  ('in_bytes', 'bigint'),
  ('out_bytes', 'bigint'),
  ('user_client_port', 'int', 'false', '0'),
  ('service_name', 'varchar:64', 'true'),
  ('total_cpu_time', 'double', 'false'),
  ('top_info', 'varchar:MAX_COLUMN_VARCHAR_LENGTH', 'true'),
  ('memory_usage', 'bigint', 'true')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'bin.lb',
  table_name     = '__all_virtual_warning',
  table_id       = '10009',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  normal_columns = [
  ('level', 'varchar:32'),
  ('code', 'int'),
  ('message', 'varchar:512'),# the same as warning buffer length
  ('ori_code', 'int'),
  ('sql_state', 'varchar:6')
  ]
  )

def_table_schema(
  owner = 'bin.lb',
  table_name     = '__all_virtual_current_tenant',
  table_id       = '10010',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ('id', 'int')
  ],
  in_tenant_space = True,
  normal_columns = [
  ('tenant_name', 'varchar:OB_MAX_TENANT_NAME_LENGTH_STORE'),
  ('create_stmt', 'varchar:TENANT_DEFINE_LENGTH')
  ]
  )

def_table_schema(
  owner = 'bin.lb',
  table_name     = '__all_virtual_database_status',
  table_id       = '10011',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  normal_columns = [
  ('db', 'varchar:OB_MAX_DATABASE_NAME_LENGTH'),
  ('read_only', 'int')
  ],  vtable_route_policy = 'local'
)

def_table_schema(
  owner = 'bin.lb',
  table_name     = '__all_virtual_tenant_status',
  table_id       = '10012',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  normal_columns = [
  ('tenant', 'varchar:OB_MAX_TENANT_NAME_LENGTH'),
  ('read_only', 'int')
  ],  vtable_route_policy = 'local'
)

# 10013: __tenant_virtual_interm_result # abandoned in 4.0
# 10014: __tenant_virtual_partition_stat # abandoned in 4.0

# 10015: __tenant_virtual_statname # removed

# 10016: __tenant_virtual_event_name # removed

def_table_schema(
  owner = 'xiaochu.yh',
  table_name     = '__all_virtual_global_variable',
  table_id       = '10017',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  normal_columns = [
  ('variable_name', 'varchar:OB_MAX_CONFIG_NAME_LEN', 'false', ''),
  ('value', 'varchar:OB_MAX_CONFIG_VALUE_LEN', 'true')
  ]
  )

def_table_schema(
  owner = 'bin.lb',
  table_name     = '__all_virtual_show_tables',
  table_id       = '10018',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  in_tenant_space = True,

  rowkey_columns = [
  ('database_id', 'int'),
  ('table_name', 'varchar:OB_MAX_TABLE_NAME_LENGTH')
  ],
  normal_columns = [
  ('table_type', 'varchar:OB_MAX_TABLE_TYPE_LENGTH')
  ]
  )

def_table_schema(
  owner = 'linlin.xll',
  table_name     = '__all_virtual_show_create_procedure',
  table_id       = '10019',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ('routine_id', 'int')
  ],
  in_tenant_space = True,

  normal_columns = [
  ('routine_name', 'varchar:OB_MAX_ROUTINE_NAME_LENGTH'),
  ('create_routine', 'longtext'),
  ('proc_type', 'int'),
  ('character_set_client', 'varchar:MAX_CHARSET_LENGTH'),
  ('collation_connection', 'varchar:MAX_CHARSET_LENGTH'),
  ('collation_database', 'varchar:MAX_CHARSET_LENGTH'),
  ('sql_mode', 'varchar:MAX_CHARSET_LENGTH')
  ]
  )

# 11001: __all_virtual_core_meta_table (abandoned)
# 11002: __all_virtual_zone_stat # abandoned in 4.0.

def_table_schema(
  owner = 'xiaoyi.xy',
  table_name     = '__all_virtual_plan_cache_stat',
  table_id       = '11003',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  in_tenant_space = True,
  rowkey_columns = [
  ],

  normal_columns = [
    ('sql_num', 'int'),
    ('mem_used', 'int'),
    ('mem_hold', 'int'),
    ('access_count', 'int'),
    ('hit_count', 'int'),
    ('hit_rate', 'int'),
    ('plan_num', 'int'),
    ('mem_limit', 'int'),
    ('hash_bucket', 'int'),
    ('stmtkey_num', 'int'),
    ('pc_ref_plan_local', 'int'),
    ('pc_ref_plan_remote', 'int'),
    ('pc_ref_plan_dist', 'int'),
    ('pc_ref_plan_arr', 'int'),
    ('pc_ref_plan_stat', 'int'),
    ('pc_ref_pl', 'int'),
    ('pc_ref_pl_stat', 'int'),
    ('plan_gen', 'int'),
    ('cli_query', 'int'),
    ('outline_exec', 'int'),
    ('plan_explain', 'int'),
    ('asyn_baseline', 'int'),
    ('load_baseline', 'int'),
    ('ps_exec', 'int'),
    ('gv_sql', 'int'),
    ('pl_anon', 'int'),
    ('pl_routine', 'int'),
    ('package_var', 'int'),
    ('package_type', 'int'),
    ('package_spec', 'int'),
    ('package_body', 'int'),
    ('package_resv', 'int'),
    ('get_pkg', 'int'),
    ('index_builder', 'int'),
    ('pcv_set', 'int'),
    ('pcv_rd', 'int'),
    ('pcv_wr', 'int'),
    ('pcv_get_plan_key', 'int'),
    ('pcv_get_pl_key', 'int'),
    ('pcv_expire_by_used', 'int'),
    ('pcv_expire_by_mem', 'int'),
    ('lc_ref_cache_node', 'int'),
    ('lc_node', 'int'),
    ('lc_node_rd', 'int'),
    ('lc_node_wr', 'int'),
    ('lc_ref_cache_obj_stat', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
    owner = 'xiaoyi.xy',
    table_name     = '__all_virtual_plan_stat',
    table_id       = '11004',
    table_type = 'VIRTUAL_TABLE',
    gm_columns = [],
    rowkey_columns = [
      ],
    enable_column_def_enum = True,
    in_tenant_space = True,

  normal_columns = [
      ('plan_id', 'int'),
      ('sql_id', 'varchar:OB_MAX_SQL_ID_LENGTH'),
      ('type', 'int'),
      ('is_bind_sensitive', 'int'),
      ('is_bind_aware', 'int'),
      ('statement', 'longtext'),
      ('query_sql', 'longtext'),
      ('special_params', 'varchar:OB_MAX_COMMAND_LENGTH'),
      ('param_infos', 'longtext'),
      ('sys_vars', 'varchar:OB_MAX_COMMAND_LENGTH'),
      ('configs', 'varchar:OB_MAX_COMMAND_LENGTH'),
      ('plan_hash', 'uint'),
      ('first_load_time', 'timestamp'),
      ('schema_version', 'int'),
      ('last_active_time', 'timestamp'),
      ('avg_exe_usec', 'int'),
      ('slowest_exe_time', 'timestamp'),
      ('slowest_exe_usec', 'int'),
      ('slow_count', 'int'),
      ('hit_count', 'int'),
      ('plan_size', 'int'),
      ('executions', 'int'),
      ('disk_reads', 'int'),
      ('direct_writes', 'int'),
      ('buffer_gets', 'int'),
      ('application_wait_time', 'uint'),
      ('concurrency_wait_time', 'uint'),
      ('user_io_wait_time', 'uint'),
      ('rows_processed', 'int'),
      ('elapsed_time', 'uint'),
      ('cpu_time', 'uint'),
      ('outline_version', 'int'),
      ('outline_id', 'int'),
      ('outline_data', 'longtext', 'false'),
      ('acs_sel_info', 'longtext', 'false'),
      ('table_scan', 'bool'),
      ('db_id', 'uint'),
      ('evolution', 'bool'),
      ('evo_executions', 'int'),
      ('evo_cpu_time', 'uint'),
      ('timeout_count', 'int'),
      ('ps_stmt_id', 'int'),
      ('delayed_px_querys', 'int'),
      ('sessid', 'uint'),
      ('temp_tables', 'longtext', 'false'),
      ('object_type', 'longtext', 'false'),
      ('enable_bf_cache', 'bool'),
      ('bf_filter_cnt', 'int'),
      ('bf_access_cnt', 'int'),
      ('enable_row_cache', 'bool'),
      ('row_cache_hit_cnt', 'int'),
      ('row_cache_miss_cnt', 'int'),
      ('enable_fuse_row_cache', 'bool'),
      ('fuse_row_cache_hit_cnt', 'int'),
      ('fuse_row_cache_miss_cnt', 'int'),
      ('hints_info', 'longtext', 'false'),
      ('hints_all_worked', 'bool'),
      ('pl_schema_id', 'uint'),
      ('is_batched_multi_stmt', 'bool'),
      ('object_status', 'int'),
      ('rule_name', 'varchar:256'),
      ('is_in_pc', 'bool'),
      ('erase_time', 'timestamp'),
      ('compile_time', 'uint'),
      ('pl_cg_mem_hold', 'int'),
      ('pl_evict_version', 'int'),
      ('plan_status', 'int'),
      ('adaptive_feedback_times', 'int'),
      ('first_get_plan_time', 'int'),
      ('first_exe_usec', 'int')
  ],
  vtable_route_policy = 'local',)

def_table_schema(
  owner = 'nijia.nj',
  table_name    = '__all_virtual_mem_leak_checker_info',
  table_id      = '11006',
  table_type = 'VIRTUAL_TABLE',
  gm_columns    = [],
  rowkey_columns = [
  ],

  normal_columns = [
  ('mod_name', 'varchar:OB_MAX_CHAR_LENGTH'),
  ('mod_type', 'varchar:OB_MAX_CHAR_LENGTH'),
  ('alloc_count', 'int'),
  ('alloc_size', 'int'),
  ('back_trace', 'varchar:DEFAULT_BUF_LENGTH')
  ],
  vtable_route_policy = 'local',)

# 11007: __all_virtual_latch # removed

def_table_schema(
  owner = 'zhaoruizhe.zrz',
  tablegroup_id = 'OB_INVALID_ID',
  table_name     = '__all_virtual_kvcache_info',
  table_id       = '11008',
  table_type = 'VIRTUAL_TABLE',
  in_tenant_space = True,
  gm_columns = [],
  rowkey_columns = [
  ],

  normal_columns = [
  ('cache_name', 'varchar:OB_MAX_KVCACHE_NAME_LENGTH', 'false'),
  ('cache_id', 'int', 'false'),
  ('cache_size', 'int', 'false'),
  ('kv_cnt', 'int', 'false'),
  ('hit_ratio', 'number:38:3', 'false'),
  ('total_put_cnt', 'int', 'false'),
  ('total_hit_cnt', 'int', 'false'),
  ('total_miss_cnt', 'int', 'false')
  ],
  vtable_route_policy = 'local',)

def_table_schema(
  owner = 'nijia.nj',
  table_name    = '__all_virtual_data_type_class',
  table_id      = '11009',
  table_type = 'VIRTUAL_TABLE',
  gm_columns    = [],
  rowkey_columns = [
  ('data_type_class', 'int')
  ],
  in_tenant_space = True,

  normal_columns = [
  ('data_type_class_str', 'varchar:OB_MAX_SYS_PARAM_NAME_LENGTH')
  ]
  )

def_table_schema(
  owner = 'nijia.nj',
  table_name    = '__all_virtual_data_type',
  table_id      = '11010',
  table_type = 'VIRTUAL_TABLE',
  gm_columns    = [],
  rowkey_columns = [
  ('data_type', 'int')
  ],
  in_tenant_space = True,

  normal_columns = [
  ('data_type_str', 'varchar:OB_MAX_SYS_PARAM_NAME_LENGTH'),
  ('data_type_class', 'int')
  ]
  )

# 11011: __all_virtual_server_stat # abandoned in 4.0.

# 11013: __all_virtual_session_event # removed

# 11014: __all_virtual_session_wait # removed


# 11015: __all_virtual_session_wait_history # removed

# 11017: __all_virtual_system_event # removed


def_table_schema(
  owner = 'jingyan.kfy',
  table_name     = '__all_virtual_memstore_info',
  table_id       = '11018',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [],
  in_tenant_space = True,

  normal_columns = [
  ('active_span', 'int'),
  ('freeze_trigger', 'int'),
  ('freeze_cnt', 'int'),
  ('memstore_used', 'int'),
  ('memstore_limit', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'fengshuo.fs',
  table_name     = '__all_virtual_concurrency_object_pool',
  table_id       = '11019',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
  ],

  normal_columns = [
  ('free_list_name', 'varchar:OB_MAX_SYS_PARAM_VALUE_LENGTH'),
  ('allocated', 'int'),
  ('in_use', 'int'),
  ('count', 'int'),
  ('type_size', 'int'),
  ('chunk_count', 'int'),
  ('chunk_byte_size', 'int')
  ],  vtable_route_policy = 'local'
  )

# 11020: __all_virtual_sesstat # removed



# 11021: __all_virtual_sysstat # removed

##11022:__all_virtual_storage_stat obsolated in 4.0

def_table_schema(
  owner = 'jiahua.cjh',
  tablegroup_id = 'OB_INVALID_ID',
  table_name     = '__all_virtual_disk_stat',
  table_id       = '11023',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ],

  normal_columns = [
  ('total_size', 'int', 'false'),
  ('used_size', 'int', 'false'),
  ('free_size', 'int', 'false'),
  ('is_disk_valid', 'int', 'false'),
  ('disk_error_begin_ts', 'int', 'false'),
  ('allocated_size', 'int', 'false')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'jingyan.kfy',
  table_name     = '__all_virtual_tablet_memstore_info',
  table_id       = '11024',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [],
  in_tenant_space = True,

  normal_columns = [
  ('tablet_id', 'int'),
  ('is_active', 'varchar:MAX_COLUMN_YES_NO_LENGTH'),
  ('start_scn', 'uint'),
  ('end_scn', 'uint'),
  ('logging_blocked', 'varchar:MAX_COLUMN_YES_NO_LENGTH'),
  ('freeze_clock', 'int'),
  ('unsubmitted_count', 'int'),
  ('max_end_scn', 'uint'),
  ('write_ref_count', 'int'),
  ('mem_used', 'int'),
  ('btree_item_count', 'int'),
  ('btree_mem_used', 'int'),
  ('insert_row_count', 'int'),
  ('update_row_count', 'int'),
  ('delete_row_count', 'int'),
  ('freeze_ts', 'int'),
  ('freeze_state', 'varchar:OB_MAX_CHAR_LENGTH'),
  ('freeze_time_dist', 'varchar:OB_MAX_CHAR_LENGTH'),
  ('compaction_info_list', 'varchar:OB_COMPACTION_INFO_LENGTH')
  ],  vtable_route_policy = 'local'
  )

# 11026: __all_virtual_upgrade_inspection (abandoned)

def_table_schema(
  owner = 'shanyan.g',
  table_name     = '__all_virtual_trans_stat',
  table_id       = '11027',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [],
  in_tenant_space = True,

  normal_columns = [
  ('trans_id', 'int'),
  ('session_id', 'int'),
  ('is_decided', 'bool'),
  ('write_state', 'varchar:1024'),
  ('ctx_create_time', 'timestamp', 'true'),
  ('expired_time', 'timestamp', 'true'),
  ('ref_cnt', 'int'),
  ('last_op_sn', 'int'),
  ('pending_write', 'int'),
  ('state', 'int'),
  ('part_trans_action', 'int'),
  ('trans_ctx_addr', 'varchar:20'),
  ('pending_log_size', 'int'),
  ('flushed_log_size', 'int'),
  ('is_exiting', 'int'),
  ('last_request_time', 'timestamp', 'true'),
  ('start_scn', 'uint'),
  ('end_scn', 'uint'),
  ('rec_scn', 'uint'),
  ('busy_cbs', 'int'),
  ('replay_complete', 'int'),
  ('serial_log_final_scn', 'int'),
  ('callback_list_stats', 'longtext')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'chensen.cs',
  table_name     = '__all_virtual_trans_ctx_mgr_stat',
  table_id       = '11028',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
  ],

  normal_columns = [
  ('is_stopped', 'int'),
  ('block_tx', 'int'),
  ('block_normal_tx', 'int'),
  ('block_all', 'int'),
  ('total_trans_ctx_count', 'int'),
  ('mgr_addr', 'bigint:20')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'wuyuefei.wyf',
  table_name     = '__all_virtual_trans_scheduler',
  table_id       = '11029',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [],
  in_tenant_space = True,

  normal_columns = [
  ('session_id', 'int'),
  ('trans_id', 'int'),
  ('state', 'int'),
  ('write_state', 'varchar:1024', 'true'),
  ('isolation_level', 'int'),
  ('snapshot_version', 'uint', 'true'),
  ('access_mode', 'int'),
  ('tx_op_sn', 'int'),
  ('flag', 'int'),
  ('active_time', 'timestamp', 'true'),
  ('expire_time', 'timestamp', 'true'),
  ('timeout_us', 'int'),
  ('ref_cnt', 'int'),
  ('tx_desc_addr', 'varchar:20'),
  ('savepoints', 'varchar:1024', 'true'),
  ('savepoints_total_cnt', 'int'),
  ('internal_abort_cause', 'int'),
  ('can_early_lock_release', 'bool')
  ],  vtable_route_policy = 'local'
  )

# 11031: __all_virtual_sql_audit # removed

# 11033: __all_virtual_partition_sstable_image_info # abandoned in 4.0

def_table_schema(**gen_iterate_core_inner_table_def(11035, '__all_virtual_core_all_table', 'VIRTUAL_TABLE', all_table_def))

def_table_schema(**gen_iterate_core_inner_table_def(11036, '__all_virtual_core_column_table', 'VIRTUAL_TABLE', all_column_def))

def_table_schema(
  owner = 'nijia.nj',
  table_name     = '__all_virtual_memory_info',
  table_id       = '11037',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  in_tenant_space = True,
  rowkey_columns = [
  ('ctx_id', 'int'),
  ('label', 'varchar:OB_MAX_CHAR_LENGTH')
  ],

  normal_columns = [
  ('ctx_name', 'varchar:OB_MAX_CHAR_LENGTH'),
  ('mod_type', 'varchar:OB_MAX_CHAR_LENGTH'),
  ('mod_id', 'int'),
  ('mod_name', 'varchar:OB_MAX_CHAR_LENGTH'),
  ('hold', 'int'),
  ('used', 'int'),
  ('count', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
    owner = 'fyy280124',
    table_name     = '__all_virtual_sys_parameter_stat',
    table_id       = '11039',
    table_type = 'VIRTUAL_TABLE',
    gm_columns = [],
    rowkey_columns = [
    ],

  normal_columns = [
      ('zone', 'varchar:MAX_ZONE_LENGTH'),
      ('svr_type', 'varchar:SERVER_TYPE_LENGTH'),
      ('name', 'varchar:OB_MAX_CONFIG_NAME_LEN'),
      ('data_type', 'varchar:OB_MAX_CONFIG_TYPE_LENGTH', 'true'),
      ('value', 'varchar:OB_MAX_CONFIG_VALUE_LEN'),
      ('value_strict', 'varchar:OB_MAX_EXTRA_CONFIG_LENGTH', 'true'),
      ('info', 'varchar:OB_MAX_CONFIG_INFO_LEN'),
      ('need_reboot', 'int'),
      ('section', 'varchar:OB_MAX_CONFIG_SECTION_LEN'),
      ('visible_level', 'varchar:OB_MAX_CONFIG_VISIBLE_LEVEL_LEN'),
      ('scope', 'varchar:OB_MAX_CONFIG_SCOPE_LEN'),
      ('source', 'varchar:OB_MAX_CONFIG_SOURCE_LEN'),
      ('edit_level', 'varchar:OB_MAX_CONFIG_EDIT_LEVEL_LEN'),
      ('default_value', 'varchar:OB_MAX_CONFIG_VALUE_LEN'),
      ('isdefault', 'int'),
  ],
  vtable_route_policy = 'local',
)

# 11040: __all_virtual_partition_replay_status # abandoned in 4.0

# 11041: __all_virtual_sys_parameter # migrated to SQLite
def_table_schema(**gen_sqlite_virtual_table_def(
  table_id = '11041',
  table_name = '__all_virtual_sys_parameter',
  keywords = all_def_keywords['__all_sys_parameter']))

def_table_schema(
  owner = 'nijia.nj',
  table_name     = '__all_virtual_engine',
  table_id       = '11043',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  normal_columns = [
  ('ENGINE', 'varchar:MAX_ENGINE_LENGTH'),
  ('SUPPORT', 'varchar:MAX_BOOL_STR_LENGTH'),
  ('COMMENT', 'varchar:MAX_COLUMN_COMMENT_LENGTH'),
  ('TRANSACTIONS', 'varchar:MAX_BOOL_STR_LENGTH'),
  ('SAVEPOINTS', 'varchar:MAX_BOOL_STR_LENGTH')
  ]
  )

# 11045: __all_virtual_proxy_server_stat (abandoned)

# 11046: __all_virtual_proxy_sys_variable (abandoned in seekdb)

# 11047: __all_virtual_proxy_schema (abandoned in seekdb)

def_table_schema(
  owner = 'xiaoyi.xy',
  table_name     = '__all_virtual_plan_cache_plan_explain',
  table_id       = '11048',
  table_type = 'VIRTUAL_TABLE',
  in_tenant_space = True,
  gm_columns = [],
  rowkey_columns = [
    ('plan_id', 'int'),
  ],

  normal_columns = [
    ('operator', 'varchar:OB_MAX_OPERATOR_NAME_LENGTH'),
    ('name', 'varchar:OB_MAX_PLAN_EXPLAIN_NAME_LENGTH'),
    ('rows', 'int'),
    ('cost', 'int'),
    ('property', 'varchar:OB_MAX_OPERATOR_PROPERTY_LENGTH'),
    ('plan_depth', 'int'),
    ('plan_line_id', 'int')
  ],  vtable_route_policy = 'local'
  )

# 11049: __all_virtual_obrpc_stat (abandoned)
# 11051: abandoned

def_table_schema(
  owner = 'xiaoyi.xy',
  table_name     = '__all_virtual_outline',
  table_id       = '11053',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ],
  in_tenant_space = True,

  normal_columns = [
      ('database_id', 'int'),
      ('outline_id', 'int'),
      ('database_name', 'varchar:OB_MAX_DATABASE_NAME_LENGTH', 'false', ''),
      ('outline_name', 'varchar:OB_MAX_OUTLINE_NAME_LENGTH', 'false', ''),
      ('visible_signature', 'longtext', 'false'),
      ('sql_text', 'longtext', 'false'),
      ('outline_target', 'longtext', 'false'),
      ('outline_sql', 'longtext', 'false'),
      ('sql_id', 'varchar:OB_MAX_SQL_ID_LENGTH', 'false', ''),
      ('outline_content', 'longtext', 'false'),
      ('format_sql_text', 'longtext', 'true'),
      ('format_sql_id', 'varbinary:OB_MAX_SQL_ID_LENGTH', 'false', ''),
      ('format_outline', 'int', 'false', '0')
    ]
  )

def_table_schema(
  owner = 'xiaoyi.xy',
  table_name     = '__all_virtual_concurrent_limit_sql',
  table_id       = '11054',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  normal_columns = [
      ('database_id', 'int'),
      ('outline_id', 'int'),
      ('database_name', 'varchar:OB_MAX_DATABASE_NAME_LENGTH', 'false', ''),
      ('outline_name', 'varchar:OB_MAX_OUTLINE_NAME_LENGTH', 'false', ''),
      ('outline_content', 'longtext', 'false'),
      ('visible_signature', 'longtext', 'false'),
      ('sql_text', 'longtext', 'false'),
      ('concurrent_num', 'int', 'false', '-1'),
      ('limit_target', 'longtext', 'false')
  ]
  )

# 11055: __all_virtual_sql_plan_statistics # abandoned in 4.0

def_table_schema(
  owner = 'jiahua.cjh',
  table_name     = '__all_virtual_tablet_sstable_macro_info',
  table_id       = '11056',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
      ('tablet_id', 'int'),
      ('end_log_scn', 'uint'),
      ('macro_idx_in_sstable', 'int')
  ],
    normal_columns = [
      ('macro_logic_version', 'uint'),
      ('macro_block_idx', 'int'),
      ('data_seq', 'int'),
      ('row_count', 'int'),
      ('original_size', 'int'),
      ('encoding_size', 'int'),
      ('compressed_size', 'int'),
      ('occupy_size', 'int'),
      ('micro_block_count', 'int'),
      ('data_checksum', 'int'),
      ('start_key', 'varchar:OB_MAX_ROW_KEY_LENGTH'),
      ('end_key', 'varchar:OB_MAX_ROW_KEY_LENGTH'),
      ('macro_block_type', 'varchar:MAX_VALUE_LENGTH'),
      ('compressor_name', 'varchar:OB_MAX_COMPRESSOR_NAME_LENGTH'),
      ('row_store_type', 'varchar:OB_MAX_COMPRESSOR_NAME_LENGTH')
  ],    vtable_route_policy = 'local'
  )

# 11057: __all_virtual_proxy_partition_info (abandoned in seekdb)

# 11058: __all_virtual_proxy_partition (abandoned in seekdb)

# 11059: __all_virtual_proxy_sub_partition (abandoned in seekdb)

# 11060: __all_virtual_proxy_route # abandoned in 4.0

# 11067: __all_virtual_election_event_history # abandoned in 4.0

# 11069: __all_virtual_leader_stat # abandoned in 4.0

# 11070: abandoned in 4.0


def_table_schema(
  owner = 'yongle.xh',
  table_name     = '__all_virtual_sys_task_status',
  table_id       = '11071',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
  ],

  normal_columns = [
  ('start_time', 'timestamp'),
  ('task_type', 'varchar:OB_SYS_TASK_TYPE_LENGTH'),
  ('task_id', 'varchar:OB_TRACE_STAT_BUFFER_SIZE'),
  ('comment', 'varchar:OB_MAX_TASK_COMMENT_LENGTH', 'false', ''),
  ('is_cancel', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'yongle.xh',
  table_name     = '__all_virtual_macro_block_marker_status',
  table_id       = '11072',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
  ],

  normal_columns = [
  ('total_count', 'int'),
  ('reserved_count', 'int'),
  ('meta_block_count', 'int'),
  ('shared_meta_block_count', 'int'),
  ('tmp_file_count', 'int'),
  ('data_block_count', 'int'),
  ('shared_data_block_count', 'int'),
  ('disk_block_count', 'int'),
  ('bloomfilter_count', 'int'),
  ('hold_count', 'int'),
  ('pending_free_count', 'int'),
  ('free_count', 'int'),
  ('mark_cost_time', 'int'),
  ('sweep_cost_time', 'int'),
  ('start_time', 'timestamp'),
  ('last_end_time', 'timestamp'),
  ('mark_finished', 'bool'),
  ('comment', 'varchar:MAX_TABLE_COMMENT_LENGTH', 'false', '')
  ],  vtable_route_policy = 'local'
  )

# 11074: __all_virtual_rootservice_stat # abandoned in 4.0.

# 11076: __all_virtual_tenant_disk_stat # abandoned in 4.0

def_table_schema(
  owner = 'chaser.ch',
  table_name     = '__all_virtual_io_stat',
  table_id       = '11080',
  table_type     = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
      ],

  normal_columns = [
      ('fd', 'int', 'false'),
      ('disk_type', 'varchar:OB_MAX_DISK_TYPE_LENGTH'),
      ('sys_io_up_limit_in_mb', 'int'),
      ('sys_io_bandwidth_in_mb', 'int'),
      ('sys_io_low_watermark_in_mb', 'int'),
      ('sys_io_high_watermark_in_mb', 'int'),
      ('io_bench_result', 'varchar:OB_MAX_IO_BENCH_RESULT_LENGTH')
  ],  vtable_route_policy = 'local'
  )


def_table_schema(
    owner = 'zhenjiang.xzj',
    table_name     = '__all_virtual_long_ops_status',
    table_id       = '11081',
    table_type     = 'VIRTUAL_TABLE',
    gm_columns     = [],
    rowkey_columns = [
      ],

    normal_columns = [
      ('sid', 'int'),
      ('op_name', 'varchar:MAX_LONG_OPS_NAME_LENGTH'),
      ('target', 'varchar:MAX_LONG_OPS_TARGET_LENGTH'),
      ('start_time', 'int'),
      ('finish_time', 'int'),
      ('elapsed_time', 'int'),
      ('remaining_time', 'int'),
      ('last_update_time', 'int'),
      ('percentage', 'int'),
      ('message', 'varchar:MAX_LONG_OPS_MESSAGE_LENGTH'),
      ('trace_id', 'varchar:OB_MAX_TRACE_ID_BUFFER_SIZE')
  ],
  vtable_route_policy = 'only_rs'
  )


def_table_schema(
  owner = 'nijia.nj',
  table_name     = '__all_virtual_server_object_pool',
  table_id       = '11084',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
      ('object_type', 'varchar:OB_MAX_SYS_PARAM_VALUE_LENGTH'),
      ('arena_id', 'int')
  ],

  normal_columns = [
      ('lock', 'int'),
      ('borrow_count', 'int'),
      ('return_count', 'int'),
      ('miss_count', 'int'),
      ('miss_return_count', 'int'),
      ('free_num', 'int'),
      ('last_borrow_ts', 'int'),
      ('last_return_ts', 'int'),
      ('last_miss_ts', 'int'),
      ('last_miss_return_ts', 'int'),
      ('next', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'shanyan.g',
  table_name     = '__all_virtual_trans_lock_stat',
  table_id       = '11085',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
  ],

  in_tenant_space = True,
  normal_columns = [
  ('trans_id', 'int'),
  ('tablet_id', 'int'),
  ('rowkey', 'varchar:512', 'true'),
  ('session_id', 'int'),
  ('ctx_create_time', 'timestamp', 'true'),
  ('expired_time', 'timestamp', 'true'),
  ('time_after_recv', 'int'),
  ('row_lock_addr', 'uint', 'true')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'yanmu.ztl',
  table_name     = '__all_virtual_show_create_tablegroup',
  table_id       = '11087',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ('tablegroup_id', 'int')
  ],
  in_tenant_space = True,

  normal_columns = [
  ('tablegroup_name', 'varchar:OB_MAX_TABLEGROUP_NAME_LENGTH'),
  ('create_tablegroup', 'longtext')
  ]
  )

# 11090: __all_virtual_trans_result_info_stat # abandoned in 4.0

# 11091: __all_virtual_duplicate_partition_mgr_stat # abandoned in 4.0

def_table_schema(
    owner = 'fyy280124',
    table_name     = '__all_virtual_parameter_stat',
    table_id       = '11092',
    table_type = 'VIRTUAL_TABLE',
    gm_columns = [],
    rowkey_columns = [
    ],
    in_tenant_space = True,
    enable_column_def_enum = True,

  normal_columns = [
      ('zone', 'varchar:MAX_ZONE_LENGTH'),
      ('svr_type', 'varchar:SERVER_TYPE_LENGTH'),
      ('name', 'varchar:OB_MAX_CONFIG_NAME_LEN'),
      ('data_type', 'varchar:OB_MAX_CONFIG_TYPE_LENGTH', 'true'),
      ('value', 'varchar:OB_MAX_CONFIG_VALUE_LEN'),
      ('value_strict', 'varchar:OB_MAX_EXTRA_CONFIG_LENGTH', 'true'),
      ('info', 'varchar:OB_MAX_CONFIG_INFO_LEN'),
      ('need_reboot', 'int'),
      ('section', 'varchar:OB_MAX_CONFIG_SECTION_LEN'),
      ('visible_level', 'varchar:OB_MAX_CONFIG_VISIBLE_LEVEL_LEN'),
      ('scope', 'varchar:OB_MAX_CONFIG_SCOPE_LEN'),
      ('source', 'varchar:OB_MAX_CONFIG_SOURCE_LEN'),
      ('edit_level', 'varchar:OB_MAX_CONFIG_EDIT_LEVEL_LEN'),
      ('default_value', 'varchar:OB_MAX_CONFIG_VALUE_LEN'),
      ('isdefault', 'int'),
  ],
  vtable_route_policy = 'local',
)

def_table_schema(
  owner = 'yanmu.ztl',
  table_name = '__all_virtual_server_schema_info',
  table_id = '11093',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ],

  in_tenant_space = True,
  normal_columns = [
    ("refreshed_schema_version", 'int'),
    ("received_schema_version", 'int'),
    ("schema_count", 'int'),
    ("schema_size", 'int'),
    ("min_sstable_schema_version", 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'nijia.nj',
  table_name     = '__all_virtual_memory_context_stat',
  table_id       = '11094',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ],

  normal_columns = [
    ('entity', 'varchar:128'),
    ('p_entity', 'varchar:128'),
    ('hold', 'bigint:20'),
    ('malloc_hold', 'bigint:20'),
    ('malloc_used', 'bigint:20'),
    ('arena_hold', 'bigint:20'),
    ('arena_used', 'bigint:20'),
    ('create_time', 'timestamp'),
    ('location', 'varchar:512')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'nijia.nj',
  table_name     = '__all_virtual_dump_info',
  table_id       = '11095',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ],

  normal_columns = [
    ('unit_min_cpu', 'double'),
    ('unit_max_cpu', 'double'),
    ('slice', 'double'),
    ('remain_slice', 'double'),
    ('token_cnt', 'bigint:20'),
    ('ass_token_cnt', 'bigint:20'),
    ('stopped', 'bigint:20'),
    ('idle_us', 'bigint:20'),
    ('recv_hp_rpc_cnt', 'bigint:20'),
    ('recv_np_rpc_cnt', 'bigint:20'),
    ('recv_lp_rpc_cnt', 'bigint:20'),
    ('recv_mysql_cnt', 'bigint:20'),
    ('recv_task_cnt', 'bigint:20'),
    ('actives', 'bigint:20'),
    ('workers', 'bigint:20'),
    ('req_queue_total_size', 'bigint:20'),
    ('queue_0', 'bigint:20'),
    ('queue_1', 'bigint:20'),
    ('queue_2', 'bigint:20'),
    ('queue_3', 'bigint:20'),
    ('queue_4', 'bigint:20'),
    ('queue_5', 'bigint:20')
  ],  vtable_route_policy = 'local'
  )

# 11096 abandoned in lite version

def_table_schema(
    owner = 'lixia.yq',
    table_name     = '__all_virtual_dag_warning_history',
    table_id       = '11099',
    table_type     = 'VIRTUAL_TABLE',
    gm_columns     = [],
    rowkey_columns = [],

    normal_columns = [
      ('task_id', 'varchar:OB_MAX_TRACE_ID_BUFFER_SIZE'),
      ('module', 'varchar:OB_MODULE_NAME_LENGTH'),
      ('type', 'varchar:OB_SYS_TASK_TYPE_LENGTH'),
      ('ret', 'varchar:OB_RET_STR_LENGTH'),
      ('status', 'varchar:OB_STATUS_STR_LENGTH'),
      ('gmt_create', 'timestamp'),
      ('gmt_modified', 'timestamp'),
      ('retry_cnt', 'int'),
      ('warning_info', 'varchar:OB_DAG_WARNING_INFO_LENGTH')
  ],    vtable_route_policy = 'local'
  )


# 11102: __tenant_virtual_show_restore_preview (removed: backup/restore/log-archive deleted)

def_table_schema(
    owner = 'lixia.yq',
    table_name     = '__all_virtual_dag',
    table_id       = '11105',
    table_type     = 'VIRTUAL_TABLE',
    gm_columns     = [],
    rowkey_columns = [],
    normal_columns = [
      ('dag_type', 'varchar:OB_SYS_TASK_TYPE_LENGTH'),
      ('dag_key', 'varchar:OB_DAG_KEY_LENGTH'),
      ('dag_net_key', 'varchar:OB_DAG_KEY_LENGTH'),
      ('dag_id', 'varchar:OB_MAX_TRACE_ID_BUFFER_SIZE'),
      ('status', 'varchar:OB_STATUS_STR_LENGTH'),
      ('running_task_cnt', 'int'),
      ('add_time', 'timestamp'),
      ('start_time', 'timestamp'),
      ('indegree', 'int'),
      ('comment', 'varchar:OB_DAG_COMMET_LENGTH')
  ],    vtable_route_policy = 'local'
  )

def_table_schema(
    owner = 'lixia.yq',
    table_name     = '__all_virtual_dag_scheduler',
    table_id       = '11106',
    table_type     = 'VIRTUAL_TABLE',
    gm_columns     = [],
    rowkey_columns = [],

    normal_columns = [
      ('value_type', 'varchar:OB_SYS_TASK_TYPE_LENGTH'),
      ('key', 'varchar:OB_DAG_KEY_LENGTH'),
      ('value', 'int')
  ],    vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'lixia.yq',
  table_name    = '__all_virtual_server_compaction_progress',
  table_id      = '11107',
  table_type = 'VIRTUAL_TABLE',
  in_tenant_space = True,
  gm_columns    = [],
  rowkey_columns = [],
  normal_columns = [
    ('type', 'varchar:OB_MERGE_TYPE_STR_LENGTH'),
    ('compaction_scn', 'uint'),
    ('status', 'varchar:OB_MERGE_STATUS_STR_LENGTH'),
    ('total_tablet_count', 'int'),
    ('unfinished_tablet_count', 'int'),
    ('data_size', 'int'),
    ('unfinished_data_size', 'int'),
    ('compression_ratio', 'double'),
    ('start_time', 'timestamp'),
    ('estimated_finish_time', 'timestamp'),
    ('comments', 'varchar:OB_COMPACTION_EVENT_STR_LENGTH')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'lixia.yq',
  table_name    = '__all_virtual_tablet_compaction_progress',
  table_id      = '11108',
  table_type = 'VIRTUAL_TABLE',
  in_tenant_space = True,
  gm_columns    = [],
  rowkey_columns = [],
  normal_columns = [
    ('type', 'varchar:OB_MERGE_TYPE_STR_LENGTH'),
    ('tablet_id', 'int'),
    ('compaction_scn', 'uint'),
    ('task_id', 'varchar:OB_MAX_TRACE_ID_BUFFER_SIZE'),
    ('status', 'varchar:OB_MERGE_STATUS_STR_LENGTH'),
    ('data_size', 'int'),
    ('unfinished_data_size', 'int'),
    ('progressive_compaction_round', 'int'),
    ('create_time', 'timestamp'),
    ('start_time', 'timestamp'),
    ('estimated_finish_time', 'timestamp')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'lixia.yq',
  table_name    = '__all_virtual_compaction_diagnose_info',
  table_id      = '11109',
  table_type = 'VIRTUAL_TABLE',
  in_tenant_space = True,
  gm_columns    = [],
  rowkey_columns = [],
  normal_columns = [
    ('type', 'varchar:OB_MERGE_TYPE_STR_LENGTH'),
    ('tablet_id', 'int'),
    ('status', 'varchar:OB_MERGE_STATUS_STR_LENGTH'),
    ('create_time', 'timestamp'),
    ('diagnose_info', 'varchar:OB_DIAGNOSE_INFO_LENGTH')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'lixia.yq',
  table_name    = '__all_virtual_compaction_suggestion',
  table_id      = '11110',
  table_type = 'VIRTUAL_TABLE',
  in_tenant_space = True,
  gm_columns    = [],
  rowkey_columns = [],
  normal_columns = [
    ('type', 'varchar:OB_MERGE_TYPE_STR_LENGTH'),
    ('tablet_id', 'int'),
    ('start_time', 'timestamp'),
    ('finish_time', 'timestamp'),
    ('suggestion', 'varchar:OB_DIAGNOSE_INFO_LENGTH')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'xiaochu.yh',
  table_name     = '__all_virtual_session_info',
  table_id       = '11111',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  normal_columns = [
  ('id', 'uint', 'false', '0'),
  ('user', 'varchar:OB_MAX_USERNAME_LENGTH', 'false', ''),
  ('tenant', 'varchar:OB_MAX_TENANT_NAME_LENGTH_STORE'),
  ('host', 'varchar:OB_MAX_HOST_NAME_LENGTH', 'false', ''),
  ('db', 'varchar:OB_MAX_DATABASE_NAME_LENGTH', 'true'),
  ('command', 'varchar:OB_MAX_COMMAND_LENGTH', 'false', ''),
  ('sql_id', 'varchar:OB_MAX_SQL_ID_LENGTH', 'false', ''),
  ('time', 'double', 'false'),
  ('state', 'varchar:OB_MAX_SESSION_STATE_LENGTH', 'true'),
  ('info', 'varchar:MAX_COLUMN_VARCHAR_LENGTH', 'true'),
  ('master_sessid', 'uint', 'true'),
  ('user_client_ip', 'varchar:MAX_IP_ADDR_LENGTH', 'true'),
  ('user_host', 'varchar:OB_MAX_HOST_NAME_LENGTH', 'true'),
  ('trans_id', 'uint'),
  ('thread_id', 'uint'),
  ('ssl_cipher', 'varchar:OB_MAX_COMMAND_LENGTH', 'true'),
  ('trace_id', 'varchar:OB_MAX_TRACE_ID_BUFFER_SIZE', 'true', ''),
  ('ref_count', 'int'),
  ('backtrace', 'varchar:16384', 'true', ''),
  ('trans_state', 'varchar:OB_MAX_TRANS_STATE_LENGTH', 'true'),
  ('user_client_port', 'int', 'false', '0'),
  ('total_cpu_time', 'double', 'false')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
    owner = 'lixia.yq',
    table_name     = '__all_virtual_tablet_compaction_history',
    table_id       = '11112',
    table_type     = 'VIRTUAL_TABLE',
    in_tenant_space = True,
    gm_columns     = [],
    rowkey_columns = [],

    normal_columns = [
      ('tablet_id', 'int'),
      ('type', 'varchar:OB_SYS_TASK_TYPE_LENGTH'),
      ('compaction_scn', 'uint'),
      ('start_time', 'timestamp'),
      ('finish_time', 'timestamp'),
      ('task_id', 'varchar:OB_MAX_TRACE_ID_BUFFER_SIZE'),
      ('occupy_size', 'int'),
      ('macro_block_count', 'int'),
      ('multiplexed_macro_block_count', 'int'),
      ('new_micro_count_in_new_macro', 'int'),
      ('multiplexed_micro_count_in_new_macro', 'int'),
      ('total_row_count', 'int'),
      ('incremental_row_count', 'int'),
      ('compression_ratio', 'double'),
      ('new_flush_data_rate', 'int'),
      ('progressive_compaction_round', 'int'),
      ('progressive_compaction_num', 'int'),
      ('parallel_degree', 'int'),
      ('parallel_info', 'varchar:OB_PARALLEL_MERGE_INFO_LENGTH'),
      ('participant_table', 'varchar:OB_PART_TABLE_INFO_LENGTH'),
      ('macro_id_list', 'varchar:OB_MACRO_ID_INFO_LENGTH'),
      ('comments', 'varchar:OB_COMPACTION_COMMENT_STR_LENGTH'),
      ('kept_snapshot', 'varchar:OB_COMPACTION_INFO_LENGTH'),
      ('merge_level', 'varchar:OB_MERGE_LEVEL_STR_LENGTH'),
      ('exec_mode', 'varchar:OB_MERGE_TYPE_STR_LENGTH'),
      ('is_full_merge', 'bool'),
      ('io_cost_time_percentage', 'int'),
      ('merge_reason', 'varchar:OB_MERGE_REASON_STR_LENGTH'),
      ('base_major_status', 'varchar:OB_MERGE_TYPE_STR_LENGTH'),
      ('mds_filter_info', 'varchar:OB_COMPACTION_COMMENT_STR_LENGTH'),
      ('execute_time', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
    owner             = 'jianyun.sjy',
    table_name        = '__all_virtual_io_calibration_status',
    table_id          = '11113',
    table_type        = 'VIRTUAL_TABLE',
    gm_columns        = [],
    rowkey_columns    = [],
    normal_columns    = [
      ('storage_name',  'varchar:1024'),
      ('status',        'varchar:256'),
      ('start_time',    'timestamp'),
      ('finish_time',   'timestamp')
  ],    vtable_route_policy = 'local'
  )

def_table_schema(
    owner             = 'jianyun.sjy',
    table_name        = '__all_virtual_io_benchmark',
    table_id          = '11114',
    table_type        = 'VIRTUAL_TABLE',
    gm_columns        = [],
    rowkey_columns    = [],
    normal_columns    = [
      ('storage_name',  'varchar:1024'),
      ('mode',          'varchar:256'),
      ('size',          'int'),
      ('iops',          'int'),
      ('mbps',          'int'),
      ('latency',       'int')
  ],    vtable_route_policy = 'local'
  )


def_table_schema(
    owner             = 'jianyun.sjy',
    table_name        = '__all_virtual_io_quota',
    table_id          = '11115',
    table_type        = 'VIRTUAL_TABLE',
    gm_columns        = [],
    rowkey_columns    = [],
    normal_columns    = [
      ('group_id',      'int'),
      ('mode',          'varchar:256'),
      ('size',          'int'),
      ('min_iops',      'int'),
      ('max_iops',      'int'),
      ('real_iops',     'int'),
      ('min_mbps',      'int'),
      ('max_mbps',      'int'),
      ('real_mbps',     'int'),
      ('schedule_us',   'int'),
      ('io_delay_us',   'int'),
      ('total_us',      'int')
    ],    vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'lixia.yq',
  table_name    = '__all_virtual_server_compaction_event_history',
  table_id      = '11116',
  table_type = 'VIRTUAL_TABLE',
  in_tenant_space = True,
  gm_columns    = [],
  rowkey_columns = [],
  normal_columns = [
    ('type', 'varchar:OB_MERGE_TYPE_STR_LENGTH'),
    ('compaction_scn', 'uint'),
    ('event_timestamp', 'timestamp'),
    ('event', 'varchar:OB_COMPACTION_EVENT_STR_LENGTH'),
    ('role', 'varchar:OB_MERGE_ROLE_STR_LENGTH')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
    owner = 'fengjingkun.fjk',
    table_name     = '__all_virtual_tablet_stat',
    table_id       = '11117',
    table_type     = 'VIRTUAL_TABLE',
    in_tenant_space = True,
    gm_columns     = [],
    rowkey_columns = [],
    normal_columns = [
      ('tablet_id', 'int'),
      ('query_cnt', 'int'),
      ('mini_merge_cnt', 'int'),
      ('scan_output_row_cnt', 'int'),
      ('scan_total_row_cnt', 'int'),
      ('pushdown_micro_block_cnt', 'int'),
      ('total_micro_block_cnt', 'int'),
      ('exist_iter_table_cnt', 'int'),
      ('exist_total_table_cnt', 'int'),
      ('insert_row_cnt', 'int'),
      ('update_row_cnt', 'int'),
      ('delete_row_cnt', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'jianyun.sjy',
  table_name = '__all_virtual_ddl_sim_point',
  table_id = '11118',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  normal_columns = [
    ('sim_point_id', 'int'),
    ('sim_point_name', 'varchar:1024'),
    ('sim_point_description', 'varchar:OB_MAX_CHAR_LENGTH'),
    ('sim_point_action', 'varchar:OB_MAX_CHAR_LENGTH')
  ],
  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'jianyun.sjy',
  table_name = '__all_virtual_ddl_sim_point_stat',
  table_id = '11119',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  normal_columns = [
    ('ddl_task_id', 'int'),
    ('sim_point_id', 'int'),
    ('trigger_count', 'int')
  ],  vtable_route_policy = 'local'
  )

# 11120: __all_virtual_res_mgr_sysstat # removed


# 11122: __all_virtual_ss_tablet_upload_stat
# 11123: __all_virtual_ss_tablet_compact_stat

################################################################
# INFORMATION SCHEMA
################################################################
def_table_schema(
  owner = 'xiaochu.yh',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'SESSION_VARIABLES',
  table_id       = '12001',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  normal_columns = [
  ('VARIABLE_NAME', 'varchar:OB_MAX_SYS_PARAM_NAME_LENGTH', 'false', ''),
  ('VARIABLE_VALUE', 'varchar:OB_MAX_SYS_PARAM_VALUE_LENGTH', 'true', 'NULL')
  ]
  )

# 12002: TABLE_PRIVILEGES # abandoned in 4.0
# 12003: USER_PRIVILEGES # abandoned in 4.0
# 12004: SCHEMA_PRIVILEGES # abandoned in 4.0
# 12005: TABLE_CONSTRAINTS # abandoned in 4.0

def_table_schema(
  owner = 'xiaochu.yh',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'GLOBAL_STATUS',
  table_id       = '12006',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  normal_columns = [
  ('VARIABLE_NAME', 'varchar:OB_MAX_SYS_PARAM_NAME_LENGTH', 'false', ''),
  ('VARIABLE_VALUE', 'varchar:OB_MAX_SYS_PARAM_VALUE_LENGTH', 'true', 'NULL')
  ]
  )

# 12007: PARTITIONS # abandoned in 4.0

def_table_schema(
  owner = 'xiaochu.yh',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'SESSION_STATUS',
  tablegroup_id = 'OB_INVALID_ID',
  table_id       = '12008',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  normal_columns = [
  ('VARIABLE_NAME', 'varchar:OB_MAX_SYS_PARAM_NAME_LENGTH', 'false', ''),
  ('VARIABLE_VALUE', 'varchar:OB_MAX_SYS_PARAM_VALUE_LENGTH', 'true', 'NULL')
  ]
  )

def_table_schema(
  owner = 'sean.yyj',
  database_id    = 'OB_MYSQL_SCHEMA_ID',
  table_name    = 'user',
  table_id      = '12009',
  table_type    = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ],
  in_tenant_space = True,

  normal_columns = [
  ('host', 'varchar:OB_MAX_HOST_NAME_LENGTH'),
  ('user', 'varchar:OB_MAX_USER_NAME_LENGTH_STORE'),
  ('password', 'varchar:OB_MAX_PASSWORD_LENGTH'),
  ('select_priv', 'varchar:1'),
  ('insert_priv', 'varchar:1'),
  ('update_priv', 'varchar:1'),
  ('delete_priv', 'varchar:1'),
  ('create_priv', 'varchar:1'),
  ('drop_priv', 'varchar:1'),
  ('reload_priv', 'varchar:1'),
  ('shutdown_priv', 'varchar:1'),
  ('process_priv', 'varchar:1'),
  ('file_priv', 'varchar:1'),
  ('grant_priv', 'varchar:1'),
  ('references_priv', 'varchar:1'),
  ('index_priv', 'varchar:1'),
  ('alter_priv', 'varchar:1'),
  ('show_db_priv', 'varchar:1'),
  ('super_priv', 'varchar:1'),
  ('create_tmp_table_priv', 'varchar:1'),
  ('lock_tables_priv', 'varchar:1'),
  ('execute_priv', 'varchar:1'),
  ('repl_slave_priv', 'varchar:1'),
  ('repl_client_priv', 'varchar:1'),
  ('create_view_priv', 'varchar:1'),
  ('show_view_priv', 'varchar:1'),
  ('create_routine_priv', 'varchar:1'),
  ('alter_routine_priv', 'varchar:1'),
  ('create_user_priv', 'varchar:1'),
  ('event_priv', 'varchar:1'),
  ('trigger_priv', 'varchar:1'),
  ('create_tablespace_priv', 'varchar:1'),
  ('ssl_type', 'varchar:10', 'false', ''),
  ('ssl_cipher', 'varchar:1024', 'false', ''),
  ('x509_issuer', 'varchar:1024', 'false', ''),
  ('x509_subject', 'varchar:1024', 'false', ''),
  ('max_questions', 'int', 'false', '0'),
  ('max_updates', 'int', 'false', '0'),
  ('max_connections', 'int', 'false', '0'),
  ('max_user_connections', 'int', 'false', '0'),
  ('plugin', 'varchar:1024'),
  ('authentication_string', 'varchar:1024'),
  ('password_expired', 'varchar:1'),
  ('account_locked', 'varchar:1'),
  ('drop_database_link_priv', 'varchar:1'),
  ('create_database_link_priv', 'varchar:1'),
  ('create_role_priv', 'varchar:1'),
  ('drop_role_priv', 'varchar:1')
  ]
  )

def_table_schema(
  owner = 'sean.yyj',
  database_id    = 'OB_MYSQL_SCHEMA_ID',
  table_name    = 'db',
  table_id      = '12010',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ],
  in_tenant_space = True,

  normal_columns = [
  ('host', 'varchar:OB_MAX_HOST_NAME_LENGTH'),
  ('db', 'varchar:OB_MAX_DATABASE_NAME_LENGTH'),
  ('user', 'varchar:OB_MAX_USER_NAME_LENGTH_STORE'),
  ('select_priv', 'varchar:1'),
  ('insert_priv', 'varchar:1'),
  ('update_priv', 'varchar:1'),
  ('delete_priv', 'varchar:1'),
  ('create_priv', 'varchar:1'),
  ('drop_priv', 'varchar:1'),
  ('grant_priv', 'varchar:1'),
  ('references_priv', 'varchar:1'),
  ('index_priv', 'varchar:1'),
  ('alter_priv', 'varchar:1'),
  ('create_tmp_table_priv', 'varchar:1'),
  ('lock_tables_priv', 'varchar:1'),
  ('create_view_priv', 'varchar:1'),
  ('show_view_priv', 'varchar:1'),
  ('create_routine_priv', 'varchar:1'),
  ('alter_routine_priv', 'varchar:1'),
  ('execute_priv', 'varchar:1'),
  ('event_priv', 'varchar:1'),
  ('trigger_priv', 'varchar:1')
  ]
  )

# 12012: __all_virtual_partition_table # abandoned in 4.0

def_table_schema(
  owner = 'shanyan.g',
  table_name = '__all_virtual_lock_wait_stat',
  table_id = '12013',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  in_tenant_space = True,
  rowkey_columns = [
  ],

  normal_columns = [
  ('tablet_id', 'int'),
  ('rowkey', 'varchar:MAX_LOCK_ROWKEY_BUF_LENGTH'),
  ('addr', 'uint'),
  ('need_wait', 'bool'),
  ('recv_ts', 'int'),
  ('lock_ts', 'int'),
  ('abs_timeout', 'int'),
  ('try_lock_times', 'int'),
  ('time_after_recv', 'int'),
  ('session_id', 'int'),
  ('block_session_id', 'int'),
  ('type', 'int'),
  ('lock_mode', 'varchar:MAX_LOCK_MODE_BUF_LENGTH'),
  ('last_compact_cnt', 'int'),
  ('total_update_cnt', 'int'),
  ('trans_id', 'int'),
  ('holder_trans_id', 'int'),
  ('holder_session_id', 'int')
  ],  vtable_route_policy = 'local'
  )

# 12014: __all_virtual_partition_item # abandoned in 4.0

# 12015: abandoned in 4.0
# 12016: __all_virtual_partition_location # abandoned in 4.0

# 12030: proc  # abandoned in 4.2.5.1, replaced by 21628



def_table_schema(
    owner = 'jim.wjh',
    table_name    = '__all_virtual_collation',
    table_id      = '12031',
    table_type = 'VIRTUAL_TABLE',
    gm_columns = [],
    in_tenant_space = True,
    rowkey_columns = [
    ],
    normal_columns = [
        ('collation_type', 'int', 'false', '0'),
        ('collation', 'varchar:MAX_COLLATION_LENGTH', 'false', ''),
        ('charset', 'varchar:MAX_CHARSET_LENGTH', 'false', ''),
        ('id', 'int', 'false', '0'),
        ('is_default', 'varchar:MAX_BOOL_STR_LENGTH', 'false', ''),
        ('is_compiled', 'varchar:MAX_BOOL_STR_LENGTH', 'false', ''),
        ('sortlen', 'int', 'false', '0')
  ]
  )

def_table_schema(
    owner = 'jim.wjh',
    table_name    = '__all_virtual_charset',
    table_id      = '12032',
    table_type = 'VIRTUAL_TABLE',
    in_tenant_space = True,
    gm_columns = [],
    rowkey_columns = [
    ],
    normal_columns = [
        ('charset', 'varchar:MAX_CHARSET_LENGTH', 'false', ''),
        ('description', 'varchar:MAX_CHARSET_DESCRIPTION_LENGTH', 'false', ''),
        ('default_collation', 'varchar:MAX_COLLATION_LENGTH', 'false', ''),
        ('max_length', 'int', 'false', '0')
  ]
  )

def_table_schema(
  owner = 'jingyan.kfy',
  table_name = '__all_virtual_memstore_allocator_info',
  table_id = '12033',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ],

  normal_columns = [
  ('tablet_id', 'int'),
  ('start_scn', 'uint'),
  ('end_scn', 'uint'),
  ('is_active', 'varchar:MAX_COLUMN_YES_NO_LENGTH'),
  ('retire_clock', 'int'),
  ('mt_protection_clock', 'int'),
  ('address', 'varchar:OB_MAX_POINTER_ADDR_LEN'),
  ('ref_count', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
    owner = 'baichangmin.bcm',
    table_name    = '__all_virtual_table_mgr',
    table_id      = '12034',
    table_type = 'VIRTUAL_TABLE',
    gm_columns = [],
    rowkey_columns = [
    ],
    in_tenant_space=True,
    normal_columns = [
      ('table_type', 'int'),
      ('tablet_id', 'int'),
      ('start_log_scn', 'uint'),
      ('end_log_scn', 'uint'),
      ('upper_trans_version', 'uint'),
      ('size', 'int'),
      ('data_block_count', 'int'),
      ('index_block_count', 'int'),
      ('linked_block_count', 'int'),
      ('ref', 'int'),
      ('is_active', 'varchar:MAX_COLUMN_YES_NO_LENGTH'),
      ('contain_uncommitted_row', 'varchar:MAX_COLUMN_YES_NO_LENGTH'),
      ('nested_offset', 'int'),
      ('nested_size', 'int'),
      ('data_checksum', 'int'),
      ('table_flag', 'int')
  ],  vtable_route_policy = 'local'
  )

# 12036: __all_virtual_freeze_info # removed (single-tenant: iterate VT mechanism deleted)

# 12037: PARAMETERS # abandoned in 4.0

def_table_schema(
  owner = 'jianyun.sjy',
  tablegroup_id = 'OB_INVALID_ID',
  table_name      = '__all_virtual_bad_block_table',
  table_id        = '12038',
  table_type      = 'VIRTUAL_TABLE',
  gm_columns      = [],

  rowkey_columns  = [
  ],

  normal_columns = [
  ('disk_id', 'int'),
  ('store_file_path', 'varchar:MAX_PATH_SIZE'),
  ('macro_block_index', 'int'),
  ('error_type', 'int'),
  ('error_msg', 'varchar:OB_MAX_ERROR_MSG_LEN'),
  ('check_time', 'timestamp')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'xiaochu.yh',
  table_name      = '__all_virtual_px_worker_stat',
  table_id        = '12039',
  table_type      = 'VIRTUAL_TABLE',
  gm_columns      = [],
  in_tenant_space = True,
  rowkey_columns  = [
  ],
  normal_columns = [
  ('session_id', 'int'),
  ('trace_id', 'varchar:OB_MAX_HOST_NAME_LENGTH'),
  ('qc_id', 'int'),
  ('sqc_id', 'int'),
  ('worker_id', 'int'),
  ('dfo_id', 'int'),
  ('start_time', 'timestamp'),
  ('thread_id', 'int')
  ],  vtable_route_policy = 'local'
  )

# 12042: __all_virtual_weak_read_stat # abandoned in 4.0

# 12054: __all_virtual_partition_audit # abandoned in 4.0

# 12055: __all_virtual_auto_increment # removed (single-tenant: iterate VT mechanism deleted)

# 12056: __all_virtual_sequence_value # removed (single-tenant: iterate VT mechanism deleted)

# 12057: __all_virtual_cluster # abandoned in 4.0

def_table_schema(
  owner = 'yht146493',
  table_name     = '__all_virtual_tablet_store_stat',
  table_id       = '12058',
  table_type     = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
    ],
  normal_columns = [
    ('table_id', 'int'),
    ('tablet_id', 'int'),
    ('row_cache_hit_count', 'int'),
    ('row_cache_miss_count', 'int'),
    ('row_cache_put_count', 'int'),
    ('bf_filter_count', 'int'),
    ('bf_empty_read_count', 'int'),
    ('bf_access_count', 'int'),
    ('block_cache_hit_count', 'int'),
    ('block_cache_miss_count', 'int'),
    ('access_row_count', 'int'),
    ('output_row_count', 'int'),
    ('fuse_row_cache_hit_count', 'int'),
    ('fuse_row_cache_miss_count', 'int'),
    ('fuse_row_cache_put_count', 'int'),
    ('single_get_call_count', 'int'),
    ('single_get_output_row_count', 'int'),
    ('multi_get_call_count', 'int'),
    ('multi_get_output_row_count', 'int'),
    ('index_back_call_count', 'int'),
    ('index_back_output_row_count', 'int'),
    ('single_scan_call_count', 'int'),
    ('single_scan_output_row_count', 'int'),
    ('multi_scan_call_count', 'int'),
    ('multi_scan_output_row_count', 'int'),
    ('exist_row_effect_read_count', 'int'),
    ('exist_row_empty_read_count', 'int'),
    ('get_row_effect_read_count', 'int'),
    ('get_row_empty_read_count', 'int'),
    ('scan_row_effect_read_count', 'int'),
    ('scan_row_empty_read_count', 'int'),
    ('macro_access_count', 'int'),
    ('micro_access_count', 'int'),
    ('pushdown_micro_access_count', 'int'),
    ('pushdown_row_access_count', 'int'),
    ('pushdown_row_select_count', 'int'),
    ('rowkey_prefix_access_info', 'varchar:COLUMN_DEFAULT_LENGTH'),
    ('index_block_cache_hit_count', 'int'),
    ('index_block_cache_miss_count', 'int'),
    ('logical_read_count', 'int'),
    ('physical_read_count', 'int')
  ],  vtable_route_policy = 'local'
  )

# Because of implementation problems, tenant schema's ddl operations can't be found in __all_virtual_ddl_operation.
# 12059: __all_virtual_ddl_operation # removed (single-tenant: iterate VT mechanism deleted)

# 12060: __all_virtual_outline # removed (single-tenant: iterate VT mechanism deleted)

# 12061: __all_virtual_outline_history # removed (single-tenant: iterate VT mechanism deleted)

# 12064: __all_virtual_database_privilege # removed (single-tenant: iterate VT mechanism deleted)

# 12065: __all_virtual_database_privilege_history # removed (single-tenant: iterate VT mechanism deleted)

# 12066: __all_virtual_table_privilege # removed (single-tenant: iterate VT mechanism deleted)

# 12067: __all_virtual_table_privilege_history # removed (single-tenant: iterate VT mechanism deleted)

# 12068: __all_virtual_database # removed (single-tenant: iterate VT mechanism deleted)

# 12069: __all_virtual_database_history # removed (single-tenant: iterate VT mechanism deleted)

# 12070: __all_virtual_tablegroup # removed (single-tenant: iterate VT mechanism deleted)

# 12071: __all_virtual_tablegroup_history # removed (single-tenant: iterate VT mechanism deleted)

# 12072: __all_virtual_table # removed (single-tenant: iterate VT mechanism deleted)

# 12073: __all_virtual_table_history # removed (single-tenant: iterate VT mechanism deleted)

# 12074: __all_virtual_column # removed (single-tenant: iterate VT mechanism deleted)

# 12075: __all_virtual_column_history # removed (single-tenant: iterate VT mechanism deleted)

# 12076: __all_virtual_part # removed (single-tenant: iterate VT mechanism deleted)

# 12077: __all_virtual_part_history # removed (single-tenant: iterate VT mechanism deleted)

# 12078: __all_virtual_part_info # removed (single-tenant: iterate VT mechanism deleted)

# 12079: __all_virtual_part_info_history # removed (single-tenant: iterate VT mechanism deleted)

# 12080: __all_virtual_def_sub_part # removed (single-tenant: iterate VT mechanism deleted)

# 12081: __all_virtual_def_sub_part_history # removed (single-tenant: iterate VT mechanism deleted)

# 12082: __all_virtual_sub_part # removed (single-tenant: iterate VT mechanism deleted)

# 12083: __all_virtual_sub_part_history # removed (single-tenant: iterate VT mechanism deleted)

# 12084: __all_virtual_constraint # removed (single-tenant: iterate VT mechanism deleted)

# 12085: __all_virtual_constraint_history # removed (single-tenant: iterate VT mechanism deleted)

# 12086: __all_virtual_foreign_key # removed (single-tenant: iterate VT mechanism deleted)

# 12087: __all_virtual_foreign_key_history # removed (single-tenant: iterate VT mechanism deleted)

# 12088: __all_virtual_foreign_key_column # removed (single-tenant: iterate VT mechanism deleted)

# 12089: __all_virtual_foreign_key_column_history # removed (single-tenant: iterate VT mechanism deleted)

# 12090: __all_virtual_temp_table # removed (single-tenant: iterate VT mechanism deleted)

# 12091: __all_virtual_ori_schema_version # removed (single-tenant: iterate VT mechanism deleted)

# 12092: __all_virtual_sys_stat # removed (single-tenant: iterate VT mechanism deleted)

# 12093: __all_virtual_user # removed (single-tenant: iterate VT mechanism deleted)

# 12094: __all_virtual_user_history # removed (single-tenant: iterate VT mechanism deleted)

# 12095: __all_virtual_sys_variable # removed (single-tenant: iterate VT mechanism deleted)

# 12096: __all_virtual_sys_variable_history # removed (single-tenant: iterate VT mechanism deleted)

# 12097: __all_virtual_func # removed (single-tenant: iterate VT mechanism deleted)

# 12098: __all_virtual_func_history # removed (single-tenant: iterate VT mechanism deleted)

# 12099: __all_virtual_package # removed (single-tenant: iterate VT mechanism deleted)

# 12100: __all_virtual_package_history # removed (single-tenant: iterate VT mechanism deleted)

# 12101: __all_virtual_routine # removed (single-tenant: iterate VT mechanism deleted)

# 12102: __all_virtual_routine_history # removed (single-tenant: iterate VT mechanism deleted)

# 12103: __all_virtual_routine_param # removed (single-tenant: iterate VT mechanism deleted)

# 12104: __all_virtual_routine_param_history # removed (single-tenant: iterate VT mechanism deleted)

# 12115: reserved for removed recyclebin virtual table

# 12116: __all_virtual_tenant_gc_partition_info # abandoned in 4.0

# 12117: __all_virtual_tenant_plan_baseline # abandoned in 4.0
# 12118: __all_virtual_tenant_plan_baseline_history # abandoned in 4.0

# 12119: __all_virtual_sequence_object # removed (single-tenant: iterate VT mechanism deleted)

# 12120: __all_virtual_sequence_object_history # removed (single-tenant: iterate VT mechanism deleted)

def_table_schema(
    owner = 'yongle.xh',
    table_name    = '__all_virtual_raid_stat',
    table_id      = '12121',
    table_type = 'VIRTUAL_TABLE',
    gm_columns = [],
    rowkey_columns = [
    ],
    normal_columns = [
      ('disk_idx', 'int'),
      ('install_seq', 'int'),
      ('data_num', 'int'),
      ('parity_num', 'int'),
      ('create_ts', 'int'),
      ('finish_ts', 'int'),
      ('alias_name', 'varchar:MAX_PATH_SIZE'),
      ('status', 'varchar:OB_STATUS_LENGTH'),
      ('percent', 'int')
  ],  vtable_route_policy = 'local'
  )

# 12122: __all_virtual_server_log_meta # abandoned in 4.0

# start for DTL
def_table_schema(
    owner = 'longzhong.wlz',
    table_name    = '__all_virtual_dtl_channel',
    table_id      = '12123',
    table_type = 'VIRTUAL_TABLE',
    gm_columns = [],
    rowkey_columns = [
    ],
    normal_columns = [
      ('channel_id', 'int'),
      ('op_id', 'int'),
      ('peer_id', 'int'),
      ('is_local', 'bool'),
      ('is_data', 'bool'),
      ('is_transmit', 'bool'),
      ('alloc_buffer_cnt', 'int'),
      ('free_buffer_cnt', 'int'),
      ('send_buffer_cnt', 'int'),
      ('recv_buffer_cnt', 'int'),
      ('processed_buffer_cnt', 'int'),
      ('send_buffer_size', 'int'),
      ('hash_val', 'int'),
      ('buffer_pool_id', 'int'),
      ('pins', 'int'),
      ('first_in_ts', 'timestamp'),
      ('first_out_ts', 'timestamp'),
      ('last_in_ts', 'timestamp'),
      ('last_out_ts', 'timestamp'),
      ('status', 'int'),
      ('thread_id', 'int'),
      ('owner_mod', 'int'),
      ('peer_ip', 'varchar:MAX_IP_ADDR_LENGTH'),
      ('peer_port', 'int'),
      ('eof', 'bool')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
    owner = 'longzhong.wlz',
    table_name    = '__all_virtual_dtl_memory',
    table_id      = '12124',
    table_type = 'VIRTUAL_TABLE',
    gm_columns = [],
    rowkey_columns = [
    ],
    normal_columns = [
      ('channel_total_cnt', 'int'),
      ('channel_block_cnt', 'int'),
      ('max_parallel_cnt', 'int'),
      ('max_blocked_buffer_size', 'int'),
      ('accumulated_blocked_cnt', 'int'),
      ('current_buffer_used', 'int'),
      ('seqno', 'int'),
      ('alloc_cnt', 'int'),
      ('free_cnt', 'int'),
      ('free_queue_len', 'int'),
      ('total_memory_size', 'int'),
      ('real_alloc_cnt', 'int'),
      ('real_free_cnt', 'int')
  ],  vtable_route_policy = 'local'
  )

# 12125: abandoned

# 12129: __all_virtual_tenant_role_grantee_map # removed (single-tenant: iterate VT mechanism deleted)

# 12130: __all_virtual_tenant_role_grantee_map_history # removed (single-tenant: iterate VT mechanism deleted)

# 12141: __all_virtual_deadlock_stat # abandoned in 4.0

def_table_schema(
  owner = 'bin.lb',
  table_name     = '__ALL_VIRTUAL_INFORMATION_COLUMNS',
  table_id       = '12144',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  in_tenant_space = True,
  rowkey_columns = [
  ('TABLE_SCHEMA', 'varchar:OB_MAX_DATABASE_NAME_LENGTH'),
  ('TABLE_NAME', 'varchar:OB_MAX_TABLE_NAME_LENGTH')
  ],

  normal_columns = [
  ('TABLE_CATALOG', 'varchar:MAX_TABLE_CATALOG_LENGTH', 'false', ''),
  ('COLUMN_NAME', 'varchar:OB_MAX_COLUMN_NAME_LENGTH', 'false', ''),
  ('ORDINAL_POSITION', 'uint', 'false', '0'),
  ('COLUMN_DEFAULT', 'longtext', 'true'),
  ('IS_NULLABLE', 'varchar:COLUMN_NULLABLE_LENGTH',  'false', ''),
  ('DATA_TYPE', 'longtext',  'false', ''),
  ('CHARACTER_MAXIMUM_LENGTH', 'uint', 'true'),
  ('CHARACTER_OCTET_LENGTH', 'uint', 'true'),
  ('NUMERIC_PRECISION', 'uint', 'true'),
  ('NUMERIC_SCALE','uint', 'true'),
  ('DATETIME_PRECISION', 'uint', 'true'),
  ('CHARACTER_SET_NAME', 'varchar:MAX_CHARSET_LENGTH', 'true'),
  ('COLLATION_NAME', 'varchar:MAX_COLLATION_LENGTH', 'true'),
  ('COLUMN_TYPE', 'longtext'),
  ('COLUMN_KEY', 'varchar:MAX_COLUMN_KEY_LENGTH', 'false', ''),
  ('EXTRA', 'varchar:COLUMN_EXTRA_LENGTH', 'false', ''),
  ('PRIVILEGES', 'varchar:MAX_COLUMN_PRIVILEGE_LENGTH', 'false', ''),
  ('COLUMN_COMMENT', 'longtext', 'false', ''),
  ('GENERATION_EXPRESSION', 'longtext', 'false', ''),
  ('SRS_ID', 'uint32', 'true')
  ]
  )

# 12146: __all_virtual_tenant_user_failed_login_stat # removed (single-tenant: iterate VT mechanism deleted)

# 12151: __all_virtual_trigger # removed (single-tenant: iterate VT mechanism deleted)

# 12152: __all_virtual_trigger_history # removed (single-tenant: iterate VT mechanism deleted)

# 12153: __all_virtual_cluster_stats # abandoned in 4.0

# 12154: __all_tenant_sstable_column_checksum # abandoned in 4.0

def_table_schema(
  owner = 'xiaoyi.xy',
  table_name     = '__all_virtual_ps_stat',
  table_id       = '12155',
  table_type = 'VIRTUAL_TABLE',
  in_tenant_space = True,
  gm_columns = [],
  rowkey_columns = [],
  enable_column_def_enum = True,

  normal_columns = [
    ('stmt_count', 'int'),
    ('hit_count', 'int'),
    ('access_count', 'int'),
    ('mem_hold', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'xiaoyi.xy',
  table_name     = '__all_virtual_ps_item_info',
  table_id       = '12156',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  enable_column_def_enum = True,
  in_tenant_space = True,

  normal_columns = [
    ('stmt_id', 'int'),
    ('db_id', 'int'),
    ('ps_sql', 'longtext'),
    ('param_count', 'int'),
    ('stmt_item_ref_count', 'int'),
    ('stmt_info_ref_count', 'int'),
    ('mem_hold', 'int'),
    ('stmt_type', 'int'),
    ('checksum', 'int'),
    ('expired', 'bool')
  ],  vtable_route_policy = 'local'
  )

# 12157: __all_virtual_standby_status # abandoned in 4.0

def_table_schema(
  owner = 'longzhong.wlz',
  table_name     = '__all_virtual_sql_workarea_history_stat',
  table_id       = '12158',
  table_type     = 'VIRTUAL_TABLE',
  in_tenant_space = True,
  gm_columns     = [],
  rowkey_columns = [],
  normal_columns = [
      ('plan_id', 'int'),
      ('sql_id', 'varchar:OB_MAX_SQL_ID_LENGTH'),
      ('operation_type', 'varchar:40'),
      ('operation_id', 'int'),
      ('estimated_optimal_size', 'int'),
      ('estimated_onepass_size', 'int'),
      ('last_memory_used', 'int'),
      ('last_execution', 'varchar:10'),
      ('last_degree', 'int'),
      ('total_executions', 'int'),
      ('optimal_executions', 'int'),
      ('onepass_executions', 'int'),
      ('multipasses_executions', 'int'),
      ('active_time', 'int'),
      ('max_tempseg_size', 'int'),
      ('last_tempseg_size', 'int'),
      ('policy', 'varchar:10'),
      ('db_id', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'longzhong.wlz',
  table_name     = '__all_virtual_sql_workarea_active',
  table_id       = '12159',
  table_type     = 'VIRTUAL_TABLE',
  in_tenant_space = True,
  gm_columns     = [],
  rowkey_columns = [],
  normal_columns = [
      ('plan_id', 'int'),
      ('sql_id', 'varchar:OB_MAX_SQL_ID_LENGTH'),
      ('sql_exec_id', 'int'),
      ('operation_type', 'varchar:40'),
      ('operation_id', 'int'),
      ('sid', 'int'),
      ('active_time', 'int'),
      ('work_area_size', 'int'),
      ('expect_size', 'int'),
      ('actual_mem_used', 'int'),
      ('max_mem_used', 'int'),
      ('number_passes', 'int'),
      ('tempseg_size', 'int'),
      ('policy', 'varchar:6'),
      ('db_id', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'longzhong.wlz',
  table_name     = '__all_virtual_sql_workarea_histogram',
  table_id       = '12160',
  table_type     = 'VIRTUAL_TABLE',
  in_tenant_space = True,
  gm_columns     = [],
  rowkey_columns = [],
  normal_columns = [
      ('low_optimal_size', 'int'),
      ('high_optimal_size', 'int'),
      ('optimal_executions', 'int'),
      ('onepass_executions', 'int'),
      ('multipasses_executions', 'int'),
      ('total_executions', 'int'),
  ],
  vtable_route_policy = 'local',
)

def_table_schema(
  owner = 'longzhong.wlz',
  table_name     = '__all_virtual_sql_workarea_memory_info',
  in_tenant_space = True,
  table_id       = '12161',
  table_type     = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [],
  normal_columns = [
      ('max_workarea_size', 'int'),
      ('workarea_hold_size', 'int'),
      ('max_auto_workarea_size', 'int'),
      ('mem_target', 'int'),
      ('total_mem_used', 'int'),
      ('global_mem_bound', 'int'),
      ('drift_size', 'int'),
      ('workarea_count', 'int'),
      ('manual_calc_count', 'int'),
  ],
  vtable_route_policy = 'local',
)

# 12163: __all_virtual_sysauth # removed (single-tenant: iterate VT mechanism deleted)

# 12164: __all_virtual_sysauth_history # removed (single-tenant: iterate VT mechanism deleted)

# 12165: __all_virtual_objauth # removed (single-tenant: iterate VT mechanism deleted)

# 12166: __all_virtual_objauth_history # removed (single-tenant: iterate VT mechanism deleted)

# 12167: __all_virtual_backup_info # abandoned

# 12168: __all_virtual_backup_log_archive_status # abandoned in 4.0
# 12170: __all_virtual_backup_task # abandoned in 4.0
# 12171: __all_virtual_pg_backup_task # abandoned in 4.0

# 12173: __all_virtual_pg_backup_log_archive_status # abandoned in 4.0
# 12174: __all_virtual_server_backup_log_archive_status # abandoned in 4.0

# 12175: __all_virtual_error # removed (single-tenant: iterate VT mechanism deleted)

def_table_schema(
  owner = 'lixinze.lxz',
  table_name     = '__all_virtual_id_service',
  table_id       = '12176',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
  ],

  normal_columns = [
    ('id_service_type', 'int'),
    ('last_id', 'int'),
    ('limit_id', 'int'),
    ('rec_log_scn', 'uint'),
    ('latest_log_scn', 'uint'),
    ('pre_allocated_range', 'int'),
    ('submit_log_ts', 'int')
  ],  vtable_route_policy = 'local'
  )

# 12177: REFERENTIAL_CONSTRAINTS # abandoned in 4.0
# 12179: __all_virtual_table_modifications # abandoned in 4.0
# 12180: __all_virtual_backup_clean_info # abandoned in 4.0

# 12184: __all_virtual_pg_log_archive_stat # abandoned in 4.0

def_table_schema(
  owner = 'adou.ly',
  table_name    = '__all_virtual_open_cursor',
  table_id      = '12187',
  table_type = 'VIRTUAL_TABLE',
  in_tenant_space = True,
  gm_columns    = [],
  rowkey_columns = [
  ],
  normal_columns = [
    ('SADDR', 'varchar:8'),
    ('SID', 'int'),
    ('USER_NAME', 'varchar:30'),
    ('ADDRESS', 'varchar:8'),
    ('HASH_VALUE', 'int'),
    ('SQL_ID', 'varchar:OB_MAX_SQL_ID_LENGTH'),
    ('SQL_TEXT', 'varchar:60'),
    ('LAST_SQL_ACTIVE_TIME', 'timestamp'),
    ('SQL_EXEC_ID', 'int'),
    ('CURSOR_TYPE', 'varchar:30'),
    ('CHILD_ADDRESS', 'varchar:30'),
    ('CON_ID', 'int')
  ],  vtable_route_policy = 'local'
  )

# 12188: __all_virtual_backup_validation_task # abandoned in 4.0
# 12189: __all_virtual_pg_backup_validation_task # abandoned in 4.0

# 12190: __all_virtual_time_zone # removed (single-tenant: iterate VT mechanism deleted)

# 12191: __all_virtual_time_zone_name # removed (single-tenant: iterate VT mechanism deleted)

# 12192: __all_virtual_time_zone_transition # removed (single-tenant: iterate VT mechanism deleted)

# 12193: __all_virtual_time_zone_transition_type # removed (single-tenant: iterate VT mechanism deleted)

# 12194: __all_virtual_constraint_column # removed (single-tenant: iterate VT mechanism deleted)

# 12195: __all_virtual_constraint_column_history # removed (single-tenant: iterate VT mechanism deleted)


def_table_schema(
  owner = 'xiaochu.yh',
  table_name     = '__all_virtual_files',
  table_id       = '12196',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,
  normal_columns = [
    ('FILE_ID','bigint:4','false','0'),
    ('FILE_NAME','varchar:64','true','NULL'),
    ('FILE_TYPE','varchar:20','false',''),
    ('TABLESPACE_NAME','varchar:64','true','NULL'),
    ('TABLE_CATALOG','varchar:64','false',''),
    ('TABLE_SCHEMA','varchar:64','true','NULL'),
    ('TABLE_NAME','varchar:64','true','NULL'),
    ('LOGFILE_GROUP_NAME','varchar:64','true','NULL'),
    ('LOGFILE_GROUP_NUMBER','bigint:4','true','NULL'),
    ('ENGINE','varchar:64','false',''),
    ('FULLTEXT_KEYS','varchar:64','true','NULL'),
    ('DELETED_ROWS','bigint:4','true','NULL'),
    ('UPDATE_COUNT','bigint:4','true','NULL'),
    ('FREE_EXTENTS','bigint:4','true','NULL'),
    ('TOTAL_EXTENTS','bigint:4','true','NULL'),
    ('EXTENT_SIZE','bigint:4','false','0'),
    ('INITIAL_SIZE','uint','true','NULL'),
    ('MAXIMUM_SIZE','uint','true','NULL'),
    ('AUTOEXTEND_SIZE','uint','true','NULL'),
    ('CREATION_TIME','timestamp','true','NULL'),
    ('LAST_UPDATE_TIME','timestamp','true','NULL'),
    ('LAST_ACCESS_TIME','timestamp','true','NULL'),
    ('RECOVER_TIME','bigint:4','true','NULL'),
    ('TRANSACTION_COUNTER','bigint:4','true','NULL'),
    ('VERSION','uint','true','NULL'),
    ('ROW_FORMAT','varchar:10','true','NULL'),
    ('TABLE_ROWS','uint','true','NULL'),
    ('AVG_ROW_LENGTH','uint','true','NULL'),
    ('DATA_LENGTH','uint','true','NULL'),
    ('MAX_DATA_LENGTH','uint','true','NULL'),
    ('INDEX_LENGTH','uint','true','NULL'),
    ('DATA_FREE','uint','true','NULL'),
    ('CREATE_TIME','timestamp','true','NULL'),
    ('UPDATE_TIME','timestamp','true','NULL'),
    ('CHECK_TIME','timestamp','true','NULL'),
    ('CHECKSUM','uint','true','NULL'),
    ('STATUS','varchar:20','false',''),
    ('EXTRA','varchar:255','true','NULL')
  ]
  )

# 12197: abandoned # INFORMATION_SCHEMA.FILES, which is moved to 21157

# 12198: __all_virtual_dependency # removed (single-tenant: iterate VT mechanism deleted)

def_table_schema(
  owner = 'dachuan.sdc',
  table_name     = '__all_virtual_object_definition',
  table_id       = '12199',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ('object_type', 'int'),
  ('object_name', 'varchar:OB_MAX_ORIGINAL_NANE_LENGTH'),
  ('schema', 'varchar:OB_MAX_DATABASE_NAME_LENGTH'),
  ('version', 'varchar:10'),
  ('model', 'varchar:OB_MAX_DATABASE_NAME_LENGTH'),
  ('transform', 'varchar:8')
  ],
  in_tenant_space = True,

  normal_columns = [
  ('definition', 'longtext'),
  ('create_database_with_if_not_exists', 'varchar:DATABASE_DEFINE_LENGTH'),
  ('character_set_client', 'varchar:MAX_CHARSET_LENGTH'),
  ('collation_connection', 'varchar:MAX_CHARSET_LENGTH'),
  ('proc_type', 'int'),
  ('collation_database', 'varchar:MAX_CHARSET_LENGTH'),
  ('sql_mode', 'varchar:MAX_CHARSET_LENGTH')
  ]
  )

# 12200: __all_virtual_reserved_table_mgr # abandoned in 4.0
# 12201: __all_virtual_backupset_history_mgr # abandoned in 4.0
# 12202: __all_virtual_backup_backupset_task # abandoned in 4.0
# 12203: __all_virtual_pg_backup_backupset_task # abandoned in 4.0

# 12205: __all_virtual_cluster_failover_info # abandoned in 4.0
# 12207: __all_virtual_all_clusters # abandoned in 4.0

# 12208: __all_virtual_ddl_task_status # removed (single-tenant: iterate VT mechanism deleted)

# __all_virtual_deadlock_event_history: SQLite virtual table (migrated from iterate)
def_table_schema(**gen_sqlite_virtual_table_def(
  table_id = '12209',
  table_name = '__all_virtual_deadlock_event_history',
  keywords = all_def_keywords['__all_deadlock_event_history']))

# 12210: __all_virtual_column_usage # removed (single-tenant: iterate VT mechanism deleted)


def_table_schema(
  owner = 'fengshuo.fs',
  table_name     = '__all_virtual_ctx_memory_info',
  table_id       = '12211',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
  ('ctx_id', 'int'),
  ],

  normal_columns = [
  ('ctx_name', 'varchar:OB_MAX_CHAR_LENGTH'),
  ('hold', 'int'),
  ('used', 'int'),
  ('limit', 'int')
  ],  vtable_route_policy = 'local'
  )

# 12212: __all_virtual_clog_agency_info # abandoned in 4.0

# 12213: __all_virtual_job # removed (single-tenant: iterate VT mechanism deleted)

# 12214: __all_virtual_job_log # removed (single-tenant: iterate VT mechanism deleted)

# 12215: __all_virtual_tenant_directory # removed (single-tenant: iterate VT mechanism deleted)
# 12216: __all_virtual_tenant_directory_history # removed (single-tenant: iterate VT mechanism deleted)

# 12217: __all_virtual_table_stat # removed (single-tenant: iterate VT mechanism deleted)

# 12218: __all_virtual_column_stat # removed (single-tenant: iterate VT mechanism deleted)

# 12219: __all_virtual_histogram_stat # removed (single-tenant: iterate VT mechanism deleted)

# 12220: __all_virtual_tenant_memory_info # removed (tenant-name scrub)

# 12221: TRIGGERS # abandoned in 4.0

def_table_schema(
  owner = 'webber.wb',
  table_name     = '__all_virtual_show_create_trigger',
  table_id       = '12222',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ('trigger_id', 'int')
  ],
  in_tenant_space = True,

  normal_columns = [
  ('trigger_name', 'varchar:OB_MAX_ROUTINE_NAME_LENGTH'),
  ('sql_mode', 'varchar:MAX_CHARSET_LENGTH'),
  ('create_trigger', 'longtext'),
  ('character_set_client', 'varchar:MAX_CHARSET_LENGTH'),
  ('collation_connection', 'varchar:MAX_CHARSET_LENGTH'),
  ('collation_database', 'varchar:MAX_CHARSET_LENGTH')
  ]
  )

def_table_schema(
  owner = 'xiaochu.yh',
  table_name = '__all_virtual_px_target_monitor',
  table_id = '12223',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ],

  in_tenant_space = True,

  normal_columns = [
  ('is_leader', 'bool'),
  ('version','uint'),
  ('peer_ip', 'varchar:MAX_IP_ADDR_LENGTH'),
  ('peer_port', 'int'),
  ('peer_target', 'int'),
  ('peer_target_used', 'int'),
  ('local_target_used', 'int'),
  ('local_parallel_session_count', 'int')
  ],  vtable_route_policy = 'local'
  )

# 12224: __all_virtual_monitor_modified # removed (single-tenant: iterate VT mechanism deleted)

# 12225: __all_virtual_table_stat_history # removed (single-tenant: iterate VT mechanism deleted)

# 12226: __all_virtual_column_stat_history # removed (single-tenant: iterate VT mechanism deleted)

# 12227: __all_virtual_histogram_stat_history # removed (single-tenant: iterate VT mechanism deleted)

# 12228: __all_virtual_optstat_global_prefs # removed (single-tenant: iterate VT mechanism deleted)

# 12229: __all_virtual_optstat_user_prefs # removed (single-tenant: iterate VT mechanism deleted)

# 12231: __all_virtual_log_archive_progress # abandoned
# 12232: __all_virtual_log_archive_history # abandoned
# 12233: __all_virtual_log_archive_piece_files # abandoned
# 12234: __all_virtual_ls_log_archive_progress # abandoned

# 12235: CHECK_CONSTRAINTS # abandoned in 4.0
# 12236: __all_virtual_backup_storage_info # abandoned

# 12237: __all_virtual_ls_status (abandoned)
# 12238: __all_virtual_ls (abandoned)

# 12239: __all_virtual_ls_meta_table (abandoned)

# 12240: __all_virtual_tablet_meta_table # migrated to SQLite
def_table_schema(**gen_sqlite_virtual_table_def(
  table_id = '12240',
  table_name = '__all_virtual_tablet_meta_table',
  keywords = all_def_keywords['__all_tablet_meta_table']))

# 12241: __all_virtual_tablet_to_table # removed (single-tenant: iterate VT mechanism deleted)

def_table_schema(
  owner = 'yuya.yu',
  table_name = '__all_virtual_load_data_stat',
  table_id = '12242',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],

  normal_columns = [
    ('job_id', 'int'),
    ('table_name', 'varchar:OB_MAX_TABLE_NAME_LENGTH'),
    ('file_path', 'varchar:MAX_PATH_SIZE'),
    ('table_column', 'int'),
    ('file_column', 'int'),
    ('batch_size', 'int'),
    ('parallel', 'int'),
    ('load_mode', 'int'),
    ('load_time', 'int'),
    ('estimated_remaining_time', 'int'),
    ('total_bytes', 'int'),
    ('read_bytes', 'int'),
    ('parsed_bytes', 'int'),
    ('parsed_rows', 'int'),
    ('total_shuffle_task', 'int'),
    ('total_insert_task', 'int'),
    ('shuffle_rt_sum', 'int'),
    ('insert_rt_sum', 'int'),
    ('total_wait_secs', 'int')
  ],  vtable_route_policy = 'local'
  )
# 12245: __all_virtual_backup_task # abandoned
# 12246: __all_virtual_backup_task_history # abandoned
# 12247: __all_virtual_backup_ls_task # abandoned
# 12248: __all_virtual_backup_ls_task_history # abandoned
# 12249: __all_virtual_backup_ls_task_info # abandoned
# 12250: __all_virtual_backup_skipped_tablet # abandoned
# 12251: __all_virtual_backup_skipped_tablet_history # abandoned

# 12253: __all_virtual_tablet_to_table_history # removed (single-tenant: iterate VT mechanism deleted)

def_table_schema(
  owner = 'zjf225077',
  table_name = '__all_virtual_log_stat',
  table_id = '12254',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  in_tenant_space = True,
  rowkey_columns = [
  ],

  normal_columns = [
  ('access_mode', 'varchar:32'),
  ('base_lsn', 'uint'),
  ('begin_lsn', 'uint'),
  ('begin_scn', 'uint'),
  ('end_lsn', 'uint'),
  ('end_scn', 'uint'),
  ('max_lsn', 'uint'),
  ('max_scn', 'uint')
  ],  vtable_route_policy = 'local'
  )

# 12255: __all_virtual_tenant_info (abandoned)
# 12256: __all_virtual_ls_recovery_stat (abandoned)
# 12257: __all_virtual_backup_ls_task_info_history # abandoned

# __all_virtual_tablet_replica_checksum: SQLite virtual table (migrated from iterate)
def_table_schema(**gen_sqlite_virtual_table_def(
  table_id = '12258',
  table_name = '__all_virtual_tablet_replica_checksum',
  keywords = all_def_keywords['__all_tablet_replica_checksum']))

# 12259: __all_virtual_ddl_checksum # removed (single-tenant: iterate VT mechanism deleted)

# 12260: __all_virtual_ddl_error_message # removed (single-tenant: iterate VT mechanism deleted)

# 12261: __all_virtual_ls_replica_task (abandoned)

# 12263: __all_virtual_tenant_scheduler_job # removed (single-tenant: iterate VT mechanism deleted)

# 12264: __all_virtual_tenant_scheduler_job_run_detail # removed (single-tenant: iterate VT mechanism deleted)

# 12265: __all_virtual_tenant_scheduler_program # removed (single-tenant: iterate VT mechanism deleted)

# 12266: __all_virtual_tenant_scheduler_program_argument # removed (single-tenant: iterate VT mechanism deleted)

# 12267: __all_virtual_backup_validation_task_v2
# 12268: __all_virtual_pg_backup_validation_task_v2

# 12269: __all_virtual_tenant_context # removed (single-tenant: iterate VT mechanism deleted)
# 12270: __all_virtual_tenant_context_history # removed (single-tenant: iterate VT mechanism deleted)

# 12271: __all_virtual_global_context_value (abandoned)
# 12272: __all_virtual_external_storage_session
# 12273: __all_virtual_external_storage_info

def_table_schema(
    owner = 'fenggu.yh',
    table_name    = '__all_virtual_unit',
    table_id      = '12274',
    table_type = 'VIRTUAL_TABLE',
    gm_columns = [],
    rowkey_columns = [
    ],
    in_tenant_space=True,
    normal_columns = [
      ('min_cpu', 'double'),
      ('max_cpu', 'double'),
      ('memory_size', 'int'),
      ('min_iops', 'int'),
      ('max_iops', 'int'),
      ('iops_weight', 'int'),
      ('log_disk_size', 'int'),
      ('log_disk_in_use', 'int'),
      ('data_disk_in_use', 'int'),
      ('status', 'varchar:64'),
      ('create_time', 'int'),
      ('data_disk_size', 'int', 'true'),
      ('max_net_bandwidth', 'int', 'true'),
      ('net_bandwidth_weight', 'int', 'true')
  ],  vtable_route_policy = 'local'
  )

# 12276: __all_virtual_server (rename to __all_virtual_server_stat)
def_table_schema(
    owner = 'wanhong.wwh',
    table_name    = '__all_virtual_server_stat',
    table_id      = '12276',
    table_type = 'VIRTUAL_TABLE',
    gm_columns = [],
    rowkey_columns = [
    ],
    in_tenant_space=False,
    normal_columns = [
      ('svr_ip', 'varchar:MAX_IP_ADDR_LENGTH'),
      ('svr_port', 'int'),
      ('sql_port', 'int'),
      ('rpc_port', 'int'),
      ('cpu_capacity', 'int'),
      ('cpu_capacity_max', 'double'),
      ('cpu_assigned', 'double'),
      ('cpu_assigned_max', 'double'),
      ('mem_capacity', 'int'),
      ('mem_assigned', 'int'),
      ('data_disk_capacity', 'int'),
      ('data_disk_in_use', 'int'),
      ('data_disk_health_status', 'varchar:OB_MAX_DEVICE_HEALTH_STATUS_STR_LENGTH'),
      ('data_disk_abnormal_time', 'int'),
      ('log_disk_capacity', 'int'),
      ('log_disk_assigned', 'int'),
      ('log_disk_in_use', 'int'),
      ('rpc_cert_expire_time', 'int'),
      ('rpc_tls_enabled', 'int'),
      ('memory_limit', 'int'),
      ('data_disk_allocated', 'int'),
      ('data_disk_assigned', 'int', 'true'),
      ('start_service_time', 'int'),
      ('create_time', 'int'),
      ('role', 'varchar:64'),
      ('switchover_status', 'varchar:100'),
      ('log_restore_source', 'varchar:1024'),
      ('sync_scn', 'uint'),
      ('readable_scn', 'uint')
    ],  vtable_route_policy = 'local'
  )

# 12277: __all_virtual_ls_election_reference_info (abandoned)

def_table_schema(
  owner = 'dachuan.sdc',
  tablegroup_id = 'OB_INVALID_ID',
  table_name    = '__all_virtual_dtl_interm_result_monitor',
  table_id      = '12278',
  table_type = 'VIRTUAL_TABLE',
  gm_columns    = [],
  in_tenant_space = True,
  rowkey_columns = [],

  normal_columns = [
    ('trace_id', 'varchar:OB_MAX_TRACE_ID_BUFFER_SIZE'),
    ('owner', 'varchar:OB_MODULE_NAME_LENGTH'),
    ('start_time' ,'timestamp'),
    ('expire_time','timestamp'),
    ('hold_memory', 'int'),
    ('dump_size', 'int'),
    ('dump_cost', 'int'),
    ('dump_time', 'timestamp', 'true'),
    ('dump_fd', 'int'),
    ('dump_dir_id', 'int'),
    ('channel_id', 'int'),
    ('qc_id', 'int'),
    ('dfo_id', 'int'),
    ('sqc_id', 'int'),
    ('batch_id', 'int'),
    ('max_hold_memory', 'int')
  ],  vtable_route_policy = 'local'
  )

# 12279: __all_virtual_archive_stat (removed: backup/restore/log-archive deleted)

def_table_schema(
  owner = 'keqing.llt',
  table_name = '__all_virtual_apply_stat',
  table_id = '12280',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  in_tenant_space = True,
  rowkey_columns = [
  ],

  normal_columns = [
    ('end_lsn', 'uint'),
    ('pending_cnt', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'keqing.llt',
  table_name = '__all_virtual_replay_stat',
  table_id = '12281',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  in_tenant_space = True,
  rowkey_columns = [
  ],

  normal_columns = [
    ('end_lsn', 'uint'),
    ('enabled', 'bool'),
    ('unsubmitted_lsn', 'uint'),
    ('unsubmitted_log_scn', 'uint'),
    ('pending_cnt', 'int')
  ],  vtable_route_policy = 'local'
  )

# 12282: __all_virtual_proxy_routine (abandoned in seekdb)

# backup clean virtual table
# 12283: __all_virtual_backup_delete_task # abandoned
# 12284: __all_virtual_backup_delete_task_history # abandoned
# 12285: __all_virtual_backup_delete_ls_task # abandoned
# 12286: __all_virtual_backup_delete_ls_task_history # abandoned

def_table_schema(
  owner = 'yanyuan.cxf',
  table_name     = '__all_virtual_ls_info',
  table_id       = '12287',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
  ],

  in_tenant_space = True,
  normal_columns = [
  ('tablet_count', 'int'),
  ('weak_read_scn', 'uint'),
  ('checkpoint_scn', 'uint'),
  ('checkpoint_lsn', 'uint'),
  ('tablet_change_checkpoint_scn', 'uint'),
  ('tx_blocked', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'yanyuan.cxf',
  table_name     = '__all_virtual_tablet_info',
  table_id       = '12288',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
  ],

  normal_columns = [
  ('tablet_id', 'int'),
  ('data_tablet_id', 'int'),
  ('ref_tablet_id', 'int'),
  ('checkpoint_scn', 'uint'),
  ('compaction_scn', 'uint'),
  ('multi_version_start', 'uint'),
  ('restore_status', 'int'),
  ('tablet_status', 'int'),
  ('is_committed', 'int'),
  ('is_empty_shell', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'yanyuan.cxf',
  table_name     = '__all_virtual_obj_lock',
  table_id       = '12289',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
  ],

  in_tenant_space = True,
  normal_columns = [
  ('lock_id', 'varchar:MAX_LOCK_ID_BUF_LENGTH'),
  ('lock_mode', 'varchar:MAX_LOCK_MODE_BUF_LENGTH'),
  ('owner_id', 'int'),
  ('create_trans_id', 'int'),
  ('op_type', 'varchar:MAX_LOCK_OP_TYPE_BUF_LENGTH'),
  ('op_status', 'varchar:MAX_LOCK_OP_STATUS_BUF_LENGTH'),
  ('trans_version', 'uint'),
  ('create_timestamp', 'int'),
  ('create_schema_version', 'int'),
  ('extra_info', 'varchar:MAX_LOCK_OP_EXTRA_INFO_LENGTH'),
  ('time_after_create', 'int'),
  ('obj_type', 'varchar:MAX_LOCK_OBJ_TYPE_BUF_LENGTH'),
  ('obj_id', 'int'),
  ('owner_type', 'int'),
  ('priority', 'varchar:MAX_LOCK_OP_PRIORITY_BUF_LENGTH'),
  ('wait_seq', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(**gen_sqlite_virtual_table_def(
  table_id = '12291',
  table_name = '__all_virtual_merge_info',
  keywords = all_def_keywords['__all_merge_info']))

def_table_schema(
  owner = 'gengli.wzy',
  table_name     = '__all_virtual_tx_data_table',
  table_id       = '12292',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
  ],

  normal_columns = [
  ('state', 'varchar:MAX_TX_DATA_TABLE_STATE_LENGTH'),
  ('start_scn', 'uint'),
  ('end_scn', 'uint'),
  ('tx_data_count', 'int'),
  ('min_tx_log_scn', 'uint'),
  ('max_tx_log_scn', 'uint')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'chunrun.ct',
  table_name     = '__all_virtual_transaction_freeze_checkpoint',
  table_id       = '12293',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
  ],

  in_tenant_space=True,
  normal_columns = [
  ('tablet_id', 'int'),
  ('rec_log_scn', 'uint'),
  ('location', 'varchar:MAX_FREEZE_CHECKPOINT_LOCATION_BUF_LENGTH'),
  ('rec_log_scn_is_stable', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'chunrun.ct',
  table_name     = '__all_virtual_transaction_checkpoint',
  table_id       = '12294',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
  ],

  in_tenant_space=True,
  normal_columns = [
  ('tablet_id', 'int'),
  ('rec_log_scn', 'uint'),
  ('checkpoint_type', 'varchar:MAX_CHECKPOINT_TYPE_BUF_LENGTH'),
  ('is_flushing', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'chunrun.ct',
  table_name     = '__all_virtual_checkpoint',
  table_id       = '12295',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
  ],

  in_tenant_space=True,
  normal_columns = [
  ('service_type', 'varchar:MAX_SERVICE_TYPE_BUF_LENGTH'),
  ('rec_log_scn', 'uint')
  ],  vtable_route_policy = 'local'
  )
# 12296: __all_virtual_backup_set_files (abandoned)
# 12297: __all_virtual_backup_job (abandoned)
# 12298: __all_virtual_backup_job_history (abandoned)


# 12299: __all_virtual_plan_baseline abandoned
# 12300: __all_virtual_plan_baseline_item abandoned
# 12301: __all_virtual_spm_config abandoned

# 12302: __all_virtual_ash # removed

def_table_schema(
  owner = 'jiangxiu.wt',
  table_name     = '__all_virtual_dml_stats',
  table_id       = '12303',
  table_type     = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ],
  in_tenant_space = True,
  normal_columns = [
  ('table_id', 'int'),
  ('tablet_id', 'int'),
  ('insert_row_count', 'int'),
  ('update_row_count', 'int'),
  ('delete_row_count', 'int')
  ],  vtable_route_policy = 'local'
  )
# 12304: abandoned
# 12305: __all_virtual_backup_parameter (abandoned)
# 12306: __all_virtual_restore_job  (abandoned)
# 12307: __all_virtual_restore_job_history (abandoned)
# 12308: __all_virtual_restore_progress (abandoned)
# 12309: __all_virtual_ls_restore_progress (abandoned)
# 12310: __all_virtual_ls_restore_history (abandoned)
# 12311: __all_virtual_backup_storage_info_history (abandoned)
# 12312: __all_virtual_backup_delete_job (abandoned)
# 12313: __all_virtual_backup_delete_job_history (abandoned)
# 12314: __all_virtual_backup_delete_policy (abandoned)

def_table_schema(
  owner = 'lihongqin.lhq',
  table_name     = '__all_virtual_tablet_ddl_kv_info',
  table_id       = '12315',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
  ],

  normal_columns = [
    ('tablet_id', 'int'),
    ('freeze_log_scn', 'uint'),
    ('start_log_scn', 'uint'),
    ('min_log_scn', 'uint'),
    ('macro_block_cnt', 'int'),
    ('ref_cnt', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
    owner = 'liuqifan.lqf',
    table_name    = '__all_virtual_privilege',
    table_id      = '12316',
    table_type = 'VIRTUAL_TABLE',
    in_tenant_space = True,
    gm_columns = [],
    rowkey_columns = [
    ],
    normal_columns = [
      ('Privilege', 'varchar:MAX_COLUMN_PRIVILEGE_LENGTH'),
      ('Context', 'varchar:MAX_PRIVILEGE_CONTEXT_LENGTH'),
      ('Comment', 'varchar:MAX_COLUMN_COMMENT_LENGTH')
  ]
  )

def_table_schema(
    owner = 'yunshan.tys',
    table_name = '__all_virtual_tablet_pointer_status',
    table_id   = '12317',
    table_type = 'VIRTUAL_TABLE',
    gm_columns = [],
    rowkey_columns = [
    ],
    normal_columns = [
      ('tablet_id', 'int'),
      ('address', 'varchar:256'),
      ('pointer_ref', 'int'),
      ('in_memory', 'bool'),
      ('tablet_ref', 'int'),
      ('wash_score', 'int'),
      ('tablet_ptr', 'varchar:128'),
      ('initial_state', 'bool'),
      ('old_chain', 'varchar:128'),
      ('occupy_size', 'bigint', 'false', '0'),
      ('required_size', 'bigint', 'false', '0')
  ],  vtable_route_policy = 'local',
  in_tenant_space = True
  )

def_table_schema(
    owner = 'yunshan.tys',
    table_name = '__all_virtual_storage_meta_memory_status',
    table_id   = '12318',
    table_type = 'VIRTUAL_TABLE',
    gm_columns = [],
    rowkey_columns = [
    ],
    normal_columns = [
      ('name', 'varchar:128'),
      ('used_size', 'int'),
      ('total_size', 'int'),
      ('used_obj_cnt', 'int'),
      ('free_obj_cnt', 'int'),
      ('each_obj_size', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'zhaoruizhe.zrz',
  table_name = '__all_virtual_kvcache_store_memblock',
  table_id = '12319',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ],
  normal_columns = [
    ('memblock_ptr', 'varchar:32'),
    ('ref_count', 'int'),
    ('status', 'int'),
    ('policy', 'int'),
    ('kv_cnt', 'int'),
    ('get_cnt', 'int'),
    ('recent_get_cnt', 'int'),
    ('score', 'number:38:6'),
    ('align_size', 'int')
  ],
  vtable_route_policy = 'local',)

# 12320: __all_virtual_mock_fk_parent_table # removed (single-tenant: iterate VT mechanism deleted)

# 12321: __all_virtual_mock_fk_parent_table_history # removed (single-tenant: iterate VT mechanism deleted)

# 12322: __all_virtual_mock_fk_parent_table_column # removed (single-tenant: iterate VT mechanism deleted)

# 12323: __all_virtual_mock_fk_parent_table_column_history # removed (single-tenant: iterate VT mechanism deleted)
# 12324: __all_virtual_log_restore_source abandoned

def_table_schema(
  owner = 'wangzelin.wzl',
  tablegroup_id='OB_INVALID_ID',
  table_name='__all_virtual_query_response_time',
  table_id='12325',
  table_type='VIRTUAL_TABLE',
  gm_columns=[],
  in_tenant_space=True,
  rowkey_columns=[
  ],
  normal_columns=[
    ('response_time', 'bigint', 'false', '0'),
    ('count',  'bigint', 'false', '0'),
    ('total',  'bigint', 'false', '0'),
    ('sql_type', 'varchar:128', 'false', '')
  ],  vtable_route_policy = 'local'
  )

# 12326: __all_virtual_kv_ttl_task (abandoned)
# 12327: __all_virtual_kv_ttl_task_history (abandoned)
# 12328: __all_virtual_tenant_datafile
# 12329: __all_virtual_tenant_datafile_history

# __all_virtual_column_checksum_error_info: SQLite virtual table (migrated from iterate)
def_table_schema(**gen_sqlite_virtual_table_def(
  table_id = '12330',
  table_name = '__all_virtual_column_checksum_error_info',
  keywords = all_def_keywords['__all_column_checksum_error_info']))

# 12331: __all_virtual_kvcache_handle_leak_info
# 12332: abandoned
# 12333: abandoned

def_table_schema(
    owner = 'lixia.yq',
    table_name     = '__all_virtual_tablet_compaction_info',
    table_id       = '12334',
    table_type     = 'VIRTUAL_TABLE',
    in_tenant_space = True,
    gm_columns     = [],
    rowkey_columns = [],

    normal_columns = [
      ('tablet_id', 'int'),
      ('finished_scn', 'int'),
      ('wait_check_scn', 'int'),
      ('max_received_scn', 'int'),
      ('serialize_scn_list', 'varchar:OB_MAX_VARCHAR_LENGTH'),
      ('validated_scn', 'int')
    ],    vtable_route_policy = 'local'
  )

# 12335: __all_virtual_ls_replica_task_plan (abandoned)

def_table_schema(
  owner = 'xingrui.cwh',
  table_name = '__all_virtual_schema_memory',
  table_id = '12336',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ],

  in_tenant_space = False,
  normal_columns = [
    ('type', 'varchar:128'),
    ('used_schema_mgr_cnt', 'int'),
    ('free_schema_mgr_cnt', 'int'),
    ('mem_used', 'int'),
    ('mem_total', 'int'),
    ('allocator_idx', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'xingrui.cwh',
  table_name = '__all_virtual_schema_slot',
  table_id = '12337',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ],

  in_tenant_space = False,
  normal_columns = [
    ('slot_id', 'int'),
    ('schema_version', 'int'),
    ('schema_count', 'int'),
    ('total_ref_cnt', 'int'),
    ('ref_info','varchar:OB_MAX_SCHEMA_REF_INFO'),
    ('allocator_idx', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'handora.qc',
  table_name     = '__all_virtual_minor_freeze_info',
  table_id       = '12338',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [],

  normal_columns = [
  ('tablet_id', 'int'),
  ('is_force', 'varchar:MAX_COLUMN_YES_NO_LENGTH'),
  ('freeze_clock', 'int'),
  ('freeze_snapshot_version', 'int'),
  ('start_time', 'timestamp', 'true'),
  ('end_time', 'timestamp', 'true'),
  ('ret_code', 'int'),
  ('state', 'varchar:OB_MAX_CHAR_LENGTH'),
  ('diagnose_info', 'varchar:OB_MAX_CHAR_LENGTH'),
  ('memtables_info', 'varchar:OB_MAX_CHAR_LENGTH')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'guoyun.lgy',
  table_name     = '__all_virtual_show_trace',
  table_id       = '12339',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ],
  in_tenant_space = True,

  normal_columns = [
    ('trace_id', 'varchar:OB_MAX_SPAN_LENGTH'),
    ('request_id', 'int'),
    ('span_id', 'varchar:OB_MAX_SPAN_LENGTH'),
    ('parent_span_id', 'varchar:OB_MAX_SPAN_LENGTH'),
    ('span_name', 'varchar:OB_MAX_SPAN_LENGTH'),
    ('ref_type', 'varchar:OB_MAX_REF_TYPE_LENGTH'),
    ('start_ts', 'timestamp'),
    ('end_ts', 'timestamp'),
    ('elapse', 'int'),
    ('tags', 'longtext'),
    ('logs', 'longtext')
  ]
  )

def_table_schema(
  owner = 'keqing.llt',
  table_name = '__all_virtual_ha_diagnose',
  table_id = '12340',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  in_tenant_space = False,
  rowkey_columns = [
  ],

  normal_columns = [
    ('palf_state', 'varchar:32'),
    ('max_applied_scn', 'uint'),
    ('max_replayed_lsn', 'uint'),
    ('max_replayed_scn', 'uint'),
    ('replay_diagnose_info', 'varchar:1024'),
    ('checkpoint_scn', 'uint'),
    ('min_rec_scn', 'uint'),
    ('min_rec_scn_log_type', 'varchar:32'),
    ('readonly_tx', 'varchar:1024')
  ],  vtable_route_policy = 'local'
  )

# 12341: __all_virtual_data_dictionary_in_log # removed (single-tenant: iterate VT mechanism deleted)

# 12358: __all_virtual_tenant_mysql_sys_agent (abandoned)

def_table_schema(
  owner = 'zhenling.zzg',
  tablegroup_id = 'OB_INVALID_ID',
  table_name    = '__all_virtual_sql_plan',
  table_id      = 12359,
  table_type = 'VIRTUAL_TABLE',
  in_tenant_space = True,
  gm_columns    = [],
  rowkey_columns = [
    ('plan_id', 'int')
  ],
  normal_columns = [
    ('sql_id', 'varchar:OB_MAX_SQL_ID_LENGTH'),
    ('db_id', 'int'),
    ('plan_hash', 'uint'),
    ('gmt_create', 'timestamp'),

    ('operator', 'varchar:255'),
    ('options', 'varchar:255'),
    ('object_node', 'varchar:40'),
    ('object_id', 'int'),
    ('object_owner', 'varchar:128'),
    ('object_name', 'varchar:128'),
    ('object_alias', 'varchar:261'),
    ('object_type', 'varchar:20'),
    ('optimizer', 'varchar:4000'),

    ('id', 'int'),
    ('parent_id', 'int'),
    ('depth', 'int'),
    ('position', 'int'),
    ('search_columns', 'int'),
    ('is_last_child', 'int'),
    ('cost', 'bigint'),
    ('real_cost', 'bigint'),
    ('cardinality', 'bigint'),
    ('real_cardinality', 'bigint'),
    ('bytes', 'bigint'),
    ('rowset', 'int'),

    ('other_tag', 'varchar:4000'),
    ('partition_start', 'varchar:4000'),
    ('partition_stop', 'varchar:4000'),
    ('partition_id', 'int'),
    ('other', 'varchar:4000'),
    ('distribution', 'varchar:64'),
    ('cpu_cost', 'bigint'),
    ('io_cost', 'bigint'),
    ('temp_space', 'bigint'),
    ('access_predicates', 'varchar:4000'),
    ('filter_predicates', 'varchar:4000'),
    ('startup_predicates', 'varchar:4000'),
    ('projection', 'varchar:4000'),
    ('special_predicates', 'varchar:4000'),
    ('time', 'int'),
    ('qblock_name','varchar:128'),
    ('remarks', 'varchar:4000'),
    ('other_xml', 'varchar:4000')
  ],  vtable_route_policy = 'local'
)
# 12360: abandoned
# 12361: abandoned

# 12362: __all_virtual_core_table # removed (single-tenant: iterate VT mechanism deleted)

def_table_schema(
  owner = 'tushicheng.tsc',
  table_name     = '__all_virtual_malloc_sample_info',
  table_id       = '12363',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  in_tenant_space = True,
  rowkey_columns = [],

  normal_columns = [
  ('ctx_id', 'int'),
  ('mod_name', 'varchar:OB_MAX_CHAR_LENGTH'),
  ('back_trace', 'varchar:DEFAULT_BUF_LENGTH'),
  ('ctx_name', 'varchar:OB_MAX_CHAR_LENGTH'),
  ('alloc_count', 'int'),
  ('alloc_bytes', 'int')
  ],
  vtable_route_policy = 'local',)

# 12364: legacy ls arb replica task table (abandoned)
# 12365: legacy ls arb replica task history table (abandoned)

# 12366: __all_virtual_archive_dest_status (removed: backup/restore/log-archive deleted)

# 12367: __all_virtual_kv_hotkey_stat

# 12371: __all_virtual_external_table_file # abandoned in seekdb

# 12372: __all_virtual_io_tracer

def_table_schema(
  owner             = 'zk250686',
  table_name        = '__all_virtual_mds_node_stat',
  table_id          = '12373',
  table_type        = 'VIRTUAL_TABLE',
  in_tenant_space   = True,
  gm_columns        = [],
  rowkey_columns    = [
    ('tablet_id',     'bigint'),
  ],
  normal_columns    = [
    ('user_key',      'longtext'),
    ('version_idx',   'bigint'),
    ('writer_type',   'longtext'),
    ('writer_id',     'bigint'),
    ('seq_no',        'bigint'),
    ('redo_scn',      'uint'),
    ('end_scn',       'uint'),
    ('trans_version', 'uint'),
    ('node_type',     'longtext'),
    ('state',         'longtext'),
    ('position',      'longtext'),
    ('user_data',     'longtext')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner             = 'zk250686',
  table_name        = '__all_virtual_mds_event_history',
  table_id          = '12374',
  table_type        = 'VIRTUAL_TABLE',
  in_tenant_space   = True,
  gm_columns        = [],
  rowkey_columns    = [
    ('tablet_id',     'bigint'),
  ],
  normal_columns    = [
    ('tid',           'int'),# common info
    ('tname',         'longtext'),# common info
    ('trace',         'longtext'),# common info
    ('timestamp',     'timestamp'),# common info
    ('event',         'longtext'),# common info
    ('info',          'longtext'),# common info
    ('user_key',      'longtext'),# row info
    ('writer_type',   'longtext'),# node info
    ('writer_id',     'bigint'),# node info
    ('seq_no',        'bigint'),# node info
    ('redo_scn',      'uint'),# node info
    ('end_scn',       'uint'),# node info
    ('trans_version', 'uint'),# node info
    ('node_type',     'longtext'),# node info
    ('state',         'longtext')# node info
  ],  vtable_route_policy = 'local'
  )
# 12375: __all_virtual_time_guard_slow_history

def_table_schema(
  owner = 'gengli.wzy',
  table_name     = '__all_virtual_tx_data',
  table_id       = '12380',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
  ('tx_id', 'int')
  ],

  normal_columns = [
  ('state', 'varchar:MAX_TX_DATA_STATE_LENGTH'),
  ('start_scn', 'uint'),
  ('end_scn', 'uint'),
  ('commit_version', 'uint'),
  ('undo_status', 'varchar:MAX_UNDO_LIST_CHAR_LENGTH'),
  ('tx_op', 'varchar:MAX_TX_OP_CHAR_LENGTH')
  ],  vtable_route_policy = 'local'
  )

# 12381: __all_virtual_task_opt_stat_gather_history # removed (single-tenant: iterate VT mechanism deleted)

# 12382: __all_virtual_table_opt_stat_gather_history # removed (single-tenant: iterate VT mechanism deleted)

def_table_schema(
  owner = 'jiangxiu.wt',
  table_name     = '__all_virtual_opt_stat_gather_monitor',
  table_id       = '12383',
  table_type     = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,
  normal_columns = [
  ('session_id', 'int'),
  ('trace_id', 'varchar:OB_MAX_TRACE_ID_BUFFER_SIZE'),
  ('task_id', 'varchar:36'),
  ('type', 'int'),
  ('task_start_time', 'timestamp'),
  ('task_table_count', 'int'),
  ('task_duration_time', 'int'),
  ('completed_table_count', 'int'),
  ('running_table_owner', 'varchar:OB_MAX_DATABASE_NAME_LENGTH'),
  ('running_table_name', 'varchar:OB_MAX_TABLE_NAME_LENGTH'),
  ('running_table_duration_time', 'int'),
  ('spare1', 'int', 'true'),
  ('spare2', 'varchar:MAX_VALUE_LENGTH', 'true')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner             = 'fengshuo.fs',
  table_name        = '__all_virtual_thread',
  table_id          = '12384',
  table_type        = 'VIRTUAL_TABLE',
  in_tenant_space   = True,
  gm_columns        = [],
  rowkey_columns    = [],
  normal_columns    = [
    ('tid',                 'int'),
    ('tname',               'varchar:16'),
    ('latch_wait',          'varchar:16'),
    ('latch_hold',          'varchar:256'),
    ('trace_id',            'varchar:40'),
    ('numa_node',           'int')
  ],  vtable_route_policy = 'local'
  )

# 12385: __all_virtual_arbitration_member_info (abandoned)

def_table_schema(
  owner = 'shifangdan.sfd',
  table_name = '__all_virtual_server_storage',
  table_id = '12386',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = False,
  normal_columns = [
    ('path', 'varchar:MAX_PATH_SIZE'),
    ('endpoint', 'varchar:OB_INNER_TABLE_DEFAULT_KEY_LENTH'),
    ('used_for', 'varchar:OB_MAX_CHAR_LENGTH'),
    ('storage_id', 'bigint:20'),
    ('max_iops', 'bigint:20'),
    ('max_bandwidth', 'bigint:20'),
    ('create_time', 'timestamp'),
    ('op_id', 'bigint:20'),
    ('sub_op_id', 'bigint:20'),
    ('authorization', 'varchar:OB_INNER_TABLE_DEFAULT_KEY_LENTH'),
    ('encrypt_info', 'varchar:OB_INNER_TABLE_DEFAULT_KEY_LENTH'),
    ('state', 'varchar:OB_MAX_CHAR_LENGTH'),
    ('state_info', 'varchar:OB_INNER_TABLE_DEFAULT_KEY_LENTH'),
    ('last_check_timestamp', 'timestamp'),
    ('extension', 'varchar:OB_INNER_TABLE_DEFAULT_VALUE_LENTH')
  ],  vtable_route_policy = 'local'
  )

# 12387: __all_virtual_arbitration_service_status (abandoned)

# 12388: __all_virtual_wr_active_session_history
# 12388: __all_virtual_wr_active_session_history # removed
# 12389: __all_virtual_wr_snapshot
# 12389: __all_virtual_wr_snapshot # removed
# 12390: __all_virtual_wr_statname
# 12390: __all_virtual_wr_statname # removed
# 12391: __all_virtual_wr_sysstat
# 12391: __all_virtual_wr_sysstat # removed
# 12392: __all_virtual_kv_connection abandoned

# 12393: __all_virtual_long_ops_status_mysql_sys_agent # removed (single-tenant: iterate VT mechanism deleted)

def_table_schema(
  owner             = 'lixinze.lxz',
  table_name        = '__all_virtual_timestamp_service',
  table_id          = '12395',
  table_type        = 'VIRTUAL_TABLE',
  in_tenant_space   = True,
  gm_columns        = [],
  rowkey_columns    = [],
  normal_columns = [
    ('ts_value', 'int'),
    ('ts_type', 'varchar:100')
  ],  vtable_route_policy = 'local'
  )

# 12396: __all_virtual_resource_pool_mysql_sys_agent (abandoned)

def_table_schema(
  owner = 'mingdou.tmd',
  table_name    = '__all_virtual_px_p2p_datahub',
  table_id      = '12397',
  table_type = 'VIRTUAL_TABLE',
  in_tenant_space = True,
  gm_columns    = [],
  rowkey_columns = [],
  normal_columns = [
    ('trace_id', 'varchar:OB_MAX_TRACE_ID_BUFFER_SIZE'),
    ('datahub_id', 'bigint'),
    ('message_type', 'varchar:256'),
    ('hold_size', 'bigint'),
    ('timeout_ts', 'timestamp'),
    ('start_time', 'timestamp')
  ],  vtable_route_policy = 'local'
  )

# 12398: removed virtual table

# 12401: __all_virtual_tenant_parameter (abandoned)
# 12402: __all_virtual_tenant_snapshot (abandoned)
# 12403: __all_virtual_tenant_snapshot_ls (abandoned)
# 12404: __all_virtual_tenant_snapshot_ls_replica (abandoned)

def_table_schema(
  owner = 'yunshan.tys',
  table_name = '__all_virtual_tablet_buffer_info',
  table_id = '12405',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ('tablet_buffer', 'varchar:128')
  ],

  normal_columns = [
  ('tablet', 'varchar:128'),
  ('pool_type', 'varchar:128'),
  ('tablet_id', 'int'),
  ('in_map', 'bool'),
  ('last_access_time', 'timestamp')
  ]
)

# 12414: __all_virtual_wr_control # removed

# 12418: removed (legacy resource isolation deleted)
# 12419: removed (legacy resource isolation deleted)

# 12421: __all_virtual_tenant_scheduler_job_class # removed (single-tenant: iterate VT mechanism deleted)

# 12422: __all_virtual_recover_table_job # abandoned
# 12423: __all_virtual_recover_table_job_history # abandoned
# 12424: __all_virtual_import_table_job # abandoned
# 12425: __all_virtual_import_table_job_history # abandoned
# 12426: __all_virtual_import_table_task # abandoned
# 12427: __all_virtual_import_table_task_history # abandoned
# 12428: __all_virtual_import_stmt_exec_history

def_table_schema(
  owner = 'handora.qc',
  table_name = '__all_virtual_data_activity_metrics',
  table_id = '12429',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ('activity_timestamp', 'timestamp')
  ],

  normal_columns = [
  ('modification_size', 'int'),
  ('freeze_times', 'int'),
  ('mini_merge_cost', 'int'),
  ('mini_merge_times', 'int'),
  ('minor_merge_cost', 'int'),
  ('minor_merge_times', 'int'),
  ('major_merge_cost', 'int'),
  ('major_merge_times', 'int')
  ]
)

# 12430-12432: removed virtual tables

# 12435: __all_virtual_clone_job (abandoned)
# 12436: __all_virtual_clone_job_history (abandoned)
# 12440: __all_virtual_wr_system_event # removed

# 12441: __all_virtual_wr_event_name # removed

def_table_schema(
  owner = 'fyy280124',
  table_name     = '__all_virtual_scheduler_running_job',
  table_id       = '12442',
  table_type     = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [],
  normal_columns = [
    ('owner', 'varchar:OB_MAX_DATABASE_NAME_LENGTH', 'true'),
    ('job_name', 'varchar:128'),
    ('job_subname', 'varchar:30', 'true'),
    ('job_style', 'varchar:11', 'true'),
    ('detached', 'varchar:5', 'true'),
    ('session_id', 'uint', 'true'),
    ('slave_process_id', 'uint', 'true'),
    ('slave_os_process_id', 'uint', 'true'),
    ('running_instance', 'varchar:30', 'true'),
    ('elapsed_time', 'int', 'true'),
    ('cpu_used', 'int', 'true'),
    ('destination_owner', 'varchar:128', 'true'),
    ('destination', 'varchar:128', 'true'),
    ('credential_owner', 'varchar:30', 'true'),
    ('credential_name', 'varchar:30', 'true'),
    ('job_class', 'varchar:128', 'true')
  ],  vtable_route_policy = 'local'
  )

# 12443: __all_virtual_routine_privilege # removed (single-tenant: iterate VT mechanism deleted)

# 12444: __all_virtual_routine_privilege_history # removed (single-tenant: iterate VT mechanism deleted)
def_table_schema(
  owner = 'yuchen.wyc',
  table_name    = '__all_virtual_sqlstat',
  table_id      = '12445',
  table_type = 'VIRTUAL_TABLE',
  gm_columns    = [],
  rowkey_columns = [],
  in_tenant_space=True,
  normal_columns = [
    ('SQL_ID', 'varchar:OB_MAX_SQL_ID_LENGTH'),
    ('PLAN_ID', 'int'),
    ('PLAN_HASH', 'uint'),
    ('PLAN_TYPE', 'int'),
    ('QUERY_SQL', 'longtext'),
    ("SQL_TYPE", 'int'),
    ('MODULE', 'varchar:64', 'true'),
    ('ACTION', 'varchar:64', 'true'),
    ('PARSING_DB_ID', 'int'),
    ('PARSING_DB_NAME', 'varchar:OB_MAX_DATABASE_NAME_LENGTH'),
    ('PARSING_USER_ID', 'int'),
    ('EXECUTIONS_TOTAL', 'bigint', 'false',  '0'),
    ('EXECUTIONS_DELTA', 'bigint', 'false',  '0'),
    ('DISK_READS_TOTAL', 'bigint', 'false',  '0'),
    ('DISK_READS_DELTA', 'bigint', 'false',  '0'),
    ('BUFFER_GETS_TOTAL', 'bigint', 'false',  '0'),
    ('BUFFER_GETS_DELTA', 'bigint', 'false',  '0'),
    ('ELAPSED_TIME_TOTAL', 'bigint', 'false',  '0'),
    ('ELAPSED_TIME_DELTA', 'bigint', 'false',  '0'),
    ('CPU_TIME_TOTAL', 'bigint', 'false',  '0'),
    ('CPU_TIME_DELTA', 'bigint', 'false',  '0'),
    ('CCWAIT_TOTAL', 'bigint', 'false',  '0'),
    ('CCWAIT_DELTA', 'bigint', 'false',  '0'),
    ('USERIO_WAIT_TOTAL', 'bigint', 'false',  '0'),
    ('USERIO_WAIT_DELTA', 'bigint', 'false',  '0'),
    ('APWAIT_TOTAL', 'bigint', 'false',  '0'),
    ('APWAIT_DELTA', 'bigint', 'false',  '0'),
    ('PHYSICAL_READ_REQUESTS_TOTAL', 'bigint', 'false',  '0'),
    ('PHYSICAL_READ_REQUESTS_DELTA', 'bigint', 'false',  '0'),
    ('PHYSICAL_READ_BYTES_TOTAL', 'bigint', 'false',  '0'),
    ('PHYSICAL_READ_BYTES_DELTA', 'bigint', 'false',  '0'),
    ('WRITE_THROTTLE_TOTAL', 'bigint', 'false',  '0'),
    ('WRITE_THROTTLE_DELTA', 'bigint', 'false',  '0'),
    ('ROWS_PROCESSED_TOTAL', 'bigint', 'false',  '0'),
    ('ROWS_PROCESSED_DELTA', 'bigint', 'false',  '0'),
    ('MEMSTORE_READ_ROWS_TOTAL', 'bigint', 'false',  '0'),
    ('MEMSTORE_READ_ROWS_DELTA', 'bigint', 'false',  '0'),
    ('MINOR_SSSTORE_READ_ROWS_TOTAL', 'bigint', 'false',  '0'),
    ('MINOR_SSSTORE_READ_ROWS_DELTA', 'bigint', 'false',  '0'),
    ('MAJOR_SSSTORE_READ_ROWS_TOTAL', 'bigint', 'false',  '0'),
    ('MAJOR_SSSTORE_READ_ROWS_DELTA', 'bigint', 'false',  '0'),
    ('RPC_TOTAL', 'bigint', 'false',  '0'),
    ('RPC_DELTA', 'bigint', 'false',  '0'),
    ('FETCHES_TOTAL', 'bigint', 'false',  '0'),
    ('FETCHES_DELTA', 'bigint', 'false',  '0'),
    ('RETRY_TOTAL', 'bigint', 'false',  '0'),
    ('RETRY_DELTA', 'bigint', 'false',  '0'),
    ('PARTITION_TOTAL', 'bigint', 'false',  '0'),
    ('PARTITION_DELTA', 'bigint', 'false',  '0'),
    ('NESTED_SQL_TOTAL', 'bigint', 'false',  '0'),
    ('NESTED_SQL_DELTA', 'bigint', 'false',  '0'),
    ('SOURCE_IP', 'varchar:MAX_IP_ADDR_LENGTH'),
    ('SOURCE_PORT', 'int'),
    ('FIRST_LOAD_TIME', 'timestamp', 'true'),
    ('PLAN_CACHE_HIT_TOTAL', 'bigint', 'false', '0'),
    ('PLAN_CACHE_HIT_DELTA', 'bigint', 'false', '0')
  ],  vtable_route_policy = 'local'
  )
# 12446: __all_virtual_wr_sqlstat # removed
# 12447: __all_virtual_aux_stat # removed (single-tenant: iterate VT mechanism deleted)

# 12448: __all_virtual_detect_lock_info # removed (single-tenant: iterate VT mechanism deleted)

# 12449: __all_virtual_client_to_server_session_info # removed (single-tenant: iterate VT mechanism deleted)

def_table_schema(
  owner = 'dingjincheng.djc',
  table_name     = '__all_virtual_sys_variable_default_value',
  table_id       = '12450',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,
  normal_columns = [
  ('variable_name', 'varchar:OB_MAX_CONFIG_NAME_LEN', 'false', ''),
  ('default_value', 'varchar:OB_MAX_CONFIG_VALUE_LEN', 'true')
  ]
  )

# 12453: __all_virtual_tenant_snapshot_job (abandoned)

# 12454: __all_virtual_wr_sqltext # removed

# 12455: __all_virtual_trusted_root_certificate_info

# 12456: __all_virtual_dbms_lock_allocated # removed (single-tenant: iterate VT mechanism deleted)

# 12457: __all_virtual_shared_storage_compaction_info (abandoned)
# 12458:__all_virtual_ls_snapshot(abandoned)

# 12459: __all_virtual_index_usage_info # removed (single-tenant: iterate VT mechanism deleted)

# 12462: __all_virtual_column_privilege # removed (single-tenant: iterate VT mechanism deleted)

# 12463: __all_virtual_column_privilege_history # removed (single-tenant: iterate VT mechanism deleted)

# 12464: __all_virtual_tenant_snapshot_ls_replica_history (abandoned)
# 12465: __all_virtual_shared_storage_quota (abandoned)

def_table_schema(
  owner = 'jim.wjh',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'ENABLED_ROLES',
  table_id       = '12466',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  normal_columns = [
  ('ROLE_NAME', 'varchar:OB_MAX_SYS_PARAM_NAME_LENGTH', 'true', 'NULL'),
  ('ROLE_HOST', 'varchar:OB_MAX_SYS_PARAM_VALUE_LENGTH', 'true', 'NULL'),
  ('IS_DEFAULT', 'varchar:OB_MAX_SYS_PARAM_VALUE_LENGTH', 'true', 'NULL'),
  ('IS_MANDATORY', 'varchar:OB_MAX_SYS_PARAM_VALUE_LENGTH', 'false', '')
  ]
  )

# 12467: __all_virtual_ls_replica_task_history (abandoned)

def_table_schema(
  owner = 'gongyusen.gys',
  table_name     = '__all_virtual_session_ps_info',
  table_id       = '12468',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  enable_column_def_enum = True,
  in_tenant_space = True,
  normal_columns = [
    ('session_id', 'uint'),
    ('ps_client_stmt_id', 'int'),
    ('ps_inner_stmt_id', 'int'),
    ('stmt_type', 'varchar:256'),
    ('param_count', 'int'),
    ('param_types', 'longtext'),
    ('ref_count', 'int'),
    ('checksum', 'int')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'fy373789',
  tablegroup_id = 'OB_INVALID_ID',
  table_name    = '__all_virtual_tracepoint_info',
  table_id      = '12469',
  table_type = 'VIRTUAL_TABLE',
  gm_columns    = [],
  in_tenant_space = True,
  rowkey_columns = [],

  normal_columns = [
    ('tp_no', 'int'),
    ('tp_name', 'varchar:OB_MAX_TRACEPOINT_NAME_LEN'),
    ('tp_describe', 'varchar:OB_MAX_TRACEPOINT_DESCRIBE_LEN'),
    ('tp_frequency', 'int'),
    ('tp_error_code', 'int'),
    ('tp_occur', 'int'),
    ('tp_match', 'int')
  ],  vtable_route_policy = 'local'
  )

# 12470: __all_virtual_ls_compaction_status
# 12471: __all_virtual_tablet_compaction_status
# 12472: __all_virtual_tablet_checksum_error_info (abandoned)
# 12473: __all_virtual_compatibility_control (removed)


# 12479: __all_virtual_res_mgr_directive # removed (single-tenant: iterate VT mechanism deleted)

# 12480: __all_virtual_service (abandoned)

def_table_schema(
  owner = 'yanyuan.cxf',
  table_name     = '__all_virtual_resource_limit',
  table_id       = '12481',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
  ],

  in_tenant_space = True,
  normal_columns = [
  ('resource_name', 'varchar:MAX_RESOURCE_NAME_LEN'),
  ('current_utilization', 'bigint'),
  ('max_utilization', 'bigint'),
  ('reserved_value', 'bigint'),
  ('limit_value', 'bigint'),
  ('effective_limit_type', 'varchar:MAX_CONSTRAINT_NAME_LEN')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner = 'yanyuan.cxf',
  table_name     = '__all_virtual_resource_limit_detail',
  table_id       = '12482',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [
  ],

  in_tenant_space = True,
  normal_columns = [
  ('resource_name', 'varchar:MAX_RESOURCE_NAME_LEN'),
  ('limit_type', 'varchar:MAX_CONSTRAINT_NAME_LEN'),
  ('limit_value', 'bigint')
  ],  vtable_route_policy = 'local'
  )

def_table_schema(
  owner      = 'wyh329796',
  table_name = '__all_virtual_group_io_stat',
  table_id = '12483',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  in_tenant_space = True,
  rowkey_columns = [
    ('group_id', 'int')
  ],
  normal_columns = [
    ('group_name', 'varchar:OB_MAX_RESOURCE_PLAN_NAME_LENGTH'),
    ('mode', 'varchar:OB_MAX_RESOURCE_PLAN_NAME_LENGTH'),
    ('min_iops', 'int'),
    ('max_iops', 'int'),
    ('real_iops', 'int'),
    ('max_net_bandwidth', 'int'),
    ('max_net_bandwidth_display', 'varchar:128'),
    ('real_net_bandwidth', 'int'),
    ('real_net_bandwidth_display', 'varchar:128'),
    ('norm_iops', 'int')
  ],  vtable_route_policy = 'local'
  )

# 21485: __all_virtual_storage_io_usage (abandoned)
# 12486: __all_virtual_zone_storage (abandoned)

def_table_schema(
  owner             = 'gengfu.zpc',
  table_name        = '__all_virtual_nic_info',
  table_id          = '12487',
  table_type        = 'VIRTUAL_TABLE',
  in_tenant_space   = True,
  gm_columns        = [],
  rowkey_columns    = [],
  normal_columns    = [
    ('devname',   'varchar:MAX_IFNAME_LENGTH'),
    ('speed_Mbps', 'int')
  ],  vtable_route_policy = 'local'
  )

# 12488: __all_virtual_scheduler_job_run_detail_v2 # removed (single-tenant: iterate VT mechanism deleted)

# 12489: __all_virtual_deadlock_detector_stat
# 12490: __all_virtual_spatial_reference_systems # removed (single-tenant: iterate VT mechanism deleted)

# 12492: __all_virtual_ss_local_cache_info abandoned

# 12493: __all_virtual_kv_group_commit_status abandoned

# 12494: __all_virtual_session_sys_variable
# 12495: __all_virtual_spm_evo_result abandoned

def_table_schema(
  owner = 'huhaosheng.hhs',
  table_name     = '__all_virtual_vector_index_info',
  table_id       = '12496',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  in_tenant_space = True,
  rowkey_columns = [
  ],

  normal_columns = [
    ('rowkey_vid_table_id', 'int'),
    ('vid_rowkey_table_id', 'int'),
    ('inc_index_table_id', 'int'),
    ('vbitmap_table_id', 'int'),
    ('snapshot_index_table_id', 'int'),
    ('data_table_id', 'int'),
    ('rowkey_vid_tablet_id', 'int'),
    ('vid_rowkey_tablet_id', 'int'),
    ('inc_index_tablet_id', 'int'),
    ('vbitmap_tablet_id', 'int'),
    ('snapshot_index_tablet_id', 'int'),
    ('data_tablet_id', 'int'),
    # memory usage, status..., logic_version
    ('statistics', 'varchar:MAX_COLUMN_COMMENT_LENGTH'),
    # sync snapshot...
    ('sync_info', 'varchar:OB_INNER_TABLE_DEFAULT_KEY_LENTH')
  ],  vtable_route_policy = 'local'
  )

# 12497: __all_virtual_pkg_type # removed (single-tenant: iterate VT mechanism deleted)

# 12498: __all_virtual_pkg_type_attr # removed (single-tenant: iterate VT mechanism deleted)

# 12499: __all_virtual_pkg_coll_type # removed (single-tenant: iterate VT mechanism deleted)

# 12500: __all_virtual_kv_client_info abandoned

# 12501: __all_virtual_wr_sql_plan # removed

# 12502: __all_virtual_wr_res_mgr_sysstat # removed

# 12503: __all_virtual_kv_redis_table abandoned

# 12504: removed (legacy function IO classification deleted)


def_table_schema(
  owner = 'wuyuefei.wyf',
  table_name     = '__all_virtual_temp_file',
  table_id       = '12505',
  table_type = 'VIRTUAL_TABLE',
  gm_columns     = [],
  rowkey_columns = [],
  in_tenant_space = True,

  normal_columns = [
    ('file_id', 'int'),
    ('trace_id', 'varchar:OB_MAX_TRACE_ID_BUFFER_SIZE',  'true', ''),
    ('dir_id', 'int'),
    ('data_bytes', 'int'),
    ('start_offset', 'int'),
    ('is_deleting', 'bool'),
    ('cached_data_page_num', 'int'),
    ('write_back_data_page_num', 'int'),
    ('flushed_data_page_num', 'int'),
    ('ref_cnt', 'int'),
    ('total_writes', 'int'),
    ('unaligned_writes', 'int'),
    ('total_reads', 'int'),
    ('unaligned_reads', 'int'),
    ('total_read_bytes', 'int'),
    ('last_access_time', 'timestamp'),
    ('last_modify_time', 'timestamp'),
    ('birth_time', 'timestamp'),
    ('file_ptr', 'varchar:20'),
    ('file_label', 'varchar:16', 'true', ''),
    ('meta_tree_epoch', 'int'),
    ('meta_tree_levels', 'int'),
    ('meta_bytes', 'int'),
    ('cached_meta_page_num', 'int'),
    ('write_back_meta_page_num', 'int'),
    ('page_flush_cnt', 'int'),
    ('type', 'int'),
    ('compressible_fd', 'int'),
    ('persisted_tail_page_writes', 'int'),
    ('lack_page_cnt', 'int'),
    ('total_truncated_page_read_cnt', 'int'),
    ('truncated_page_hits', 'int'),
    ('total_kv_cache_page_read_cnt', 'int'),
    ('kv_cache_page_read_hits', 'int'),
    ('total_uncached_page_read_cnt', 'int'),
    ('uncached_page_hits', 'int'),
    ('aggregate_read_io_cnt', 'int'),
    ('total_wbp_page_read_cnt', 'int'),
    ('wbp_page_hits', 'int')
  ],  vtable_route_policy = 'local'
  )

# 12506: __all_virtual_ncomp_dll_v2 # removed (single-tenant: iterate VT mechanism deleted)
# 12507: __all_virtual_logstore_service_status
# 12508: __all_virtual_logstore_service_info
# 12510: __all_virtual_standby_log_transport_stat

# 12511: __all_virtual_wr_sql_plan_aux_key2snapshot # removed
# 12512: __all_virtual_tablet_mds_info
# 12513: removed

# 12515: __all_virtual_plugin_info
def_table_schema(
  owner = 'wangyunlai.wyl',
  table_name = '__all_virtual_plugin_info',
  table_id   = '12515',
  table_type = 'VIRTUAL_TABLE',
  in_tenant_space = True,
  gm_columns = [],
  rowkey_columns = [
  ],
  normal_columns = [
    ('name', 'varchar:64', 'true', 'NULL'),               # true means nullable and NULL is the default value
    ('status', 'varchar:64', 'true', 'NULL'),             # plugin status: READY, UNINIT, DEAD
    ('type', 'varchar:80', 'true', 'NULL'),               # plugin type, such as tokenizer
    ('library', 'varchar:128', 'true', 'NULL'),           # plugin dynamic link library name (built-in plugins do not have corresponding link libraries)
    ('library_version', 'varchar:80', 'true', 'NULL'),    # version of the plugin library itself
    ('library_revision', 'varchar:80', 'true', 'NULL'),   # plugin library revision version, such as git commit id
    ('interface_version', 'varchar:80', 'true', 'NULL'),  # specific interface API version implemented by this plugin
    ('author', 'varchar:64', 'true', 'NULL'),             # plugin author information
    ('license', 'varchar:64', 'true', 'NULL'),            # plugin LICENSE
    ('description', 'varchar:65535', 'true', 'NULL')      # plugin description information
  ],  vtable_route_policy = 'local'
)

# 12516: __all_virtual_catalog # removed (single-tenant: iterate VT mechanism deleted)

# 12517: __all_virtual_catalog_history # removed (single-tenant: iterate VT mechanism deleted)

# 12518: __all_virtual_catalog_privilege # removed (single-tenant: iterate VT mechanism deleted)

# 12519: __all_virtual_catalog_privilege_history # removed (single-tenant: iterate VT mechanism deleted)

# 12520: __all_virtual_sswriter_group_stat
# 12521: __all_virtual_sswriter_lease_mgr

# 12523: __all_virtual_pl_recompile_objinfo # removed (single-tenant: iterate VT mechanism deleted)

# 12524: __all_virtual_vector_index_task # removed (single-tenant: iterate VT mechanism deleted)

# 12525: __all_virtual_vector_index_task_history # removed (single-tenant: iterate VT mechanism deleted)

def_table_schema(
  owner = 'linyi.cl',
  table_name     = '__all_virtual_show_create_catalog',
  table_id       = '12526',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ('catalog_id', 'int')
  ],
  in_tenant_space = True,

  normal_columns = [
  ('catalog_name', 'varchar:OB_MAX_CATALOG_NAME_LENGTH'),
  ('create_catalog', 'longtext')
  ]
  )

def_table_schema(
    owner = 'chendingchao.cdc',
    table_name    = '__all_virtual_show_catalog_databases',
    table_id      = '12527',
    table_type = 'VIRTUAL_TABLE',
    in_tenant_space = True,
    gm_columns = [],
    rowkey_columns = [
        ('catalog_id', 'int'),
        ('database_name', 'varchar:OB_MAX_TABLE_NAME_LENGTH')
  ],
    normal_columns = []
  )
# 12528: __tenant_virtual_show_catalog_tables
# 12529: __all_virtual_storage_cache_task
# 12530: __all_virtual_tablet_local_cache

# 12531: __tenant_virtual_catalog_table_column
# 12532: __all_virtual_show_create_catalog_table

# 12533: __all_virtual_ccl_rule # removed (single-tenant: iterate VT mechanism deleted)

def_table_schema(
  owner = 'zhl413386',
  table_name    = '__all_virtual_ccl_status',
  table_id      = '12534',
  table_type = 'VIRTUAL_TABLE',
  in_tenant_space = True,
  gm_columns    = [],
  rowkey_columns = [
    ('ccl_rule_id','int'),
    ('format_sqlid','varchar:OB_MAX_SQL_ID_LENGTH')
  ],
  normal_columns = [
    ('current_concurrency', 'int'),
    ('max_concurrency', 'int')
  ],  vtable_route_policy = 'local'
  )


# 12537: __all_virtual_ls_migration_task
# 12538 __all_virtual_ss_notify_tasks_stat
# 12539 __all_virtual_ss_notify_tablets_stat

# 12549: __all_virtual_ccl_rule_history # removed (single-tenant: iterate VT mechanism deleted)

def_table_schema(
  owner = 'tonghui.ht',
  table_name     = '__all_virtual_vector_mem_info',
  table_id       = '12550',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  normal_columns = [
  ('raw_malloc_size', 'int'),
  ('index_metadata_size', 'int'),
  ('vector_mem_hold', 'int'),
  ('vector_mem_used', 'int'),
  ('vector_mem_limit', 'int'),
  ('tx_share_limit', 'int'),
  ('vector_mem_detail_info', 'varchar:OB_MAX_MYSQL_VARCHAR_LENGTH')
  ],  vtable_route_policy = 'local'
  )

# 12551: __all_virtual_ai_model # removed (single-tenant: iterate VT mechanism deleted)

# 12552: __all_virtual_ai_model_history # removed (single-tenant: iterate VT mechanism deleted)

# 12553: __all_virtual_ai_model_endpoint # removed (single-tenant: iterate VT mechanism deleted)

# 12554: __all_virtual_tenant_location # removed (single-tenant: iterate VT mechanism deleted)
# 12555: __all_virtual_tenant_location_history # removed (single-tenant: iterate VT mechanism deleted)
# 12556: __all_virtual_objauth_mysql # removed (single-tenant: iterate VT mechanism deleted)
# 12557: __all_virtual_objauth_mysql_history # removed (single-tenant: iterate VT mechanism deleted)
def_table_schema(
  owner = 'cjl476581',
  table_name     = '__all_virtual_show_create_location',
  table_id       = '12558',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [
  ('location_id', 'int')
  ],
  in_tenant_space = True,
  normal_columns = [
  ('location_name', 'varchar:OB_MAX_LOCATION_NAME_LENGTH'),
  ('create_location', 'varchar:LOCATION_DEFINE_LENGTH')
  ]
  )
# 12559: __tenant_virtual_list_file # abandoned in seekdb

def_table_schema(**gen_sqlite_virtual_table_def(
    table_id = '12563',
    table_name = '__all_virtual_rootservice_job',
    keywords = all_def_keywords['__all_rootservice_job']
  ))

# 12564: __all_virtual_change_stream_refresh_stat
def_table_schema(
  owner = 'xiebaoma.xbm',
  table_name    = '__all_virtual_change_stream_refresh_stat',
  table_id      = '12564',
  table_type = 'VIRTUAL_TABLE',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space=True,
  normal_columns = [
    ('refresh_scn', 'int'),
    ('min_dep_lsn', 'int'),
    ('pending_tx_count', 'int'),
    ('fetch_tx', 'int'),
    ('fetch_lsn', 'int'),
    ('fetch_scn', 'int'),
  ],
)

# Reserved position (placeholder before this line)
# Placeholder suggestion for this section: Use actual table names for placeholders
################################################################################
# End of Mysql Virtual Table (10000, 15000]
################################################################################
################################### Placeholder Notice ###################################
# Placeholder example: Write the comment at the beginning of the line, indicating which TABLE_ID to occupy and the corresponding name
# TABLE_ID: TABLE_NAME
#
# FARM will base the placeholder validation development branch TABLE_ID and TABLE_NAME matching check, if they do not match, FARM will intercept and report an error
#
# Note:
# 0. Placeholder before 'reserved position'
# 1. Always start by occupying the master, ensuring the master branch is a superset of all other branches, to avoid NAME and ID conflicts
# 2. After the master placeholder is set, do not change NAME on the development branch, otherwise FARM will consider it an ID placeholder conflict. If this scenario occurs, you need to modify the master placeholder first
# 3. It is recommended to use the accurate TABLE_NAME as a placeholder, TABLE_ID and TABLE_NAME are one-to-one corresponding within the system
# 4. Some tables are defined based on the schema of other base tables (e.g., gen_xx_table_def()), their actual table names are relatively complex, to facilitate placeholder usage, it is recommended to use the base table name for placeholders
#    - Example 1: def_table_schema(**gen_mysql_sys_agent_virtual_table_def('12393', all_def_keywords['__all_virtual_long_ops_status']))
#      * Base table name placeholder: # 12393: __all_virtual_long_ops_status
#      * Real table name placeholder: # 12393: __all_virtual_virtual_long_ops_status_mysql_sys_agent
#      * Base table name placeholder: # 15009: __all_virtual_sql_audit
#      * Real table name placeholder: # 15009: ALL_VIRTUAL_SQL_AUDIT
#    - Example 3: def_table_schema(**gen_sys_agent_virtual_table_def('15111', all_def_keywords['__all_routine_param']))
#      * Base table name placeholder: # 15111: __all_routine_param
#      * Real table name placeholder: # 15111: ALL_VIRTUAL_ROUTINE_PARAM_SYS_AGENT
# 5. Index table placeholder requirements TABLE_NAME should be used as follows: base table (data table) name, index name (index_name), actual index table name
#    For example: 100001 The placeholder method for the index table can be:
#       * # 100001: __idx_3_idx_data_table_id
#       * # 100001: idx_data_table_id
#       * # 100001: __all_table
################################################################################


################################################################################
# Extended Virtual Table(15000,20000]
################################################################################

# Reserved position (placeholder before this line)
# This section defines mapped table names which are relatively complex, generally defined using the gen_xxx_table_def() method, placeholder suggestion is to use the base table name as a placeholder
#   * Base table name placeholder: # 15009: __all_virtual_sql_audit
#   * Real table name placeholder: # 15009: ALL_VIRTUAL_SQL_AUDIT
################################################################################
# End of Extended Virtual Table(15000,20000]
################################################################################
################################### Placeholder Notice ###################################
# Placeholder example: Write the comment at the beginning of the line, indicating which TABLE_ID to occupy and the corresponding name
# TABLE_ID: TABLE_NAME
#
# FARM will base the placeholder validation development branch TABLE_ID and TABLE_NAME matching check, if they do not match, FARM will intercept and report an error
#
# Note:
# 0. Placeholder before 'reserved position'
# 1. Always start by occupying the master, ensuring the master branch is a superset of all other branches, to avoid NAME and ID conflicts
# 2. After the master placeholder is set, do not change NAME on the development branch, otherwise FARM will consider it an ID placeholder conflict. If this scenario occurs, you need to modify the master placeholder first
# 3. It is recommended to use the accurate TABLE_NAME for placeholder, TABLE_ID and TABLE_NAME are one-to-one corresponding within the system
# 4. Some tables are defined based on the schema of other base tables (e.g., gen_xx_table_def()), their actual table names are relatively complex, to facilitate placeholder usage, it is recommended to use the base table name for placeholders
#    - Example 1: def_table_schema(**gen_mysql_sys_agent_virtual_table_def('12393', all_def_keywords['__all_virtual_long_ops_status']))
#      * Base table name placeholder: # 12393: __all_virtual_long_ops_status
#      * Real table name placeholder: # 12393: __all_virtual_virtual_long_ops_status_mysql_sys_agent
#      * Base table name placeholder: # 15009: __all_virtual_sql_audit
#      * Real table name placeholder: # 15009: ALL_VIRTUAL_SQL_AUDIT
#    - Example 3: def_table_schema(**gen_sys_agent_virtual_table_def('15111', all_def_keywords['__all_routine_param']))
#      * Base table name placeholder: # 15111: __all_routine_param
#      * Real table name placeholder: # 15111: ALL_VIRTUAL_ROUTINE_PARAM_SYS_AGENT
# 5. Index table placeholder requirements TABLE_NAME should be used as follows: base table (data table) name, index name (index_name), actual index table name
#    For example: 100001 The placeholder method for the index table can be:
#       * # 100001: __idx_3_idx_data_table_id
#       * # 100001: idx_data_table_id
#       * # 100001: __all_table
################################################################################

################################################################################
# System View (20000,30000]
# MySQL System View (20000, 25000]
# Extended System View (25000, 30000]
################################################################################

# 20001: GV$OB_PLAN_CACHE_STAT # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_plan_cache_stat)

# 20002: GV$OB_PLAN_CACHE_PLAN_STAT # removed (single-tenant GV/V collapse; folded into V$OB_PLAN_CACHE_PLAN_STAT)

def_table_schema(
  owner = 'bin.lb',
  tablegroup_id  = 'OB_INVALID_ID',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'SCHEMATA',
  table_id       = '20003',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  view_definition = """
  SELECT 'def' AS CATALOG_NAME,
         DATABASE_NAME collate utf8mb4_name_case AS SCHEMA_NAME,
         b.charset AS DEFAULT_CHARACTER_SET_NAME,
         b.collation AS DEFAULT_COLLATION_NAME,
         CAST(NULL AS CHAR(512)) as SQL_PATH,
         'NO' as DEFAULT_ENCRYPTION
  FROM oceanbase.__all_database a inner join oceanbase.__all_virtual_collation b ON a.collation_type = b.collation_type
  WHERE in_recyclebin = 0
    and a.database_name not in ('__recyclebin', '__public')
    and 0 = sys_privilege_check('db_acc', 0, a.database_name)
  ORDER BY a.database_id
""".replace("\n", " "),

  in_tenant_space = True,

  normal_columns = [
  ]
  )

def_table_schema(
  owner = 'jim.wjh',
  tablegroup_id  = 'OB_INVALID_ID',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'CHARACTER_SETS',
  table_id       = '20004',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  view_definition = """
  SELECT CHARSET AS CHARACTER_SET_NAME, DEFAULT_COLLATION AS DEFAULT_COLLATE_NAME, DESCRIPTION, max_length AS MAXLEN FROM oceanbase.__all_virtual_charset
""".replace("\n", " "),

  in_tenant_space = True,

  normal_columns = [
  ]
  )

def_table_schema(
  owner = 'xiaochu.yh',
  tablegroup_id  = 'OB_INVALID_ID',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'GLOBAL_VARIABLES',
  table_id       = '20005',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  view_definition = """
  SELECT `variable_name` as VARIABLE_NAME, `value` as VARIABLE_VALUE  FROM oceanbase.__all_virtual_global_variable
""".replace("\n", " "),

  in_tenant_space = True,

  normal_columns = [
  ]
  )

def_table_schema(
  owner = 'jiangxiu.wt',
  tablegroup_id = 'OB_INVALID_ID',
  database_id   = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'STATISTICS',
  table_id       = '20006',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,
  view_definition = """
  SELECT CAST('def' AS             CHAR(512))    AS TABLE_CATALOG,
         V.TABLE_SCHEMA collate utf8mb4_name_case AS TABLE_SCHEMA,
         V.TABLE_NAME collate utf8mb4_name_case  AS TABLE_NAME,
         CAST(V.NON_UNIQUE AS      SIGNED)       AS NON_UNIQUE,
         V.INDEX_SCHEMA collate utf8mb4_name_case AS INDEX_SCHEMA,
         V.INDEX_NAME collate utf8mb4_name_case  AS INDEX_NAME,
         CAST(V.SEQ_IN_INDEX AS    UNSIGNED)     AS SEQ_IN_INDEX,
         V.COLUMN_NAME                           AS COLUMN_NAME,
         CAST('A' AS               CHAR(1))      AS COLLATION,
         CAST(NULL AS              SIGNED)       AS CARDINALITY,
         CAST(V.SUB_PART AS        SIGNED)       AS SUB_PART,
         CAST(NULL AS              CHAR(10))     AS PACKED,
         CAST(V.NULLABLE AS        CHAR(3))      AS NULLABLE,
         CAST(V.INDEX_TYPE AS      CHAR(16))     AS INDEX_TYPE,
         CAST(V.COMMENT AS         CHAR(16))     AS COMMENT,
         CAST(V.INDEX_COMMENT AS   CHAR(1024))   AS INDEX_COMMENT,
         CAST(V.IS_VISIBLE AS      CHAR(3))      AS IS_VISIBLE,
         V.EXPRESSION                            AS EXPRESSION
  FROM   (SELECT db.database_name                                              AS TABLE_SCHEMA,
                 t.table_name                                                  AS TABLE_NAME,
                 CASE WHEN i.index_type IN (2,4,8,41) THEN 0 ELSE 1 END        AS NON_UNIQUE,
                 db.database_name                                              AS INDEX_SCHEMA,
                 CASE WHEN i.index_type = 41 THEN 'PRIMARY' ELSE
                 substr(i.table_name, 7 + instr(substr(i.table_name, 7), '_')) END AS INDEX_NAME,
                 c.index_position                                              AS SEQ_IN_INDEX,
                 CASE WHEN d_col.column_name IS NOT NULL THEN d_col.column_name ELSE c.column_name END AS COLUMN_NAME,
                 CASE WHEN d_col.column_name IS NOT NULL THEN c.data_length ELSE NULL END AS SUB_PART,
                 CASE WHEN c.nullable = 1 THEN 'YES' ELSE '' END               AS NULLABLE,
                 CASE WHEN i.index_type in (15, 18, 21) THEN 'FULLTEXT'
                      WHEN i.index_using_type = 0 THEN 'BTREE'
                      WHEN i.index_using_type = 1 THEN 'HASH'
                      ELSE 'UNKOWN' END      AS INDEX_TYPE,
                 CASE i.index_status
                 WHEN 2 THEN 'VALID'
                 WHEN 3 THEN 'CHECKING'
                 WHEN 4 THEN 'INELEGIBLE'
                 WHEN 5 THEN 'ERROR'
                 ELSE 'UNUSABLE' END                                                  AS COMMENT,
                 i.comment                                                     AS INDEX_COMMENT,
                 CASE WHEN (i.index_attributes_set & 1) THEN 'NO' ELSE 'YES' END AS IS_VISIBLE,
                 d_col2.cur_default_value_v2                                     AS EXPRESSION
          FROM   oceanbase.__all_table i
          JOIN   oceanbase.__all_table t
          ON     i.data_table_id=t.table_id
          AND    i.database_id = t.database_id
          AND    i.table_type = 5
          AND    i.index_type NOT IN (13, 14, 16, 17, 19, 20, 22)
          AND    i.table_mode >> 12 & 15 in (0,1)
          AND    i.index_attributes_set & 16 = 0
          AND    t.table_type in (0,3)
          JOIN   oceanbase.__all_column c
          ON     i.table_id=c.table_id
          AND    c.index_position > 0
          JOIN   oceanbase.__all_database db
          ON     i.database_id = db.database_id
          AND    db.in_recyclebin = 0
          AND    db.database_name != '__recyclebin'
          LEFT JOIN oceanbase.__all_column d_col
          ON    i.data_table_id = d_col.table_id
          AND   (case when (c.is_hidden = 1 and substr(c.column_name, 1, 8) = '__substr') then
                   substr(c.column_name, 8 + instr(substr(c.column_name, 8), '_')) else 0 end) = d_col.column_id
          LEFT JOIN oceanbase.__all_column d_col2
          ON    i.data_table_id = d_col2.table_id
          AND   c.column_id = d_col2.column_id
          AND   d_col2.cur_default_value_v2 is not null
          AND   d_col2.is_hidden = 1
          AND   (d_col2.column_flags & (0x1 << 0) = 1 or d_col2.column_flags & (0x1 << 1) = 1)
          AND   substr(d_col2.column_name, 1, 6) = 'SYS_NC'
        UNION ALL
          SELECT  db.database_name  AS TABLE_SCHEMA,
                  t.table_name      AS TABLE_NAME,
                  0                 AS NON_UNIQUE,
                  db.database_name  AS INDEX_SCHEMA,
                  'PRIMARY'         AS INDEX_NAME,
                  c.rowkey_position AS SEQ_IN_INDEX,
                  c.column_name     AS COLUMN_NAME,
                  NULL              AS SUB_PART,
                  ''                AS NULLABLE,
                  CASE WHEN t.index_using_type = 0 THEN 'BTREE' ELSE (
                    CASE WHEN t.index_using_type = 1 THEN 'HASH' ELSE 'UNKOWN' END) END AS INDEX_TYPE,
                  'VALID'          AS COMMENT,
                  t.comment        AS INDEX_COMMENT,
                  'YES'            AS IS_VISIBLE,
                  NULL             AS EXPRESSION
          FROM   oceanbase.__all_table t
          JOIN   oceanbase.__all_column c
          ON     t.table_id=c.table_id
          AND    c.rowkey_position > 0
          AND    c.is_hidden = 0
          AND    t.table_type in (0,3)
          JOIN   oceanbase.__all_database db
          ON     t.database_id = db.database_id
          AND    db.in_recyclebin = 0
          AND    db.database_name != '__recyclebin'
        UNION ALL
          SELECT db.database_name                                           AS TABLE_SCHEMA,
              t.table_name                                                  AS TABLE_NAME,
              CASE WHEN i.index_type IN (2,4,8,41) THEN 0 ELSE 1 END        AS NON_UNIQUE,
              db.database_name                                              AS INDEX_SCHEMA,
              substr(i.table_name, 7 + instr(substr(i.table_name, 7), '_')) AS INDEX_NAME,
              c.index_position                                              AS SEQ_IN_INDEX,
              CASE WHEN d_col.column_name IS NOT NULL THEN d_col.column_name ELSE c.column_name END AS COLUMN_NAME,
              CASE WHEN d_col.column_name IS NOT NULL THEN c.data_length ELSE NULL END AS SUB_PART,
              CASE WHEN c.nullable = 1 THEN 'YES' ELSE '' END               AS NULLABLE,
              CASE WHEN i.index_type in (15, 18, 21) THEN 'FULLTEXT'
                   WHEN i.index_using_type = 0 THEN 'BTREE'
                   WHEN i.index_using_type = 1 THEN 'HASH'
                   ELSE 'UNKOWN' END      AS INDEX_TYPE,
              CASE i.index_status
              WHEN 2 THEN 'VALID'
              WHEN 3 THEN 'CHECKING'
              WHEN 4 THEN 'INELEGIBLE'
              WHEN 5 THEN 'ERROR'
              ELSE 'UNUSABLE' END                                           AS COMMENT,
              i.comment                                                     AS INDEX_COMMENT,
              CASE WHEN (i.index_attributes_set & 1) THEN 'NO' ELSE 'YES' END AS IS_VISIBLE,
              d_col2.cur_default_value_v2                                   AS EXPRESSION
          FROM   oceanbase.__ALL_VIRTUAL_CORE_ALL_TABLE i
          JOIN   oceanbase.__ALL_VIRTUAL_CORE_ALL_TABLE t
          ON     i.data_table_id=t.table_id
          AND    i.database_id = t.database_id
          AND    i.table_type = 5
          AND    i.index_type NOT IN (13, 14, 16, 17, 19, 20, 22)
          AND    t.table_type in (0,3)
          JOIN   oceanbase.__ALL_VIRTUAL_CORE_COLUMN_TABLE c
          ON     i.table_id=c.table_id
          AND    c.index_position > 0
          JOIN   oceanbase.__all_database db
          ON     i.database_id = db.database_id
          LEFT JOIN oceanbase.__ALL_VIRTUAL_CORE_COLUMN_TABLE d_col
          ON    i.data_table_id = d_col.table_id
          AND   (case when (c.is_hidden = 1 and substr(c.column_name, 1, 8) = '__substr') then
                   substr(c.column_name, 8 + instr(substr(c.column_name, 8), '_')) else 0 end) = d_col.column_id
          LEFT JOIN oceanbase.__ALL_VIRTUAL_CORE_COLUMN_TABLE d_col2
          ON    i.data_table_id = d_col2.table_id
          AND   c.column_id = d_col2.column_id
          AND   d_col2.cur_default_value_v2 is not null
          AND   d_col2.is_hidden = 1
          AND   (d_col2.column_flags & (0x1 << 0) = 1 or d_col2.column_flags & (0x1 << 1) = 1)
          AND   substr(d_col2.column_name, 1, 6) = 'SYS_NC'
        UNION ALL
          SELECT db.database_name  AS TABLE_SCHEMA,
                  t.table_name      AS TABLE_NAME,
                  0                 AS NON_UNIQUE,
                  db.database_name  AS INDEX_SCHEMA,
                  'PRIMARY'         AS INDEX_NAME,
                  c.rowkey_position AS SEQ_IN_INDEX,
                  c.column_name     AS COLUMN_NAME,
                  NULL              AS SUB_PART,
                  ''                AS NULLABLE,
                  CASE WHEN t.index_using_type = 0 THEN 'BTREE' ELSE (
                    CASE WHEN t.index_using_type = 1 THEN 'HASH' ELSE 'UNKOWN' END) END AS INDEX_TYPE,
                  'VALID'          AS COMMENT,
                  t.comment        AS INDEX_COMMENT,
                  'YES'            AS IS_VISIBLE,
                  NULL             AS EXPRESSION
          FROM   oceanbase.__ALL_VIRTUAL_CORE_ALL_TABLE t
          JOIN   oceanbase.__ALL_VIRTUAL_CORE_COLUMN_TABLE c
          ON     t.table_id=c.table_id
          AND    c.rowkey_position > 0
          AND    c.is_hidden = 0
          AND    t.table_type in (0,3)
          JOIN   oceanbase.__all_database db
          ON     t.database_id = db.database_id)V
          WHERE 0 = sys_privilege_check('table_acc', 1)
                OR 0 = sys_privilege_check('table_acc', 1, V.TABLE_SCHEMA, V.TABLE_NAME)
""".replace("\n", " "),

  normal_columns = [
  ]
  )

def_table_schema(
  owner = 'bin.lb',
  tablegroup_id = 'OB_INVALID_ID',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'VIEWS',
  table_id       = '20007',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  view_definition = """select
                   cast('def' as CHAR(64)) AS TABLE_CATALOG,
                   d.database_name collate utf8mb4_name_case as TABLE_SCHEMA,
                   t.table_name collate utf8mb4_name_case as TABLE_NAME,
                   t.view_definition as VIEW_DEFINITION,
                   case t.view_check_option when 1 then 'LOCAL' when 2 then 'CASCADED' else 'NONE' end as CHECK_OPTION,
                   case t.view_is_updatable when 1 then 'YES' else 'NO' end as IS_UPDATABLE,
                   cast((case t.define_user_id
                         when -1 then 'NONE'
                         else concat(u.user_name, '@', u.host) end) as CHAR(288)) as DEFINER,
                   cast('NONE' as CHAR(7)) AS SECURITY_TYPE,
                   cast((case t.collation_type
                         when 45 then 'utf8mb4'
                         else 'NONE' end) as CHAR(64)) AS CHARACTER_SET_CLIENT,
                   cast((case t.collation_type
                         when 45 then 'utf8mb4_general_ci'
                         else 'NONE' end) as CHAR(64)) AS COLLATION_CONNECTION
                   from oceanbase.__all_table as t
                   join oceanbase.__all_database as d
                     on t.database_id = d.database_id
                   left join oceanbase.__all_user as u
                     on t.define_user_id = u.user_id and t.define_user_id != -1
                   where t.table_type in (1, 4)
                     and t.table_mode >> 12 & 15 in (0,1)
                     and t.index_attributes_set & 16 = 0
                     and d.in_recyclebin = 0
                     and d.database_name != '__recyclebin'
                     and d.database_name != 'information_schema'
                     and d.database_name != 'oceanbase'
                     and 0 = sys_privilege_check('table_acc', 1, d.database_name, t.table_name)
""".replace("\n", " "),


  normal_columns = [
  ]
  )

def_table_schema(
  owner = 'bin.lb',
  tablegroup_id  = 'OB_INVALID_ID',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'TABLES',
  table_id       = '20008',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  view_definition = """
                    select /*+ leading(a) no_use_nl(ts)*/
                    cast('def' as char(512)) as TABLE_CATALOG,
                    cast(b.database_name as char(64) IGNORE) collate utf8mb4_name_case as TABLE_SCHEMA,
                    cast(a.table_name as char(64) IGNORE) collate utf8mb4_name_case as TABLE_NAME,
                    cast(case when (a.database_id = 201002 or a.table_type = 1) then 'SYSTEM VIEW'
                         when a.table_type in (0, 2) then 'SYSTEM TABLE'
                         when a.table_type = 4 then 'VIEW'
                         when a.table_type = 14 then 'EXTERNAL TABLE'
                         else 'BASE TABLE' end as char(64)) as TABLE_TYPE,
                    cast(case when a.table_type in (0,3,5,6,11,12,13) then 'InnoDB'
                        else 'MEMORY' end as char(64)) as ENGINE,
                    cast(NULL as unsigned) as VERSION,
                    cast(a.store_format as char(10)) as ROW_FORMAT,
                    cast( coalesce(ts.row_cnt,0) as unsigned) as TABLE_ROWS,
                    cast( coalesce(ts.avg_row_len,0) as unsigned) as AVG_ROW_LENGTH,
                    cast( coalesce(ts.data_size,0) as unsigned) as DATA_LENGTH,
                    cast(NULL as unsigned) as MAX_DATA_LENGTH,
                    cast( coalesce(idx_stat.index_length, 0) as unsigned) as INDEX_LENGTH,
                    cast(NULL as unsigned) as DATA_FREE,
                    cast(NULL as unsigned) as AUTO_INCREMENT,
                    cast(a.gmt_create as datetime) as CREATE_TIME,
                    cast(a.gmt_modified as datetime) as UPDATE_TIME,
                    cast(NULL as datetime) as CHECK_TIME,
                    cast(d.collation as char(32)) as TABLE_COLLATION,
                    cast(NULL as unsigned) as CHECKSUM,
                    cast(NULL as char(255)) as CREATE_OPTIONS,
                    cast(case when a.table_type = 4 then 'VIEW'
                             else a.comment end as char(2048)) as TABLE_COMMENT,
                    cast(case when a.table_mode >> 30 = 1 then 'HEAP'
                              else 'INDEX' end as char(12)) as ORGANIZATION
                    from
                    (
                    select c.database_id,
                           c.table_id,
                           c.table_name,
                           c.collation_type,
                           c.table_type,
                           usec_to_time(d.schema_version) as gmt_create,
                           usec_to_time(d.schema_version) as gmt_modified,
                           c.comment,
                           c.store_format,
                           c.table_mode
                    from (select 201001 as database_id, 1 as table_id, '__all_core_table' as table_name, 45 as collation_type, 0 as table_type, '' as comment, 'DYNAMIC' as store_format, 0 as table_mode
                union all select 201001 as database_id, 3 as table_id, '__all_table'      as table_name, 45 as collation_type, 0 as table_type, '' as comment, 'DYNAMIC' as store_format, 0 as table_mode
                union all select 201001 as database_id, 4 as table_id, '__all_column'     as table_name, 45 as collation_type, 0 as table_type, '' as comment, 'DYNAMIC' as store_format, 0 as table_mode
                union all select 201001 as database_id, 5 as table_id, '__all_ddl_operation'     as table_name, 45 as collation_type, 0 as table_type, '' as comment, 'DYNAMIC' as store_format, 0 as table_mode) c
                    join oceanbase.__all_virtual_core_all_table d
                      on d.table_name = '__all_core_table'
                    where 1 = 1
                    union all
                    select database_id,
                           table_id,
                           table_name,
                           collation_type,
                           table_type,
                           gmt_create,
                           gmt_modified,
                           comment,
                           store_format,
                           table_mode
                    from oceanbase.__all_table where table_mode >> 12 & 15 in (0,1) and index_attributes_set & 16 = 0) a
                    join oceanbase.__all_database b
                    on a.database_id = b.database_id
                    join oceanbase.__all_virtual_collation d
                    on a.collation_type = d.collation_type
                    left join (
                      select table_id,
                             row_cnt,
                             avg_row_len,
                             (macro_blk_cnt * 2 * 1024 * 1024) as data_size
                      from oceanbase.__all_table_stat
                      where partition_id = -1 or partition_id = table_id) ts
                    on a.table_id = ts.table_id
                    left join (
                      select e.data_table_id as data_table_id,
                             SUM(f.macro_blk_cnt * 2 * 1024 * 1024) AS index_length
                      FROM oceanbase.__all_table e JOIN oceanbase.__all_table_stat f
                            ON e.table_id = f.table_id and (f.partition_id = -1 or f.partition_id = e.table_id)
                      WHERE e.index_type in (1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 41) and e.table_type = 5
                            group by data_table_id
                    ) idx_stat on idx_stat.data_table_id = a.table_id
                    where a.table_type in (0, 1, 2, 3, 4, 14)
                    and b.database_name != '__recyclebin'
                    and b.in_recyclebin = 0
                    and 0 = sys_privilege_check('table_acc', 1, b.database_name, a.table_name)
""".replace("\n", " "),


  normal_columns = [
  ]
  )

def_table_schema(
  owner = 'jim.wjh',
  tablegroup_id = 'OB_INVALID_ID',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'COLLATIONS',
  table_id       = '20009',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  view_definition = """select collation as COLLATION_NAME, charset as CHARACTER_SET_NAME, id as ID, `is_default` as IS_DEFAULT, is_compiled as IS_COMPILED, sortlen as SORTLEN from oceanbase.__all_virtual_collation
""".replace("\n", " "),

  in_tenant_space = True,

  normal_columns = [
  ]
  )

def_table_schema(
  owner = 'jim.wjh',
  tablegroup_id = 'OB_INVALID_ID',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'COLLATION_CHARACTER_SET_APPLICABILITY',
  table_id       = '20010',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  view_definition = """select collation as COLLATION_NAME, charset as CHARACTER_SET_NAME from oceanbase.__all_virtual_collation
""".replace("\n", " "),

  in_tenant_space = True,

  normal_columns = [
  ]
  )

def_table_schema(
  owner = 'xiaochu.yh',
  tablegroup_id  = 'OB_INVALID_ID',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'PROCESSLIST',
  table_id       = '20011',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  view_definition = """SELECT id AS ID, user AS USER, concat(user_client_ip, ':', user_client_port) AS HOST, db AS DB, command AS COMMAND, cast(time as SIGNED) AS TIME, state AS STATE, info AS INFO FROM oceanbase.__all_virtual_processlist WHERE  1
""".replace("\n", " "),

  in_tenant_space = True,

  normal_columns = [
  ]
  )

def_table_schema(
  owner = 'bin.lb',
  tablegroup_id  = 'OB_INVALID_ID',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'KEY_COLUMN_USAGE',
  table_id       = '20012',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  view_definition = """
                    (select 'def' as CONSTRAINT_CATALOG,
                    c.database_name collate utf8mb4_name_case as  CONSTRAINT_SCHEMA,
                    'PRIMARY' as CONSTRAINT_NAME, 'def' as TABLE_CATALOG,
                    c.database_name collate utf8mb4_name_case as TABLE_SCHEMA,
                    a.table_name collate utf8mb4_name_case as TABLE_NAME,
                    b.column_name as COLUMN_NAME,
                    b.rowkey_position as ORDINAL_POSITION,
                    CAST(NULL AS UNSIGNED) as POSITION_IN_UNIQUE_CONSTRAINT,
                    CAST(NULL AS CHAR(64)) as REFERENCED_TABLE_SCHEMA,
                    CAST(NULL AS CHAR(64)) as REFERENCED_TABLE_NAME,
                    CAST(NULL AS CHAR(64)) as REFERENCED_COLUMN_NAME
                    from oceanbase.__all_table a
                    join oceanbase.__all_column b
                      on a.table_id = b.table_id
                    join oceanbase.__all_database c
                      on a.database_id = c.database_id
                    where a.table_mode >> 12 & 15 in (0,1)
                      and a.index_attributes_set & 16 = 0
                      and c.in_recyclebin = 0
                      and c.database_name != '__recyclebin'
                      and b.rowkey_position > 0
                      and b.column_id >= 16
                      and a.table_type != 5 and a.table_type != 12 and a.table_type != 13
                      and b.column_flags & (0x1 << 8) = 0
                      and (0 = sys_privilege_check('table_acc', 1)
                           or 0 = sys_privilege_check('table_acc', 1, c.database_name, a.table_name)))

                    union all
                    (select 'def' as CONSTRAINT_CATALOG,
                    d.database_name collate utf8mb4_name_case as CONSTRAINT_SCHEMA,
                    substr(a.table_name, 2 + length(substring_index(a.table_name,'_',4))) as CONSTRAINT_NAME,
                    'def' as TABLE_CATALOG,
                    d.database_name collate utf8mb4_name_case as TABLE_SCHEMA,
                    c.table_name collate utf8mb4_name_case as TABLE_NAME,
                    b.column_name as COLUMN_NAME,
                    b.index_position as ORDINAL_POSITION,
                    CAST(NULL AS UNSIGNED) as POSITION_IN_UNIQUE_CONSTRAINT,
                    CAST(NULL AS CHAR(64)) as REFERENCED_TABLE_SCHEMA,
                    CAST(NULL AS CHAR(64)) as REFERENCED_TABLE_NAME,
                    CAST(NULL AS CHAR(64)) as REFERENCED_COLUMN_NAME
                    from oceanbase.__all_table a
                    join oceanbase.__all_column b
                      on a.table_id = b.table_id
                    join oceanbase.__all_table c
                      on a.data_table_id = c.table_id
                    join oceanbase.__all_database d
                      on c.database_id = d.database_id
                    where 1 = 1
                      and d.in_recyclebin = 0
                      and d.database_name != '__recyclebin'
                      and a.table_type = 5
                      and a.index_type in (2, 4, 8, 41)
                      and b.index_position > 0
                      and (0 = sys_privilege_check('table_acc', 1)
                           or 0 = sys_privilege_check('table_acc', 1, d.database_name, c.table_name))

                    union all
                    (select 'def' as CONSTRAINT_CATALOG,
                    d.database_name collate utf8mb4_name_case as CONSTRAINT_SCHEMA,
                    f.foreign_key_name as CONSTRAINT_NAME,
                    'def' as TABLE_CATALOG,
                    d.database_name collate utf8mb4_name_case as TABLE_SCHEMA,
                    t.table_name collate utf8mb4_name_case as TABLE_NAME,
                    c.column_name as COLUMN_NAME,
                    fc.position as ORDINAL_POSITION,
                    CAST(fc.position AS UNSIGNED) as POSITION_IN_UNIQUE_CONSTRAINT,
                    d2.database_name as REFERENCED_TABLE_SCHEMA,
                    t2.table_name as REFERENCED_TABLE_NAME,
                    c2.column_name as REFERENCED_COLUMN_NAME
                    from
                    oceanbase.__all_foreign_key f
                    join oceanbase.__all_table t
                      on f.child_table_id = t.table_id
                    join oceanbase.__all_database d
                      on t.database_id = d.database_id
                    join oceanbase.__all_foreign_key_column fc
                      on f.foreign_key_id = fc.foreign_key_id
                    join oceanbase.__all_column c
                      on fc.child_column_id = c.column_id and t.table_id = c.table_id
                    join oceanbase.__all_table t2
                      on f.parent_table_id = t2.table_id
                    join oceanbase.__all_database d2
                      on t2.database_id = d2.database_id
                    join oceanbase.__all_column c2
                      on fc.parent_column_id = c2.column_id and t2.table_id = c2.table_id
                    where (0 = sys_privilege_check('table_acc', 1)
                           or 0 = sys_privilege_check('table_acc', 1, d.database_name, t.table_name))

                    union all
                    (select 'def' as CONSTRAINT_CATALOG,
                    d.database_name collate utf8mb4_name_case as CONSTRAINT_SCHEMA,
                    f.foreign_key_name as CONSTRAINT_NAME,
                    'def' as TABLE_CATALOG,
                    d.database_name collate utf8mb4_name_case as TABLE_SCHEMA,
                    t.table_name collate utf8mb4_name_case as TABLE_NAME,
                    c.column_name as COLUMN_NAME,
                    fc.position as ORDINAL_POSITION,
                    CAST(fc.position AS UNSIGNED) as POSITION_IN_UNIQUE_CONSTRAINT,
                    d.database_name as REFERENCED_TABLE_SCHEMA,
                    t2.mock_fk_parent_table_name as REFERENCED_TABLE_NAME,
                    c2.parent_column_name as REFERENCED_COLUMN_NAME
                    from oceanbase.__all_foreign_key f
                    join oceanbase.__all_table t
                      on f.child_table_id = t.table_id
                    join oceanbase.__all_database d
                      on t.database_id = d.database_id
                    join oceanbase.__all_foreign_key_column fc
                      on f.foreign_key_id = fc.foreign_key_id
                    join oceanbase.__all_column c
                      on fc.child_column_id = c.column_id and t.table_id = c.table_id
                    join oceanbase.__all_mock_fk_parent_table t2
                      on f.parent_table_id = t2.mock_fk_parent_table_id
                    join oceanbase.__all_mock_fk_parent_table_column c2
                      on fc.parent_column_id = c2.parent_column_id and t2.mock_fk_parent_table_id = c2.mock_fk_parent_table_id
                    where (0 = sys_privilege_check('table_acc', 1)
                           or 0 = sys_privilege_check('table_acc', 1, d.database_name, t.table_name)))))
                    """.replace("\n", " "),

  in_tenant_space = True,

  normal_columns = [
  ]
  )

# 20013: DBA_OB_OUTLINES # abandoned in 4.0

def_table_schema(
  owner = 'jiangxiu.wt',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'ENGINES',
  table_id        = '20014',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT CAST('InnoDB' as CHAR(64)) as ENGINE,
           CAST('YES' AS CHAR(8)) as SUPPORT,
           CAST('Supports transactions' as CHAR(80)) as COMMENT,
           CAST('YES' as CHAR(3)) as TRANSACTIONS,
           CAST('YES' as CHAR(3)) as SAVEPOINTS
    FROM DUAL;
""".replace("\n", " ")
)

def_table_schema(
  owner = 'linlin.xll',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'ROUTINES',
  table_id        = '20015',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """select
                      CAST(mp.specific_name AS CHAR(64)) AS SPECIFIC_NAME,
                      CAST('def' AS CHAR(512)) as ROUTINE_CATALOG,
                      CAST(mp.db AS CHAR(64)) collate utf8mb4_name_case as ROUTINE_SCHEMA,
                      CAST(mp.name AS CHAR(64)) as ROUTINE_NAME,
                      CAST(mp.type AS CHAR(9)) as ROUTINE_TYPE,
                      CAST(lower(v.data_type_str) AS CHAR(64)) AS DATA_TYPE,
                      CAST(
                        CASE
                        WHEN mp.type = 'FUNCTION' THEN CASE
                        WHEN rp.param_type IN (22, 23, 27, 28, 29, 30) THEN rp.param_length
                        ELSE NULL
                        END
                          ELSE NULL
                        END
                          AS SIGNED
                      ) as CHARACTER_MAXIMUM_LENGTH,
                      CASE
                      WHEN rp.param_type IN (22, 23, 27, 28, 29, 30, 43, 44, 46) THEN CAST(
                        rp.param_length * CASE rp.param_coll_type
                        WHEN 63 THEN 1
                        WHEN 249 THEN 4
                        WHEN 248 THEN 4
                        WHEN 87 THEN 2
                        WHEN 28 THEN 2
                        WHEN 55 THEN 4
                        WHEN 54 THEN 4
                        WHEN 101 THEN 2
                        WHEN 46 THEN 4
                        WHEN 45 THEN 4
                        WHEN 224 THEN 4
                        ELSE 1
                        END
                          AS SIGNED
                      )
                      ELSE CAST(NULL AS SIGNED)
                    END
                      AS CHARACTER_OCTET_LENGTH,
                      CASE
                      WHEN rp.param_type IN (1, 2, 3, 4, 5, 15, 16, 50) THEN CAST(rp.param_precision AS UNSIGNED)
                      ELSE CAST(NULL AS UNSIGNED)
                    END
                      AS NUMERIC_PRECISION,
                      CASE
                      WHEN rp.param_type IN (15, 16, 50) THEN CAST(rp.param_scale AS SIGNED)
                      WHEN rp.param_type IN (1, 2, 3, 4, 5, 11, 12, 13, 14) THEN CAST(0 AS SIGNED)
                      ELSE CAST(NULL AS SIGNED)
                    END
                      AS NUMERIC_SCALE,
                      CASE
                      WHEN rp.param_type IN (17, 18, 20, 42) THEN CAST(rp.param_scale AS UNSIGNED)
                      ELSE CAST(NULL AS UNSIGNED)
                    END
                      AS DATETIME_PRECISION,
                      CAST(
                        CASE rp.param_charset
                        WHEN 1 THEN 'binary'
                        WHEN 2 THEN 'utf8mb4'
                        WHEN 3 THEN 'gbk'
                        WHEN 4 THEN 'utf16'
                        WHEN 5 THEN 'gb18030'
                        WHEN 6 THEN 'latin1'
                        WHEN 7 THEN 'gb18030_2022'
                        WHEN 8 THEN 'ascii'
                        WHEN 9 THEN 'tis620'
                        ELSE NULL
                        END
                          AS CHAR(64)
                      ) AS CHARACTER_SET_NAME,
                      CAST(
                        CASE rp.param_coll_type
                        WHEN 45 THEN 'utf8mb4_general_ci'
                        WHEN 46 THEN 'utf8mb4_bin'
                        WHEN 63 THEN 'binary'
                        ELSE NULL
                        END
                          AS CHAR(64)
                      ) AS COLLATION_NAME,
                      CAST(
                        CASE
                        WHEN rp.param_type IN (1, 2, 3, 4, 5) THEN CONCAT(
                          lower(v.data_type_str),
                          '(',
                          rp.param_precision,
                          ')'
                        )
                        WHEN rp.param_type IN (15, 16, 50) THEN CONCAT(
                          lower(v.data_type_str),
                          '(',
                          rp.param_precision,
                          ',',
                          rp.param_scale,
                          ')'
                        )
                        WHEN rp.param_type IN (18, 20) THEN CONCAT(lower(v.data_type_str), '(', rp.param_scale, ')')
                        WHEN rp.param_type IN (22, 23) and rp.param_length > 0 THEN CONCAT(lower(v.data_type_str), '(', rp.param_length, ')')
                        WHEN rp.param_type IN (32, 33)
                        THEN get_mysql_routine_parameter_type_str(rp.routine_id, rp.param_position)
                        WHEN rp.param_type = 41 THEN lower('DATE')
                        WHEN rp.param_type = 42 THEN lower('DATETIME')
                        ELSE lower(v.data_type_str)
                        END
                          AS CHAR(4194304)
                      ) AS DTD_IDENTIFIER,
                      CAST('SQL' AS CHAR(8)) as ROUTINE_BODY,
                      CAST(mp.body AS CHAR(4194304)) as ROUTINE_DEFINITION,
                      CAST(NULL AS CHAR(64)) as EXTERNAL_NAME,
                      CAST(NULL AS CHAR(64)) as EXTERNAL_LANGUAGE,
                      CAST('SQL' AS CHAR(8)) as PARAMETER_STYLE,
                      CAST(mp.IS_DETERMINISTIC AS CHAR(3)) AS IS_DETERMINISTIC,
                      CAST(mp.SQL_DATA_ACCESS AS CHAR(64)) AS SQL_DATA_ACCESS,
                      CAST(NULL AS CHAR(64)) as SQL_PATH,
                      CAST(mp.SECURITY_TYPE AS CHAR(7)) as SECURITY_TYPE,
                      CAST(r.gmt_create AS datetime) as CREATED,
                      CAST(r.gmt_modified AS datetime) as LAST_ALTERED,
                      CAST(mp.SQL_MODE AS CHAR(8192)) as SQL_MODE,
                      CAST(mp.comment AS CHAR(4194304)) as ROUTINE_COMMENT,
                      CAST(mp.DEFINER AS CHAR(93)) as DEFINER,
                      CAST(mp.CHARACTER_SET_CLIENT AS CHAR(32)) as CHARACTER_SET_CLIENT,
                      CAST(mp.COLLATION_CONNECTION AS CHAR(32)) as COLLATION_CONNECTION,
                      CAST(mp.db_collation AS CHAR(32)) as DATABASE_COLLATION
                    from
                      mysql.proc as mp
                      join oceanbase.__all_database a
                      on mp.DB = a.DATABASE_NAME
                      and  a.in_recyclebin = 0
                      join oceanbase.__all_routine as r on mp.specific_name = r.routine_name
                      and r.DATABASE_ID = a.DATABASE_ID
                      and
                      CAST(
                        CASE r.routine_type
                        WHEN 1 THEN 'PROCEDURE'
                        WHEN 2 THEN 'FUNCTION'
                        ELSE NULL
                        END
                          AS CHAR(9)
                      ) = mp.type
                      left join oceanbase.__all_routine_param as rp on rp.subprogram_id = r.subprogram_id
                      and rp.routine_id = r.routine_id
                      and rp.param_position = 0
                      left join oceanbase.__all_virtual_data_type v on rp.param_type = v.data_type
                    where (0 = sys_privilege_check('routine_acc', 1)
                           or 0 = sys_privilege_check('routine_acc', 1, mp.DB, r.routine_name, r.routine_type))
                    """.replace("\n", " ")
)

# 20016: PROFILING
# 20017: OPTIMIZER_TRACE
# 20018: PLUGINS
# 20019: INNODB_SYS_COLUMNS

def_table_schema(
  owner = 'ailing.lcq',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'PROFILING',
  table_id        = '20016',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT  CAST(00000000000000000000 as SIGNED) as QUERY_ID,
            CAST(00000000000000000000 as SIGNED) as SEQ,
            CAST('' as CHAR(30)) as STATE,
            CAST(0.000000 as DECIMAL(9, 6)) as DURATION,
            CAST(NULL as DECIMAL(9, 6)) as CPU_USER,
            CAST(NULL as DECIMAL(9, 6)) as CPU_SYSTEM,
            CAST(00000000000000000000 as SIGNED) as CONTEXT_VOLUNTARY,
            CAST(00000000000000000000 as SIGNED) as CONTEXT_INVOLUNTARY,
            CAST(00000000000000000000 as SIGNED) as BLOCK_OPS_IN,
            CAST(00000000000000000000 as SIGNED) as BLOCK_OPS_OUT,
            CAST(00000000000000000000 as SIGNED) as MESSAGES_SENT,
            CAST(00000000000000000000 as SIGNED) as MESSAGES_RECEIVED,
            CAST(00000000000000000000 as SIGNED) as PAGE_FAULTS_MAJOR,
            CAST(00000000000000000000 as SIGNED) as PAGE_FAULTS_MINOR,
            CAST(00000000000000000000 as SIGNED) as SWAPS,
            CAST(NULL as CHAR(30)) as SOURCE_FUNCTION,
            CAST(NULL as CHAR(20)) as SOURCE_FILE,
            CAST(00000000000000000000 as SIGNED) as SOURCE_LINE
    FROM DUAL limit 0;
""".replace("\n", " ")
)


def_table_schema(
  owner           = 'sanquan.qz',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'OPTIMIZER_TRACE',
  table_id        = '20017',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT CAST('query'              as CHAR(200)) as QUERY,
           CAST('trace'              as CHAR(200)) as TRACE,
           CAST(00000000000000000000 as SIGNED) as MISSING_BYTES_MAX_MEM_SIZE,
           CAST(0 as SIGNED) as INSUFFICIENT_PRIVILEGES
    FROM DUAL limit 0;
  """.replace("\n", " ")
)

def_table_schema(
  owner         = 'sanquan.qz',
  tablegroup_id = 'OB_INVALID_ID',
  database_id   = 'OB_INFORMATION_SCHEMA_ID',
  table_name    = 'PLUGINS',
  table_id      = '20018',
  table_type    = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT CAST('plugin name'    as CHAR(64)) as PLUGIN_NAME,
           CAST('version'        as CHAR(20)) as PLUGIN,
           CAST('plugin status'  as CHAR(10)) as PLUGIN_STATUS,
           CAST('type'           as CHAR(80)) as PLUGIN_TYPE,
           CAST('version'        as CHAR(20)) as PLUGIN_TYPE_VERSION,
           CAST('library'        as CHAR(64)) as PLUGIN_LIBRARY,
           CAST('lib version'    as CHAR(20)) as PLUGIN_LIBRARY_VERSION,
           CAST('author'         as CHAR(64)) as PLUGIN_AUTHOR,
           CAST('description'    as CHAR(200)) as PLUGIN_DESCRIPTION,
           CAST('license'        as CHAR(80)) as PLUGIN_LICENSE,
           CAST('load option'    as CHAR(64)) as LOAD_OPTION
    FROM DUAL limit 0;
  """.replace("\n", " ")
)

def_table_schema(
  owner         = 'sanquan.qz',
  tablegroup_id = 'OB_INVALID_ID',
  database_id   = 'OB_INFORMATION_SCHEMA_ID',
  table_name    = 'INNODB_SYS_COLUMNS',
  table_id      = '20019',
  table_type    = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT CAST(000000000000000000000 as UNSIGNED) AS TABLE_ID,
           CAST('name'               as CHAR(193)) AS NAME,
           CAST(000000000000000000000 as UNSIGNED) AS POS,
           CAST(00000000000 as SIGNED) AS MTYPE,
           CAST(00000000000 as SIGNED) AS PRTYPE,
           CAST(00000000000 as SIGNED) AS LEN
    FROM DUAL limit 0;
  """.replace("\n", " ")
)


def_table_schema(
  owner           = 'sanquan.qz',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_FT_BEING_DELETED',
  table_id        = '20020',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT CAST(000000000000000000000 as UNSIGNED) AS DOC_ID
    FROM DUAL limit 0;
  """.replace("\n", " ")
)

def_table_schema(
  owner           = 'sanquan.qz',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_FT_CONFIG',
  table_id        = '20021',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT CAST('key'               as CHAR(100)) AS FT_CONFIG_KEY,
           CAST('value'             as CHAR(100)) AS VALUE
    FROM DUAL limit 0;
  """.replace("\n", " ")
)

def_table_schema(
  owner           = 'sanquan.qz',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_FT_DELETED',
  table_id        = '20022',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT CAST(000000000000000000000 as UNSIGNED) AS DOC_ID
    FROM DUAL limit 0;
  """.replace("\n", " ")
)

def_table_schema(
  owner           = 'sanquan.qz',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_FT_INDEX_CACHE',
  table_id        = '20023',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT CAST('word'               as CHAR(193)) AS WORD,
           CAST(000000000000000000000 as UNSIGNED) AS FIRST_DOC_ID,
           CAST(000000000000000000000 as UNSIGNED) AS LAST_DOC_ID,
           CAST(000000000000000000000 as UNSIGNED) AS DOC_COUNT,
           CAST(000000000000000000000 as UNSIGNED) AS DOC_ID,
           CAST(000000000000000000000 as UNSIGNED) AS POSITION
    FROM DUAL limit 0;
  """.replace("\n", " ")
)

# 21000: GV$SESSION_EVENT # removed

# 21001: GV$SESSION_WAIT # removed

# 21002: GV$SESSION_WAIT_HISTORY # removed

# 21003: GV$SYSTEM_EVENT # removed

# 21004: GV$SESSTAT # removed

# 21005: GV$SYSSTAT # removed

# 21006: V$STATNAME # removed

# 21007: V$EVENT_NAME # removed

# 21008: V$SESSION_EVENT # removed

# 21009: V$SESSION_WAIT # removed

# 21010: V$SESSION_WAIT_HISTORY # removed

# 21011: V$SESSTAT # removed

# 21012: V$SYSSTAT # removed

# 21013: V$SYSTEM_EVENT # removed

# 21014: GV$OB_SQL_AUDIT # removed

# 21015: GV$LATCH # removed

# 21016: GV$OB_MEMORY # removed (single-tenant GV/V collapse; folded into V$OB_MEMORY)

def_table_schema(
  owner = 'nijia.nj',
  table_name      = 'V$OB_MEMORY',
  table_id        = '21017',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
SELECT
     ctx_name AS CTX_NAME,
     mod_name AS MOD_NAME,
     sum(COUNT) AS COUNT,
     sum(hold) AS HOLD,
     sum(USED) AS USED
FROM
    oceanbase.__all_virtual_memory_info
WHERE
        mod_type='user'
GROUP BY ctx_name, mod_name
ORDER BY ctx_name, mod_name
""".replace("\n", " ")
)

# 21018: GV$OB_MEMSTORE # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_memstore_info)

# 21019: V$OB_MEMSTORE # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_memstore_info)

# 21020: GV$OB_MEMSTORE_INFO # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_tablet_memstore_info)

# 21021: V$OB_MEMSTORE_INFO # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_tablet_memstore_info)

# 21022: V$OB_PLAN_CACHE_STAT # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_plan_cache_stat)

def_table_schema(
    owner = 'xiaoyi.xy',
    table_name     = 'V$OB_PLAN_CACHE_PLAN_STAT',
    table_id       = '21023',
    table_type = 'SYSTEM_VIEW',
    gm_columns = [],
    in_tenant_space = True,
    rowkey_columns = [],
    view_definition = """
    SELECT PLAN_ID,SQL_ID,TYPE,IS_BIND_SENSITIVE,IS_BIND_AWARE,
    DB_ID,STATEMENT,QUERY_SQL,SPECIAL_PARAMS,PARAM_INFOS, SYS_VARS, CONFIGS, PLAN_HASH,
    FIRST_LOAD_TIME,SCHEMA_VERSION,LAST_ACTIVE_TIME,AVG_EXE_USEC,SLOWEST_EXE_TIME,SLOWEST_EXE_USEC,
    SLOW_COUNT,HIT_COUNT,PLAN_SIZE,EXECUTIONS,DISK_READS,DIRECT_WRITES,BUFFER_GETS,APPLICATION_WAIT_TIME,
    CONCURRENCY_WAIT_TIME,USER_IO_WAIT_TIME,ROWS_PROCESSED,ELAPSED_TIME,CPU_TIME,
    DELAYED_PX_QUERYS,OUTLINE_VERSION,OUTLINE_ID,OUTLINE_DATA,ACS_SEL_INFO,
    TABLE_SCAN,EVOLUTION, EVO_EXECUTIONS, EVO_CPU_TIME, TIMEOUT_COUNT, PS_STMT_ID, SESSID,
    TEMP_TABLES, OBJECT_TYPE,HINTS_INFO,HINTS_ALL_WORKED, PL_SCHEMA_ID,
    IS_BATCHED_MULTI_STMT, RULE_NAME,
    (CASE PLAN_STATUS WHEN 0 THEN 'ACTIVE' ELSE 'INACTIVE' END) AS PLAN_STATUS,
    ADAPTIVE_FEEDBACK_TIMES, FIRST_GET_PLAN_TIME, FIRST_EXE_USEC
    FROM oceanbase.__all_virtual_plan_stat WHERE OBJECT_STATUS = 0 AND is_in_pc=true
""".replace("\n", " "),


    normal_columns = [
    ]
  )

# 21024: GV$OB_PLAN_CACHE_PLAN_EXPLAIN # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_plan_cache_plan_explain)

# 21025: V$OB_PLAN_CACHE_PLAN_EXPLAIN # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_plan_cache_plan_explain)

# 21026: V$OB_SQL_AUDIT # removed

# 21027: V$LATCH # removed

# 21028: GV$OB_RPC_OUTGOING (abandoned)
# 21029: V$OB_RPC_OUTGOING (abandoned)
# 21030: GV$OB_RPC_INCOMING (abandoned)
# 21031: V$OB_RPC_INCOMING (abandoned)

# 21032: GV$SQL # abandoned in 4.0
# 21033: V$SQL # abandoned in 4.0

# rename to DBA_RECYCLEBIN
def_table_schema(
  owner = 'bin.lb',
  table_name      = 'DBA_RECYCLEBIN',
  table_id        = '21038',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
  CAST(B.DATABASE_NAME AS CHAR(128)) AS OWNER,
  CAST(A.OBJECT_NAME AS CHAR(128)) AS OBJECT_NAME,
  CAST(A.ORIGINAL_NAME AS CHAR(128)) AS ORIGINAL_NAME,
  CAST(NULL AS CHAR(9)) AS OPERATION,
  CAST(CASE A.TYPE
       WHEN 1 THEN 'TABLE'
       WHEN 2 THEN 'NORMAL INDEX'
       WHEN 3 THEN 'VIEW'
       ELSE NULL END AS CHAR(25)) AS TYPE,
  CAST(NULL AS CHAR(30)) AS TS_NAME,
  CAST(C.GMT_CREATE AS DATE) AS CREATETIME,
  CAST(C.GMT_MODIFIED AS DATE) AS DROPTIME,
  CAST(NULL AS SIGNED) AS DROPSCN,
  CAST(NULL AS CHAR(128)) AS PARTITION_NAME,
  CAST('YES' AS CHAR(3)) AS CAN_UNDROP,
  CAST('YES' AS CHAR(3)) AS CAN_PURGE,
  CAST(NULL AS SIGNED) AS RELATED,
  CAST(NULL AS SIGNED) AS BASE_OBJECT,
  CAST(NULL AS SIGNED) AS PURGE_OBJECT,
  CAST(NULL AS SIGNED) AS SPACE
  FROM OCEANBASE.__ALL_RECYCLEBIN A
  JOIN OCEANBASE.__ALL_DATABASE B
    ON A.DATABASE_ID = B.DATABASE_ID
  JOIN OCEANBASE.__ALL_TABLE C
    ON A.TABLE_ID = C.TABLE_ID
  WHERE A.TYPE IN (1, 2, 3)
    AND C.TABLE_MODE >> 12 & 15 in (0,1)
    AND C.INDEX_ATTRIBUTES_SET & 16 = 0

  UNION ALL

  SELECT
  CAST(A.ORIGINAL_NAME AS CHAR(128)) AS OWNER,
  CAST(A.OBJECT_NAME AS CHAR(128)) AS OBJECT_NAME,
  CAST(A.ORIGINAL_NAME AS CHAR(128)) AS ORIGINAL_NAME,
  CAST(NULL AS CHAR(9)) AS OPERATION,
  CAST('DATABASE' AS CHAR(25)) AS TYPE,
  CAST(NULL AS CHAR(30)) AS TS_NAME,
  CAST(B.GMT_CREATE AS DATE) AS CREATETIME,
  CAST(B.GMT_MODIFIED AS DATE) AS DROPTIME,
  CAST(NULL AS SIGNED) AS DROPSCN,
  CAST(NULL AS CHAR(128)) AS PARTITION_NAME,
  CAST('YES' AS CHAR(3)) AS CAN_UNDROP,
  CAST('YES' AS CHAR(3)) AS CAN_PURGE,
  CAST(NULL AS SIGNED) AS RELATED,
  CAST(NULL AS SIGNED) AS BASE_OBJECT,
  CAST(NULL AS SIGNED) AS PURGE_OBJECT,
  CAST(NULL AS SIGNED) AS SPACE
  FROM OCEANBASE.__ALL_RECYCLEBIN A
  JOIN OCEANBASE.__ALL_DATABASE B
    ON A.DATABASE_ID = B.DATABASE_ID
  WHERE A.TYPE = 4

  UNION ALL

  SELECT
  CAST(B.DATABASE_NAME AS CHAR(128)) AS OWNER,
  CAST(A.OBJECT_NAME AS CHAR(128)) AS OBJECT_NAME,
  CAST(A.ORIGINAL_NAME AS CHAR(128)) AS ORIGINAL_NAME,
  CAST(NULL AS CHAR(9)) AS OPERATION,
  CAST('TRIGGER' AS CHAR(25)) AS TYPE,
  CAST(NULL AS CHAR(30)) AS TS_NAME,
  CAST(C.GMT_CREATE AS DATE) AS CREATETIME,
  CAST(C.GMT_MODIFIED AS DATE) AS DROPTIME,
  CAST(NULL AS SIGNED) AS DROPSCN,
  CAST(NULL AS CHAR(128)) AS PARTITION_NAME,
  CAST('YES' AS CHAR(3)) AS CAN_UNDROP,
  CAST('YES' AS CHAR(3)) AS CAN_PURGE,
  CAST(NULL AS SIGNED) AS RELATED,
  CAST(NULL AS SIGNED) AS BASE_OBJECT,
  CAST(NULL AS SIGNED) AS PURGE_OBJECT,
  CAST(NULL AS SIGNED) AS SPACE
  FROM OCEANBASE.__ALL_RECYCLEBIN A
  JOIN OCEANBASE.__ALL_DATABASE B
    ON A.DATABASE_ID = B.DATABASE_ID
  JOIN OCEANBASE.__ALL_TRIGGER C
    ON A.TABLE_ID = C.TRIGGER_ID
  WHERE A.TYPE = 6

  UNION ALL

  SELECT
  CAST(NULL AS CHAR(128)) AS OWNER,
  CAST(A.OBJECT_NAME AS CHAR(128)) AS OBJECT_NAME,
  CAST(A.ORIGINAL_NAME AS CHAR(128)) AS ORIGINAL_NAME,
  CAST(NULL AS CHAR(9)) AS OPERATION,
  CAST('TENANT' AS CHAR(25)) AS TYPE,
  CAST(NULL AS CHAR(30)) AS TS_NAME,
  CAST(USEC_TO_TIME(B.SCHEMA_VERSION) AS DATE) AS CREATETIME,
  CAST(A.GMT_CREATE AS DATE) AS DROPTIME,
  CAST(NULL AS SIGNED) AS DROPSCN,
  CAST(NULL AS CHAR(128)) AS PARTITION_NAME,
  CAST('YES' AS CHAR(3)) AS CAN_UNDROP,
  CAST('YES' AS CHAR(3)) AS CAN_PURGE,
  CAST(NULL AS SIGNED) AS RELATED,
  CAST(NULL AS SIGNED) AS BASE_OBJECT,
  CAST(NULL AS SIGNED) AS PURGE_OBJECT,
  CAST(NULL AS SIGNED) AS SPACE
  FROM OCEANBASE.__ALL_RECYCLEBIN A
  JOIN OCEANBASE.__ALL_VIRTUAL_CORE_ALL_TABLE B
    ON B.TABLE_NAME = '__all_core_table'
  WHERE A.TYPE = 7
""".replace("\n", " ")
  )

# 21039: GV$OB_OUTLINES # abandoned in 4.0
# 21040: GV$OB_CONCURRENT_LIMIT_SQL # abandoned in 4.0
# 21041: GV$SQL_PLAN_STATISTICS # abandoned in 4.0
# 21042: V$SQL_PLAN_STATISTICS # abandoned in 4.0

def_table_schema(
  owner = 'dachuan.sdc',
  database_id    = 'OB_MYSQL_SCHEMA_ID',
  table_name      = 'time_zone_name',
  table_id        = '21055',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT name as Name,
           time_zone_id as Time_zone_id
    FROM oceanbase.__all_time_zone_name
""".replace("\n", " ")
)

def_table_schema(
  owner = 'dachuan.sdc',
  database_id    = 'OB_MYSQL_SCHEMA_ID',
  table_name      = 'time_zone_transition',
  table_id        = '21056',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT time_zone_id as Time_zone_id,
           transition_time as Transition_time,
           transition_type_id as Transition_type_id
    FROM oceanbase.__all_time_zone_transition
""".replace("\n", " ")
)

def_table_schema(
  owner = 'dachuan.sdc',
  database_id    = 'OB_MYSQL_SCHEMA_ID',
  table_name      = 'time_zone_transition_type',
  table_id        = '21057',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT time_zone_id as Time_zone_id,
           transition_type_id as Transition_type_id,
           offset as Offset,
           is_dst as Is_DST,
           abbreviation as Abbreviation
    FROM oceanbase.__all_time_zone_transition_type
""".replace("\n", " ")
)

# 21059: GV$SESSION_LONGOPS # removed (single-tenant GV/V collapse; folded into V$SESSION_LONGOPS)

def_table_schema(
  owner = 'zhenjiang.xzj',
  table_name      = 'V$SESSION_LONGOPS',
  table_id        = '21060',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT CAST(sid AS SIGNED) AS SID,
           CAST(trace_id AS CHAR(64)) AS TRACE_ID,
           CAST(op_name AS CHAR(64)) AS OPNAME,
           CAST(TARGET AS CHAR(64)) AS TARGET,
           CAST(USEC_TO_TIME(START_TIME) AS DATETIME) AS START_TIME,
           CAST(ELAPSED_TIME/1000000 AS SIGNED) AS ELAPSED_SECONDS,
           CAST(REMAINING_TIME AS SIGNED) AS TIME_REMAINING,
           CAST(USEC_TO_TIME(LAST_UPDATE_TIME) AS DATETIME) AS LAST_UPDATE_TIME,
           CAST(MESSAGE AS CHAR(512)) AS MESSAGE
    FROM oceanbase.__all_virtual_long_ops_status
""".replace("\n", " ")
)

def_table_schema(
  owner = 'xiaochu.yh',
  table_name      = 'DBA_OB_SEQUENCE_OBJECTS',
  table_id        = '21066',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT
      SEQUENCE_ID,
      SCHEMA_VERSION,
      DATABASE_ID,
      SEQUENCE_NAME,
      MIN_VALUE,
      MAX_VALUE,
      INCREMENT_BY,
      START_WITH,
      CACHE_SIZE,
      ORDER_FLAG,
      CYCLE_FLAG,
      IS_SYSTEM_GENERATED
    FROM oceanbase.__all_sequence_object
""".replace("\n", " ")
)

# 21067: abandoned

def_table_schema(
  owner = 'bin.lb',
  tablegroup_id = 'OB_INVALID_ID',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'COLUMNS',
  table_id       = '21068',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  view_definition = """
SELECT /*+LEADING((D T) VC) USE_NL(VC) NO_USE_NL_MATERIALIZATION(VC)*/
       VC.TABLE_CATALOG,
       D.DATABASE_NAME collate utf8mb4_name_case AS TABLE_SCHEMA,
       T.TABLE_NAME collate utf8mb4_name_case AS TABLE_NAME,
       VC.COLUMN_NAME,
       VC.ORDINAL_POSITION,
       VC.COLUMN_DEFAULT,
       VC.IS_NULLABLE,
       VC.DATA_TYPE,
       VC.CHARACTER_MAXIMUM_LENGTH,
       VC.CHARACTER_OCTET_LENGTH,
       VC.NUMERIC_PRECISION,
       VC.NUMERIC_SCALE,
       VC.DATETIME_PRECISION,
       VC.CHARACTER_SET_NAME,
       VC.COLLATION_NAME,
       VC.COLUMN_TYPE,
       VC.COLUMN_KEY,
       VC.EXTRA,
       VC.PRIVILEGES,
       VC.COLUMN_COMMENT,
       VC.GENERATION_EXPRESSION,
       VC.SRS_ID FROM OCEANBASE.__ALL_TABLE T INNER JOIN OCEANBASE.__ALL_DATABASE D INNER JOIN OCEANBASE.__ALL_VIRTUAL_INFORMATION_COLUMNS VC
WHERE (T.OBJECT_STATUS = 0 OR (T.TABLE_ID > 20000 AND T.TABLE_ID < 30000) OR (T.GMT_CREATE != T.GMT_MODIFIED AND T.TABLE_TYPE = 3))
      AND T.DATABASE_ID = D.DATABASE_ID
      AND D.DATABASE_NAME = VC.TABLE_SCHEMA
      AND T.TABLE_NAME = VC.TABLE_NAME
      AND D.IN_RECYCLEBIN = 0
      AND 0 = sys_privilege_check('table_acc', 1, D.DATABASE_NAME, T.TABLE_NAME)
UNION ALL
SELECT /*+LEADING((D T) VC) USE_NL(VC) NO_USE_NL_MATERIALIZATION(VC)*/
       VC.TABLE_CATALOG,
       D.DATABASE_NAME collate utf8mb4_name_case AS TABLE_SCHEMA,
       T.TABLE_NAME collate utf8mb4_name_case AS TABLE_NAME,
       VC.COLUMN_NAME,
       VC.ORDINAL_POSITION,
       VC.COLUMN_DEFAULT,
       VC.IS_NULLABLE,
       VC.DATA_TYPE,
       VC.CHARACTER_MAXIMUM_LENGTH,
       VC.CHARACTER_OCTET_LENGTH,
       VC.NUMERIC_PRECISION,
       VC.NUMERIC_SCALE,
       VC.DATETIME_PRECISION,
       VC.CHARACTER_SET_NAME,
       VC.COLLATION_NAME,
       VC.COLUMN_TYPE,
       VC.COLUMN_KEY,
       VC.EXTRA,
       VC.PRIVILEGES,
       VC.COLUMN_COMMENT,
       VC.GENERATION_EXPRESSION,
       VC.SRS_ID FROM (SELECT 1 AS TABLE_ID, 201001 AS DATABASE_ID, '__all_core_table' AS TABLE_NAME FROM DUAL
             UNION ALL SELECT 3 AS TABLE_ID, 201001 AS DATABASE_ID, '__all_table' AS TABLE_NAME FROM DUAL
             UNION ALL SELECT 4 AS TABLE_ID, 201001 AS DATABASE_ID, '__all_column' AS TABLE_NAME FROM DUAL
             UNION ALL SELECT 5 AS TABLE_ID, 201001 AS DATABASE_ID, '__all_ddl_operation' AS TABLE_NAME FROM DUAL) T INNER JOIN OCEANBASE.__ALL_DATABASE D INNER JOIN OCEANBASE.__ALL_VIRTUAL_INFORMATION_COLUMNS VC
WHERE T.DATABASE_ID = D.DATABASE_ID
      AND D.DATABASE_NAME = VC.TABLE_SCHEMA
      AND T.TABLE_NAME = VC.TABLE_NAME
      AND 0 = sys_privilege_check('table_acc', 1, D.DATABASE_NAME, T.TABLE_NAME)
UNION ALL
      SELECT CAST ("def" AS CHAR(512)) AS TABLE_CATALOG,
       D.DATABASE_NAME collate utf8mb4_name_case AS TABLE_SCHEMA,
       T.TABLE_NAME collate utf8mb4_name_case AS TABLE_NAME,
       C.COLUMN_NAME AS COLUMN_NAME,
       ROW_NUMBER() OVER (PARTITION BY D.DATABASE_NAME, T.TABLE_NAME, T.TABLE_ID ORDER BY C.COLUMN_ID) AS ORDINAL_POSITION,
       inner_info_cols_column_def_printer(T.TABLE_ID, C.COLUMN_ID) AS COLUMN_DEFAULT,
       CASE C.NULLABLE WHEN 1 THEN "YES" ELSE "NO" END AS IS_NULLABLE,
       inner_info_cols_data_type_printer(C.DATA_TYPE, C.COLLATION_TYPE, C.EXTENDED_TYPE_INFO, C.SRS_ID) AS DATA_TYPE,
       CAST (CASE WHEN (C.DATA_TYPE = 22 OR C.DATA_TYPE = 23 OR (C.DATA_TYPE >= 27 AND C.DATA_TYPE <= 30) OR C.DATA_TYPE = 36 OR C.DATA_TYPE = 37) THEN C.DATA_LENGTH ELSE NULL END AS UNSIGNED)  AS CHARACTER_MAXIMUM_LENGTH,
       inner_info_cols_char_len_printer(C.DATA_TYPE, C.COLLATION_TYPE, C.DATA_LENGTH) AS CHARACTER_OCTET_LENGTH,
       CAST (CASE WHEN (C.DATA_SCALE < 0 AND (C.DATA_TYPE = 11 OR C.DATA_TYPE = 13)) THEN 12 WHEN (C.DATA_SCALE < 0 AND (C.DATA_TYPE = 12 OR C.DATA_TYPE = 14)) THEN 22 WHEN (((C.DATA_TYPE >= 1 AND C.DATA_TYPE <= 16) OR C.DATA_TYPE = 31 OR C.DATA_TYPE = 39) AND C.DATA_PRECISION >= 0) THEN C.DATA_PRECISION ELSE NULL END AS UNSIGNED) AS NUMERIC_PRECISION,
       CAST (CASE WHEN (((C.DATA_TYPE >= 1 AND C.DATA_TYPE <= 16) OR C.DATA_TYPE = 31 OR C.DATA_TYPE = 39) AND C.DATA_SCALE >= 0) THEN C.DATA_SCALE ELSE NULL END AS UNSIGNED) AS NUMERIC_SCALE,
       CAST (CASE WHEN (C.DATA_TYPE = 17 OR C.DATA_TYPE = 18 OR C.DATA_TYPE = 20 OR C.DATA_TYPE = 42) THEN C.DATA_SCALE ELSE NULL END AS UNSIGNED) AS DATETIME_PRECISION,
       inner_info_cols_char_name_printer(C.DATA_TYPE, C.COLLATION_TYPE) AS CHARACTER_SET_NAME,
       inner_info_cols_coll_name_printer(C.DATA_TYPE, C.COLLATION_TYPE) AS COLLATION_NAME,
       inner_info_cols_column_type_printer(C.DATA_TYPE, C.SUB_DATA_TYPE, C.SRS_ID, C.COLLATION_TYPE, C.DATA_SCALE, C.DATA_LENGTH, C.DATA_PRECISION, C.ZERO_FILL, C.EXTENDED_TYPE_INFO, C.COLUMN_FLAGS & (0x1 << 29)) AS COLUMN_TYPE,
       inner_info_cols_column_key_printer(T.TABLE_ID, C.COLUMN_ID) AS COLUMN_KEY,
       inner_info_cols_extra_printer(C.AUTOINCREMENT, C.ON_UPDATE_CURRENT_TIMESTAMP, C.DATA_SCALE, C.COLUMN_FLAGS) AS EXTRA,
       inner_info_cols_priv_printer(D.DATABASE_NAME, T.TABLE_NAME) AS PRIVILEGES,
       C.COMMENT AS COLUMN_COMMENT,
       CASE WHEN (C.COLUMN_FLAGS & 0x3) THEN CAST(C.ORIG_DEFAULT_VALUE_V2 AS CHAR(4194304)) ELSE "" END AS GENERATION_EXPRESSION,
       CAST(CASE WHEN (C.SRS_ID >> 32 = ((2 << 31) - 1)) THEN NULL ELSE C.SRS_ID >> 32 END AS UNSIGNED) AS SRS_ID FROM OCEANBASE.__ALL_TABLE T INNER JOIN OCEANBASE.__ALL_DATABASE D INNER JOIN OCEANBASE.__ALL_COLUMN C
WHERE T.TABLE_ID = C.TABLE_ID
      AND T.DATABASE_ID = D.DATABASE_ID
      AND D.DATABASE_ID != 201004
      AND D.IN_RECYCLEBIN = 0
      AND T.OBJECT_STATUS = 1
      AND T.TABLE_TYPE != 5
      AND T.TABLE_TYPE != 6
      AND T.TABLE_TYPE != 8
      AND T.TABLE_TYPE != 9
      AND T.TABLE_TYPE != 11
      AND T.TABLE_TYPE != 12
      AND T.TABLE_TYPE != 13
      AND C.IS_HIDDEN = 0
      AND (T.TABLE_ID < 20000 OR T.TABLE_ID > 30000)
      AND (T.GMT_CREATE = T.GMT_MODIFIED OR T.TABLE_TYPE != 3)
      AND 0 = sys_privilege_check('table_acc', 1, D.DATABASE_NAME, T.TABLE_NAME)""",
  in_tenant_space = True,
  normal_columns = [ ]
  )

# 21071: GV$OB_PX_WORKER_STAT # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_px_worker_stat)

# 21072: V$OB_PX_WORKER_STAT # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_px_worker_stat)

# 21073: gv$partition_audit # abandoned in 4.0
# 21074: v$partition_audit # abandoned in 4.0
# 21075: V$OB_CLUSTER # abandoned in 4.0
# 21076: v$ob_standby_status # abandoned in 4.0
# 21077: v$ob_cluster_stats # abandoned in 4.0
# 21078: V$OB_CLUSTER_EVENT_HISTORY # abandoned in 4.0

# 21079: GV$OB_PS_STAT # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_ps_stat)

# 21080: V$OB_PS_STAT # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_ps_stat)

# 21081: GV$OB_PS_ITEM_INFO # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_ps_item_info)

# 21082: V$OB_PS_ITEM_INFO # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_ps_item_info)


# 21083: GV$SQL_WORKAREA # removed (single-tenant GV/V collapse; folded into V$SQL_WORKAREA)

def_table_schema(
  owner = 'longzhong.wlz',
  table_name      = 'V$SQL_WORKAREA',
  table_id        = '21084',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT
      CAST(NULL AS BINARY(8)) AS ADDRESS,
      CAST(NULL AS SIGNED) AS HASH_VALUE,
      DB_ID,
      SQL_ID,
      CAST(PLAN_ID AS SIGNED) AS CHILD_NUMBER,
      CAST(NULL AS BINARY(8)) AS WORKAREA_ADDRESS,
      OPERATION_TYPE,
      OPERATION_ID,
      POLICY,
      ESTIMATED_OPTIMAL_SIZE,
      ESTIMATED_ONEPASS_SIZE,
      LAST_MEMORY_USED,
      LAST_EXECUTION,
      LAST_DEGREE,
      TOTAL_EXECUTIONS,
      OPTIMAL_EXECUTIONS,
      ONEPASS_EXECUTIONS,
      MULTIPASSES_EXECUTIONS,
      ACTIVE_TIME,
      MAX_TEMPSEG_SIZE,
      LAST_TEMPSEG_SIZE,
      1 AS CON_ID
    FROM OCEANBASE.__ALL_VIRTUAL_SQL_WORKAREA_HISTORY_STAT
""".replace("\n", " ")
)

# 21085: GV$SQL_WORKAREA_ACTIVE # removed (single-tenant GV/V collapse; folded into V$SQL_WORKAREA_ACTIVE)

def_table_schema(
  owner = 'longzhong.wlz',
  table_name      = 'V$SQL_WORKAREA_ACTIVE',
  table_id        = '21086',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT
      CAST(NULL AS SIGNED) AS SQL_HASH_VALUE,
      DB_ID,
      SQL_ID,
      CAST(NULL AS DATE) AS SQL_EXEC_START,
      SQL_EXEC_ID,
      CAST(NULL AS BINARY(8)) AS WORKAREA_ADDRESS,
      OPERATION_TYPE,
      OPERATION_ID,
      POLICY,
      SID,
      CAST(NULL AS SIGNED) AS QCINST_ID,
      CAST(NULL AS SIGNED) AS QCSID,
      ACTIVE_TIME,
      WORK_AREA_SIZE,
      EXPECT_SIZE,
      ACTUAL_MEM_USED,
      MAX_MEM_USED,
      NUMBER_PASSES,
      TEMPSEG_SIZE,
      CAST(NULL AS CHAR(20)) AS TABLESPACE,
      CAST(NULL AS SIGNED) AS `SEGRFNO#`,
      CAST(NULL AS SIGNED) AS `SEGBLK#`,
      1 AS CON_ID
    FROM OCEANBASE.__ALL_VIRTUAL_SQL_WORKAREA_ACTIVE
""".replace("\n", " ")
)

# 21087: GV$SQL_WORKAREA_HISTOGRAM # removed (single-tenant GV/V collapse; folded into V$SQL_WORKAREA_HISTOGRAM)

def_table_schema(
  owner = 'longzhong.wlz',
  table_name      = 'V$SQL_WORKAREA_HISTOGRAM',
  table_id        = '21088',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT
      LOW_OPTIMAL_SIZE,
      HIGH_OPTIMAL_SIZE,
      OPTIMAL_EXECUTIONS,
      ONEPASS_EXECUTIONS,
      MULTIPASSES_EXECUTIONS,
      TOTAL_EXECUTIONS,
      1 AS CON_ID
    FROM OCEANBASE.__ALL_VIRTUAL_SQL_WORKAREA_HISTOGRAM
""".replace("\n", " ")
)

# 21089: GV$OB_SQL_WORKAREA_MEMORY_INFO # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_sql_workarea_memory_info)

# 21090: V$OB_SQL_WORKAREA_MEMORY_INFO # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_sql_workarea_memory_info)

# 21097: GV$OB_PLAN_CACHE_REFERENCE_INFO # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_plan_cache_stat)

# 21098: V$OB_PLAN_CACHE_REFERENCE_INFO # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_plan_cache_stat)

# 21100: GV$OB_SSTABLES # removed (single-tenant GV/V collapse; folded into V$OB_SSTABLES)

def_table_schema(
owner = 'baichangmin.bcm',
table_name      = 'V$OB_SSTABLES',
table_id        = '21101',
table_type      = 'SYSTEM_VIEW',
rowkey_columns  = [],
normal_columns  = [],
gm_columns      = [],
in_tenant_space = True,
view_definition = """
SELECT
 (case M.TABLE_TYPE
    when 0 then 'MEMTABLE' when 1 then 'TX_DATA_MEMTABLE' when 2 then 'TX_CTX_MEMTABLE'
    when 3 then 'LOCK_MEMTABLE' when 10 then 'MAJOR' when 11 then 'MINOR'
    when 12 then 'MINI' when 13 then 'META'
    when 14 then 'DDL_DUMP' when 15 then 'REMOTE_LOGICAL_MINOR' when 16 then 'DDL_MEM'
    when 17 then 'DDL_MEM_MINI_SSTABLE' when 18 then 'MDS_MINI' when 19 then 'MDS_MINOR'
    when 20 then 'MICRO_MINI_SSTABLE' when 21 then 'INC_MAJOR'
    when 22 then 'INC_MAJOR_DDL_DUMP' when 23 then 'INC_MAJOR_DDL_MEM'
    else 'INVALID'
 end) as TABLE_TYPE,
 M.TABLET_ID,
 M.START_LOG_SCN,
 M.END_LOG_SCN,
 M.DATA_CHECKSUM,
 M.SIZE,
 M.REF,
 M.UPPER_TRANS_VERSION,
 M.IS_ACTIVE,
 M.CONTAIN_UNCOMMITTED_ROW
FROM
 oceanbase.__all_virtual_table_mgr M
""".replace("\n", " ")
)

# 21102: CDB_OB_BACKUP_ARCHIVELOG_SUMMARY # abandoned in 4.0
# 21103: CDB_OB_BACKUP_JOB_DETAILS # abandoned in 4.0
# 21104: CDB_OB_BACKUP_SET_DETAILS # abandoned in 4.0
# 21105: CDB_OB_BACKUP_SET_EXPIRED # abandoned in 4.0
# 21106: CDB_OB_BACKUP_PROGRESS # abandoned in 4.0
# 21107: CDB_OB_BACKUP_ARCHIVELOG_PROGRESS # abandoned in 4.0
# 21108: CDB_OB_BACKUP_CLEAN_HISTORY # abandoned in 4.0
# 21109: CDB_OB_BACKUP_TASK_CLEAN_HISTORY # abandoned in 4.0
# 21110: CDB_OB_RESTORE_PROGRESS # abandoned
# 21111: CDB_OB_RESTORE_JOB_HISTORY # abandoned

# 21112: GV$OB_SERVER_SCHEMA_INFO # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_server_schema_info)

# 21113: V$OB_SERVER_SCHEMA_INFO # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_server_schema_info)

# 21114: CDB_CKPT_HISTORY # abandoned in 4.0
# 21115: gv$ob_trans_table_status # abandoned in 4.0
# 21116: v$ob_trans_table_status # abandoned in 4.0

# 21118: GV$OB_MERGE_INFO # removed (single-tenant GV/V collapse; folded into V$OB_MERGE_INFO)

def_table_schema(
  owner = 'lixia.yq',
  table_name      = 'V$OB_MERGE_INFO',
  table_id        = '21119',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT
        TABLET_ID,
        TYPE AS ACTION,
        COMPACTION_SCN,
        START_TIME,
        FINISH_TIME AS END_TIME,
        MACRO_BLOCK_COUNT,
        CASE MACRO_BLOCK_COUNT WHEN 0 THEN 0.00 ELSE ROUND(MULTIPLEXED_MACRO_BLOCK_COUNT/MACRO_BLOCK_COUNT*100, 2) END AS REUSE_PCT,
        PARALLEL_DEGREE
    FROM oceanbase.__ALL_VIRTUAL_TABLET_COMPACTION_HISTORY
""".replace("\n", " ")
)

# 21122: CDB_OB_BACKUP_VALIDATION_JOB # abandoned in 4.0
# 21123: CDB_OB_BACKUP_VALIDATION_JOB_HISTORY # abandoned in 4.0
# 21124: CDB_OB_TENANT_BACKUP_VALIDATION_TASK # abandoned in 4.0
# 21125: CDB_OB_BACKUP_VALIDATION_TASK_HISTORY # abandoned in 4.0
# 21126: v$restore_point # abandoned in 4.0
# 21127: CDB_OB_BACKUP_SET_OBSOLETE # abandoned in 4.0
# 21128: CDB_OB_BACKUP_BACKUPSET_JOB # abandoned in 4.0
# 21129: CDB_OB_BACKUP_BACKUPSET_JOB_HISTORY # abandoned in 4.0
# 21130: CDB_OB_BACKUP_BACKUPSET_TASK # abandoned in 4.0
# 21131: CDB_OB_BACKUP_BACKUPSET_TASK_HISTORY # abandoned in 4.0
# 21132: CDB_OB_BACKUP_BACKUP_ARCHIVELOG_SUMMARY # abandoned in 4.0
# 21133: v$ob_cluster_failover_info # abandoned in 4.0
# 21136: CDB_OB_ARCHIVELOG_PIECE_FILES # abandoned
# 21137: CDB_OB_BACKUP_SET_FILES (abandoned)

# 21138: CDB_OB_BACKUP_BACKUPPIECE_JOB # abandoned in 4.0
# 21139: CDB_OB_BACKUP_BACKUPPIECE_JOB_HISTORY # abandoned in 4.0
# 21140: CDB_OB_BACKUP_BACKUPPIECE_TASK # abandoned in 4.0
# 21141: CDB_OB_BACKUP_BACKUPPIECE_TASK_HISTORY # abandoned in 4.0
# 21142: v$ob_all_clusters # abandoned in 4.0
# 21143: CDB_OB_BACKUP_ARCHIVELOG # abandoned in 4.0
# 21144: CDB_OB_BACKUP_BACKUP_ARCHIVELOG # abandoned in 4.0

def_table_schema(
  owner = 'jim.wjh',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'CONNECTION_CONTROL_FAILED_LOGIN_ATTEMPTS',
  table_id        = '21145',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  select
    concat('''',u.user_name,'''@''',u.host,'''') as USERHOST,
    s.failed_login_attempts as FAILED_ATTEMPTS
  from oceanbase.__all_user_failed_login_stat s
  join oceanbase.__all_user u
  on s.user_id = u.user_id
""".replace("\n", " ")
)

# 21146: GV$OB_TENANT_MEMORY # removed (single-tenant GV/V collapse; folded into V$OB_TENANT_MEMORY)

# 21147: V$OB_TENANT_MEMORY # removed (tenant-name scrub)

# 21148: GV$OB_PX_TARGET_MONITOR # removed (single-tenant GV/V collapse; folded into V$OB_PX_TARGET_MONITOR)

def_table_schema(
    owner = 'xiaochu.yh',
    table_name     = 'V$OB_PX_TARGET_MONITOR',
    table_id       = '21149',
    table_type = 'SYSTEM_VIEW',
    rowkey_columns  = [],
    normal_columns  = [],
    gm_columns      = [],
    in_tenant_space = True,
    view_definition = """SELECT
          CASE is_leader WHEN 1 THEN 'Y'
                         ELSE 'N' END AS IS_LEADER,
          VERSION,
          PEER_IP,
          PEER_PORT,
          PEER_TARGET,
          PEER_TARGET_USED,
          LOCAL_TARGET_USED,
          LOCAL_PARALLEL_SESSION_COUNT
        FROM oceanbase.__all_virtual_px_target_monitor
""".replace("\n", " ")
)

def_table_schema(
  owner = 'sean.yyj',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'COLUMN_PRIVILEGES',
  table_id        = '21150',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  WITH DB_PRIV AS (
    select A.user_id USER_ID,
           A.database_name DATABASE_NAME,
           A.priv_alter PRIV_ALTER,
           A.priv_create PRIV_CREATE,
           A.priv_delete PRIV_DELETE,
           A.priv_drop PRIV_DROP,
           A.priv_grant_option PRIV_GRANT_OPTION,
           A.priv_insert PRIV_INSERT,
           A.priv_update PRIV_UPDATE,
           A.priv_select PRIV_SELECT,
           A.priv_index PRIV_INDEX,
           A.priv_create_view PRIV_CREATE_VIEW,
           A.priv_show_view PRIV_SHOW_VIEW,
           A.GMT_CREATE GMT_CREATE,
           A.GMT_MODIFIED GMT_MODIFIED,
           A.PRIV_OTHERS PRIV_OTHERS
    from oceanbase.__all_database_privilege_history A,
        (select user_id, database_name, max(schema_version) schema_version from oceanbase.__all_database_privilege_history group by user_id, database_name, database_name collate utf8mb4_bin) B
    where A.user_id = B.user_id and A.database_name collate utf8mb4_bin = B.database_name collate utf8mb4_bin and A.schema_version = B.schema_version and A.is_deleted = 0
  )
  SELECT cast(concat('''', B.user_name, '''', '@', '''', B.host, '''') as char(292)) as GRANTEE,
         cast('def' as char(512)) AS TABLE_CATALOG,
         cast(DATABASE_NAME as char(64)) AS TABLE_SCHEMA,
         cast(TABLE_NAME as char(64)) AS TABLE_NAME,
         cast(COLUMN_NAME as char(64)) AS COLUMN_NAME,
         cast(CASE WHEN V1.C1 = 0  AND (CP.all_priv & 1) != 0 THEN 'SELECT'
               WHEN V1.C1 = 1  AND (CP.all_priv & 2) != 0 THEN 'INSERT'
               WHEN V1.C1 = 2  AND (CP.all_priv & 4) != 0 THEN 'UPDATE'
               WHEN V1.C1 = 3  AND (CP.all_priv & 8) != 0 THEN 'REFERENCES'
               END AS char(64)) AS PRIVILEGE_TYPE,
         cast(case when priv_grant_option = 1 then 'YES' ELSE 'NO' END as char(3)) AS IS_GRANTABLE
  FROM oceanbase.__all_column_privilege CP, oceanbase.__all_user B,
      (SELECT 0 AS C1
        UNION ALL SELECT 1 AS C1
        UNION ALL SELECT 2 AS C1
        UNION ALL SELECT 3 AS C1) V1,
      (SELECT USER_ID
        FROM oceanbase.__all_user
        WHERE CONCAT(USER_NAME, '@', HOST) = CURRENT_USER()) CURR
      LEFT JOIN
      (SELECT USER_ID
        FROM DB_PRIV
        WHERE DATABASE_NAME = 'mysql'
          AND PRIV_SELECT = 1) DB ON CURR.USER_ID = DB.USER_ID
  WHERE CP.user_id = B.user_id
    AND ((V1.C1 = 0 AND (CP.all_priv & 1) != 0)
         OR (V1.C1 = 1 AND (CP.all_priv & 2) != 0)
         OR (V1.C1 = 2 AND (CP.all_priv & 4) != 0)
         OR (V1.C1 = 0 AND (CP.all_priv & 8) != 0))
    AND (DB.USER_ID IS NOT NULL
          OR 512 & CURRENT_USER_PRIV() = 512
          OR CP.user_id = CURR.USER_ID)
""".replace("\n", " ")
  )

def_table_schema(
  owner = 'luofan.zp',
  tablegroup_id = 'OB_INVALID_ID',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'VIEW_TABLE_USAGE',
  table_id       = '21151',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  view_definition = """
    select
    cast('def' as CHAR(64)) AS VIEW_CATALOG,
    v.VIEW_SCHEMA collate utf8mb4_name_case as VIEW_SCHEMA,
    v.VIEW_NAME collate utf8mb4_name_case as VIEW_NAME,
    t.TABLE_SCHEMA collate utf8mb4_name_case as TABLE_SCHEMA,
    t.TABLE_NAME collate utf8mb4_name_case as TABLE_NAME,
    cast('def' as CHAR(64)) AS TABLE_CATALOG
    from
    (select o.database_name as VIEW_SCHEMA,
            o.table_name as VIEW_NAME,
            d.dep_obj_id as DEP_OBJ_ID,
            d.ref_obj_id as REF_OBJ_ID
     from (select d.database_name as database_name,
                  t.table_name as table_name,
                  t.table_id as table_id
           from oceanbase.__all_table as t
           join oceanbase.__all_database as d
           on t.database_id = d.database_id
           where t.table_mode >> 12 & 15 in (0,1) and t.index_attributes_set & 16 = 0) o
           join oceanbase.__all_dependency d
           on d.dep_obj_id = o.table_id) v

     join

     (select o.database_name as TABLE_SCHEMA,
             o.table_name as TABLE_NAME,
             d.dep_obj_id as DEP_OBJ_ID,
             d.ref_obj_id as REF_OBJ_ID
      from (select d.database_name as database_name,
                   t.table_name as table_name,
                   t.table_id as table_id
            from oceanbase.__all_table as t
            join oceanbase.__all_database as d
            on t.database_id = d.database_id) o
            join oceanbase.__all_dependency d
            on d.ref_obj_id = o.table_id) t

    on v.dep_obj_id = t.dep_obj_id and v.ref_obj_id = t.ref_obj_id
    where (0 = sys_privilege_check('table_acc', 1)
            or 0 = sys_privilege_check('table_acc', 1, t.table_schema, v.view_name))
""".replace("\n", " "),


  normal_columns = [
  ]
  )
#
# 21152: CDB_OB_BACKUP_JOBS # abandoned
# 21153: CDB_OB_BACKUP_JOB_HISTORY # abandoned
# 21154: CDB_OB_BACKUP_TASKS # abandoned
# 21155: CDB_OB_BACKUP_TASK_HISTORY # abandoned
# 21156: CDB_OB_LOG_ARCHIVE_LS_SUMMARY

def_table_schema(
  owner = 'xiaochu.yh',
  tablegroup_id  = 'OB_INVALID_ID',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'FILES',
  table_id       = '21157',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,
  view_definition = """SELECT FILE_ID,
                              FILE_NAME,
                              FILE_TYPE,
                              TABLESPACE_NAME,
                              TABLE_CATALOG,
                              TABLE_SCHEMA,
                              TABLE_NAME,
                              LOGFILE_GROUP_NAME,
                              LOGFILE_GROUP_NUMBER,
                              ENGINE,
                              FULLTEXT_KEYS,
                              DELETED_ROWS,
                              UPDATE_COUNT,
                              FREE_EXTENTS,
                              TOTAL_EXTENTS,
                              EXTENT_SIZE,
                              INITIAL_SIZE,
                              MAXIMUM_SIZE,
                              AUTOEXTEND_SIZE,
                              CREATION_TIME,
                              LAST_UPDATE_TIME,
                              LAST_ACCESS_TIME,
                              RECOVER_TIME,
                              TRANSACTION_COUNTER,
                              VERSION,
                              ROW_FORMAT,
                              TABLE_ROWS,
                              AVG_ROW_LENGTH,
                              DATA_LENGTH,
                              MAX_DATA_LENGTH,
                              INDEX_LENGTH,
                              DATA_FREE,
                              CREATE_TIME,
                              UPDATE_TIME,
                              CHECK_TIME,
                              CHECKSUM,
                              STATUS,
                              EXTRA
                   FROM oceanbase.__all_virtual_files""".replace("\n", " "),
  normal_columns = [
  ]
  )

# 21158: DBA_OB_TENANTS (abandoned)
# 21159: DBA_OB_UNITS (abandoned)
# 21160: DBA_OB_UNIT_CONFIGS (abandoned)
# 21161: DBA_OB_RESOURCE_POOLS (abandoned)
# 21161: DBA_OB_SERVERS (abandoned)
# 21163: DBA_OB_ZONES (abandoned)

# 21165: DBA_OB_TENANT_JOBS (abandoned)
# 21166: DBA_OB_UNIT_JOBS (abandoned)
# 21167: DBA_OB_SERVER_JOBS (abandoned)
# 21168: DBA_OB_LS_LOCATIONS (abandoned)
# 21169: CDB_OB_LS_LOCATIONS (abandoned)

def_table_schema(
  owner           = 'yanmu.ztl',
  table_name      = 'DBA_OB_TABLET_REPLICAS',
  table_id        = '21172',
  table_type      = 'SYSTEM_VIEW',
  gm_columns      = [],
  rowkey_columns  = [],
  normal_columns  = [],
  in_tenant_space = True,
  view_definition =
  """
  SELECT gmt_create AS CREATE_TIME,
         gmt_modified AS MODIFY_TIME,
         TABLET_ID,
         COMPACTION_SCN,
         DATA_SIZE,
         REQUIRED_SIZE
  FROM OCEANBASE.__ALL_VIRTUAL_TABLET_META_TABLE
  """.replace("\n", " ")
  )

def_table_schema(
  owner           = 'yanmu.ztl',
  table_name      = 'DBA_OB_TABLEGROUPS',
  table_id        = '21174',
  table_type      = 'SYSTEM_VIEW',
  gm_columns      = [],
  rowkey_columns  = [],
  normal_columns  = [],
  in_tenant_space = True,
  view_definition =
  """
  SELECT TABLEGROUP_NAME,

         CAST("NONE" AS CHAR(13)) AS PARTITIONING_TYPE,

         CAST("NONE" AS CHAR(13)) AS SUBPARTITIONING_TYPE,

         CAST(NULL AS SIGNED) AS PARTITION_COUNT,

         CAST(NULL AS SIGNED) AS DEF_SUBPARTITION_COUNT,

         CAST(NULL AS SIGNED) AS PARTITIONING_KEY_COUNT,

         CAST(NULL AS SIGNED) AS SUBPARTITIONING_KEY_COUNT,

         SHARDING

  FROM OCEANBASE.__ALL_TABLEGROUP
  """.replace("\n", " ")
  )

def_table_schema(
  owner           = 'yanmu.ztl',
  table_name      = 'DBA_OB_TABLEGROUP_PARTITIONS',
  table_id        = '21176',
  table_type      = 'SYSTEM_VIEW',
  gm_columns      = [],
  rowkey_columns  = [],
  normal_columns  = [],
  in_tenant_space = True,
  view_definition =
  """
  SELECT CAST("" AS CHAR(128)) AS TABLEGROUP_NAME,

         CAST("NO" AS CHAR(3)) AS COMPOSITE,

         CAST("" AS CHAR(64)) AS PARTITION_NAME,

         CAST(NULL AS SIGNED) AS SUBPARTITION_COUNT,

         CAST(NULL AS CHAR(4096)) AS HIGH_VALUE,

         CAST(NULL AS SIGNED) AS HIGH_VALUE_LENGTH,

         CAST(NULL AS UNSIGNED) AS PARTITION_POSITION
  FROM
    DUAL
  WHERE
    0 = 1
  """.replace("\n", " ")
  )

def_table_schema(
  owner           = 'yanmu.ztl',
  table_name      = 'DBA_OB_TABLEGROUP_SUBPARTITIONS',
  table_id        = '21178',
  table_type      = 'SYSTEM_VIEW',
  gm_columns      = [],
  rowkey_columns  = [],
  normal_columns  = [],
  in_tenant_space = True,
  view_definition =
  """
  SELECT CAST("" AS CHAR(128)) AS TABLEGROUP_NAME,

         CAST("" AS CHAR(64)) AS PARTITION_NAME,

         CAST("" AS CHAR(64)) AS SUBPARTITION_NAME,

         CAST(NULL AS CHAR(4096)) AS HIGH_VALUE,

         CAST(NULL AS SIGNED) AS HIGH_VALUE_LENGTH,

         CAST(NULL AS UNSIGNED) AS PARTITION_POSITION,

         CAST(NULL AS UNSIGNED) AS SUBPARTITION_POSITION
   FROM
      DUAL
   WHERE
      0 = 1
  """.replace("\n", " ")
  )

def_table_schema(
  owner           = 'yanmu.ztl',
  table_name      = 'DBA_OB_DATABASES',
  table_id        = '21180',
  table_type      = 'SYSTEM_VIEW',
  gm_columns      = [],
  rowkey_columns  = [],
  normal_columns  = [],
  in_tenant_space = True,
  view_definition =
  """
  SELECT D.DATABASE_NAME AS DATABASE_NAME,
         (CASE D.IN_RECYCLEBIN WHEN 0 THEN 'NO' ELSE 'YES' END) AS IN_RECYCLEBIN,
         C.COLLATION AS COLLATION,
         (CASE D.READ_ONLY WHEN 0 THEN 'NO' ELSE 'YES' END) AS READ_ONLY,
         D.COMMENT AS COMMENT
  FROM OCEANBASE.__ALL_DATABASE AS D
  LEFT JOIN OCEANBASE.__ALL_VIRTUAL_COLLATION AS C
  ON D.COLLATION_TYPE = C.COLLATION_TYPE
  """.replace("\n", " ")
  )

def_table_schema(
  owner           = 'yanmu.ztl',
  table_name      = 'DBA_OB_TABLEGROUP_TABLES',
  table_id        = '21182',
  table_type      = 'SYSTEM_VIEW',
  gm_columns      = [],
  rowkey_columns  = [],
  normal_columns  = [],
  in_tenant_space = True,
  view_definition =
  """
  SELECT TG.TABLEGROUP_NAME AS TABLEGROUP_NAME,
         D.DATABASE_NAME AS OWNER,
         T.TABLE_NAME AS TABLE_NAME,
         TG.SHARDING AS SHARDING
  FROM OCEANBASE.__ALL_TABLE AS T
  JOIN OCEANBASE.__ALL_DATABASE AS D
  ON T.DATABASE_ID = D.DATABASE_ID
  JOIN OCEANBASE.__ALL_TABLEGROUP AS TG
  ON T.TABLEGROUP_ID = TG.TABLEGROUP_ID
  WHERE T.TABLE_TYPE in (0, 3, 6)
  AND T.TABLE_MODE >> 12 & 15 in (0,1)
  AND T.INDEX_ATTRIBUTES_SET & 16 = 0
  """.replace("\n", " ")
  )

def_table_schema(
  owner           = 'donglou.zl',
  table_name      = 'DBA_OB_MAJOR_COMPACTION',
  table_id        = '21186',
  table_type      = 'SYSTEM_VIEW',
  gm_columns      = [],
  rowkey_columns  = [],
  normal_columns  = [],
  in_tenant_space = True,
  view_definition =
  """
  SELECT FROZEN_SCN,
         USEC_TO_TIME(FROZEN_SCN/1000) AS FROZEN_TIME,
         GLOBAL_BROADCAST_SCN,
         LAST_MERGED_SCN AS LAST_SCN,
         USEC_TO_TIME(LAST_MERGED_TIME) AS LAST_FINISH_TIME,
         USEC_TO_TIME(MERGE_START_TIME) AS START_TIME,
         (CASE MERGE_STATUS
                WHEN 0 THEN 'IDLE'
                WHEN 1 THEN 'COMPACTING'
                WHEN 2 THEN 'VERIFYING'
                ELSE 'UNKNOWN' END) AS STATUS,
         (CASE IS_MERGE_ERROR WHEN 0 THEN 'NO' ELSE 'YES' END) AS IS_ERROR,
         (CASE SUSPEND_MERGING WHEN 0 THEN 'NO' ELSE 'YES' END) AS IS_SUSPENDED,
         (CASE ERROR_TYPE
                WHEN 0 THEN ''
                WHEN 1 THEN 'CHECKSUM_ERROR'
                ELSE 'UNKNOWN' END) AS INFO
  FROM OCEANBASE.__ALL_VIRTUAL_MERGE_INFO
  """.replace("\n", " ")
  )

# TODO:(yanmu.ztl)
# tablespace/constraint is not supported yet.
# TODO:(yanmu.ztl)
# 1. sys package is not visible in user tenant.
# 2. tablespace/constraint are not supported yet.
# 3. sequence_object/synonym/context objects are not exposed.
def_table_schema(
  owner           = 'yanmu.ztl',
  table_name      = 'DBA_OBJECTS',
  database_id     = 'OB_SYS_DATABASE_ID',
  table_id        = '21204',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT
      CAST(B.DATABASE_NAME AS CHAR(128)) AS OWNER,
      CAST(A.OBJECT_NAME AS CHAR(128)) AS OBJECT_NAME,
      CAST(A.SUBOBJECT_NAME AS CHAR(128)) AS SUBOBJECT_NAME,
      CAST(A.OBJECT_ID AS SIGNED) AS OBJECT_ID,
      CAST(A.DATA_OBJECT_ID AS SIGNED) AS DATA_OBJECT_ID,
      CAST(A.OBJECT_TYPE AS CHAR(23)) AS OBJECT_TYPE,
      CAST(A.GMT_CREATE AS DATETIME) AS CREATED,
      CAST(A.GMT_MODIFIED AS DATETIME) AS LAST_DDL_TIME,
      CAST(A.GMT_CREATE AS DATETIME) AS TIMESTAMP,
      CAST(A.STATUS AS CHAR(7)) AS STATUS,
      CAST(A.TEMPORARY AS CHAR(1)) AS TEMPORARY,
      CAST(A.`GENERATED` AS CHAR(1)) AS "GENERATED",
      CAST(A.SECONDARY AS CHAR(1)) AS SECONDARY,
      CAST(A.NAMESPACE AS SIGNED) AS NAMESPACE,
      CAST(A.EDITION_NAME AS CHAR(128)) AS EDITION_NAME,
      CAST(NULL AS CHAR(18)) AS SHARING,
      CAST(NULL AS CHAR(1)) AS EDITIONABLE,
      CAST(NULL AS CHAR(1)) AS APPLICATION,
      CAST(NULL AS CHAR(1)) AS DEFAULT_COLLATION,
      CAST(NULL AS CHAR(1)) AS DUPLICATED,
      CAST(NULL AS CHAR(1)) AS SHARDED,
      CAST(NULL AS CHAR(1)) AS IMPORTED_OBJECT,
      CAST(NULL AS SIGNED) AS CREATED_APPID,
      CAST(NULL AS SIGNED) AS CREATED_VSNID,
      CAST(NULL AS SIGNED) AS MODIFIED_APPID,
      CAST(NULL AS SIGNED) AS MODIFIED_VSNID
    FROM (
      SELECT USEC_TO_TIME(B.SCHEMA_VERSION) AS GMT_CREATE,
             USEC_TO_TIME(A.SCHEMA_VERSION) AS GMT_MODIFIED,
             A.DATABASE_ID,
             A.TABLE_NAME AS OBJECT_NAME,
             NULL AS SUBOBJECT_NAME,
             CAST(A.TABLE_ID AS SIGNED) AS OBJECT_ID,
             A.TABLET_ID AS DATA_OBJECT_ID,
             'TABLE' AS OBJECT_TYPE,
             'VALID' AS STATUS,
             'N' AS TEMPORARY,
             'N' AS "GENERATED",
             'N' AS SECONDARY,
             0 AS NAMESPACE,
             NULL AS EDITION_NAME
      FROM OCEANBASE.__ALL_VIRTUAL_CORE_ALL_TABLE A
      JOIN OCEANBASE.__ALL_VIRTUAL_CORE_ALL_TABLE B
        ON B.TABLE_NAME = '__all_core_table'

      UNION ALL

      SELECT
      GMT_CREATE
      ,GMT_MODIFIED
      ,DATABASE_ID
      ,CAST((CASE
             WHEN DATABASE_ID = 201004 THEN TABLE_NAME
             WHEN TABLE_TYPE = 5 THEN SUBSTR(TABLE_NAME, 7 + POSITION('_' IN SUBSTR(TABLE_NAME, 7)))
             ELSE TABLE_NAME END) AS CHAR(128)) AS OBJECT_NAME
      ,NULL SUBOBJECT_NAME
      ,CAST(TABLE_ID AS SIGNED) AS OBJECT_ID
      ,(CASE WHEN TABLET_ID != 0 THEN TABLET_ID ELSE NULL END) DATA_OBJECT_ID
      ,CASE WHEN TABLE_TYPE IN (0,3,6,8,9,14) THEN 'TABLE'
            WHEN TABLE_TYPE IN (2) THEN 'VIRTUAL TABLE'
            WHEN TABLE_TYPE IN (1,4) THEN 'VIEW'
            WHEN TABLE_TYPE IN (5) THEN 'INDEX'
            ELSE NULL END AS OBJECT_TYPE
      ,CAST(CASE WHEN TABLE_TYPE IN (5) THEN CASE WHEN INDEX_STATUS = 2 THEN 'VALID'
              WHEN INDEX_STATUS = 3 THEN 'CHECKING'
              WHEN INDEX_STATUS = 4 THEN 'INELEGIBLE'
              WHEN INDEX_STATUS = 5 THEN 'ERROR'
              ELSE 'UNUSABLE' END
            ELSE  CASE WHEN OBJECT_STATUS = 1 THEN 'VALID' ELSE 'INVALID' END END AS CHAR(10)) AS STATUS
      ,CASE WHEN TABLE_TYPE IN (6,8,9) THEN 'Y'
          ELSE 'N' END AS TEMPORARY
      ,CASE WHEN TABLE_TYPE IN (0,1) THEN 'Y'
          ELSE 'N' END AS "GENERATED"
      ,'N' AS SECONDARY
      , 0 AS NAMESPACE
      ,NULL AS EDITION_NAME
      FROM
      OCEANBASE.__ALL_TABLE
      WHERE TABLE_TYPE != 12 AND TABLE_TYPE != 13
        AND TABLE_MODE >> 12 & 15 in (0,1)
        AND INDEX_ATTRIBUTES_SET & 16 = 0

      UNION ALL

      SELECT
          CST.GMT_CREATE
         ,CST.GMT_MODIFIED
         ,DB.DATABASE_ID
         ,CST.constraint_name AS OBJECT_NAME
         ,NULL AS SUBOBJECT_NAME
         ,CAST(TBL.TABLE_ID AS SIGNED) AS OBJECT_ID
         ,NULL AS DATA_OBJECT_ID
         ,'INDEX' AS OBJECT_TYPE
         ,'VALID' AS STATUS
         ,'N' AS TEMPORARY
         ,'N' AS "GENERATED"
         ,'N' AS SECONDARY
         ,0 AS NAMESPACE
         ,NULL AS EDITION_NAME
         FROM OCEANBASE.__ALL_CONSTRAINT CST, OCEANBASE.__ALL_TABLE TBL, OCEANBASE.__ALL_DATABASE DB
         WHERE DB.DATABASE_ID = TBL.DATABASE_ID AND TBL.TABLE_ID = CST.TABLE_ID and CST.CONSTRAINT_TYPE = 1
          AND TBL.TABLE_MODE >> 12 & 15 in (0,1)
          AND TBL.INDEX_ATTRIBUTES_SET & 16 = 0

      UNION ALL

      SELECT
      P.GMT_CREATE
      ,P.GMT_MODIFIED
      ,T.DATABASE_ID
      ,CAST((CASE
             WHEN T.DATABASE_ID = 201004 THEN T.TABLE_NAME
             WHEN T.TABLE_TYPE = 5 THEN SUBSTR(T.TABLE_NAME, 7 + POSITION('_' IN SUBSTR(T.TABLE_NAME, 7)))
             ELSE T.TABLE_NAME END) AS CHAR(128)) AS OBJECT_NAME
      ,P.PART_NAME SUBOBJECT_NAME
      ,P.PART_ID OBJECT_ID
      ,CASE WHEN P.TABLET_ID != 0 THEN P.TABLET_ID ELSE NULL END AS DATA_OBJECT_ID
      ,(CASE WHEN T.TABLE_TYPE = 5 THEN 'INDEX PARTITION' ELSE 'TABLE PARTITION' END) AS OBJECT_TYPE
      ,'VALID' AS STATUS
      ,'N' AS TEMPORARY
      , NULL AS "GENERATED"
      ,'N' AS SECONDARY
      , 0 AS NAMESPACE
      ,NULL AS EDITION_NAME
      FROM OCEANBASE.__ALL_TABLE T JOIN OCEANBASE.__ALL_PART P ON T.TABLE_ID = P.TABLE_ID
      WHERE T.TABLE_MODE >> 12 & 15 in (0,1)
      AND P.PARTITION_TYPE = 0 AND T.INDEX_ATTRIBUTES_SET & 16 = 0

      UNION ALL

      SELECT
      SUBP.GMT_CREATE
      ,SUBP.GMT_MODIFIED
      ,T.DATABASE_ID
      ,CAST((CASE
             WHEN T.DATABASE_ID = 201004 THEN T.TABLE_NAME
             WHEN T.TABLE_TYPE = 5 THEN SUBSTR(T.TABLE_NAME, 7 + POSITION('_' IN SUBSTR(T.TABLE_NAME, 7)))
             ELSE T.TABLE_NAME END) AS CHAR(128)) AS OBJECT_NAME
      ,SUBP.SUB_PART_NAME SUBOBJECT_NAME
      ,SUBP.SUB_PART_ID OBJECT_ID
      ,SUBP.TABLET_ID AS DATA_OBJECT_ID
      ,(CASE WHEN T.TABLE_TYPE = 5 THEN 'INDEX SUBPARTITION' ELSE 'TABLE SUBPARTITION' END) AS OBJECT_TYPE
      ,'VALID' AS STATUS
      ,'N' AS TEMPORARY
      ,'N' AS "GENERATED"
      ,'N' AS SECONDARY
      , 0 AS NAMESPACE
      ,NULL AS EDITION_NAME
      FROM OCEANBASE.__ALL_TABLE T, OCEANBASE.__ALL_PART P,OCEANBASE.__ALL_SUB_PART SUBP
      WHERE T.TABLE_ID =P.TABLE_ID AND P.TABLE_ID=SUBP.TABLE_ID AND P.PART_ID =SUBP.PART_ID
      AND T.TABLE_MODE >> 12 & 15 in (0,1)
      AND SUBP.PARTITION_TYPE = 0 AND P.PARTITION_TYPE = 0 AND T.INDEX_ATTRIBUTES_SET & 16 = 0

      UNION ALL

      SELECT
      P.GMT_CREATE
      ,P.GMT_MODIFIED
      ,P.DATABASE_ID
      ,P.PACKAGE_NAME AS OBJECT_NAME
      ,NULL AS SUBOBJECT_NAME
      ,CAST(P.PACKAGE_ID AS SIGNED) AS OBJECT_ID
      ,NULL AS DATA_OBJECT_ID
      ,CASE WHEN TYPE = 1 THEN 'PACKAGE'
            WHEN TYPE = 2 THEN 'PACKAGE BODY'
            ELSE NULL END AS OBJECT_TYPE
      ,CASE WHEN EXISTS
                  (SELECT OBJ_ID FROM OCEANBASE.__ALL_ERROR E
                    WHERE P.PACKAGE_ID = E.OBJ_ID AND (E.OBJ_TYPE = 3 OR E.OBJ_TYPE = 5))
                 THEN 'INVALID'
            WHEN TYPE = 2 AND EXISTS
                  (SELECT OBJ_ID FROM OCEANBASE.__ALL_ERROR Eb
                    WHERE OBJ_ID IN
                            (SELECT PACKAGE_ID FROM OCEANBASE.__ALL_PACKAGE Pb
                              WHERE Pb.PACKAGE_NAME = P.PACKAGE_NAME AND Pb.DATABASE_ID = P.DATABASE_ID AND TYPE = 1)
                          AND Eb.OBJ_TYPE = 3)
              THEN 'INVALID'
            ELSE 'VALID' END AS STATUS
      ,'N' AS TEMPORARY
      ,'N' AS "GENERATED"
      ,'N' AS SECONDARY
      , 0 AS NAMESPACE
      ,NULL AS EDITION_NAME
      FROM OCEANBASE.__ALL_PACKAGE P

      UNION ALL

      SELECT
      R.GMT_CREATE
      ,R.GMT_MODIFIED
      ,R.DATABASE_ID
      ,R.ROUTINE_NAME AS OBJECT_NAME
      ,NULL AS SUBOBJECT_NAME
      ,CAST(R.ROUTINE_ID AS SIGNED) AS OBJECT_ID
      ,NULL AS DATA_OBJECT_ID
      ,CASE WHEN ROUTINE_TYPE = 1 THEN 'PROCEDURE'
            WHEN ROUTINE_TYPE = 2 THEN 'FUNCTION'
            ELSE NULL END AS OBJECT_TYPE
      ,CASE WHEN EXISTS
                  (SELECT OBJ_ID FROM OCEANBASE.__ALL_ERROR E
                    WHERE R.ROUTINE_ID = E.OBJ_ID AND (E.OBJ_TYPE = 9 OR E.OBJ_TYPE = 12))
                 THEN 'INVALID'
            ELSE 'VALID' END AS STATUS
      ,'N' AS TEMPORARY
      ,'N' AS "GENERATED"
      ,'N' AS SECONDARY
      , 0 AS NAMESPACE
      ,NULL AS EDITION_NAME
      FROM OCEANBASE.__ALL_ROUTINE R
      WHERE (ROUTINE_TYPE = 1 OR ROUTINE_TYPE = 2)

      UNION ALL

      SELECT
      T.GMT_CREATE
      ,T.GMT_MODIFIED
      ,T.DATABASE_ID
      ,T.TRIGGER_NAME AS OBJECT_NAME
      ,NULL AS SUBOBJECT_NAME
      ,CAST(T.TRIGGER_ID AS SIGNED) AS OBJECT_ID
      ,NULL AS DATA_OBJECT_ID
      ,'TRIGGER' OBJECT_TYPE
      ,CASE WHEN EXISTS
                  (SELECT OBJ_ID FROM OCEANBASE.__ALL_ERROR E
                    WHERE T.TRIGGER_ID = E.OBJ_ID AND (E.OBJ_TYPE = 7))
                 THEN 'INVALID'
            ELSE 'VALID' END AS STATUS
      ,'N' AS TEMPORARY
      ,'N' AS "GENERATED"
      ,'N' AS SECONDARY
      , 0 AS NAMESPACE
      ,NULL AS EDITION_NAME
      FROM OCEANBASE.__ALL_TRIGGER T

      UNION ALL

      SELECT
        GMT_CREATE,
        GMT_MODIFIED,
        DATABASE_ID,
        DATABASE_NAME AS OBJECT_NAME,
        NULL AS SUBOBJECT_NAME,
        CAST(DATABASE_ID AS SIGNED) AS OBJECT_ID,
        NULL AS DATA_OBJECT_ID,
        'DATABASE' AS OBJECT_TYPE,
        'VALID' AS STATUS,
        'N' AS TEMPORARY,
        'N' AS "GENERATED",
        'N' AS SECONDARY,
        0 AS NAMESPACE,
        NULL AS EDITION_NAME
      FROM OCEANBASE.__ALL_DATABASE

      UNION ALL

      SELECT
        GMT_CREATE,
        GMT_MODIFIED,
        CAST(201001 AS SIGNED) AS DATABASE_ID,
        TABLEGROUP_NAME AS OBJECT_NAME,
        NULL AS SUBOBJECT_NAME,
        CAST(TABLEGROUP_ID AS SIGNED) AS OBJECT_ID,
        NULL AS DATA_OBJECT_ID,
        'TABLEGROUP' AS OBJECT_TYPE,
        'VALID' AS STATUS,
        'N' AS TEMPORARY,
        'N' AS "GENERATED",
        'N' AS SECONDARY,
        0 AS NAMESPACE,
        NULL AS EDITION_NAME
      FROM OCEANBASE.__ALL_TABLEGROUP

      UNION ALL

      SELECT
        GMT_CREATE,
        GMT_MODIFIED,
        CAST(201001 AS SIGNED) AS DATABASE_ID,
        CATALOG_NAME AS OBJECT_NAME,
        NULL AS SUBOBJECT_NAME,
        CAST(CATALOG_ID AS SIGNED) AS OBJECT_ID,
        NULL AS DATA_OBJECT_ID,
        'CATALOG' AS OBJECT_TYPE,
        'VALID' AS STATUS,
        'N' AS TEMPORARY,
        'N' AS "GENERATED",
        'N' AS SECONDARY,
        0 AS NAMESPACE,
        NULL AS EDITION_NAME
      FROM OCEANBASE.__ALL_CATALOG
    ) A
    JOIN OCEANBASE.__ALL_DATABASE B
    ON A.DATABASE_ID = B.DATABASE_ID
""".replace("\n", " ")
)

def_table_schema(
  owner           = 'yanmu.ztl',
  table_name      = 'DBA_PART_TABLES',
  database_id     = 'OB_SYS_DATABASE_ID',
  table_id        = '21205',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT CAST(DB.DATABASE_NAME AS CHAR(128)) OWNER,
         CAST(TB.TABLE_NAME AS CHAR(128)) TABLE_NAME,
         CAST((CASE TB.PART_FUNC_TYPE
              WHEN 0 THEN 'HASH'
              WHEN 1 THEN 'KEY'
              WHEN 2 THEN 'KEY'
              WHEN 3 THEN 'RANGE'
              WHEN 4 THEN 'RANGE COLUMNS'
              WHEN 5 THEN 'LIST'
              WHEN 6 THEN 'LIST COLUMNS'
              WHEN 7 THEN 'RANGE' END)
              AS CHAR(13)) PARTITIONING_TYPE,
         CAST((CASE TB.PART_LEVEL
               WHEN 1 THEN 'NONE'
               WHEN 2 THEN
               (CASE TB.SUB_PART_FUNC_TYPE
                WHEN 0 THEN 'HASH'
                WHEN 1 THEN 'KEY'
                WHEN 2 THEN 'KEY'
                WHEN 3 THEN 'RANGE'
                WHEN 4 THEN 'RANGE COLUMNS'
                WHEN 5 THEN 'LIST'
                WHEN 6 THEN 'LIST COLUMNS'
                WHEN 7 THEN 'RANGE' END) END)
              AS CHAR(13)) SUBPARTITIONING_TYPE,
         CAST((CASE TB.PART_FUNC_TYPE
               WHEN 7 THEN 1048575
               ELSE TB.PART_NUM END) AS SIGNED) PARTITION_COUNT,
         CAST ((CASE TB.PART_LEVEL
                WHEN 1 THEN 0
                WHEN 2 THEN (CASE WHEN TB.SUB_PART_TEMPLATE_FLAGS > 0 THEN TB.SUB_PART_NUM ELSE 1 END)
                END) AS SIGNED) DEF_SUBPARTITION_COUNT,
         CAST(PART_INFO.PART_KEY_COUNT AS SIGNED) PARTITIONING_KEY_COUNT,
         CAST((CASE TB.PART_LEVEL
              WHEN 1 THEN 0
              WHEN 2 THEN PART_INFO.SUBPART_KEY_COUNT END)
              AS SIGNED) SUBPARTITIONING_KEY_COUNT,
         CAST(NULL AS CHAR(8)) STATUS,
         CAST(NULL AS CHAR(30)) DEF_TABLESPACE_NAME,
         CAST(NULL AS SIGNED) DEF_PCT_FREE,
         CAST(NULL AS SIGNED) DEF_PCT_USED,
         CAST(NULL AS SIGNED) DEF_INI_TRANS,
         CAST(NULL AS SIGNED) DEF_MAX_TRANS,
         CAST(NULL AS CHAR(40)) DEF_INITIAL_EXTENT,
         CAST(NULL AS CHAR(40)) DEF_NEXT_EXTENT,
         CAST(NULL AS CHAR(40)) DEF_MIN_EXTENTS,
         CAST(NULL AS CHAR(40)) DEF_MAX_EXTENTS,
         CAST(NULL AS CHAR(40)) DEF_MAX_SIZE,
         CAST(NULL AS CHAR(40)) DEF_PCT_INCREASE,
         CAST(NULL AS SIGNED) DEF_FREELISTS,
         CAST(NULL AS SIGNED) DEF_FREELIST_GROUPS,
         CAST(NULL AS CHAR(7)) DEF_LOGGING,
         CAST(CASE WHEN TB.COMPRESS_FUNC_NAME IS NULL THEN 'DISABLED'
              ELSE 'ENABLED' END AS CHAR(8)) DEF_COMPRESSION,
         CAST(TB.COMPRESS_FUNC_NAME AS CHAR(12)) DEF_COMPRESS_FOR,
         CAST(NULL AS CHAR(7)) DEF_BUFFER_POOL,
         CAST(NULL AS CHAR(7)) DEF_FLASH_CACHE,
         CAST(NULL AS CHAR(7)) DEF_CELL_FLASH_CACHE,
         CAST(NULL AS CHAR(30)) REF_PTN_CONSTRAINT_NAME,
         CAST(TB.INTERVAL_RANGE AS CHAR(1000)) "INTERVAL",
         CAST('NO' AS CHAR(3)) AUTOLIST,
         CAST(NULL AS CHAR(1000)) INTERVAL_SUBPARTITION,
         CAST('NO' AS CHAR(3)) AUTOLIST_SUBPARTITION,
         CAST(NULL AS CHAR(3)) IS_NESTED,
         CAST(NULL AS CHAR(4)) DEF_SEGMENT_CREATED,
         CAST(NULL AS CHAR(3)) DEF_INDEXING,
         CAST(NULL AS CHAR(8)) DEF_INMEMORY,
         CAST(NULL AS CHAR(8)) DEF_INMEMORY_PRIORITY,
         CAST(NULL AS CHAR(15)) DEF_INMEMORY_DISTRIBUTE,
         CAST(NULL AS CHAR(17)) DEF_INMEMORY_COMPRESSION,
         CAST(NULL AS CHAR(13)) DEF_INMEMORY_DUPLICATE,
         CAST(NULL AS CHAR(3)) DEF_READ_ONLY,
         CAST(NULL AS CHAR(24)) DEF_CELLMEMORY,
         CAST(NULL AS CHAR(12)) DEF_INMEMORY_SERVICE,
         CAST(NULL AS CHAR(1000)) DEF_INMEMORY_SERVICE_NAME,
         CAST('NO' AS CHAR(3)) AUTO
      FROM
			oceanbase.__all_table TB
      JOIN OCEANBASE.__ALL_DATABASE DB
      ON TB.DATABASE_ID = DB.DATABASE_ID
      JOIN
        (SELECT
         TABLE_ID,
         SUM(CASE WHEN (PARTITION_KEY_POSITION & 255) > 0 THEN 1 ELSE 0 END) AS PART_KEY_COUNT,
         SUM(CASE WHEN (PARTITION_KEY_POSITION & 65280) > 0 THEN 1 ELSE 0 END) AS SUBPART_KEY_COUNT
         FROM OCEANBASE.__ALL_COLUMN
         WHERE PARTITION_KEY_POSITION > 0
         GROUP BY TABLE_ID) PART_INFO
      ON TB.TABLE_ID = PART_INFO.TABLE_ID
      WHERE TB.TABLE_TYPE IN (3, 6)
            AND TB.PART_LEVEL != 0
            AND TB.TABLE_MODE >> 12 & 15 in (0,1)
            AND TB.INDEX_ATTRIBUTES_SET & 16 = 0
  """.replace("\n", " ")
)

def_table_schema(
  owner           = 'yanmu.ztl',
  table_name      = 'DBA_PART_KEY_COLUMNS',
  database_id     = 'OB_SYS_DATABASE_ID',
  table_id        = '21206',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT  CAST(D.DATABASE_NAME AS CHAR(128)) AS OWNER,
            CAST(T.TABLE_NAME AS CHAR(128)) AS NAME,
            CAST('TABLE' AS CHAR(5)) AS OBJECT_TYPE,
            CAST(C.COLUMN_NAME AS CHAR(4000)) AS COLUMN_NAME,
            CAST((C.PARTITION_KEY_POSITION & 255) AS SIGNED) AS COLUMN_POSITION,
            CAST(NULL AS SIGNED) AS COLLATED_COLUMN_ID
    FROM OCEANBASE.__ALL_COLUMN C, OCEANBASE.__ALL_TABLE T, OCEANBASE.__ALL_DATABASE D
    WHERE C.TABLE_ID = T.TABLE_ID
          AND T.DATABASE_ID = D.DATABASE_ID
          AND (C.PARTITION_KEY_POSITION & 255) > 0
          AND T.TABLE_TYPE IN (3, 6)
          AND T.TABLE_MODE >> 12 & 15 in (0,1)
          AND T.INDEX_ATTRIBUTES_SET & 16 = 0
    UNION
    SELECT  CAST(D.DATABASE_NAME AS CHAR(128)) AS OWNER,
            CAST(CASE WHEN D.DATABASE_NAME = '__recyclebin' THEN T.TABLE_NAME
                ELSE SUBSTR(T.TABLE_NAME, 7 + POSITION('_' IN SUBSTR(T.TABLE_NAME, 7))) END AS CHAR(128)) AS NAME,
            CAST('INDEX' AS CHAR(5)) AS OBJECT_TYPE,
            CAST(C.COLUMN_NAME AS CHAR(4000)) AS COLUMN_NAME,
            CAST((C.PARTITION_KEY_POSITION & 255) AS SIGNED) AS COLUMN_POSITION,
            CAST(NULL AS SIGNED) AS COLLATED_COLUMN_ID
    FROM OCEANBASE.__ALL_COLUMN C, OCEANBASE.__ALL_TABLE T, OCEANBASE.__ALL_DATABASE D
    WHERE T.DATABASE_ID = D.DATABASE_ID
          AND C.TABLE_ID = T.TABLE_ID
          AND T.TABLE_TYPE = 5
          AND T.INDEX_TYPE NOT IN (17,19,20,22)
          AND (C.PARTITION_KEY_POSITION & 255) > 0
    UNION
    SELECT  CAST(D.DATABASE_NAME AS CHAR(128)) AS OWNER,
            CAST(CASE WHEN D.DATABASE_NAME =  '__recyclebin' THEN T.TABLE_NAME
                ELSE SUBSTR(T.TABLE_NAME, 7 + POSITION('_' IN SUBSTR(T.TABLE_NAME, 7))) END AS CHAR(128)) AS NAME,
            CAST('INDEX' AS CHAR(5)) AS OBJECT_TYPE,
            CAST(C.COLUMN_NAME AS CHAR(4000)) AS COLUMN_NAME,
            CAST((C.PARTITION_KEY_POSITION & 255) AS SIGNED) AS COLUMN_POSITION,
            CAST(NULL AS SIGNED) AS COLLATED_COLUMN_ID
    FROM OCEANBASE.__ALL_COLUMN C, OCEANBASE.__ALL_TABLE T, OCEANBASE.__ALL_DATABASE D
    WHERE T.DATABASE_ID = D.DATABASE_ID
          AND C.TABLE_ID = T.DATA_TABLE_ID
          AND T.TABLE_TYPE = 5
          AND T.INDEX_TYPE IN (1,2,10,15,23,24,41)
          AND (C.PARTITION_KEY_POSITION & 255) > 0
""".replace("\n", " ")
)

def_table_schema(
  owner           = 'yanmu.ztl',
  table_name      = 'DBA_SUBPART_KEY_COLUMNS',
  database_id     = 'OB_SYS_DATABASE_ID',
  table_id        = '21207',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT  CAST(D.DATABASE_NAME AS CHAR(128)) AS OWNER,
            CAST(T.TABLE_NAME AS CHAR(128)) AS NAME,
            CAST('TABLE' AS CHAR(5)) AS OBJECT_TYPE,
            CAST(C.COLUMN_NAME AS CHAR(4000)) AS COLUMN_NAME,
            CAST((C.PARTITION_KEY_POSITION & 65280)/256 AS SIGNED) AS COLUMN_POSITION,
            CAST(NULL AS SIGNED) AS COLLATED_COLUMN_ID
    FROM OCEANBASE.__ALL_COLUMN C, OCEANBASE.__ALL_TABLE T, OCEANBASE.__ALL_DATABASE D
    WHERE C.TABLE_ID = T.TABLE_ID
          AND T.DATABASE_ID = D.DATABASE_ID
          AND (C.PARTITION_KEY_POSITION & 65280) > 0
          AND T.TABLE_TYPE IN (3, 6)
          AND T.TABLE_MODE >> 12 & 15 in (0,1)
          AND T.INDEX_ATTRIBUTES_SET & 16 = 0
    UNION
    SELECT  CAST(D.DATABASE_NAME AS CHAR(128)) AS OWNER,
            CAST(CASE WHEN D.DATABASE_NAME = '__recyclebin' THEN T.TABLE_NAME
                ELSE SUBSTR(T.TABLE_NAME, 7 + POSITION('_' IN SUBSTR(T.TABLE_NAME, 7))) END AS CHAR(128)) AS NAME,
            CAST('INDEX' AS CHAR(5)) AS OBJECT_TYPE,
            CAST(C.COLUMN_NAME AS CHAR(4000)) AS COLUMN_NAME,
            CAST((C.PARTITION_KEY_POSITION & 65280)/256 AS SIGNED) AS COLUMN_POSITION,
            CAST(NULL AS SIGNED) AS COLLATED_COLUMN_ID
    FROM OCEANBASE.__ALL_COLUMN C, OCEANBASE.__ALL_TABLE T, OCEANBASE.__ALL_DATABASE D
    WHERE T.DATABASE_ID = D.DATABASE_ID
          AND C.TABLE_ID = T.TABLE_ID
          AND T.TABLE_TYPE = 5
          AND T.INDEX_TYPE NOT IN (17,19,20,22)
          AND (C.PARTITION_KEY_POSITION & 65280) > 0
    UNION
    SELECT  CAST(D.DATABASE_NAME AS CHAR(128)) AS OWNER,
            CAST(CASE WHEN D.DATABASE_NAME =  '__recyclebin' THEN T.TABLE_NAME
                ELSE SUBSTR(T.TABLE_NAME, 7 + POSITION('_' IN SUBSTR(T.TABLE_NAME, 7))) END AS CHAR(128)) AS NAME,
            CAST('INDEX' AS CHAR(5)) AS OBJECT_TYPE,
            CAST(C.COLUMN_NAME AS CHAR(4000)) AS COLUMN_NAME,
            CAST((C.PARTITION_KEY_POSITION & 65280)/256 AS SIGNED) AS COLUMN_POSITION,
            CAST(NULL AS SIGNED) AS COLLATED_COLUMN_ID
    FROM OCEANBASE.__ALL_COLUMN C, OCEANBASE.__ALL_TABLE T, OCEANBASE.__ALL_DATABASE D
    WHERE T.DATABASE_ID = D.DATABASE_ID
          AND C.TABLE_ID = T.DATA_TABLE_ID
          AND T.TABLE_TYPE = 5
          AND T.INDEX_TYPE IN (1,2,10,15,23,24,41)
          AND (C.PARTITION_KEY_POSITION & 65280) > 0
""".replace("\n", " ")
)

def_table_schema(
  owner = 'yanmu.ztl',
  table_name      = 'DBA_TAB_PARTITIONS',
  database_id     = 'OB_SYS_DATABASE_ID',
  table_id        = '21208',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
      CAST(DB_TB.DATABASE_NAME AS CHAR(128)) TABLE_OWNER,
      CAST(DB_TB.TABLE_NAME AS CHAR(128)) TABLE_NAME,

      CAST(CASE DB_TB.PART_LEVEL
           WHEN 2 THEN 'YES'
           ELSE 'NO' END AS CHAR(3)) COMPOSITE,

      CAST(PART.PART_NAME AS CHAR(128)) PARTITION_NAME,

      CAST(CASE DB_TB.PART_LEVEL
           WHEN 2 THEN PART.SUB_PART_NUM
           ELSE 0 END AS SIGNED)  SUBPARTITION_COUNT,

      CAST(CASE WHEN length(PART.HIGH_BOUND_VAL) > 0 THEN PART.HIGH_BOUND_VAL
           ELSE PART.LIST_VAL END AS CHAR(262144)) HIGH_VALUE,

      CAST(CASE WHEN length(PART.HIGH_BOUND_VAL) > 0 THEN length(PART.HIGH_BOUND_VAL)
           ELSE length(PART.LIST_VAL) END AS SIGNED) HIGH_VALUE_LENGTH,

      CAST(PART.PARTITION_POSITION AS SIGNED) PARTITION_POSITION,
      CAST(NULL AS CHAR(30)) TABLESPACE_NAME,
      CAST(NULL AS SIGNED) PCT_FREE,
      CAST(NULL AS SIGNED) PCT_USED,
      CAST(NULL AS SIGNED) INI_TRANS,
      CAST(NULL AS SIGNED) MAX_TRANS,
      CAST(NULL AS SIGNED) INITIAL_EXTENT,
      CAST(NULL AS SIGNED) NEXT_EXTENT,
      CAST(NULL AS SIGNED) MIN_EXTENT,
      CAST(NULL AS SIGNED) MAX_EXTENT,
      CAST(NULL AS SIGNED) MAX_SIZE,
      CAST(NULL AS SIGNED) PCT_INCREASE,
      CAST(NULL AS SIGNED) FREELISTS,
      CAST(NULL AS SIGNED) FREELIST_GROUPS,
      CAST(NULL AS CHAR(7)) LOGGING,

      CAST(CASE WHEN PART.COMPRESS_FUNC_NAME IS NULL THEN 'DISABLED'
           ELSE 'ENABLED' END AS CHAR(8)) COMPRESSION,

      CAST(PART.COMPRESS_FUNC_NAME AS CHAR(30)) COMPRESS_FOR,
      CAST(NULL AS SIGNED) NUM_ROWS,
      CAST(NULL AS SIGNED) BLOCKS,
      CAST(NULL AS SIGNED) EMPTY_BLOCKS,
      CAST(NULL AS SIGNED) AVG_SPACE,
      CAST(NULL AS SIGNED) CHAIN_CNT,
      CAST(NULL AS SIGNED) AVG_ROW_LEN,
      CAST(NULL AS SIGNED) SAMPLE_SIZE,
      CAST(NULL AS DATE) LAST_ANALYZED,
      CAST(NULL AS CHAR(7)) BUFFER_POOL,
      CAST(NULL AS CHAR(7)) FLASH_CACHE,
      CAST(NULL AS CHAR(7)) CELL_FLASH_CACHE,
      CAST(NULL AS CHAR(3)) GLOBAL_STATS,
      CAST(NULL AS CHAR(3)) USER_STATS,
      CAST(NULL AS CHAR(3)) IS_NESTED,
      CAST(NULL AS CHAR(128)) PARENT_TABLE_PARTITION,

      CAST (CASE WHEN PART.PARTITION_POSITION >
            MAX (CASE WHEN PART.HIGH_BOUND_VAL = DB_TB.B_TRANSITION_POINT
                 THEN PART.PARTITION_POSITION ELSE NULL END)
            OVER(PARTITION BY DB_TB.TABLE_ID)
            THEN 'YES' ELSE 'NO' END AS CHAR(3)) "INTERVAL",

      CAST(NULL AS CHAR(4)) SEGMENT_CREATED,
      CAST(NULL AS CHAR(4)) INDEXING,
      CAST(NULL AS CHAR(4)) READ_ONLY,
      CAST(NULL AS CHAR(8)) INMEMORY,
      CAST(NULL AS CHAR(8)) INMEMORY_PRIORITY,
      CAST(NULL AS CHAR(15)) INMEMORY_DISTRIBUTE,
      CAST(NULL AS CHAR(17)) INMEMORY_COMPRESSION,
      CAST(NULL AS CHAR(13)) INMEMORY_DUPLICATE,
      CAST(NULL AS CHAR(24)) CELLMEMORY,
      CAST(NULL AS CHAR(12)) INMEMORY_SERVICE,
      CAST(NULL AS CHAR(100)) INMEMORY_SERVICE_NAME,
      CAST(NULL AS CHAR(8)) MEMOPTIMIZE_READ,
      CAST(NULL AS CHAR(8)) MEMOPTIMIZE_WRITE

      FROM (SELECT DB.DATABASE_NAME,
                   DB.DATABASE_ID,
                   TB.TABLE_ID,
                   TB.TABLE_NAME AS TABLE_NAME,
                   TB.B_TRANSITION_POINT,
                   TB.PART_LEVEL
            FROM
			      oceanbase.__all_table TB,
                 OCEANBASE.__ALL_DATABASE DB
            WHERE TB.DATABASE_ID = DB.DATABASE_ID
              AND TB.TABLE_TYPE in (3, 6)
              AND TB.TABLE_MODE >> 12 & 15 in (0,1)
              AND TB.INDEX_ATTRIBUTES_SET & 16 = 0
           ) DB_TB
      JOIN (SELECT TABLE_ID,
                   PART_NAME,
                   SUB_PART_NUM,
                   HIGH_BOUND_VAL,
                   LIST_VAL,
                   COMPRESS_FUNC_NAME,
                   TABLESPACE_ID,
                   PARTITION_TYPE,
                   ROW_NUMBER() OVER (
                     PARTITION BY TABLE_ID
                     ORDER BY PART_IDX, PART_ID ASC
                   ) PARTITION_POSITION
            FROM OCEANBASE.__ALL_PART) PART
      ON DB_TB.TABLE_ID = PART.TABLE_ID

      WHERE PART.PARTITION_TYPE = 0
""".replace("\n", " ")
)

def_table_schema(
  owner           = 'yanmu.ztl',
  table_name      = 'DBA_TAB_SUBPARTITIONS',
  database_id     = 'OB_SYS_DATABASE_ID',
  table_id        = '21209',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
      CAST(DB_TB.DATABASE_NAME AS CHAR(128)) TABLE_OWNER,
      CAST(DB_TB.TABLE_NAME AS CHAR(128)) TABLE_NAME,
      CAST(PART.PART_NAME AS CHAR(128)) PARTITION_NAME,
      CAST(PART.SUB_PART_NAME AS CHAR(128))  SUBPARTITION_NAME,
      CAST(CASE WHEN length(PART.HIGH_BOUND_VAL) > 0 THEN PART.HIGH_BOUND_VAL
           ELSE PART.LIST_VAL END AS CHAR(262144)) HIGH_VALUE,
      CAST(CASE WHEN length(PART.HIGH_BOUND_VAL) > 0 THEN length(PART.HIGH_BOUND_VAL)
           ELSE length(PART.LIST_VAL) END AS SIGNED) HIGH_VALUE_LENGTH,
      CAST(PART.PARTITION_POSITION AS SIGNED) PARTITION_POSITION,
      CAST(PART.SUBPARTITION_POSITION AS SIGNED) SUBPARTITION_POSITION,
      CAST(NULL AS CHAR(30)) TABLESPACE_NAME,
      CAST(NULL AS SIGNED) PCT_FREE,
      CAST(NULL AS SIGNED) PCT_USED,
      CAST(NULL AS SIGNED) INI_TRANS,
      CAST(NULL AS SIGNED) MAX_TRANS,
      CAST(NULL AS SIGNED) INITIAL_EXTENT,
      CAST(NULL AS SIGNED) NEXT_EXTENT,
      CAST(NULL AS SIGNED) MIN_EXTENT,
      CAST(NULL AS SIGNED) MAX_EXTENT,
      CAST(NULL AS SIGNED) MAX_SIZE,
      CAST(NULL AS SIGNED) PCT_INCREASE,
      CAST(NULL AS SIGNED) FREELISTS,
      CAST(NULL AS SIGNED) FREELIST_GROUPS,
      CAST(NULL AS CHAR(3)) LOGGING,
      CAST(CASE WHEN
      PART.COMPRESS_FUNC_NAME IS NULL THEN
      'DISABLED' ELSE 'ENABLED' END AS CHAR(8)) COMPRESSION,
      CAST(PART.COMPRESS_FUNC_NAME AS CHAR(30)) COMPRESS_FOR,
      CAST(NULL AS SIGNED) NUM_ROWS,
      CAST(NULL AS SIGNED) BLOCKS,
      CAST(NULL AS SIGNED) EMPTY_BLOCKS,
      CAST(NULL AS SIGNED) AVG_SPACE,
      CAST(NULL AS SIGNED) CHAIN_CNT,
      CAST(NULL AS SIGNED) AVG_ROW_LEN,
      CAST(NULL AS SIGNED) SAMPLE_SIZE,
      CAST(NULL AS DATE) LAST_ANALYZED,
      CAST(NULL AS CHAR(7)) BUFFER_POOL,
      CAST(NULL AS CHAR(7)) FLASH_CACHE,
      CAST(NULL AS CHAR(7)) CELL_FLASH_CACHE,
      CAST(NULL AS CHAR(3)) GLOBAL_STATS,
      CAST(NULL AS CHAR(3)) USER_STATS,
      CAST('NO' AS CHAR(3)) "INTERVAL",
      CAST(NULL AS CHAR(3)) SEGMENT_CREATED,
      CAST(NULL AS CHAR(3)) INDEXING,
      CAST(NULL AS CHAR(3)) READ_ONLY,
      CAST(NULL AS CHAR(8)) INMEMORY,
      CAST(NULL AS CHAR(8)) INMEMORY_PRIORITY,
      CAST(NULL AS CHAR(15)) INMEMORY_DISTRIBUTE,
      CAST(NULL AS CHAR(17)) INMEMORY_COMPRESSION,
      CAST(NULL AS CHAR(13)) INMEMORY_DUPLICATE,
      CAST(NULL AS CHAR(12)) INMEMORY_SERVICE,
      CAST(NULL AS CHAR(1000)) INMEMORY_SERVICE_NAME,
      CAST(NULL AS CHAR(24)) CELLMEMORY,
      CAST(NULL AS CHAR(8)) MEMOPTIMIZE_READ,
      CAST(NULL AS CHAR(8)) MEMOPTIMIZE_WRITE
      FROM
      (SELECT DB.DATABASE_NAME,
              DB.DATABASE_ID,
              TB.TABLE_ID,
              TB.TABLE_NAME AS TABLE_NAME
       FROM
			 oceanbase.__all_table TB,
             OCEANBASE.__ALL_DATABASE DB
       WHERE TB.DATABASE_ID = DB.DATABASE_ID
         AND TB.TABLE_MODE >> 12 & 15 in (0,1)
         AND TB.INDEX_ATTRIBUTES_SET & 16 = 0
         AND TB.TABLE_TYPE IN (3, 6)) DB_TB
      JOIN
      (SELECT P_PART.TABLE_ID,
              P_PART.PART_NAME,
              P_PART.PARTITION_POSITION,
              S_PART.SUB_PART_NAME,
              S_PART.HIGH_BOUND_VAL,
              S_PART.LIST_VAL,
              S_PART.COMPRESS_FUNC_NAME,
              S_PART.TABLESPACE_ID,
              S_PART.SUBPARTITION_POSITION
       FROM (SELECT
               TABLE_ID,
               PART_ID,
               PART_NAME,
               PARTITION_TYPE,
               ROW_NUMBER() OVER (
                 PARTITION BY TABLE_ID
                 ORDER BY PART_IDX, PART_ID ASC
               ) AS PARTITION_POSITION
             FROM OCEANBASE.__ALL_PART) P_PART,
            (SELECT
               TABLE_ID,
               PART_ID,
               SUB_PART_NAME,
               HIGH_BOUND_VAL,
               LIST_VAL,
               COMPRESS_FUNC_NAME,
               TABLESPACE_ID,
               PARTITION_TYPE,
               ROW_NUMBER() OVER (
                 PARTITION BY TABLE_ID, PART_ID
                 ORDER BY SUB_PART_IDX, SUB_PART_ID ASC
               ) AS SUBPARTITION_POSITION
             FROM OCEANBASE.__ALL_SUB_PART) S_PART
       WHERE P_PART.PART_ID = S_PART.PART_ID AND
             P_PART.TABLE_ID = S_PART.TABLE_ID
             AND P_PART.PARTITION_TYPE = 0
             AND S_PART.PARTITION_TYPE = 0) PART
      ON DB_TB.TABLE_ID = PART.TABLE_ID

""".replace("\n", " ")
)

def_table_schema(
  owner           = 'yanmu.ztl',
  table_name      = 'DBA_SUBPARTITION_TEMPLATES',
  database_id     = 'OB_SYS_DATABASE_ID',
  table_id        = '21210',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
      CAST(DB.DATABASE_NAME AS CHAR(128)) USER_NAME,
      CAST(TB.TABLE_NAME AS CHAR(128)) TABLE_NAME,
      CAST(SP.SUB_PART_NAME AS CHAR(132)) SUBPARTITION_NAME,
      CAST(SP.SUB_PART_ID + 1 AS SIGNED) SUBPARTITION_POSITION,
      CAST(NULL AS CHAR(30)) TABLESPACE_NAME,
      CAST(CASE WHEN SP.HIGH_BOUND_VAL IS NULL THEN SP.LIST_VAL
           ELSE SP.HIGH_BOUND_VAL END AS CHAR(262144)) HIGH_BOUND,
      CAST(NULL AS CHAR(4)) COMPRESSION,
      CAST(NULL AS CHAR(4)) INDEXING,
      CAST(NULL AS CHAR(4)) READ_ONLY

      FROM OCEANBASE.__ALL_DATABASE DB

      JOIN OCEANBASE.__ALL_TABLE TB
      ON DB.DATABASE_ID = TB.DATABASE_ID
         AND TB.TABLE_TYPE IN (3, 6)
         AND TB.TABLE_MODE >> 12 & 15 in (0,1)
         AND TB.INDEX_ATTRIBUTES_SET & 16 = 0

      JOIN OCEANBASE.__ALL_DEF_SUB_PART SP
      ON TB.TABLE_ID = SP.TABLE_ID

      """.replace("\n", " ")
)

def_table_schema(
  owner           = 'yanmu.ztl',
  table_name      = 'DBA_PART_INDEXES',
  database_id     = 'OB_SYS_DATABASE_ID',
  table_id        = '21211',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition =
    """
SELECT
CAST(I_T.OWNER AS CHAR(128)) AS OWNER,
CAST(I_T.INDEX_NAME AS CHAR(128)) AS INDEX_NAME,
CAST(I_T.TABLE_NAME AS CHAR(128)) AS TABLE_NAME,

CAST(CASE I_T.PART_FUNC_TYPE
     WHEN 0 THEN 'HASH'
     WHEN 1 THEN 'KEY'
     WHEN 2 THEN 'KEY'
     WHEN 3 THEN 'RANGE'
     WHEN 4 THEN 'RANGE COLUMNS'
     WHEN 5 THEN 'LIST'
     WHEN 6 THEN 'LIST COLUMNS'
     WHEN 7 THEN 'RANGE' END AS CHAR(13)) AS PARTITIONING_TYPE,

CAST(CASE WHEN I_T.PART_LEVEL < 2 THEN 'NONE'
     ELSE (CASE I_T.SUB_PART_FUNC_TYPE
           WHEN 0 THEN 'HASH'
           WHEN 1 THEN 'KEY'
           WHEN 2 THEN 'KEY'
           WHEN 3 THEN 'RANGE'
           WHEN 4 THEN 'RANGE COLUMNS'
           WHEN 5 THEN 'LIST'
           WHEN 6 THEN 'LIST COLUMNS'
           WHEN 7 THEN 'RANGE' END)
     END AS CHAR(13)) AS SUBPARTITIONING_TYPE,

CAST(I_T.PART_NUM AS SIGNED) AS PARTITION_COUNT,

CAST(CASE WHEN (I_T.PART_LEVEL < 2 OR I_T.SUB_PART_TEMPLATE_FLAGS = 0) THEN 0
     ELSE I_T.SUB_PART_NUM END AS SIGNED) AS DEF_SUBPARTITION_COUNT,

CAST(PKC.PARTITIONING_KEY_COUNT AS SIGNED) AS PARTITIONING_KEY_COUNT,
CAST(PKC.SUBPARTITIONING_KEY_COUNT AS SIGNED) AS SUBPARTITIONING_KEY_COUNT,

CAST(CASE I_T.IS_LOCAL WHEN 1 THEN 'LOCAL'
     ELSE 'GLOBAL' END AS CHAR(6)) AS LOCALITY,

CAST(CASE WHEN I_T.IS_LOCAL = 0 THEN 'PREFIXED'
          WHEN (I_T.IS_LOCAL = 1 AND LOCAL_PARTITIONED_PREFIX_INDEX.IS_PREFIXED = 1) THEN 'PREFIXED'
          ELSE 'NON_PREFIXED' END AS CHAR(12)) AS ALIGNMENT,

CAST(NULL AS CHAR(30)) AS DEF_TABLESPACE_NAME,
CAST(0 AS SIGNED) AS DEF_PCT_FREE,
CAST(0 AS SIGNED) AS DEF_INI_TRANS,
CAST(0 AS SIGNED) AS DEF_MAX_TRANS,
CAST(NULL AS CHAR(40)) AS DEF_INITIAL_EXTENT,
CAST(NULL AS CHAR(40)) AS DEF_NEXT_EXTENT,
CAST(NULL AS CHAR(40)) AS DEF_MIN_EXTENTS,
CAST(NULL AS CHAR(40)) AS DEF_MAX_EXTENTS,
CAST(NULL AS CHAR(40)) AS DEF_MAX_SIZE,
CAST(NULL AS CHAR(40)) AS DEF_PCT_INCREASE,
CAST(0 AS SIGNED) AS DEF_FREELISTS,
CAST(0 AS SIGNED) AS DEF_FREELIST_GROUPS,
CAST(NULL AS CHAR(7)) AS DEF_LOGGING,
CAST(NULL AS CHAR(7)) AS DEF_BUFFER_POOL,
CAST(NULL AS CHAR(7)) AS DEF_FLASH_CACHE,
CAST(NULL AS CHAR(7)) AS DEF_CELL_FLASH_CACHE,
CAST(NULL AS CHAR(1000)) AS DEF_PARAMETERS,
CAST('NO' AS CHAR(1000)) AS "INTERVAL",
CAST('NO' AS CHAR(3)) AS AUTOLIST,
CAST(NULL AS CHAR(1000)) AS INTERVAL_SUBPARTITION,
CAST(NULL AS CHAR(1000)) AS AUTOLIST_SUBPARTITION

FROM
(SELECT D.DATABASE_NAME AS OWNER,
        I.TABLE_ID AS INDEX_ID,
        CAST(CASE WHEN D.DATABASE_NAME = '__recyclebin' THEN I.TABLE_NAME
            ELSE SUBSTR(I.TABLE_NAME, 7 + POSITION('_' IN SUBSTR(I.TABLE_NAME, 7)))
            END AS CHAR(128)) AS INDEX_NAME,
        I.PART_LEVEL,
        I.PART_FUNC_TYPE,
        I.PART_NUM,
        I.SUB_PART_FUNC_TYPE,
        T.TABLE_NAME AS TABLE_NAME,
        T.SUB_PART_NUM,
        T.SUB_PART_TEMPLATE_FLAGS,
        T.TABLESPACE_ID,
        (CASE I.INDEX_TYPE
         WHEN 1 THEN 1
         WHEN 2 THEN 1
         WHEN 10 THEN 1
         WHEN 15 THEN 1
         WHEN 23 THEN 1
         WHEN 24 THEN 1
         WHEN 41 THEN 1
         ELSE 0 END) AS IS_LOCAL,
        (CASE I.INDEX_TYPE
         WHEN 1 THEN T.TABLE_ID
         WHEN 2 THEN T.TABLE_ID
         WHEN 10 THEN T.TABLE_ID
         WHEN 15 THEN T.TABLE_ID
         WHEN 23 THEN T.TABLE_ID
         WHEN 24 THEN T.TABLE_ID
         ELSE I.TABLE_ID END) AS JOIN_TABLE_ID
 FROM OCEANBASE.__ALL_TABLE I
 JOIN
			oceanbase.__all_table T
 ON I.DATA_TABLE_ID = T.TABLE_ID
 JOIN OCEANBASE.__ALL_DATABASE D
 ON T.DATABASE_ID = D.DATABASE_ID
 WHERE I.TABLE_TYPE = 5 AND I.INDEX_TYPE NOT IN (13, 14, 16, 17, 19, 20, 22) AND I.PART_LEVEL != 0
 AND I.TABLE_MODE >> 12 & 15 in (0,1)
 AND I.INDEX_ATTRIBUTES_SET & 16 = 0
) I_T

JOIN
(SELECT
   TABLE_ID,
   SUM(CASE WHEN (PARTITION_KEY_POSITION & 255) != 0 THEN 1 ELSE 0 END) AS PARTITIONING_KEY_COUNT,
   SUM(CASE WHEN (PARTITION_KEY_POSITION & 65280)/256 != 0 THEN 1 ELSE 0 END) AS SUBPARTITIONING_KEY_COUNT
   FROM OCEANBASE.__ALL_COLUMN
   GROUP BY TABLE_ID) PKC
ON I_T.JOIN_TABLE_ID = PKC.TABLE_ID

LEFT JOIN
(
 SELECT I.TABLE_ID AS INDEX_ID,
        1 AS IS_PREFIXED
 FROM OCEANBASE.__ALL_TABLE I
 WHERE I.TABLE_TYPE = 5
   AND I.INDEX_TYPE IN (1, 2, 10, 15, 23, 24, 41)
   AND I.PART_LEVEL != 0
 AND NOT EXISTS
 (SELECT *
  FROM
   (SELECT *
    FROM OCEANBASE.__ALL_COLUMN C
    WHERE C.TABLE_ID = I.DATA_TABLE_ID
      AND C.PARTITION_KEY_POSITION != 0
   ) PART_COLUMNS
   LEFT JOIN
   (SELECT *
    FROM OCEANBASE.__ALL_COLUMN C
    WHERE C.TABLE_ID = I.TABLE_ID
    AND C.INDEX_POSITION != 0
   ) INDEX_COLUMNS
   ON PART_COLUMNS.COLUMN_ID = INDEX_COLUMNS.COLUMN_ID
   WHERE
   ((PART_COLUMNS.PARTITION_KEY_POSITION & 255) != 0
    AND
    (INDEX_COLUMNS.INDEX_POSITION IS NULL
     OR (PART_COLUMNS.PARTITION_KEY_POSITION & 255) != INDEX_COLUMNS.INDEX_POSITION)
   )
   OR
   ((PART_COLUMNS.PARTITION_KEY_POSITION & 65280)/256 != 0
    AND (INDEX_COLUMNS.INDEX_POSITION IS NULL)
   )
 )
) LOCAL_PARTITIONED_PREFIX_INDEX
ON I_T.INDEX_ID = LOCAL_PARTITIONED_PREFIX_INDEX.INDEX_ID

    """
 .replace("\n", " ")
)

def_table_schema(
  owner           = 'yanmu.ztl',
  table_name      = 'DBA_IND_PARTITIONS',
  database_id     = 'OB_SYS_DATABASE_ID',
  table_id        = '21212',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(D.DATABASE_NAME AS CHAR(128)) AS INDEX_OWNER,
    CAST(CASE WHEN D.DATABASE_NAME = '__recyclebin' THEN I.TABLE_NAME
        ELSE SUBSTR(I.TABLE_NAME, 7 + POSITION('_' IN SUBSTR(I.TABLE_NAME, 7)))
        END AS CHAR(128)) AS INDEX_NAME,
    CAST(DT.TABLE_NAME AS CHAR(128)) AS TABLE_NAME,

    CAST(CASE I.PART_LEVEL
         WHEN 2 THEN 'YES'
         ELSE 'NO' END AS CHAR(3)) COMPOSITE,

    CAST(PART.PART_NAME AS CHAR(128)) AS PARTITION_NAME,

    CAST(CASE I.PART_LEVEL
         WHEN 2 THEN PART.SUB_PART_NUM
         ELSE 0 END AS SIGNED)  SUBPARTITION_COUNT,

    CAST(CASE WHEN length(PART.HIGH_BOUND_VAL) > 0 THEN PART.HIGH_BOUND_VAL
         ELSE PART.LIST_VAL END AS CHAR(262144)) HIGH_VALUE,

    CAST(CASE WHEN length(PART.HIGH_BOUND_VAL) > 0 THEN length(PART.HIGH_BOUND_VAL)
         ELSE length(PART.LIST_VAL) END AS SIGNED) HIGH_VALUE_LENGTH,

    CAST(PART.PARTITION_POSITION AS SIGNED) PARTITION_POSITION,
    CAST(NULL AS CHAR(8)) AS STATUS,
    CAST(NULL AS CHAR(30)) AS TABLESPACE_NAME,
    CAST(NULL AS SIGNED) AS PCT_FREE,
    CAST(NULL AS SIGNED) AS INI_TRANS,
    CAST(NULL AS SIGNED) AS MAX_TRANS,
    CAST(NULL AS SIGNED) AS INITIAL_EXTENT,
    CAST(NULL AS SIGNED) AS NEXT_EXTENT,
    CAST(NULL AS SIGNED) AS MIN_EXTENT,
    CAST(NULL AS SIGNED) AS MAX_EXTENT,
    CAST(NULL AS SIGNED) AS MAX_SIZE,
    CAST(NULL AS SIGNED) AS PCT_INCREASE,
    CAST(NULL AS SIGNED) AS FREELISTS,
    CAST(NULL AS SIGNED) AS FREELIST_GROUPS,
    CAST(NULL AS CHAR(7)) AS LOGGING,
    CAST(CASE WHEN PART.COMPRESS_FUNC_NAME IS NULL THEN 'DISABLED' ELSE 'ENABLED' END AS CHAR(13)) AS COMPRESSION,
    CAST(NULL AS SIGNED) AS BLEVEL,
    CAST(NULL AS SIGNED) AS LEAF_BLOCKS,
    CAST(NULL AS SIGNED) AS DISTINCT_KEYS,
    CAST(NULL AS SIGNED) AS AVG_LEAF_BLOCKS_PER_KEY,
    CAST(NULL AS SIGNED) AS AVG_DATA_BLOCKS_PER_KEY,
    CAST(NULL AS SIGNED) AS CLUSTERING_FACTOR,
    CAST(NULL AS SIGNED) AS NUM_ROWS,
    CAST(NULL AS SIGNED) AS SAMPLE_SIZE,
    CAST(NULL AS DATE) AS LAST_ANALYZED,
    CAST(NULL AS CHAR(7)) AS BUFFER_POOL,
    CAST(NULL AS CHAR(7)) AS FLASH_CACHE,
    CAST(NULL AS CHAR(7)) AS CELL_FLASH_CACHE,
    CAST(NULL AS CHAR(3)) AS USER_STATS,
    CAST(NULL AS SIGNED) AS PCT_DIRECT_ACCESS,
    CAST(NULL AS CHAR(3)) AS GLOBAL_STATS,
    CAST(NULL AS CHAR(6)) AS DOMIDX_OPSTATUS,
    CAST(NULL AS CHAR(1000)) AS PARAMETERS,
    CAST('NO' AS CHAR(3)) AS "INTERVAL",
    CAST(NULL AS CHAR(3)) AS SEGMENT_CREATED,
    CAST(NULL AS CHAR(3)) AS ORPHANED_ENTRIES
    FROM
    OCEANBASE.__ALL_TABLE I
    JOIN OCEANBASE.__ALL_TABLE DT
    ON I.DATA_TABLE_ID = DT.TABLE_ID
    JOIN OCEANBASE.__ALL_DATABASE D
    ON I.DATABASE_ID = D.DATABASE_ID
       AND I.TABLE_TYPE = 5

    JOIN (SELECT TABLE_ID,
                 PART_NAME,
                 SUB_PART_NUM,
                 HIGH_BOUND_VAL,
                 LIST_VAL,
                 COMPRESS_FUNC_NAME,
                 PARTITION_TYPE,
                 ROW_NUMBER() OVER (
                   PARTITION BY TABLE_ID
                   ORDER BY PART_IDX, PART_ID ASC
                 ) PARTITION_POSITION
          FROM OCEANBASE.__ALL_PART) PART
    ON I.TABLE_ID = PART.TABLE_ID

    WHERE I.TABLE_MODE >> 12 & 15 in (0,1)
        AND PART.PARTITION_TYPE = 0 AND I.INDEX_ATTRIBUTES_SET & 16 = 0
""".replace("\n", " ")
)

def_table_schema(
  owner           = 'yanmu.ztl',
  table_name      = 'DBA_IND_SUBPARTITIONS',
  database_id     = 'OB_SYS_DATABASE_ID',
  table_id        = '21213',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(D.DATABASE_NAME AS CHAR(128)) AS INDEX_OWNER,
    CAST(CASE WHEN D.DATABASE_NAME = '__recyclebin' THEN I.TABLE_NAME
        ELSE SUBSTR(I.TABLE_NAME, 7 + POSITION('_' IN SUBSTR(I.TABLE_NAME, 7)))
        END AS CHAR(128)) AS INDEX_NAME,
    CAST(DT.TABLE_NAME AS CHAR(128)) AS TABLE_NAME,
    CAST(PART.PART_NAME AS CHAR(128)) PARTITION_NAME,
    CAST(PART.SUB_PART_NAME AS CHAR(128))  SUBPARTITION_NAME,
    CAST(CASE WHEN length(PART.HIGH_BOUND_VAL) > 0 THEN PART.HIGH_BOUND_VAL
         ELSE PART.LIST_VAL END AS CHAR(262144)) HIGH_VALUE,
    CAST(CASE WHEN length(PART.HIGH_BOUND_VAL) > 0 THEN length(PART.HIGH_BOUND_VAL)
         ELSE length(PART.LIST_VAL) END AS SIGNED) HIGH_VALUE_LENGTH,
    CAST(PART.PARTITION_POSITION AS SIGNED) PARTITION_POSITION,
    CAST(PART.SUBPARTITION_POSITION AS SIGNED) SUBPARTITION_POSITION,
    CAST(NULL AS CHAR(8)) AS STATUS,
    CAST(NULL AS CHAR(30)) AS TABLESPACE_NAME,
    CAST(NULL AS SIGNED) AS PCT_FREE,
    CAST(NULL AS SIGNED) AS INI_TRANS,
    CAST(NULL AS SIGNED) AS MAX_TRANS,
    CAST(NULL AS SIGNED) AS INITIAL_EXTENT,
    CAST(NULL AS SIGNED) AS NEXT_EXTENT,
    CAST(NULL AS SIGNED) AS MIN_EXTENT,
    CAST(NULL AS SIGNED) AS MAX_EXTENT,
    CAST(NULL AS SIGNED) AS MAX_SIZE,
    CAST(NULL AS SIGNED) AS PCT_INCREASE,
    CAST(NULL AS SIGNED) AS FREELISTS,
    CAST(NULL AS SIGNED) AS FREELIST_GROUPS,
    CAST(NULL AS CHAR(3)) AS LOGGING,
    CAST(CASE WHEN PART.COMPRESS_FUNC_NAME IS NULL THEN 'DISABLED' ELSE 'ENABLED' END AS CHAR(13)) AS COMPRESSION,
    CAST(NULL AS SIGNED) AS BLEVEL,
    CAST(NULL AS SIGNED) AS LEAF_BLOCKS,
    CAST(NULL AS SIGNED) AS DISTINCT_KEYS,
    CAST(NULL AS SIGNED) AS AVG_LEAF_BLOCKS_PER_KEY,
    CAST(NULL AS SIGNED) AS AVG_DATA_BLOCKS_PER_KEY,
    CAST(NULL AS SIGNED) AS CLUSTERING_FACTOR,
    CAST(NULL AS SIGNED) AS NUM_ROWS,
    CAST(NULL AS SIGNED) AS SAMPLE_SIZE,
    CAST(NULL AS DATE) AS LAST_ANALYZED,
    CAST(NULL AS CHAR(7)) AS BUFFER_POOL,
    CAST(NULL AS CHAR(7)) AS FLASH_CACHE,
    CAST(NULL AS CHAR(7)) AS CELL_FLASH_CACHE,
    CAST(NULL AS CHAR(3)) AS USER_STATS,
    CAST(NULL AS CHAR(3)) AS GLOBAL_STATS,
    CAST('NO' AS CHAR(3)) AS "INTERVAL",
    CAST(NULL AS CHAR(3)) AS SEGMENT_CREATED,
    CAST(NULL AS CHAR(6)) AS DOMIDX_OPSTATUS,
    CAST(NULL AS CHAR(1000)) AS PARAMETERS
    FROM OCEANBASE.__ALL_TABLE I
    JOIN OCEANBASE.__ALL_TABLE DT
    ON I.DATA_TABLE_ID = DT.TABLE_ID
    JOIN OCEANBASE.__ALL_DATABASE D
    ON I.DATABASE_ID = D.DATABASE_ID
       AND I.TABLE_TYPE = 5
    JOIN
    (SELECT P_PART.TABLE_ID,
            P_PART.PART_NAME,
            P_PART.PARTITION_POSITION,
            S_PART.SUB_PART_NAME,
            S_PART.HIGH_BOUND_VAL,
            S_PART.LIST_VAL,
            S_PART.COMPRESS_FUNC_NAME,
            S_PART.SUBPARTITION_POSITION
     FROM (SELECT
             TABLE_ID,
             PART_ID,
             PART_NAME,
             PARTITION_TYPE,
             ROW_NUMBER() OVER (
               PARTITION BY TABLE_ID
               ORDER BY PART_IDX, PART_ID ASC
             ) AS PARTITION_POSITION
           FROM OCEANBASE.__ALL_PART) P_PART,
          (SELECT
             TABLE_ID,
             PART_ID,
             SUB_PART_NAME,
             HIGH_BOUND_VAL,
             LIST_VAL,
             COMPRESS_FUNC_NAME,
             PARTITION_TYPE,
             ROW_NUMBER() OVER (
               PARTITION BY TABLE_ID, PART_ID
               ORDER BY SUB_PART_IDX, SUB_PART_ID ASC
             ) AS SUBPARTITION_POSITION
           FROM OCEANBASE.__ALL_SUB_PART) S_PART
     WHERE P_PART.PART_ID = S_PART.PART_ID AND
           P_PART.TABLE_ID = S_PART.TABLE_ID
           AND P_PART.PARTITION_TYPE = 0
           AND S_PART.PARTITION_TYPE = 0) PART
    ON I.TABLE_ID = PART.TABLE_ID
    WHERE I.TABLE_MODE >> 12 & 15 in (0,1)
    AND I.INDEX_ATTRIBUTES_SET & 16 = 0
""".replace("\n", " ")
)

# 21214: GV$OB_SERVERS (abandoned)
# 21215: V$OB_SERVERS (rename to V$OB_SERVER_STAT)
def_table_schema(
  owner = 'wanhong.wwh',
  table_name      = 'V$OB_SERVER_STAT',
  table_id        = '21215',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = False, # sys tenant only
  view_definition = """
  SELECT
    SVR_IP,
    SVR_PORT,
    SQL_PORT,
    CPU_CAPACITY,
    CPU_CAPACITY_MAX,
    CPU_ASSIGNED,
    CPU_ASSIGNED_MAX,
    MEM_CAPACITY,
    MEM_ASSIGNED,
    LOG_DISK_CAPACITY,
    LOG_DISK_ASSIGNED,
    LOG_DISK_IN_USE,
    DATA_DISK_CAPACITY,
    DATA_DISK_ASSIGNED,
    DATA_DISK_IN_USE,
    DATA_DISK_HEALTH_STATUS,
    MEMORY_LIMIT,
    DATA_DISK_ALLOCATED,
    (CASE
        WHEN data_disk_abnormal_time > 0 THEN usec_to_time(data_disk_abnormal_time)
        ELSE NULL
     END) AS DATA_DISK_ABNORMAL_TIME,
    (CASE
        WHEN rpc_cert_expire_time > 0 THEN usec_to_time(rpc_cert_expire_time)
        ELSE NULL
     END) AS RPC_CERT_EXPIRE_TIME,
    START_SERVICE_TIME,
    USEC_TO_TIME(CREATE_TIME) AS CREATE_TIME,
    ROLE,
    LOG_RESTORE_SOURCE,
    SYNC_SCN,
    READABLE_SCN
  FROM oceanbase.__all_virtual_server_stat

""".replace("\n", " ")
)

# 21216: abandoned

# 21217: GV$OB_UNITS # removed (single-tenant GV/V collapse; folded into V$OB_UNITS)

def_table_schema(
  owner = 'fenggu.yh',
  table_name      = 'V$OB_UNITS',
  table_id        = '21218',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT
           MAX_CPU,
           MIN_CPU,
           MEMORY_SIZE,
           MAX_IOPS,
           MIN_IOPS,
           IOPS_WEIGHT,
           MAX_NET_BANDWIDTH,
           NET_BANDWIDTH_WEIGHT,
           LOG_DISK_SIZE,
           LOG_DISK_IN_USE,
           DATA_DISK_SIZE,
           DATA_DISK_IN_USE,
           STATUS,
           usec_to_time(create_time) AS CREATE_TIME
    FROM oceanbase.__all_virtual_unit
""".replace("\n", " ")
)

# 21219: GV$OB_PARAMETERS # removed (single-tenant GV/V collapse; folded into V$OB_PARAMETERS)

def_table_schema(
  owner = 'fyy280124',
  table_name      = 'V$OB_PARAMETERS',
  table_id        = '21220',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
SELECT
  ZONE,
  SCOPE,
  NAME,
  DATA_TYPE,
  VALUE,
  INFO,
  SECTION,
  EDIT_LEVEL,
  DEFAULT_VALUE,
  CAST (CASE ISDEFAULT
        WHEN 1
        THEN 'YES'
        ELSE 'NO'
        END AS CHAR(3)) AS ISDEFAULT
FROM oceanbase.__all_virtual_parameter_stat
""".replace("\n", " ")
)

# 21221: GV$OB_PROCESSLIST # removed (single-tenant GV/V collapse; folded into V$OB_PROCESSLIST)

def_table_schema(
  owner = 'xiaochu.yh',
  table_name      = 'V$OB_PROCESSLIST',
  table_id        = '21222',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
SELECT

  ID,
  USER,
  HOST,
  DB,
  TENANT,
  COMMAND,
  TIME,
  TOTAL_TIME,
  STATE,
  INFO,
  MASTER_SESSID,
  USER_CLIENT_IP,
  USER_HOST,
  RETRY_CNT,
  RETRY_INFO,
  SQL_ID,
  TRANS_ID,
  THREAD_ID,
  SSL_CIPHER,
  TRACE_ID,
  TRANS_STATE,
  ACTION,
  MODULE,
  CLIENT_INFO,
  LEVEL,
  SAMPLE_PERCENTAGE,
  RECORD_POLICY,
  LB_VID,
  LB_VIP,
  LB_VPORT,
  IN_BYTES,
  OUT_BYTES,
  USER_CLIENT_PORT,
  SERVICE_NAME,
  cast(total_cpu_time as SIGNED) as TOTAL_CPU_TIME,
  TOP_INFO,
  MEMORY_USAGE
FROM oceanbase.__all_virtual_processlist
""".replace("\n", " ")
)

# 21223: GV$OB_KVCACHE # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_kvcache_info)

# 21224: V$OB_KVCACHE # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_kvcache_info)

# 21225: GV$OB_TRANSACTION_WRITE_STATE # removed (single-tenant GV/V collapse; folded into V$OB_TRANSACTION_WRITE_STATE)

def_table_schema(
  owner = 'gjw228474',
  table_name      = 'V$OB_TRANSACTION_WRITE_STATE',
  table_id        = '21226',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """SELECT
      session_id AS SESSION_ID,
      trans_id AS TX_ID,
      write_state AS WRITE_STATE,
      ctx_create_time AS CTX_CREATE_TIME,
      expired_time AS TX_EXPIRED_TIME,
      CASE
        WHEN state = 0 THEN 'UNKNOWN'
        WHEN state = 10 THEN 'ACTIVE'
        WHEN state = 20 THEN 'REDO COMPLETE'
        WHEN state = 30 THEN 'PREPARE'
        WHEN state = 40 THEN 'PRECOMMIT'
        WHEN state = 50 THEN 'COMMIT'
        WHEN state = 60 THEN 'ABORT'
        WHEN state = 70 THEN 'CLEAR'
        ELSE 'UNDEFINED'
        END AS STATE,
      CAST (CASE
        WHEN part_trans_action = 1 THEN 'NULL'
        WHEN part_trans_action = 2 THEN 'START'
        WHEN part_trans_action = 3 THEN 'COMMIT'
        WHEN part_trans_action = 4 THEN 'ABORT'
        WHEN part_trans_action = 5 THEN 'DIED'
        WHEN part_trans_action = 6 THEN 'END'
        ELSE 'UNKNOWN'
        END AS CHAR(10)) AS ACTION,
      pending_log_size AS PENDING_LOG_SIZE,
      flushed_log_size AS FLUSHED_LOG_SIZE,
      LAST_REQUEST_TIME
    FROM oceanbase.__all_virtual_trans_stat
    WHERE is_exiting = 0
""".replace("\n", " ")
  )

# 21227: GV$OB_COMPACTION_PROGRESS # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_server_compaction_progress)

# 21228: V$OB_COMPACTION_PROGRESS # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_server_compaction_progress)

# 21229: GV$OB_TABLET_COMPACTION_PROGRESS # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_tablet_compaction_progress)

# 21230: V$OB_TABLET_COMPACTION_PROGRESS # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_tablet_compaction_progress)

# 21231: GV$OB_TABLET_COMPACTION_HISTORY # removed (single-tenant GV/V collapse; folded into V$OB_TABLET_COMPACTION_HISTORY)

def_table_schema(
  owner = 'lixia.yq',
  table_name      = 'V$OB_TABLET_COMPACTION_HISTORY',
  table_id        = '21232',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT
      TABLET_ID,
      TYPE,
      COMPACTION_SCN,
      START_TIME,
      FINISH_TIME,
      TASK_ID,
      OCCUPY_SIZE,
      MACRO_BLOCK_COUNT,
      MULTIPLEXED_MACRO_BLOCK_COUNT,
      NEW_MICRO_COUNT_IN_NEW_MACRO,
      MULTIPLEXED_MICRO_COUNT_IN_NEW_MACRO,
      TOTAL_ROW_COUNT,
      INCREMENTAL_ROW_COUNT,
      COMPRESSION_RATIO,
      NEW_FLUSH_DATA_RATE,
      PROGRESSIVE_COMPACTION_ROUND,
      PROGRESSIVE_COMPACTION_NUM,
      PARALLEL_DEGREE,
      PARALLEL_INFO,
      PARTICIPANT_TABLE,
      MACRO_ID_LIST,
      COMMENTS,
      KEPT_SNAPSHOT,
      MERGE_LEVEL,
      EXEC_MODE,
      (CASE IS_FULL_MERGE
           WHEN false THEN "FALSE"
           ELSE "TRUE" END) AS IS_FULL_MERGE,
      IO_COST_TIME_PERCENTAGE,
      MERGE_REASON,
      BASE_MAJOR_STATUS,
      MDS_FILTER_INFO,
      EXECUTE_TIME
    FROM oceanbase.__all_virtual_tablet_compaction_history
""".replace("\n", " ")
)

# 21233: GV$OB_COMPACTION_DIAGNOSE_INFO # removed (single-tenant GV/V collapse; folded into V$OB_COMPACTION_DIAGNOSE_INFO)

def_table_schema(
  owner = 'lixia.yq',
  table_name      = 'V$OB_COMPACTION_DIAGNOSE_INFO',
  table_id        = '21234',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT
      TYPE,
      TABLET_ID,
      STATUS,
      CREATE_TIME,
      DIAGNOSE_INFO
    FROM oceanbase.__all_virtual_compaction_diagnose_info
    WHERE
      STATUS != "RS_UNCOMPACTED"
    AND
      STATUS != "NOT_SCHEDULE"
    AND
      STATUS != "SPECIAL"
""".replace("\n", " ")
)

# 21235: GV$OB_COMPACTION_SUGGESTIONS # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_compaction_suggestion)

# 21236: V$OB_COMPACTION_SUGGESTIONS # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_compaction_suggestion)

# 21237: GV$OB_DTL_INTERM_RESULT_MONITOR # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_dtl_interm_result_monitor)

# 21238: V$OB_DTL_INTERM_RESULT_MONITOR # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_dtl_interm_result_monitor)

# 21239: GV$OB_IO_CALIBRATION_STATUS # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_io_calibration_status)

# 21240: V$OB_IO_CALIBRATION_STATUS # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_io_calibration_status)

# 21241: GV$OB_IO_BENCHMARK # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_io_benchmark)

# 21242: V$OB_IO_BENCHMARK # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_io_benchmark)

# 21243: GV$OB_IO_QUOTA
# 21244: V$OB_IO_QUOTA


# 4.0 backup clean view
# 21245: CDB_OB_BACKUP_DELETE_JOBS # abandoned
# 21246: CDB_OB_BACKUP_DELETE_JOB_HISTORY # abandoned
# 21247: CDB_OB_BACKUP_DELETE_TASKS # abandoned
# 21248: CDB_OB_BACKUP_DELETE_TASK_HISTORY # abandoned
# 21249: CDB_OB_BACKUP_DELETE_POLICY # abandoned
# 21250: CDB_OB_BACKUP_STORAGE_INFO # abandoned


def_table_schema(
    owner = 'jiangxiu.wt',
    table_name     = 'DBA_TAB_STATISTICS',
    table_id       = '21251',
    table_type = 'SYSTEM_VIEW',
    rowkey_columns  = [],
    normal_columns  = [],
    gm_columns      = [],
    in_tenant_space = True,
    view_definition = """SELECT
    CAST(DB.DATABASE_NAME AS     CHAR(128)) AS OWNER,
    CAST(V.TABLE_NAME       AS  CHAR(128)) AS TABLE_NAME,
    CAST(V.PARTITION_NAME   AS  CHAR(128)) AS PARTITION_NAME,
    CAST(V.PARTITION_POSITION AS    NUMBER) AS PARTITION_POSITION,
    CAST(V.SUBPARTITION_NAME  AS    CHAR(128)) AS SUBPARTITION_NAME,
    CAST(V.SUBPARTITION_POSITION AS NUMBER) AS SUBPARTITION_POSITION,
    CAST(V.OBJECT_TYPE AS   CHAR(12)) AS OBJECT_TYPE,
    CAST(STAT.ROW_CNT AS    NUMBER) AS NUM_ROWS,
    CAST(NULL AS    NUMBER) AS BLOCKS,
    CAST(NULL AS    NUMBER) AS EMPTY_BLOCKS,
    CAST(NULL AS    NUMBER) AS AVG_SPACE,
    CAST(NULL AS    NUMBER) AS CHAIN_CNT,
    CAST(STAT.AVG_ROW_LEN AS    NUMBER) AS AVG_ROW_LEN,
    CAST(NULL AS    NUMBER) AS AVG_SPACE_FREELIST_BLOCKS,
    CAST(NULL AS    NUMBER) AS NUM_FREELIST_BLOCKS,
    CAST(NULL AS    NUMBER) AS AVG_CACHED_BLOCKS,
    CAST(NULL AS    NUMBER) AS AVG_CACHE_HIT_RATIO,
    CAST(NULL AS    NUMBER) AS IM_IMCU_COUNT,
    CAST(NULL AS    NUMBER) AS IM_BLOCK_COUNT,
    CAST(NULL AS    DATETIME) AS IM_STAT_UPDATE_TIME,
    CAST(NULL AS    NUMBER) AS SCAN_RATE,
    CAST(STAT.SPARE1 AS    DECIMAL(20, 0)) AS SAMPLE_SIZE,
    CAST(STAT.LAST_ANALYZED AS DATETIME(6)) AS LAST_ANALYZED,
    CAST((CASE STAT.GLOBAL_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS GLOBAL_STATS,
    CAST((CASE STAT.USER_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS USER_STATS,
    CAST((CASE WHEN STAT.STATTYPE_LOCKED & 15 IS NULL THEN NULL ELSE (CASE STAT.STATTYPE_LOCKED & 15 WHEN 0 THEN NULL WHEN 1 THEN 'DATA' WHEN 2 THEN 'CACHE' ELSE 'ALL' END) END) AS CHAR(5)) AS STATTYPE_LOCKED,
    CAST((CASE STAT.STALE_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS STALE_STATS,
    CAST(NULL AS    CHAR(7)) AS SCOPE
    FROM
    (
      (SELECT DATABASE_ID,
              TABLE_ID,
              -2 AS PARTITION_ID,
              TABLE_NAME,
              NULL AS PARTITION_NAME,
              NULL AS SUBPARTITION_NAME,
              NULL AS PARTITION_POSITION,
              NULL AS SUBPARTITION_POSITION,
              'TABLE' AS OBJECT_TYPE
          FROM
            OCEANBASE.__ALL_VIRTUAL_CORE_ALL_TABLE
          WHERE TABLE_TYPE IN (0,2,3,6,14)
        UNION ALL
        SELECT DATABASE_ID,
               TABLE_ID,
               CASE WHEN PART_LEVEL = 0 THEN -2 ELSE -1 END AS PARTITION_ID,
               TABLE_NAME,
               NULL AS PARTITION_NAME,
               NULL AS SUBPARTITION_NAME,
               NULL AS PARTITION_POSITION,
               NULL AS SUBPARTITION_POSITION,
               'TABLE' AS OBJECT_TYPE
        FROM
            oceanbase.__all_table T
        WHERE T.TABLE_TYPE IN (0,2,3,6,14)
        AND T.TABLE_MODE >> 12 & 15 in (0,1)
        AND T.INDEX_ATTRIBUTES_SET & 16 = 0)
    UNION ALL
        SELECT T.DATABASE_ID,
                T.TABLE_ID,
                P.PART_ID,
                T.TABLE_NAME,
                P.PART_NAME,
                NULL,
                P.PART_IDX + 1,
                NULL,
                'PARTITION'
        FROM
            oceanbase.__all_table T
          JOIN
            oceanbase.__all_part P
            ON T.TABLE_ID = P.TABLE_ID
        WHERE T.TABLE_TYPE IN (0,2,3,6,14)
              AND T.TABLE_MODE >> 12 & 15 in (0,1)
              AND (P.PARTITION_TYPE = 0 OR P.PARTITION_TYPE IS NULL)
              AND T.INDEX_ATTRIBUTES_SET & 16 = 0
    UNION ALL
        SELECT T.DATABASE_ID,
               T.TABLE_ID,
               SP.SUB_PART_ID AS PARTITION_ID,
               T.TABLE_NAME,
                 P.PART_NAME,
                 SP.SUB_PART_NAME,
                 P.PART_IDX + 1,
                 SP.SUB_PART_IDX + 1,
                 'SUBPARTITION'
        FROM
            oceanbase.__all_table T
        JOIN
            oceanbase.__all_part P
            ON T.TABLE_ID = P.TABLE_ID
        JOIN
            oceanbase.__all_sub_part SP
            ON T.TABLE_ID = SP.TABLE_ID
            AND P.PART_ID = SP.PART_ID
        WHERE T.TABLE_TYPE IN (0,2,3,6,14)
              AND T.TABLE_MODE >> 12 & 15 in (0,1)
              AND (P.PARTITION_TYPE = 0 OR P.PARTITION_TYPE IS NULL)
              AND (SP.PARTITION_TYPE = 0 OR SP.PARTITION_TYPE IS NULL)
              AND T.INDEX_ATTRIBUTES_SET & 16 = 0
    ) V
    JOIN
        oceanbase.__all_database DB
        ON DB.DATABASE_ID = V.DATABASE_ID
    LEFT JOIN
        oceanbase.__all_table_stat STAT
        ON V.TABLE_ID = STAT.TABLE_ID
        AND (V.PARTITION_ID = STAT.PARTITION_ID OR V.PARTITION_ID = -2)
        AND STAT.INDEX_TYPE = 0
""".replace("\n", " ")
)

def_table_schema(
    owner = 'jiangxiu.wt',
    table_name     = 'DBA_TAB_COL_STATISTICS',
    table_id       = '21252',
    table_type = 'SYSTEM_VIEW',
    rowkey_columns  = [],
    normal_columns  = [],
    gm_columns      = [],
    in_tenant_space = True,
    view_definition = """SELECT
  cast(db.database_name as CHAR(128)) as OWNER,
  cast(tc.table_name as CHAR(128)) as  TABLE_NAME,
  cast(tc.column_name as CHAR(128)) as  COLUMN_NAME,
  cast(stat.distinct_cnt as NUMBER) as  NUM_DISTINCT,
  cast(stat.min_value as CHAR(128)) as  LOW_VALUE,
  cast(stat.max_value as CHAR(128)) as  HIGH_VALUE,
  cast(stat.density as NUMBER) as  DENSITY,
  cast(stat.null_cnt as NUMBER) as  NUM_NULLS,
  cast(stat.bucket_cnt as NUMBER) as  NUM_BUCKETS,
  cast(stat.last_analyzed as DATETIME(6)) as  LAST_ANALYZED,
  cast(stat.sample_size as NUMBER) as  SAMPLE_SIZE,
  CAST((CASE stat.GLOBAL_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS GLOBAL_STATS,
  CAST((CASE stat.USER_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS USER_STATS,
  cast(NULL as CHAR(80)) as  NOTES,
  cast(stat.avg_len as NUMBER) as  AVG_COL_LEN,
  cast((case when stat.histogram_type = 1 then 'FREQUENCY'
        when stat.histogram_type = 3 then 'TOP-FREQUENCY'
        when stat.histogram_type = 4 then 'HYBRID'
        else NULL end) as CHAR(15)) as HISTOGRAM,
  cast(NULL as CHAR(7)) SCOPE
    FROM
    (SELECT t.DATABASE_ID,
            t.TABLE_ID,
            t.TABLE_NAME,
            c.COLUMN_ID,
            c.COLUMN_NAME,
            c.IS_HIDDEN
          FROM
            oceanbase.__all_virtual_core_all_table t,
            oceanbase.__all_virtual_core_column_table c
          WHERE c.table_id = t.table_id
     UNION ALL
     SELECT t.database_id,
            t.table_id,
            t.table_name,
            c.COLUMN_ID,
            c.COLUMN_NAME,
            c.IS_HIDDEN
      FROM oceanbase.__all_table t,
           oceanbase.__all_column c
      where t.table_type in (0,2,3,6,14)
        and t.table_mode >> 12 & 15 in (0,1)
        and t.index_attributes_set & 16 = 0
        and c.table_id = t.table_id) tc
  JOIN
    oceanbase.__all_database db
    ON db.database_id = tc.database_id
  left join
    oceanbase.__all_column_stat stat
    ON tc.table_id = stat.table_id
    AND tc.column_id = stat.column_id
    AND stat.object_type = 1
WHERE
  tc.is_hidden = 0
""".replace("\n", " ")
)

def_table_schema(
  owner = 'jiangxiu.wt',
  table_name      = 'DBA_PART_COL_STATISTICS',
  table_id        = '21253',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """SELECT
  cast(db.database_name as CHAR(128)) as OWNER,
  cast(t.table_name as CHAR(128)) as  TABLE_NAME,
  cast (part.part_name as CHAR(128)) as PARTITION_NAME,
  cast(c.column_name as CHAR(128)) as  COLUMN_NAME,
  cast(stat.distinct_cnt as NUMBER) as  NUM_DISTINCT,
  cast(stat.min_value as CHAR(128)) as  LOW_VALUE,
  cast(stat.max_value as CHAR(128)) as  HIGH_VALUE,
  cast(stat.density as NUMBER) as  DENSITY,
  cast(stat.null_cnt as NUMBER) as  NUM_NULLS,
  cast(stat.bucket_cnt as NUMBER) as  NUM_BUCKETS,
  cast(stat.last_analyzed as DATETIME(6)) as  LAST_ANALYZED,
  cast(stat.sample_size as NUMBER) as  SAMPLE_SIZE,
  CAST((CASE stat.GLOBAL_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS GLOBAL_STATS,
  CAST((CASE stat.USER_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS USER_STATS,
  cast(NULL as CHAR(80)) as  NOTES,
  cast(stat.avg_len as NUMBER) as  AVG_COL_LEN,
  cast((case when stat.histogram_type = 1 then 'FREQUENCY'
        when stat.histogram_type = 3 then 'TOP-FREQUENCY'
        when stat.histogram_type = 4 then 'HYBRID'
        else NULL end) as CHAR(15)) as HISTOGRAM
    FROM
    oceanbase.__all_table t
  JOIN
    oceanbase.__all_database db
    ON db.database_id = t.database_id
  JOIN
    oceanbase.__all_column c
    ON c.table_id = t.table_id
  JOIN
    oceanbase.__all_part part
    on t.table_id = part.table_id
  left join
    oceanbase.__all_column_stat stat
    ON c.table_id = stat.table_id
    AND c.column_id = stat.column_id
    AND part.part_id = stat.partition_id
    AND stat.object_type = 2
WHERE
  c.is_hidden = 0
  AND t.table_type in (0,3,6,14)
  AND t.table_mode >> 12 & 15 in (0,1)
  AND part.partition_type = 0
  AND t.index_attributes_set & 16 = 0
""".replace("\n", " ")
)

def_table_schema(
  owner = 'jiangxiu.wt',
  table_name      = 'DBA_SUBPART_COL_STATISTICS',
  table_id        = '21254',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """SELECT
  cast(db.database_name as CHAR(128)) as OWNER,
  cast(t.table_name as CHAR(128)) as  TABLE_NAME,
  cast (subpart.sub_part_name as CHAR(128)) as SUBPARTITION_NAME,
  cast(c.column_name as CHAR(128)) as  COLUMN_NAME,
  cast(stat.distinct_cnt as NUMBER) as  NUM_DISTINCT,
  cast(stat.min_value as CHAR(128)) as  LOW_VALUE,
  cast(stat.max_value as CHAR(128)) as  HIGH_VALUE,
  cast(stat.density as NUMBER) as  DENSITY,
  cast(stat.null_cnt as NUMBER) as  NUM_NULLS,
  cast(stat.bucket_cnt as NUMBER) as  NUM_BUCKETS,
  cast(stat.last_analyzed as DATETIME(6)) as  LAST_ANALYZED,
  cast(stat.sample_size as NUMBER) as  SAMPLE_SIZE,
  CAST((CASE stat.GLOBAL_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS GLOBAL_STATS,
  CAST((CASE stat.USER_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS USER_STATS,
  cast(NULL as CHAR(80)) as  NOTES,
  cast(stat.avg_len as NUMBER) as  AVG_COL_LEN,
  cast((case when stat.histogram_type = 1 then 'FREQUENCY'
        when stat.histogram_type = 3 then 'TOP-FREQUENCY'
        when stat.histogram_type = 4 then 'HYBRID'
        else NULL end) as CHAR(15)) as HISTOGRAM
    FROM
    oceanbase.__all_table t
  JOIN
    oceanbase.__all_database db
    ON db.database_id = t.database_id
  JOIN
    oceanbase.__all_column c
    ON c.table_id = t.table_id
  JOIN
    oceanbase.__all_sub_part subpart
    on t.table_id = subpart.table_id
  left join
    oceanbase.__all_column_stat stat
    ON c.table_id = stat.table_id
    AND c.column_id = stat.column_id
    AND stat.partition_id = subpart.sub_part_id
    AND stat.object_type = 3
WHERE
  c.is_hidden = 0
  AND t.table_type in (0,3,6,14)
  AND t.table_mode >> 12 & 15 in (0,1)
  AND subpart.partition_type = 0
  AND t.index_attributes_set & 16 = 0
""".replace("\n", " ")
)

def_table_schema(
    owner = 'jiangxiu.wt',
    table_name     = 'DBA_TAB_HISTOGRAMS',
    table_id       = '21255',
    table_type = 'SYSTEM_VIEW',
    rowkey_columns  = [],
    normal_columns  = [],
    gm_columns      = [],
    in_tenant_space = True,
    view_definition = """select
  cast(db.database_name as CHAR(128)) as OWNER,
  cast(t.table_name as CHAR(128)) as  TABLE_NAME,
  cast(c.column_name as CHAR(128)) as  COLUMN_NAME,
  cast(hist.endpoint_num as NUMBER) as  ENDPOINT_NUMBER,
  cast(NULL as NUMBER) as  ENDPOINT_VALUE,
  cast(hist.endpoint_value as CHAR(4000)) as ENDPOINT_ACTUAL_VALUE,
  cast(hist.b_endpoint_value as CHAR(4000)) as ENDPOINT_ACTUAL_VALUE_RAW,
  cast(hist.endpoint_repeat_cnt as NUMBER) as ENDPOINT_REPEAT_COUNT,
  cast(NULL as CHAR(7)) as SCOPE
    FROM
    (SELECT DATABASE_ID,
            TABLE_ID,
            TABLE_NAME
          FROM
            oceanbase.__all_virtual_core_all_table
     UNION ALL
     SELECT database_id,
            table_id,
            table_name
      FROM oceanbase.__all_table where table_type in (0,3,6,14)
      and table_mode >> 12 & 15 in (0,1)
      and index_attributes_set & 16 = 0) t
  JOIN
    oceanbase.__all_database db
    ON db.database_id = t.database_id
  JOIN
    oceanbase.__all_column c
    ON c.table_id = t.table_id
  JOIN
    oceanbase.__all_histogram_stat hist
    ON c.table_id = hist.table_id
    AND c.column_id = hist.column_id
    AND hist.object_type = 1
WHERE
  c.is_hidden = 0
""".replace("\n", " ")
)

def_table_schema(
    owner = 'jiangxiu.wt',
    table_name      = 'DBA_PART_HISTOGRAMS',
    table_id        = '21256',
    table_type      = 'SYSTEM_VIEW',
    rowkey_columns  = [],
    normal_columns  = [],
    gm_columns      = [],
    in_tenant_space = True,
    view_definition = """select
    cast(db.database_name as CHAR(128)) as OWNER,
    cast(t.table_name as CHAR(128)) as  TABLE_NAME,
    cast(part.part_name as CHAR(128)) as PARTITION_NAME,
    cast(c.column_name as CHAR(128)) as  COLUMN_NAME,
    cast(hist.endpoint_num as NUMBER) as  ENDPOINT_NUMBER,
    cast(NULL as NUMBER) as  ENDPOINT_VALUE,
    cast(hist.endpoint_value as CHAR(4000)) as ENDPOINT_ACTUAL_VALUE,
    cast(hist.b_endpoint_value as CHAR(4000)) as ENDPOINT_ACTUAL_VALUE_RAW,
    cast(hist.endpoint_repeat_cnt as NUMBER) as ENDPOINT_REPEAT_COUNT
    FROM
      oceanbase.__all_table t
    JOIN
      oceanbase.__all_database db
      ON db.database_id = t.database_id
    JOIN
      oceanbase.__all_column c
      ON c.table_id = t.table_id
    JOIN
      oceanbase.__all_part part
      on t.table_id = part.table_id
    JOIN
      oceanbase.__all_histogram_stat hist
      ON c.table_id = hist.table_id
      AND c.column_id = hist.column_id
      AND part.part_id = hist.partition_id
      AND hist.object_type = 2
  WHERE
    c.is_hidden = 0
    AND t.table_type in (0,3,6,14)
    AND t.table_mode >> 12 & 15 in (0,1)
    AND part.partition_type = 0
    AND t.index_attributes_set & 16 = 0
  """.replace("\n", " ")
)

def_table_schema(
    owner = 'jiangxiu.wt',
    table_name      = 'DBA_SUBPART_HISTOGRAMS',
    table_id        = '21257',
    table_type      = 'SYSTEM_VIEW',
    rowkey_columns  = [],
    normal_columns  = [],
    gm_columns      = [],
    in_tenant_space = True,
    view_definition = """select
    cast(db.database_name as CHAR(128)) as OWNER,
    cast(t.table_name as CHAR(128)) as  TABLE_NAME,
    cast(subpart.sub_part_name as CHAR(128)) as SUBPARTITION_NAME,
    cast(c.column_name as CHAR(128)) as  COLUMN_NAME,
    cast(hist.endpoint_num as NUMBER) as  ENDPOINT_NUMBER,
    cast(NULL as NUMBER) as  ENDPOINT_VALUE,
    cast(hist.endpoint_value as CHAR(4000)) as ENDPOINT_ACTUAL_VALUE,
    cast(hist.b_endpoint_value as CHAR(4000)) as ENDPOINT_ACTUAL_VALUE_RAW,
    cast(hist.endpoint_repeat_cnt as NUMBER) as ENDPOINT_REPEAT_COUNT
    FROM
      oceanbase.__all_table t
    JOIN
      oceanbase.__all_database db
      ON db.database_id = t.database_id
    JOIN
      oceanbase.__all_column c
      ON c.table_id = t.table_id
    JOIN
      oceanbase.__all_sub_part subpart
      on t.table_id = subpart.table_id
    JOIN
      oceanbase.__all_histogram_stat hist
      ON c.table_id = hist.table_id
      AND c.column_id = hist.column_id
      AND hist.partition_id = subpart.sub_part_id
      AND hist.object_type = 3
  WHERE
    c.is_hidden = 0
    AND t.table_type in (0,3,6,14)
    AND t.table_mode >> 12 & 15 in (0,1)
    AND subpart.partition_type = 0
    AND t.index_attributes_set & 16 = 0
  """.replace("\n", " ")
)

def_table_schema(
  owner = 'jiangxiu.wt',
  table_name      = 'DBA_TAB_STATS_HISTORY',
  table_id        = '21258',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(DB.DATABASE_NAME AS     CHAR(128)) AS OWNER,
    CAST(V.TABLE_NAME       AS  CHAR(128)) AS TABLE_NAME,
    CAST(V.PARTITION_NAME   AS  CHAR(128)) AS PARTITION_NAME,
    CAST(V.SUBPARTITION_NAME  AS    CHAR(128)) AS SUBPARTITION_NAME,
    CAST(STAT.SAVTIME AS DATETIME(6)) AS STATS_UPDATE_TIME
    FROM
    (
      (SELECT DATABASE_ID,
              TABLE_ID,
              -2 AS PARTITION_ID,
              TABLE_NAME,
              NULL AS PARTITION_NAME,
              NULL AS SUBPARTITION_NAME,
              NULL AS PARTITION_POSITION,
              NULL AS SUBPARTITION_POSITION,
              'TABLE' AS OBJECT_TYPE
          FROM
            OCEANBASE.__ALL_VIRTUAL_CORE_ALL_TABLE
      UNION ALL
        SELECT DATABASE_ID,
               TABLE_ID,
               CASE WHEN PART_LEVEL = 0 THEN -2 ELSE -1 END AS PARTITION_ID,
               TABLE_NAME,
               NULL AS PARTITION_NAME,
               NULL AS SUBPARTITION_NAME,
               NULL AS PARTITION_POSITION,
                NULL AS SUBPARTITION_POSITION,
               'TABLE' AS OBJECT_TYPE
        FROM
            oceanbase.__all_table T
        WHERE T.TABLE_TYPE IN (0,3,6,14)
        AND T.TABLE_MODE >> 12 & 15 in (0,1)
        AND T.INDEX_ATTRIBUTES_SET & 16 = 0)
    UNION ALL
        SELECT T.DATABASE_ID,
                T.TABLE_ID,
                P.PART_ID,
                T.TABLE_NAME,
                P.PART_NAME,
                NULL,
                P.PART_IDX + 1,
                NULL,
                'PARTITION'
        FROM
            oceanbase.__all_table T
          JOIN
            oceanbase.__all_part P
            ON T.TABLE_ID = P.TABLE_ID
            AND T.TABLE_MODE >> 12 & 15 in (0,1)
            AND T.INDEX_ATTRIBUTES_SET & 16 = 0
        WHERE T.TABLE_TYPE IN (0,3,6,14)
    UNION ALL
        SELECT T.DATABASE_ID,
               T.TABLE_ID,
               SP.SUB_PART_ID AS PARTITION_ID,
               T.TABLE_NAME,
                 P.PART_NAME,
                 SP.SUB_PART_NAME,
                 P.PART_IDX + 1,
                 SP.SUB_PART_IDX + 1,
                 'SUBPARTITION'
        FROM
            oceanbase.__all_table T
        JOIN
            oceanbase.__all_part P
            ON T.TABLE_ID = P.TABLE_ID
            AND T.TABLE_MODE >> 12 & 15 in (0,1)
            AND T.INDEX_ATTRIBUTES_SET & 16 = 0
        JOIN
            oceanbase.__all_sub_part SP
            ON T.TABLE_ID = SP.TABLE_ID
            AND P.PART_ID = SP.PART_ID
        WHERE T.TABLE_TYPE IN (0,3,6,14)
    ) V
    JOIN
        oceanbase.__all_database DB
        ON DB.DATABASE_ID = V.DATABASE_ID
    LEFT JOIN
        oceanbase.__all_table_stat_history STAT
        ON V.TABLE_ID = STAT.TABLE_ID
        AND (V.PARTITION_ID = STAT.PARTITION_ID OR V.PARTITION_ID = -2)
        AND STAT.INDEX_TYPE = 0
""".replace("\n", " ")
)

def_table_schema(
    owner = 'jiangxiu.wt',
    table_name     = 'DBA_IND_STATISTICS',
    table_id       = '21259',
    table_type = 'SYSTEM_VIEW',
    rowkey_columns  = [],
    normal_columns  = [],
    gm_columns      = [],
    in_tenant_space = True,
    view_definition = """SELECT
    CAST(DB.DATABASE_NAME AS     CHAR(128)) AS OWNER,
    CAST(V.INDEX_NAME AS     CHAR(128)) AS INDEX_NAME,
    CAST(DB.DATABASE_NAME AS     CHAR(128)) AS TABLE_OWNER,
    CAST(T.TABLE_NAME       AS  CHAR(128)) AS TABLE_NAME,
    CAST(V.PARTITION_NAME   AS  CHAR(128)) AS PARTITION_NAME,
    CAST(V.PARTITION_POSITION AS    NUMBER) AS PARTITION_POSITION,
    CAST(V.SUBPARTITION_NAME  AS    CHAR(128)) AS SUBPARTITION_NAME,
    CAST(V.SUBPARTITION_POSITION AS NUMBER) AS SUBPARTITION_POSITION,
    CAST(V.OBJECT_TYPE AS   CHAR(12)) AS OBJECT_TYPE,
    CAST(NULL AS    NUMBER) AS BLEVEL,
    CAST(NULL AS    NUMBER) AS LEAF_BLOCKS,
    CAST(NULL AS    NUMBER) AS DISTINCT_KEYS,
    CAST(NULL AS    NUMBER) AS AVG_LEAF_BLOCKS_PER_KEY,
    CAST(NULL AS    NUMBER) AS AVG_DATA_BLOCKS_PER_KEY,
    CAST(NULL AS    NUMBER) AS CLUSTERING_FACTOR,
    CAST(STAT.ROW_CNT AS    NUMBER) AS NUM_ROWS,
    CAST(NULL AS    NUMBER) AS AVG_CACHED_BLOCKS,
    CAST(NULL AS    NUMBER) AS AVG_CACHE_HIT_RATIO,
    CAST(NULL AS    NUMBER) AS SAMPLE_SIZE,
    CAST(STAT.LAST_ANALYZED AS DATETIME(6)) AS LAST_ANALYZED,
    CAST((CASE STAT.GLOBAL_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS GLOBAL_STATS,
    CAST((CASE STAT.USER_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS USER_STATS,
    CAST((CASE WHEN STAT.STATTYPE_LOCKED & 15 IS NULL THEN NULL ELSE (CASE STAT.STATTYPE_LOCKED & 15 WHEN 0 THEN NULL WHEN 1 THEN 'DATA' WHEN 2 THEN 'CACHE' ELSE 'ALL' END) END) AS CHAR(5)) AS STATTYPE_LOCKED,
    CAST((CASE STAT.STALE_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS STALE_STATS,
    CAST(NULL AS    CHAR(7)) AS SCOPE
    FROM
    (
        (SELECT DATABASE_ID,
                TABLE_ID,
                DATA_TABLE_ID,
                -2 AS PARTITION_ID,
                SUBSTR(TABLE_NAME, 7 + INSTR(SUBSTR(TABLE_NAME, 7), '_')) AS INDEX_NAME,
                NULL AS PARTITION_NAME,
                NULL AS SUBPARTITION_NAME,
                NULL AS PARTITION_POSITION,
                NULL AS SUBPARTITION_POSITION,
                'INDEX' AS OBJECT_TYPE
          FROM
            OCEANBASE.__ALL_VIRTUAL_CORE_ALL_TABLE T
          WHERE T.TABLE_TYPE = 5 AND T.INDEX_TYPE NOT IN (13, 14, 16, 17, 19, 20, 22)
        UNION ALL
         SELECT DATABASE_ID,
                TABLE_ID,
                DATA_TABLE_ID,
                CASE WHEN PART_LEVEL = 0 THEN -2 ELSE -1 END AS PARTITION_ID,
                SUBSTR(TABLE_NAME, 7 + INSTR(SUBSTR(TABLE_NAME, 7), '_')) AS INDEX_NAME,
                NULL AS PARTITION_NAME,
                NULL AS SUBPARTITION_NAME,
                NULL AS PARTITION_POSITION,
                NULL AS SUBPARTITION_POSITION,
                'INDEX' AS OBJECT_TYPE
        FROM
            oceanbase.__all_table T
        WHERE T.TABLE_TYPE = 5 AND T.INDEX_TYPE NOT IN (13, 14, 16, 17, 19, 20, 22)
        AND T.TABLE_MODE >> 12 & 15 in (0,1)
        AND T.INDEX_ATTRIBUTES_SET & 16 = 0)
    UNION ALL
        SELECT T.DATABASE_ID,
                T.TABLE_ID,
                T.DATA_TABLE_ID,
                P.PART_ID,
                SUBSTR(T.TABLE_NAME, 7 + INSTR(SUBSTR(T.TABLE_NAME, 7), '_')) AS INDEX_NAME,
                P.PART_NAME,
                NULL,
                P.PART_IDX + 1,
                NULL,
                'PARTITION'
        FROM
            oceanbase.__all_table T
          JOIN
            oceanbase.__all_part P
            ON T.TABLE_ID = P.TABLE_ID
        WHERE T.TABLE_TYPE = 5
              AND P.PARTITION_TYPE = 0
              AND T.INDEX_TYPE NOT IN (13, 14, 16, 17, 19, 20, 22)
    UNION ALL
        SELECT T.DATABASE_ID,
               T.TABLE_ID,
               T.DATA_TABLE_ID,
               SP.SUB_PART_ID AS PARTITION_ID,
               SUBSTR(T.TABLE_NAME, 7 + INSTR(SUBSTR(T.TABLE_NAME, 7), '_')) AS INDEX_NAME,
               P.PART_NAME,
               SP.SUB_PART_NAME,
               P.PART_IDX + 1,
               SP.SUB_PART_IDX + 1,
               'SUBPARTITION'
        FROM
            oceanbase.__all_table T
        JOIN
            oceanbase.__all_part P
            ON T.TABLE_ID = P.TABLE_ID
        JOIN
            oceanbase.__all_sub_part SP
            ON T.TABLE_ID = SP.TABLE_ID
            AND P.PART_ID = SP.PART_ID
        WHERE T.TABLE_TYPE = 5
              AND P.PARTITION_TYPE = 0
              AND SP.PARTITION_TYPE = 0
              AND T.INDEX_TYPE NOT IN (13, 14, 16, 17, 19, 20, 22)
    ) V
    JOIN oceanbase.__all_table T
         ON T.TABLE_ID = V.DATA_TABLE_ID
         AND T.DATABASE_ID = V.DATABASE_ID
    JOIN
        oceanbase.__all_database DB
        ON DB.DATABASE_ID = V.DATABASE_ID
    LEFT JOIN
        oceanbase.__all_table_stat STAT
        ON V.TABLE_ID = STAT.TABLE_ID
        AND (V.PARTITION_ID = STAT.PARTITION_ID OR V.PARTITION_ID = -2)
        AND STAT.INDEX_TYPE = 1
""".replace("\n", " ")
)
# 21260: DBA_OB_BACKUP_JOBS # abandoned
# 21261: DBA_OB_BACKUP_JOB_HISTORY # abandoned
# 21262: DBA_OB_BACKUP_TASKS # abandoned
# 21263: DBA_OB_BACKUP_TASK_HISTORY # abandoned
# 21264: DBA_OB_BACKUP_SET_FILES (abandoned)

# 21265: DBA_SQL_PLAN_BASELINES abandoned
# 21266: DBA_SQL_MANAGEMENT_CONFIG abandoned

# 21267: GV$ACTIVE_SESSION_HISTORY # removed

# 21268: V$ACTIVE_SESSION_HISTORY # removed

# 21269: GV$DML_STATS # removed (single-tenant GV/V collapse; folded into V$DML_STATS)

def_table_schema(
    owner = 'jiangxiu.wt',
    table_name     = 'V$DML_STATS',
    table_id       = '21270',
    table_type = 'SYSTEM_VIEW',
    rowkey_columns  = [],
    normal_columns  = [],
    gm_columns      = [],
    in_tenant_space = True,
    view_definition = """SELECT
          CAST(1 AS SIGNED) AS INST_ID,
          CAST(TABLE_ID AS SIGNED) AS OBJN,
          CAST(INSERT_ROW_COUNT AS SIGNED) AS INS,
          CAST(UPDATE_ROW_COUNT AS SIGNED) AS UPD,
          CAST(DELETE_ROW_COUNT AS SIGNED) AS DEL,
          CAST(NULL AS SIGNED) AS DROPSEG,
          CAST(NULL AS SIGNED) AS CURROWS,
          CAST(TABLET_ID AS SIGNED) AS PAROBJN,
          CAST(NULL AS SIGNED) AS LASTUSED,
          CAST(NULL AS SIGNED) AS FLAGS,
          CAST(NULL AS SIGNED) AS CON_ID
          FROM oceanbase.__all_virtual_dml_stats
""".replace("\n", " ")
)

def_table_schema(
  owner = 'jiangxiu.wt',
  table_name      = 'DBA_TAB_MODIFICATIONS',
  table_id        = '21271',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """SELECT
  CAST(DB.DATABASE_NAME AS     CHAR(128)) AS TABLE_OWNER,
  CAST(T.TABLE_NAME AS         CHAR(128)) AS TABLE_NAME,
  CAST(P.PART_NAME AS     CHAR(128)) AS PARTITION_NAME,
  CAST(SP.SUB_PART_NAME AS CHAR(128)) AS SUBPARTITION_NAME,
  CAST(V.INSERTS AS     SIGNED) AS INSERTS,
  CAST(V.UPDATES AS     SIGNED) AS UPDATES,
  CAST(V.DELETES AS     SIGNED) AS DELETES,
  CAST(V.MODIFIED_TIME AS DATE) AS TIMESTAMP,
  CAST(NULL AS     CHAR(3)) AS TRUNCATED,
  CAST(NULL AS     SIGNED) AS DROP_SEGMENTS
  FROM
    (SELECT
     CASE WHEN T.TABLE_ID IS NOT NULL THEN T.TABLE_ID ELSE VT.TABLE_ID END AS TABLE_ID,
     CASE WHEN T.TABLET_ID IS NOT NULL THEN T.TABLET_ID ELSE VT.TABLET_ID END AS TABLET_ID,

     CASE WHEN T.TABLET_ID IS NOT NULL AND VT.TABLET_ID IS NOT NULL THEN T.INSERTS + VT.INSERT_ROW_COUNT - T.LAST_INSERTS ELSE
       (CASE WHEN T.TABLET_ID IS NOT NULL THEN T.INSERTS - T.LAST_INSERTS ELSE VT.INSERT_ROW_COUNT END) END AS INSERTS,

     CASE WHEN T.TABLET_ID IS NOT NULL AND VT.TABLET_ID IS NOT NULL THEN T.UPDATES + VT.UPDATE_ROW_COUNT - T.LAST_UPDATES  ELSE
       (CASE WHEN T.TABLET_ID IS NOT NULL THEN T.UPDATES - T.LAST_UPDATES  ELSE VT.UPDATE_ROW_COUNT END) END AS UPDATES,

     CASE WHEN T.TABLET_ID IS NOT NULL AND VT.TABLET_ID IS NOT NULL THEN T.DELETES + VT.DELETE_ROW_COUNT - T.LAST_DELETES ELSE
       (CASE WHEN T.TABLET_ID IS NOT NULL THEN T.DELETES - T.LAST_DELETES ELSE VT.DELETE_ROW_COUNT END) END AS DELETES,

     CASE WHEN T.GMT_MODIFIED IS NOT NULL THEN T.GMT_MODIFIED ELSE NULL END AS MODIFIED_TIME
     FROM
     OCEANBASE.__ALL_MONITOR_MODIFIED T
     FULL JOIN
     OCEANBASE.__ALL_VIRTUAL_DML_STATS VT
     ON T.TABLET_ID = VT.TABLET_ID
    )V
    JOIN OCEANBASE.__ALL_TABLE T
         ON V.TABLE_ID = T.TABLE_ID
         AND T.TABLE_TYPE in (0, 3, 6)
         AND T.TABLE_MODE >> 12 & 15 in (0,1)
         AND T.INDEX_ATTRIBUTES_SET & 16 = 0
    JOIN
        OCEANBASE.__ALL_DATABASE DB
        ON DB.DATABASE_ID = T.DATABASE_ID
    LEFT JOIN
        OCEANBASE.__ALL_PART P
        ON V.TABLE_ID = P.TABLE_ID
        AND V.TABLET_ID = P.TABLET_ID
    LEFT JOIN
        OCEANBASE.__ALL_SUB_PART SP
        ON V.TABLE_ID = SP.TABLE_ID
        AND V.TABLET_ID = SP.TABLET_ID
  """.replace("\n", " ")
)

def_table_schema(
  owner = 'fyy280124',
  table_name      = 'DBA_SCHEDULER_JOBS',
  table_id        = '21272',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """SELECT
    CAST(T.POWNER AS CHAR(128)) AS OWNER,
    CAST(T.JOB_NAME AS CHAR(128)) AS JOB_NAME,
    CAST(NULL AS CHAR(128)) AS JOB_SUBNAME,
    CAST(T.JOB_STYLE AS CHAR(17)) AS JOB_STYLE,
    CAST(NULL AS CHAR(128)) AS JOB_CREATOR,
    CAST(NULL AS CHAR(65)) AS CLIENT_ID,
    CAST(NULL AS CHAR(33)) AS GLOBAL_UID,
    CAST(T.POWNER AS CHAR(4000)) AS PROGRAM_OWNER,
    CAST(T.PROGRAM_NAME AS CHAR(4000)) AS PROGRAM_NAME,
    CAST(T.JOB_TYPE AS CHAR(16)) AS JOB_TYPE,
    CAST(T.JOB_ACTION AS CHAR(4000)) AS JOB_ACTION,
    CAST(T.NUMBER_OF_ARGUMENT AS SIGNED) AS NUMBER_OF_ARGUMENTS,
    CAST(NULL AS CHAR(4000)) AS SCHEDULE_OWNER,
    CAST(NULL AS CHAR(4000)) AS SCHEDULE_NAME,
    CAST(NULL AS CHAR(12)) AS SCHEDULE_TYPE,
    CAST(T.START_DATE AS DATETIME(6)) AS START_DATE,
    CAST(T.REPEAT_INTERVAL AS CHAR(4000)) AS REPEAT_INTERVAL,
    CAST(NULL AS CHAR(128)) AS EVENT_QUEUE_OWNER,
    CAST(NULL AS CHAR(128)) AS EVENT_QUEUE_NAME,
    CAST(NULL AS CHAR(523)) AS EVENT_QUEUE_AGENT,
    CAST(NULL AS CHAR(4000)) AS EVENT_CONDITION,
    CAST(NULL AS CHAR(261)) AS EVENT_RULE,
    CAST(NULL AS CHAR(261)) AS FILE_WATCHER_OWNER,
    CAST(NULL AS CHAR(261)) AS FILE_WATCHER_NAME,
    CAST(T.END_DATE AS DATETIME(6)) AS END_DATE,
    CAST(T.JOB_CLASS AS CHAR(128)) AS JOB_CLASS,
    CAST(T.ENABLED AS CHAR(5)) AS ENABLED,
    CAST(T.AUTO_DROP AS CHAR(5)) AS AUTO_DROP,
    CAST(NULL AS CHAR(5)) AS RESTART_ON_RECOVERY,
    CAST(NULL AS CHAR(5)) AS RESTART_ON_FAILURE,
    CAST(T.STATE AS CHAR(15)) AS STATE,
    CAST(NULL AS SIGNED) AS JOB_PRIORITY,
    CAST(T.RUN_COUNT AS SIGNED) AS RUN_COUNT,
    CAST(NULL AS SIGNED) AS MAX_RUNS,
    CAST(T.FAILURES AS SIGNED) AS FAILURE_COUNT,
    CAST(NULL AS SIGNED) AS MAX_FAILURES,
    CAST(T.RETRY_COUNT AS SIGNED) AS RETRY_COUNT,
    CAST(T.LAST_DATE AS DATETIME(6)) AS LAST_START_DATE,
    CAST(T.LAST_RUN_DURATION AS SIGNED) AS LAST_RUN_DURATION,
    CAST(T.NEXT_DATE AS DATETIME(6)) AS NEXT_RUN_DATE,
    CAST(NULL AS SIGNED) AS SCHEDULE_LIMIT,
    CAST(T.MAX_RUN_DURATION AS SIGNED) AS MAX_RUN_DURATION,
    CAST(NULL AS CHAR(11)) AS LOGGING_LEVEL,
    CAST(NULL AS CHAR(5)) AS STORE_OUTPUT,
    CAST(NULL AS CHAR(5)) AS STOP_ON_WINDOW_CLOSE,
    CAST(NULL AS CHAR(5)) AS INSTANCE_STICKINESS,
    CAST(NULL AS CHAR(4000)) AS RAISE_EVENTS,
    CAST(NULL AS CHAR(5)) AS SYSTEM,
    CAST(NULL AS SIGNED) AS JOB_WEIGHT,
    CAST(T.NLSENV AS CHAR(4000)) AS NLS_ENV,
    CAST(NULL AS CHAR(128)) AS SOURCE,
    CAST(NULL AS SIGNED) AS NUMBER_OF_DESTINATIONS,
    CAST(NULL AS CHAR(261)) AS DESTINATION_OWNER,
    CAST(NULL AS CHAR(261)) AS DESTINATION,
    CAST(NULL AS CHAR(128)) AS CREDENTIAL_OWNER,
    CAST(NULL AS CHAR(128)) AS CREDENTIAL_NAME,
    CAST(T.FIELD1 AS CHAR(128)) AS INSTANCE_ID,
    CAST(NULL AS CHAR(5)) AS DEFERRED_DROP,
    CAST(NULL AS CHAR(5)) AS ALLOW_RUNS_IN_RESTRICTED_MODE,
    CAST(T.COMMENTS AS CHAR(4000)) AS COMMENTS,
    CAST(T.FLAG AS SIGNED) AS FLAGS,
    CAST(NULL AS CHAR(5)) AS RESTARTABLE,
    CAST(NULL AS CHAR(128)) AS CONNECT_CREDENTIAL_OWNER,
    CAST(NULL AS CHAR(128)) AS CONNECT_CREDENTIAL_NAME
  FROM oceanbase.__all_scheduler_job T WHERE T.JOB_NAME != '__dummy_guard' and T.JOB > 0
""".replace("\n", " ")
)

def_table_schema(
    owner = 'guoyun.lgy',
    table_name     = 'DBA_OB_OUTLINE_CONCURRENT_HISTORY',
    table_id       = '21273',
    table_type = 'SYSTEM_VIEW',
    gm_columns = [],
    in_tenant_space = True,
    rowkey_columns = [],
    view_definition = """
    SELECT
      GMT_CREATE AS CREATE_TIME,
      GMT_MODIFIED AS MODIFY_TIME,
      DATABASE_ID,
      OUTLINE_ID,
      NAME AS OUTLINE_NAME,
      SQL_TEXT,
      OUTLINE_PARAMS,
      OUTLINE_TARGET,
      CAST(SQL_ID AS CHAR(32)) AS SQL_ID,
      OUTLINE_CONTENT,
      CASE WHEN IS_DELETED = 1 THEN 'YES' ELSE 'NO' END AS IS_DELETED,
      CASE WHEN ENABLED = 1 THEN 'YES' ELSE 'NO' END AS ENABLED
    FROM oceanbase.__all_outline_history
""".replace("\n", " "),

    normal_columns = [
    ]
  )

# 21274: CDB_OB_BACKUP_STORAGE_INFO_HISTORY # abandoned
# 21275: DBA_OB_BACKUP_STORAGE_INFO # abandoned
# 21276: DBA_OB_BACKUP_STORAGE_INFO_HISTORY # abandoned
# 21277: DBA_OB_BACKUP_DELETE_POLICY # abandoned
# 21278: DBA_OB_BACKUP_DELETE_JOBS # abandoned
# 21279: DBA_OB_BACKUP_DELETE_JOB_HISTORY # abandoned
# 21280: DBA_OB_BACKUP_DELETE_TASKS # abandoned
# 21281: DBA_OB_BACKUP_DELETE_TASK_HISTORY # abandoned


def_table_schema(
    owner = 'xiaoyi.xy',
    table_name     = 'DBA_OB_OUTLINES',
    table_id       = '21282',
    table_type = 'SYSTEM_VIEW',
    gm_columns = [],
    in_tenant_space = True,
    rowkey_columns = [],
    view_definition = """
    SELECT
      B.GMT_CREATE AS CREATE_TIME,
      B.GMT_MODIFIED AS MODIFY_TIME,
      A.DATABASE_ID,
      A.OUTLINE_ID,
      A.DATABASE_NAME,
      A.OUTLINE_NAME,
      A.VISIBLE_SIGNATURE,
      A.SQL_TEXT,
      A.OUTLINE_TARGET,
      A.OUTLINE_SQL,
      A.SQL_ID,
      A.OUTLINE_CONTENT
    FROM oceanbase.__all_virtual_outline A, oceanbase.__all_outline B
    WHERE A.OUTLINE_ID = B.OUTLINE_ID AND B.FORMAT_OUTLINE = 0
""".replace("\n", " "),

    normal_columns = [
    ]
  )

def_table_schema(
    owner = 'xiaoyi.xy',
    table_name     = 'DBA_OB_CONCURRENT_LIMIT_SQL',
    table_id       = '21283',
    table_type = 'SYSTEM_VIEW',
    gm_columns = [],
    in_tenant_space = True,
    rowkey_columns = [],
    view_definition = """
    SELECT
      B.GMT_CREATE AS CREATE_TIME,
      B.GMT_MODIFIED AS MODIFY_TIME,
      A.DATABASE_ID,
      A.OUTLINE_ID,
      A.DATABASE_NAME,
      A.OUTLINE_NAME,
      A.OUTLINE_CONTENT,
      A.VISIBLE_SIGNATURE,
      A.SQL_TEXT,
      A.CONCURRENT_NUM,
      A.LIMIT_TARGET
    FROM oceanbase.__all_virtual_concurrent_limit_sql A, oceanbase.__all_outline B
    WHERE A.OUTLINE_ID = B.OUTLINE_ID
""".replace("\n", " "),

    normal_columns = [
    ]
  )
# 21284: DBA_OB_RESTORE_PROGRESS (abandoned)
# 21285: DBA_OB_RESTORE_HISTORY (abandoned)

# 21286: DBA_OB_ARCHIVE_MODE
# 21287: DBA_OB_ARCHIVE_DEST (abandoned)
# 21288: DBA_OB_ARCHIVELOG (abandoned)
# 21289: DBA_OB_ARCHIVELOG_SUMMARY (abandoned)
# 21290: DBA_OB_ARCHIVELOG_PIECE_FILES (abandoned)
# 21291: DBA_OB_BACKUP_PARAMETER (abandoned)

# 21292: CDB_OB_ARCHIVE_MODE
# 21293: CDB_OB_ARCHIVE_DEST (abandoned)
# 21294: CDB_OB_ARCHIVELOG (abandoned)
# 21295: CDB_OB_ARCHIVELOG_SUMMARY (abandoned)
# 21296: CDB_OB_BACKUP_PARAMETER (abandoned)
# 21297: DBA_OB_DEADLOCK_EVENT_HISTORY (abandoned)

def_table_schema(
  owner           = 'wx372254',
  table_name      = 'DBA_OB_DEADLOCK_EVENT_HISTORY',
  table_id        = '21297',
  table_type      = 'SYSTEM_VIEW',
  gm_columns      = [],
  rowkey_columns  = [],
  normal_columns  = [],
  in_tenant_space = True,
  view_definition =
  """
  SELECT EVENT_ID,
         DETECTOR_ID,
         REPORT_TIME,
         CYCLE_IDX,
         CYCLE_SIZE,
         ROLE,
         PRIORITY_LEVEL,
         PRIORITY,
         CREATE_TIME,
         START_DELAY AS START_DELAY_US,
         MODULE,
         VISITOR,
         OBJECT,
         EXTRA_NAME1,
         EXTRA_VALUE1,
         EXTRA_NAME2,
         EXTRA_VALUE2,
         EXTRA_NAME3,
         EXTRA_VALUE3
  FROM OCEANBASE.__ALL_VIRTUAL_DEADLOCK_EVENT_HISTORY
  """.replace("\n", " ")
  )

# 21300: DBA_OB_KV_TTL_TASKS (abandoned)
# 21301: DBA_OB_KV_TTL_TASK_HISTORY (abandoned)

# 21302: GV$OB_LOG_STAT # removed (single-tenant GV/V collapse; folded into V$OB_LOG_STAT)

def_table_schema(
    owner = 'xianlin.lh',
    table_name     = 'V$OB_LOG_STAT',
    table_id       = '21303',
    table_type = 'SYSTEM_VIEW',
    gm_columns = [],
    rowkey_columns = [],
    normal_columns  = [],
    in_tenant_space = True,
  view_definition = """
  SELECT
    ACCESS_MODE,
    BASE_LSN,
    BEGIN_LSN,
    BEGIN_SCN,
    END_LSN,
    END_SCN,
    MAX_LSN,
    MAX_SCN
  FROM oceanbase.__all_virtual_log_stat
""".replace("\n", " ")
  )

def_table_schema(
  owner = 'tonghui.ht',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'ST_GEOMETRY_COLUMNS',
  table_id        = '21304',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  select CAST(db.database_name AS CHAR(128)) collate utf8mb4_name_case as TABLE_SCHEMA,
         CAST(tbl.table_name AS CHAR(256)) collate utf8mb4_name_case as TABLE_NAME,
         CAST(col.column_name AS CHAR(128)) as COLUMN_NAME,
         CAST(srs.srs_name AS CHAR(128)) as SRS_NAME,
         CAST(if ((col.srs_id >> 32) = 4294967295, NULL, col.srs_id >> 32) AS UNSIGNED) as SRS_ID,
         CAST(case (col.srs_id & 31)
                when 0 then 'geometry'
                when 1 then 'point'
                when 2 then 'linestring'
                when 3 then 'polygon'
                when 4 then 'multipoint'
                when 5 then 'multilinestring'
                when 6 then 'multipolygon'
                when 7 then 'geomcollection'
                else 'invalid'
          end AS CHAR(128))as GEOMETRY_TYPE_NAME
  from
      oceanbase.__all_column col left join oceanbase.__all_spatial_reference_systems srs on (col.srs_id >> 32) = srs.srs_id
      join oceanbase.__all_table tbl on (tbl.table_id = col.table_id)
      join oceanbase.__all_database db on (db.database_id = tbl.database_id)
      and db.database_name != '__recyclebin'
  where col.data_type  = 37
    and ((col.column_flags & 2097152) = 0)
    and tbl.table_mode >> 12 & 15 in (0,1)
    and tbl.index_attributes_set & 16 = 0
    and (0 = sys_privilege_check('table_acc', 1)
         or 0 = sys_privilege_check('table_acc', 1, db.database_name, tbl.table_name));
""".replace("\n", " ")
)

def_table_schema(
  owner = 'tonghui.ht',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'ST_SPATIAL_REFERENCE_SYSTEMS',
  table_id        = '21305',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  select CAST(srs_name AS CHAR(128)) as SRS_NAME,
         CAST(srs_id AS UNSIGNED) as SRS_ID,
         CAST(organization AS CHAR(256)) as ORGANIZATION,
         CAST(organization_coordsys_id AS UNSIGNED) as ORGANIZATION_COORDSYS_ID,
         CAST(definition AS CHAR(4096)) as DEFINITION,
         CAST(description AS CHAR(2048)) as DESCRIPTION
  from oceanbase.__all_spatial_reference_systems; """
)


def_table_schema(
  owner = 'wangzelin.wzl',
  database_id='OB_INFORMATION_SCHEMA_ID',
  table_name='QUERY_RESPONSE_TIME',
  table_id='21306',
  table_type='SYSTEM_VIEW',
  gm_columns=[],
  rowkey_columns=[],
  normal_columns=[],
  in_tenant_space=True,
  view_definition="""select
                   response_time as RESPONSE_TIME,
                   sum(count) as COUNT,
                   sum(total) as TOTAL
                   from oceanbase.__all_virtual_query_response_time
                   group by response_time
""".replace("\n", " ")
  )

# 21307: CDB_OB_KV_TTL_TASKS (abandoned)
# 21308: CDB_OB_KV_TTL_TASK_HISTORY (abandoned)
# 21309: CDB_OB_DATAFILE
# 21310: DBA_OB_DATAFILE

# 21311: removed (legacy resource manager deleted)
# 21312: removed (legacy resource manager deleted)
# 21313: removed (legacy resource manager deleted)
# 21314: removed (legacy resource manager deleted)
# 21315: removed (legacy resource manager deleted)

# 21318: DBA_OB_LS (abandoned)
# 21319: CDB_OB_LS (abandoned)
# 21320: DBA_OB_TABLE_LOCATIONS (abandoned)
# 21321: CDB_OB_TABLE_LOCATIONS (abandoned)

# 21322: GV$OB_TENANTS
# 21323: V$OB_TENANTS

def_table_schema(
  owner           = 'donglou.zl',
  table_name      = 'DBA_OB_FREEZE_INFO',
  table_id        = '21326',
  table_type      = 'SYSTEM_VIEW',
  gm_columns      = [],
  rowkey_columns  = [],
  normal_columns  = [],
  in_tenant_space = True,
  view_definition =
  """
  SELECT FROZEN_SCN,
         CLUSTER_VERSION,
         SCHEMA_VERSION,
         GMT_CREATE,
         GMT_MODIFIED
  FROM OCEANBASE.__ALL_FREEZE_INFO
  """.replace("\n", " ")
  )

# 21327:  CDB_OB_SWITCHOVER_CHECKPOINTS
# 21328:  DBA_OB_SWITCHOVER_CHECKPOINTS
# 21329: DBA_OB_LS_REPLICA_TASKS (abandoned)
# 21330: CDB_OB_LS_REPLICA_TASKS (abandoned)
# 21331: V$OB_LS_REPLICA_TASK_PLAN (abandoned)

def_table_schema(
  owner           = 'zuojiao.hzj',
  table_name      = 'DBA_OB_AUTO_INCREMENT',
  table_id        = '21332',
  table_type      = 'SYSTEM_VIEW',
  gm_columns      = [],
  rowkey_columns  = [],
  normal_columns  = [],
  in_tenant_space = True,
  view_definition =
  """
  SELECT CAST(GMT_CREATE AS DATETIME(6)) AS CREATE_TIME,
         CAST(GMT_MODIFIED AS DATETIME(6)) AS MODIFY_TIME,
         CAST(SEQUENCE_KEY AS SIGNED) AS AUTO_INCREMENT_KEY,
         CAST(COLUMN_ID AS SIGNED) AS COLUMN_ID,
         CAST(SEQUENCE_VALUE AS UNSIGNED) AS AUTO_INCREMENT_VALUE,
         CAST(SYNC_VALUE AS UNSIGNED) AS SYNC_VALUE
  FROM OCEANBASE.__ALL_AUTO_INCREMENT
  """.replace("\n", " ")
  )

def_table_schema(
  owner = 'zuojiao.hzj',
  table_name      = 'DBA_SEQUENCES',
  table_id        = '21334',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT
      CAST(C.DATABASE_NAME AS CHAR(128)) AS SEQUENCE_OWNER,
      CAST(A.SEQUENCE_NAME AS CHAR(128)) AS SEQUENCE_NAME,
      CAST(A.MIN_VALUE AS NUMBER(28, 0)) AS MIN_VALUE,
      CAST(A.MAX_VALUE AS NUMBER(28, 0)) AS MAX_VALUE,
      CAST(A.INCREMENT_BY AS NUMBER(28, 0)) AS INCREMENT_BY,
      CAST(CASE A.CYCLE_FLAG WHEN 1 THEN 'Y'
                             WHEN 0 THEN 'N'
                             ELSE NULL END AS CHAR(1)) AS CYCLE_FLAG,
      CAST(CASE A.ORDER_FLAG WHEN 1 THEN 'Y'
                             WHEN 0 THEN 'N'
                             ELSE NULL END AS CHAR(1)) AS ORDER_FLAG,
      CAST(A.CACHE_SIZE AS NUMBER(28, 0)) AS CACHE_SIZE,
      CAST(COALESCE(B.NEXT_VALUE,A.START_WITH) AS NUMBER(38,0)) AS LAST_NUMBER
    FROM
      OCEANBASE.__ALL_SEQUENCE_OBJECT A
    INNER JOIN
      OCEANBASE.__ALL_DATABASE C
    ON
      A.DATABASE_ID = C.DATABASE_ID
    LEFT JOIN
      OCEANBASE.__ALL_SEQUENCE_VALUE B
    ON
      A.SEQUENCE_ID = B.SEQUENCE_ID
""".replace("\n", " ")
)

def_table_schema(
  owner = 'jiangxiu.wt',
  table_name      = 'DBA_SCHEDULER_WINDOWS',
  table_id        = '21335',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """SELECT
    CAST(T.POWNER AS CHAR(128)) AS OWNER,
    CAST(T.JOB_NAME AS CHAR(128)) AS WINDOW_NAME,
    CAST(NULL AS CHAR(128)) AS RESOURCE_PLAN,
    CAST(NULL AS CHAR(4000)) AS SCHEDULE_OWNER,
    CAST(NULL AS CHAR(4000)) AS SCHEDULE_NAME,
    CAST(NULL AS CHAR(8)) AS SCHEDULE_TYPE,
    CAST(T.START_DATE AS DATETIME(6)) AS START_DATE,
    CAST(T.REPEAT_INTERVAL AS CHAR(4000)) AS REPEAT_INTERVAL,
    CAST(T.END_DATE AS DATETIME(6)) AS END_DATE,
    CAST(T.MAX_RUN_DURATION AS SIGNED) AS DURATION,
    CAST(NULL AS CHAR(4)) AS WINDOW_PRIORITY,
    CAST(T.NEXT_DATE AS DATETIME(6)) AS NEXT_RUN_DATE,
    CAST(T.LAST_DATE AS DATETIME(6)) AS LAST_START_DATE,
    CAST(T.ENABLED AS CHAR(5)) AS ENABLED,
    CAST(NULL AS CHAR(5)) AS ACTIVE,
    CAST(NULL AS DATETIME(6)) AS MANUAL_OPEN_TIME,
    CAST(NULL AS SIGNED) AS MANUAL_DURATION,
    CAST(T.COMMENTS AS CHAR(4000)) AS COMMENTS
  FROM oceanbase.__all_scheduler_job T WHERE T.JOB > 0 and T.JOB_NAME in ('MONDAY_WINDOW',
    'TUESDAY_WINDOW', 'WEDNESDAY_WINDOW', 'THURSDAY_WINDOW', 'FRIDAY_WINDOW', 'SATURDAY_WINDOW', 'SUNDAY_WINDOW')
  """.replace("\n", " ")
)

def_table_schema(
  owner           = 'mingye.swj',
  table_name      = 'DBA_OB_USERS',
  table_id        = '21336',
  table_type      = 'SYSTEM_VIEW',
  gm_columns      = [],
  rowkey_columns  = [],
  normal_columns  = [],
  in_tenant_space = True,
  view_definition =
  """
  SELECT USER_NAME,
          HOST,
          PASSWD,
          INFO,
          (CASE WHEN PRIV_ALTER = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_ALTER,
          (CASE WHEN PRIV_CREATE = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_CREATE,
          (CASE WHEN PRIV_DELETE = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_DELETE,
          (CASE WHEN PRIV_DROP = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_DROP,
          (CASE WHEN PRIV_GRANT_OPTION = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_GRANT_OPTION,
          (CASE WHEN PRIV_INSERT = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_INSERT,
          (CASE WHEN PRIV_UPDATE = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_UPDATE,
          (CASE WHEN PRIV_SELECT = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_SELECT,
          (CASE WHEN PRIV_INDEX = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_INDEX,
          (CASE WHEN PRIV_CREATE_VIEW = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_CREATE_VIEW,
          (CASE WHEN PRIV_SHOW_VIEW = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_SHOW_VIEW,
          (CASE WHEN PRIV_SHOW_DB = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_SHOW_DB,
          (CASE WHEN PRIV_CREATE_USER = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_CREATE_USER,
          (CASE WHEN PRIV_SUPER = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_SUPER,
          (CASE WHEN IS_LOCKED = 0 THEN 'NO' ELSE 'YES' END) AS IS_LOCKED,
          (CASE WHEN PRIV_PROCESS = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_PROCESS,
          (CASE WHEN PRIV_CREATE_SYNONYM = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_CREATE_SYNONYM,
          SSL_TYPE,
          SSL_CIPHER,
          X509_ISSUER,
          X509_SUBJECT,
          (CASE WHEN TYPE = 0 THEN 'USER' ELSE 'ROLE' END) AS TYPE,
          PROFILE_ID,
          PASSWORD_LAST_CHANGED,
          (CASE WHEN PRIV_FILE = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_FILE,
          (CASE WHEN PRIV_ALTER_TENANT = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_ALTER_TENANT,
          (CASE WHEN PRIV_ALTER_SYSTEM = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_ALTER_SYSTEM,
          (CASE WHEN PRIV_CREATE_RESOURCE_POOL = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_CREATE_RESOURCE_POOL,
          (CASE WHEN PRIV_CREATE_RESOURCE_UNIT = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_CREATE_RESOURCE_UNIT,
          MAX_CONNECTIONS,
          MAX_USER_CONNECTIONS,
          (CASE WHEN PRIV_REPL_SLAVE = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_REPL_SLAVE,
          (CASE WHEN PRIV_REPL_CLIENT = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_REPL_CLIENT,
          (CASE WHEN PRIV_DROP_DATABASE_LINK = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_DROP_DATABASE_LINK,
          (CASE WHEN PRIV_CREATE_DATABASE_LINK = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_CREATE_DATABASE_LINK,
          (CASE WHEN (PRIV_OTHERS & (1 << 0)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_EXECUTE,
          (CASE WHEN (PRIV_OTHERS & (1 << 1)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_ALTER_ROUTINE,
          (CASE WHEN (PRIV_OTHERS & (1 << 2)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_CREATE_ROUTINE,
          (CASE WHEN (PRIV_OTHERS & (1 << 3)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_CREATE_TABLESPACE,
          (CASE WHEN (PRIV_OTHERS & (1 << 4)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_SHUTDOWN,
          (CASE WHEN (PRIV_OTHERS & (1 << 5)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_RELOAD,
          (CASE WHEN (PRIV_OTHERS & (1 << 6)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_REFERENCES,
          (CASE WHEN (PRIV_OTHERS & (1 << 7)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_CREATE_ROLE,
          (CASE WHEN (PRIV_OTHERS & (1 << 8)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_DROP_ROLE,
          (CASE WHEN (PRIV_OTHERS & (1 << 9)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_TRIGGER,
          (CASE WHEN (PRIV_OTHERS & (1 << 10)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_LOCK_TABLE,
          (CASE WHEN (PRIV_OTHERS & (1 << 11)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_ENCRYPT,
          (CASE WHEN (PRIV_OTHERS & (1 << 12)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_DECRYPT,
          (CASE WHEN (PRIV_OTHERS & (1 << 13)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_EVENT,
          (CASE WHEN (PRIV_OTHERS & (1 << 14)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_CREATE_CATALOG,
          (CASE WHEN (PRIV_OTHERS & (1 << 15)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_USE_CATALOG
  FROM OCEANBASE.__all_user;
  """.replace("\n", " ")
)


def_table_schema(
  owner           = 'mingye.swj',
  table_name      = 'DBA_OB_DATABASE_PRIVILEGE',
  table_id        = '21338',
  table_type      = 'SYSTEM_VIEW',
  gm_columns      = [],
  rowkey_columns  = [],
  normal_columns  = [],
  in_tenant_space = True,
  view_definition =
  """
  WITH DB_PRIV AS (
    select A.user_id USER_ID,
           A.database_name DATABASE_NAME,
           A.priv_alter PRIV_ALTER,
           A.priv_create PRIV_CREATE,
           A.priv_delete PRIV_DELETE,
           A.priv_drop PRIV_DROP,
           A.priv_grant_option PRIV_GRANT_OPTION,
           A.priv_insert PRIV_INSERT,
           A.priv_update PRIV_UPDATE,
           A.priv_select PRIV_SELECT,
           A.priv_index PRIV_INDEX,
           A.priv_create_view PRIV_CREATE_VIEW,
           A.priv_show_view PRIV_SHOW_VIEW,
           A.GMT_CREATE GMT_CREATE,
           A.GMT_MODIFIED GMT_MODIFIED,
           A.priv_others PRIV_OTHERS
    from oceanbase.__all_database_privilege_history A,
        (select user_id, database_name, max(schema_version) schema_version from oceanbase.__all_database_privilege_history group by user_id, database_name, database_name collate utf8mb4_bin) B
    where A.user_id = B.user_id and A.database_name collate utf8mb4_bin = B.database_name collate utf8mb4_bin and A.schema_version = B.schema_version and A.is_deleted = 0
  )
  SELECT A.USER_ID USER_ID,
          B.USER_NAME USERNAME,
          A.DATABASE_NAME DATABASE_NAME,
          A.GMT_CREATE GMT_CREATE,
          A.GMT_MODIFIED GMT_MODIFIED,
          (CASE WHEN A.PRIV_ALTER = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_ALTER,
          (CASE WHEN A.PRIV_CREATE = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_CREATE,
          (CASE WHEN A.PRIV_DELETE = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_DELETE,
          (CASE WHEN A.PRIV_DROP = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_DROP,
          (CASE WHEN A.PRIV_GRANT_OPTION = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_GRANT_OPTION,
          (CASE WHEN A.PRIV_INSERT = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_INSERT,
          (CASE WHEN A.PRIV_UPDATE = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_UPDATE,
          (CASE WHEN A.PRIV_SELECT = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_SELECT,
          (CASE WHEN A.PRIV_INDEX = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_INDEX,
          (CASE WHEN A.PRIV_CREATE_VIEW = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_CREATE_VIEW,
          (CASE WHEN A.PRIV_SHOW_VIEW = 0 THEN 'NO' ELSE 'YES' END) AS PRIV_SHOW_VIEW,
          (CASE WHEN (A.PRIV_OTHERS & (1 << 0)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_EXECUTE,
          (CASE WHEN (A.PRIV_OTHERS & (1 << 1)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_ALTER_ROUTINE,
          (CASE WHEN (A.PRIV_OTHERS & (1 << 2)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_CREATE_ROUTINE,
          (CASE WHEN (A.PRIV_OTHERS & (1 << 6)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_REFERENCES,
          (CASE WHEN (A.PRIV_OTHERS & (1 << 9)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_TRIGGER,
          (CASE WHEN (A.PRIV_OTHERS & (1 << 10)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_LOCK_TABLE,
          (CASE WHEN (A.PRIV_OTHERS & (1 << 13)) != 0 THEN 'YES' ELSE 'NO' END) AS PRIV_EVENT
  FROM DB_PRIV A INNER JOIN OCEANBASE.__all_user B
        ON A.USER_ID = B.USER_ID;
  """.replace("\n", " ")
)

# 21341: GV$OB_SQL_PLAN # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_sql_plan)
# 21342: V$OB_SQL_PLAN # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_sql_plan)

# 21343: abandoned
# 21344: abandoned

def_table_schema(
  owner = 'shady.hxy',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'PARAMETERS',
  table_id       = '21346',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,
  view_definition = """select CAST('def' AS CHAR(512)) AS SPECIFIC_CATALOG,
                        CAST(d.database_name AS CHAR(128)) collate utf8mb4_name_case AS SPECIFIC_SCHEMA,
                        CAST(r.routine_name AS CHAR(64)) AS SPECIFIC_NAME,
                        CAST(rp.param_position AS signed) AS ORDINAL_POSITION,
                        CAST(CASE rp.param_position WHEN 0 THEN NULL
                          ELSE CASE rp.flag & 0x03
                          WHEN 1 THEN 'IN'
                          WHEN 2 THEN 'OUT'
                          WHEN 3 THEN 'INOUT'
                          ELSE NULL
                          END
                        END AS CHAR(5)) AS PARAMETER_MODE,
                        CAST(rp.param_name AS CHAR(64)) AS PARAMETER_NAME,
                        CAST(lower(case v.data_type_str
                                   when 'TINYINT UNSIGNED' then 'TINYINT'
                                   when 'SMALLINT UNSIGNED' then 'SMALLINT'
                                   when 'MEDIUMINT UNSIGNED' then 'MEDIUMINT'
                                   when 'INT UNSIGNED' then 'INT'
                                   when 'BIGINT UNSIGNED' then 'BIGINT'
                                   when 'FLOAT UNSIGNED' then 'FLOAT'
                                   when 'DOUBLE UNSIGNED' then 'DOUBLE'
                                   when 'DECIMAL UNSIGNED' then 'DECIMAL'
                                   when 'CHAR' then if(rp.param_charset = 1, 'BINARY', 'CHAR')
                                   when 'VARCHAR' then if(rp.param_charset = 1, 'VARBINARY', 'VARCHAR')
                                   when 'TINYTEXT' then if(rp.param_charset = 1, 'TINYBLOB', 'TINYTEXT')
                                   when 'TEXT' then if(rp.param_charset = 1, 'BLOB', 'TEXT')
                                   when 'MEDIUMTEXT' then if(rp.param_charset = 1, 'MEDIUMBLOB', 'MEDIUMTEXT')
                                   when 'LONGTEXT' then if(rp.param_charset = 1, 'LONGBLOB', 'LONGTEXT')
                                   when 'MYSQL_DATE' then 'DATE'
                                   when 'MYSQL_DATETIME' then 'DATETIME'
                                   else v.data_type_str end) AS CHAR(64)) AS DATA_TYPE,
                        CASE WHEN rp.param_type IN (22, 23, 27, 28, 29, 30) THEN CAST(rp.param_length AS SIGNED)
                          ELSE CAST(NULL AS SIGNED)
                        END AS CHARACTER_MAXIMUM_LENGTH,
                        CASE WHEN rp.param_type IN (22, 23, 27, 28, 29, 30, 43, 44, 46)
                          THEN CAST(
                            rp.param_length * CASE rp.param_coll_type
                            WHEN 63 THEN 1
                            WHEN 249 THEN 4
                            WHEN 248 THEN 4
                            WHEN 87 THEN 2
                            WHEN 28 THEN 2
                            WHEN 55 THEN 4
                            WHEN 54 THEN 4
                            WHEN 101 THEN 2
                            WHEN 46 THEN 4
                            WHEN 45 THEN 4
                            WHEN 224 THEN 4
                            ELSE 1
                            END
                              AS SIGNED
                          )
                          ELSE CAST(NULL AS SIGNED)
                        END AS CHARACTER_OCTET_LENGTH,
                        CASE WHEN rp.param_type IN (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 16, 31, 50) THEN CAST(rp.param_precision AS UNSIGNED)
                          WHEN rp.param_type IN (11, 13) THEN CAST(if(rp.param_scale = -1, 12, rp.param_precision) AS UNSIGNED)
                          WHEN rp.param_type IN (12, 14) THEN CAST(if(rp.param_scale = -1, 22, rp.param_precision) AS UNSIGNED)
                          ELSE CAST(NULL AS UNSIGNED)
                        END AS NUMERIC_PRECISION,
                        CASE WHEN rp.param_type IN (15, 16, 50) THEN CAST(rp.param_scale AS SIGNED)
                          WHEN rp.param_type IN (11, 12, 13, 14) THEN CAST(if(rp.param_scale = -1, 0, rp.param_scale) AS SIGNED)
                          WHEN rp.param_type IN (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 31) THEN CAST(0 AS SIGNED)
                          ELSE CAST(NULL AS SIGNED)
                        END AS NUMERIC_SCALE,
                        CASE WHEN rp.param_type IN (17, 18, 20, 42) THEN CAST(rp.param_scale AS UNSIGNED)
                          ELSE CAST(NULL AS UNSIGNED)
                        END AS DATETIME_PRECISION,
                        CAST(CASE rp.param_charset
                          WHEN 1 THEN 'binary'
                          WHEN 2 THEN 'utf8mb4'
                          WHEN 3 THEN 'gbk'
                          WHEN 4 THEN 'utf16'
                          WHEN 5 THEN 'gb18030'
                          WHEN 6 THEN 'latin1'
                          WHEN 7 THEN 'gb18030_2022'
                          WHEN 8 THEN 'ascii'
                          WHEN 9 THEN 'tis620'
                          ELSE NULL
                        END AS CHAR(64)) AS CHARACTER_SET_NAME,
                        CAST(CASE rp.param_coll_type
                        WHEN 8 THEN 'latin1_swedish_ci'
                        WHEN 11 THEN 'ascii_general_ci'
                        WHEN 18 THEN 'tis620_thai_ci'
                        WHEN 28 THEN 'gbk_chinese_ci'
                        WHEN 45 THEN 'utf8mb4_general_ci'
                        WHEN 46 THEN 'utf8mb4_bin'
                        WHEN 47 THEN 'latin1_bin'
                        WHEN 54 THEN 'utf16_general_ci'
                        WHEN 55 THEN 'utf16_bin'
                        WHEN 63 THEN 'binary'
                        WHEN 65 THEN 'ascii_bin'
                        WHEN 87 THEN 'gbk_bin'
                        WHEN 89 THEN 'tis620_bin'
                        WHEN 101 THEN 'utf16_unicode_ci'
                        WHEN 216 THEN 'gb18030_2022_bin'
                        WHEN 217 THEN 'gb18030_2022_chinese_ci'
                        WHEN 218 THEN 'gb18030_2022_chinese_cs'
                        WHEN 219 THEN 'gb18030_2022_radical_ci'
                        WHEN 220 THEN 'gb18030_2022_radical_cs'
                        WHEN 221 THEN 'gb18030_2022_stroke_ci'
                        WHEN 222 THEN 'gb18030_2022_stroke_cs'
                        WHEN 224 THEN 'utf8mb4_unicode_ci'
                        WHEN 234 THEN 'utf8mb4_czech_ci'
                        WHEN 245 THEN 'utf8mb4_croatian_ci'
                        WHEN 246 THEN 'utf8mb4_unicode_520_ci'
                        WHEN 248 THEN 'gb18030_chinese_ci'
                        WHEN 249 THEN 'gb18030_bin'
                        WHEN 255 THEN 'utf8mb4_0900_ai_ci'
                          ELSE NULL
                        END AS CHAR(64)) AS COLLATION_NAME,
                        CAST(CASE WHEN rp.param_type IN (1, 2, 3, 4, 5, 31)
                          THEN CONCAT(lower(v.data_type_str),'(',rp.param_precision,')')
                          WHEN (rp.param_type in (6, 7, 8, 9, 10) AND rp.param_zero_fill)
                          THEN CONCAT(lower(v.data_type_str), ' zerofill')
                          WHEN rp.param_type IN (15,16,50)
                          THEN CONCAT(lower(v.data_type_str),'(',rp.param_precision, ',', rp.param_scale,')')
                          WHEN rp.param_type IN (17, 18, 20)
                          THEN CONCAT(lower(v.data_type_str),'(', rp.param_scale, ')')
                          WHEN (rp.param_type IN (22, 23) AND rp.param_charset != 1)
                          THEN CONCAT(lower(v.data_type_str),'(', rp.param_length, ')')
                          WHEN (rp.param_type IN (22) AND rp.param_charset = 1)
                          THEN CONCAT(lower('VARBINARY'),'(', rp.param_length, ')')
                          WHEN (rp.param_type IN (23) AND rp.param_charset = 1)
                          THEN CONCAT(lower('BINARY'),'(', rp.param_length, ')')
                          WHEN (rp.param_type IN (27, 28, 29, 30) AND rp.param_charset = 1)
                          THEN lower(REPLACE(v.data_type_str, 'TEXT', 'BLOB'))
                          WHEN rp.param_type IN (32, 33)
                          THEN get_mysql_routine_parameter_type_str(rp.routine_id, rp.param_position)
                          WHEN rp.param_type = 41 THEN lower('DATE')
                          WHEN rp.param_type = 42 THEN CONCAT(lower('DATETIME'),'(', rp.param_scale, ')')
                          ELSE lower(v.data_type_str) END AS char(4194304)) AS DTD_IDENTIFIER,
                        CAST(CASE WHEN r.routine_type = 1 THEN 'PROCEDURE'
                          WHEN ROUTINE_TYPE = 2 THEN 'FUNCTION'
                          ELSE NULL
                        END AS CHAR(9)) AS ROUTINE_TYPE
                      from
                        oceanbase.__all_routine_param as rp
                        join oceanbase.__all_routine as r on rp.subprogram_id = r.subprogram_id
                        and rp.routine_id = r.routine_id
                        join oceanbase.__all_database as d on r.database_id = d.database_id
                        left join oceanbase.__all_virtual_data_type v on rp.param_type = v.data_type
                      WHERE
                        in_recyclebin = 0
                        and database_name != '__recyclebin'
                        and (0 = sys_privilege_check('routine_acc', 1)
                             or 0 = sys_privilege_check('routine_acc', 1, d.database_name, r.routine_name, r.routine_type))
                      order by SPECIFIC_SCHEMA,
                        SPECIFIC_NAME,
                        ORDINAL_POSITION
                      """.replace("\n", " "),
  normal_columns = []
  )

def_table_schema(
  owner = 'sean.yyj',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'TABLE_PRIVILEGES',
  table_id       = '21347',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  view_definition = """
  WITH DB_PRIV AS (
    select A.user_id USER_ID,
           A.database_name DATABASE_NAME,
           A.priv_alter PRIV_ALTER,
           A.priv_create PRIV_CREATE,
           A.priv_delete PRIV_DELETE,
           A.priv_drop PRIV_DROP,
           A.priv_grant_option PRIV_GRANT_OPTION,
           A.priv_insert PRIV_INSERT,
           A.priv_update PRIV_UPDATE,
           A.priv_select PRIV_SELECT,
           A.priv_index PRIV_INDEX,
           A.priv_create_view PRIV_CREATE_VIEW,
           A.priv_show_view PRIV_SHOW_VIEW,
           A.GMT_CREATE GMT_CREATE,
           A.GMT_MODIFIED GMT_MODIFIED,
           A.PRIV_OTHERS PRIV_OTHERS
    from oceanbase.__all_database_privilege_history A,
        (select user_id, database_name, max(schema_version) schema_version from oceanbase.__all_database_privilege_history group by user_id, database_name, database_name collate utf8mb4_bin) B
    where A.user_id = B.user_id and A.database_name collate utf8mb4_bin = B.database_name collate utf8mb4_bin and A.schema_version = B.schema_version and A.is_deleted = 0
  ),
  TABLE_PRIV AS (
    select A.user_id USER_ID,
           A.database_name DATABASE_NAME,
           A.table_name TABLE_NAME,
           A.priv_alter PRIV_ALTER,
           A.priv_create PRIV_CREATE,
           A.priv_delete PRIV_DELETE,
           A.priv_drop PRIV_DROP,
           A.priv_grant_option PRIV_GRANT_OPTION,
           A.priv_insert PRIV_INSERT,
           A.priv_update PRIV_UPDATE,
           A.priv_select PRIV_SELECT,
           A.priv_index PRIV_INDEX,
           A.priv_create_view PRIV_CREATE_VIEW,
           A.priv_show_view PRIV_SHOW_VIEW,
           A.PRIV_OTHERS PRIV_OTHERS
    from oceanbase.__all_table_privilege_history A,
        (select user_id, database_name, table_name, max(schema_version) schema_version from oceanbase.__all_table_privilege_history group by user_id, database_name, database_name collate utf8mb4_bin, table_name, table_name collate utf8mb4_bin) B
    where A.user_id = B.user_id and A.database_name collate utf8mb4_bin = B.database_name collate utf8mb4_bin and A.schema_version = B.schema_version and A.table_name collate utf8mb4_bin = B.table_name collate utf8mb4_bin and A.is_deleted = 0
  )
  SELECT
         CAST(CONCAT('''', V.USER_NAME, '''', '@', '''', V.HOST, '''') AS CHAR(81)) AS GRANTEE ,
         CAST('def' AS CHAR(512)) AS TABLE_CATALOG ,
         CAST(V.DATABASE_NAME AS CHAR(128)) collate utf8mb4_name_case AS TABLE_SCHEMA ,
         CAST(V.TABLE_NAME AS CHAR(64)) collate utf8mb4_name_case AS TABLE_NAME,
         CAST(V.PRIVILEGE_TYPE AS CHAR(64)) AS PRIVILEGE_TYPE ,
         CAST(V.IS_GRANTABLE AS CHAR(3)) AS IS_GRANTABLE
  FROM
    (SELECT TP.DATABASE_NAME AS DATABASE_NAME,
            TP.TABLE_NAME AS TABLE_NAME,
            U.USER_NAME AS USER_NAME,
            U.HOST AS HOST,
            CASE
                WHEN V1.C1 = 1
                     AND TP.PRIV_ALTER = 1 THEN 'ALTER'
                WHEN V1.C1 = 2
                     AND TP.PRIV_CREATE = 1 THEN 'CREATE'
                WHEN V1.C1 = 4
                     AND TP.PRIV_DELETE = 1 THEN 'DELETE'
                WHEN V1.C1 = 5
                     AND TP.PRIV_DROP = 1 THEN 'DROP'
                WHEN V1.C1 = 7
                     AND TP.PRIV_INSERT = 1 THEN 'INSERT'
                WHEN V1.C1 = 8
                     AND TP.PRIV_UPDATE = 1 THEN 'UPDATE'
                WHEN V1.C1 = 9
                     AND TP.PRIV_SELECT = 1 THEN 'SELECT'
                WHEN V1.C1 = 10
                     AND TP.PRIV_INDEX = 1 THEN 'INDEX'
                WHEN V1.C1 = 11
                     AND TP.PRIV_CREATE_VIEW = 1 THEN 'CREATE VIEW'
                WHEN V1.C1 = 12
                     AND TP.PRIV_SHOW_VIEW = 1 THEN 'SHOW VIEW'
                WHEN V1.C1 = 22
                     AND (TP.PRIV_OTHERS & (1 << 6)) != 0 THEN 'REFERENCES'
                WHEN V1.C1 = 44
                     AND (TP.PRIV_OTHERS & (1 << 9)) != 0 THEN 'TRIGGER'
                WHEN V1.C1 = 45
                     AND (TP.PRIV_OTHERS & (1 << 19)) != 0 THEN 'LOCK TABLES'
                ELSE NULL
            END PRIVILEGE_TYPE ,
            CASE
                WHEN TP.PRIV_GRANT_OPTION = 1 THEN 'YES'
                WHEN TP.PRIV_GRANT_OPTION = 0 THEN 'NO'
            END IS_GRANTABLE
     FROM TABLE_PRIV TP,
                      oceanbase.__all_user U,
       (SELECT 1 AS C1
        UNION ALL SELECT 2 AS C1
        UNION ALL SELECT 4 AS C1
        UNION ALL SELECT 5 AS C1
        UNION ALL SELECT 7 AS C1
        UNION ALL SELECT 8 AS C1
        UNION ALL SELECT 9 AS C1
        UNION ALL SELECT 10 AS C1
        UNION ALL SELECT 11 AS C1
        UNION ALL SELECT 12 AS C1
        UNION ALL SELECT 22 AS C1
        UNION ALL SELECT 44 AS C1
        UNION ALL SELECT 45 AS C1) V1,
       (SELECT USER_ID
        FROM oceanbase.__all_user
        WHERE CONCAT(USER_NAME, '@', HOST) = CURRENT_USER()) CURR
     LEFT JOIN
       (SELECT USER_ID
        FROM DB_PRIV
        WHERE DATABASE_NAME = 'mysql'
          AND PRIV_SELECT = 1) DB ON CURR.USER_ID = DB.USER_ID
     WHERE TP.USER_ID = U.USER_ID
       AND (DB.USER_ID IS NOT NULL
            OR 512 & CURRENT_USER_PRIV() = 512
            OR TP.USER_ID = CURR.USER_ID)) V
  WHERE V.PRIVILEGE_TYPE IS NOT NULL
  """.replace("\n", " "),

  normal_columns = [
  ]
  )

def_table_schema(
  owner = 'sean.yyj',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'USER_PRIVILEGES',
  table_id       = '21348',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  view_definition = """
  SELECT CAST(CONCAT('''', V.USER_NAME, '''', '@', '''', V.HOST, '''') AS CHAR(81)) AS GRANTEE ,
         CAST('def' AS CHAR(512)) AS TABLE_CATALOG ,
         CAST(V.PRIVILEGE_TYPE AS CHAR(64)) AS PRIVILEGE_TYPE ,
         CAST(V.IS_GRANTABLE AS CHAR(3)) AS IS_GRANTABLE
  FROM
    (SELECT U.USER_NAME AS USER_NAME,
            U.HOST AS HOST,
            CASE
                WHEN V1.C1 = 1
                     AND U.PRIV_ALTER = 1 THEN 'ALTER'
                WHEN V1.C1 = 2
                     AND U.PRIV_CREATE = 1 THEN 'CREATE'
                WHEN V1.C1 = 3
                     AND U.PRIV_CREATE_USER = 1 THEN 'CREATE USER'
                WHEN V1.C1 = 4
                     AND U.PRIV_DELETE = 1 THEN 'DELETE'
                WHEN V1.C1 = 5
                     AND U.PRIV_DROP = 1 THEN 'DROP'
                WHEN V1.C1 = 7
                     AND U.PRIV_INSERT = 1 THEN 'INSERT'
                WHEN V1.C1 = 8
                     AND U.PRIV_UPDATE = 1 THEN 'UPDATE'
                WHEN V1.C1 = 9
                     AND U.PRIV_SELECT = 1 THEN 'SELECT'
                WHEN V1.C1 = 10
                     AND U.PRIV_INDEX = 1 THEN 'INDEX'
                WHEN V1.C1 = 11
                     AND U.PRIV_CREATE_VIEW = 1 THEN 'CREATE VIEW'
                WHEN V1.C1 = 12
                     AND U.PRIV_SHOW_VIEW = 1 THEN 'SHOW VIEW'
                WHEN V1.C1 = 13
                     AND U.PRIV_SHOW_DB = 1 THEN 'SHOW DATABASES'
                WHEN V1.C1 = 14
                     AND U.PRIV_SUPER = 1 THEN 'SUPER'
                WHEN V1.C1 = 15
                     AND U.PRIV_PROCESS = 1 THEN 'PROCESS'
                WHEN V1.C1 = 17
                     AND U.PRIV_CREATE_SYNONYM = 1 THEN 'CREATE SYNONYM'
                WHEN V1.C1 = 22
                     AND (U.PRIV_OTHERS & (1 << 6)) != 0 THEN 'REFERENCES'
                WHEN V1.C1 = 23
                     AND (U.PRIV_OTHERS & (1 << 0)) != 0 THEN 'EXECUTE'
                WHEN V1.C1 = 27
                     AND U.PRIV_FILE = 1 THEN 'FILE'
                WHEN V1.C1 = 28
                     AND U.PRIV_ALTER_TENANT = 1 THEN 'ALTER TENANT'
                WHEN V1.C1 = 29
                     AND U.PRIV_ALTER_SYSTEM = 1 THEN 'ALTER SYSTEM'
                WHEN V1.C1 = 30
                     AND U.PRIV_CREATE_RESOURCE_POOL = 1 THEN 'CREATE RESOURCE POOL'
                WHEN V1.C1 = 31
                     AND U.PRIV_CREATE_RESOURCE_UNIT = 1 THEN 'CREATE RESOURCE UNIT'
                WHEN V1.C1 = 33
                     AND U.PRIV_REPL_SLAVE = 1 THEN 'REPLICATION SLAVE'
                WHEN V1.C1 = 34
                     AND U.PRIV_REPL_CLIENT = 1 THEN 'REPLICATION CLIENT'
                WHEN V1.C1 = 35
                     AND U.PRIV_DROP_DATABASE_LINK = 1 THEN 'DROP DATABASE LINK'
                WHEN V1.C1 = 36
                     AND U.PRIV_CREATE_DATABASE_LINK = 1 THEN 'CREATE DATABASE LINK'
                WHEN V1.C1 = 37
                     AND (U.PRIV_OTHERS & (1 << 1)) != 0 THEN 'ALTER ROUTINE'
                WHEN V1.C1 = 38
                     AND (U.PRIV_OTHERS & (1 << 2)) != 0 THEN 'CREATE ROUTINE'
                WHEN V1.C1 = 39
                     AND (U.PRIV_OTHERS & (1 << 3)) != 0 THEN 'CREATE TABLESPACE'
                WHEN V1.C1 = 40
                     AND (U.PRIV_OTHERS & (1 << 4)) != 0 THEN 'SHUTDOWN'
                WHEN V1.C1 = 41
                     AND (U.PRIV_OTHERS & (1 << 5)) != 0 THEN 'RELOAD'
                WHEN V1.C1 = 42
                     AND (U.PRIV_OTHERS & (1 << 7)) != 0 THEN 'CREATE ROLE'
                WHEN V1.C1 = 43
                     AND (U.PRIV_OTHERS & (1 << 8)) != 0 THEN 'DROP ROLE'
                WHEN V1.C1 = 44
                     AND (U.PRIV_OTHERS & (1 << 9)) != 0 THEN 'TRIGGER'
                WHEN V1.C1 = 45
                     AND (U.PRIV_OTHERS & (1 << 10)) != 0 THEN 'LOCK TABLES'
                WHEN V1.C1 = 46
                     AND (U.PRIV_OTHERS & (1 << 11) != 0) THEN 'ENCRYPT'
                WHEN V1.C1 = 47
                     AND (U.PRIV_OTHERS & (1 << 12) != 0) THEN 'DECRYPT'
                WHEN V1.C1 = 49
                     AND (U.PRIV_OTHERS & (1 << 13) != 0) THEN 'EVENT'
                WHEN V1.C1 = 50
                     AND (U.PRIV_OTHERS & (1 << 14) != 0) THEN 'CREATE CATALOG'
                WHEN V1.C1 = 51
                     AND (U.PRIV_OTHERS & (1 << 15) != 0) THEN 'USE CATALOG'
                WHEN V1.C1 = 52
                     AND (U.PRIV_OTHERS & (1 << 20) != 0) THEN 'CREATE LOCATION'
                WHEN V1.C1 = 55
                     AND (U.PRIV_OTHERS & (1 << 16) != 0) THEN 'CREATE AI MODEL'
                WHEN V1.C1 = 56
                     AND (U.PRIV_OTHERS & (1 << 17) != 0) THEN 'ALTER AI MODEL'
                WHEN V1.C1 = 57
                     AND (U.PRIV_OTHERS & (1 << 18) != 0) THEN 'DROP AI MODEL'
                WHEN V1.C1 = 58
                     AND (U.PRIV_OTHERS & (1 << 19) != 0) THEN 'ACCESS AI MODEL'
                WHEN V1.C1 = 0
                     AND U.PRIV_ALTER = 0
                     AND U.PRIV_CREATE = 0
                     AND U.PRIV_CREATE_USER = 0
                     AND U.PRIV_DELETE = 0
                     AND U.PRIV_DROP = 0
                     AND U.PRIV_INSERT = 0
                     AND U.PRIV_UPDATE = 0
                     AND U.PRIV_SELECT = 0
                     AND U.PRIV_INDEX = 0
                     AND U.PRIV_CREATE_VIEW = 0
                     AND U.PRIV_SHOW_VIEW = 0
                     AND U.PRIV_SHOW_DB = 0
                     AND U.PRIV_SUPER = 0
                     AND U.PRIV_PROCESS = 0
                     AND U.PRIV_CREATE_SYNONYM = 0
                     AND U.PRIV_FILE = 0
                     AND U.PRIV_ALTER_TENANT = 0
                     AND U.PRIV_ALTER_SYSTEM = 0
                     AND U.PRIV_CREATE_RESOURCE_POOL = 0
                     AND U.PRIV_CREATE_RESOURCE_UNIT = 0
                     AND U.PRIV_REPL_SLAVE = 0
                     AND U.PRIV_REPL_CLIENT = 0
                     AND U.PRIV_DROP_DATABASE_LINK = 0
                     AND U.PRIV_CREATE_DATABASE_LINK = 0
                     AND U.PRIV_OTHERS = 0 THEN 'USAGE'
            END PRIVILEGE_TYPE ,
            CASE
                WHEN U.PRIV_GRANT_OPTION = 0 THEN 'NO'
                WHEN U.PRIV_ALTER = 0
                     AND U.PRIV_CREATE = 0
                     AND U.PRIV_CREATE_USER = 0
                     AND U.PRIV_DELETE = 0
                     AND U.PRIV_DROP = 0
                     AND U.PRIV_INSERT = 0
                     AND U.PRIV_UPDATE = 0
                     AND U.PRIV_SELECT = 0
                     AND U.PRIV_INDEX = 0
                     AND U.PRIV_CREATE_VIEW = 0
                     AND U.PRIV_SHOW_VIEW = 0
                     AND U.PRIV_SHOW_DB = 0
                     AND U.PRIV_SUPER = 0
                     AND U.PRIV_PROCESS = 0
                     AND U.PRIV_CREATE_SYNONYM = 0
                     AND U.PRIV_FILE = 0
                     AND U.PRIV_ALTER_TENANT = 0
                     AND U.PRIV_ALTER_SYSTEM = 0
                     AND U.PRIV_CREATE_RESOURCE_POOL = 0
                     AND U.PRIV_CREATE_RESOURCE_UNIT = 0
                     AND U.PRIV_REPL_SLAVE = 0
                     AND U.PRIV_REPL_CLIENT = 0
                     AND U.PRIV_DROP_DATABASE_LINK = 0
                     AND U.PRIV_CREATE_DATABASE_LINK = 0
                     AND U.PRIV_OTHERS = 0 THEN 'NO'
                WHEN U.PRIV_GRANT_OPTION = 1 THEN 'YES'
            END IS_GRANTABLE
     FROM oceanbase.__all_user U,
       (SELECT 0 AS C1
        UNION ALL SELECT 1 AS C1
        UNION ALL SELECT 2 AS C1
        UNION ALL SELECT 3 AS C1
        UNION ALL SELECT 4 AS C1
        UNION ALL SELECT 5 AS C1
        UNION ALL SELECT 7 AS C1
        UNION ALL SELECT 8 AS C1
        UNION ALL SELECT 9 AS C1
        UNION ALL SELECT 10 AS C1
        UNION ALL SELECT 11 AS C1
        UNION ALL SELECT 12 AS C1
        UNION ALL SELECT 13 AS C1
        UNION ALL SELECT 14 AS C1
        UNION ALL SELECT 15 AS C1
        UNION ALL SELECT 17 AS C1
        UNION ALL SELECT 22 AS C1
        UNION ALL SELECT 23 AS C1
        UNION ALL SELECT 27 AS C1
        UNION ALL SELECT 28 AS C1
        UNION ALL SELECT 29 AS C1
        UNION ALL SELECT 30 AS C1
        UNION ALL SELECT 31 AS C1
        UNION ALL SELECT 33 AS C1
        UNION ALL SELECT 34 AS C1
        UNION ALL SELECT 35 AS C1
        UNION ALL SELECT 36 AS C1
        UNION ALL SELECT 37 AS C1
        UNION ALL SELECT 38 AS C1
        UNION ALL SELECT 39 AS C1
        UNION ALL SELECT 40 AS C1
        UNION ALL SELECT 41 AS C1
        UNION ALL SELECT 42 AS C1
        UNION ALL SELECT 43 AS C1
        UNION ALL SELECT 44 AS C1
        UNION ALL SELECT 45 AS C1
        UNION ALL SELECT 46 AS C1
        UNION ALL SELECT 47 AS C1
        UNION ALL SELECT 49 AS C1
        UNION ALL SELECT 50 AS C1
        UNION ALL SELECT 51 AS C1
        UNION ALL SELECT 52 AS C1
        UNION ALL SELECT 55 AS C1
        UNION ALL SELECT 56 AS C1
        UNION ALL SELECT 57 AS C1
        UNION ALL SELECT 58 AS C1) V1,
       (SELECT USER_ID
        FROM oceanbase.__all_user
        WHERE CONCAT(USER_NAME, '@', HOST) = CURRENT_USER()) CURR
     LEFT JOIN
       (SELECT USER_ID
        FROM oceanbase.__all_database_privilege
        WHERE DATABASE_NAME = 'mysql'
          AND PRIV_SELECT = 1) DB ON CURR.USER_ID = DB.USER_ID
     WHERE (DB.USER_ID IS NOT NULL
            OR 512 & CURRENT_USER_PRIV() = 512
            OR U.USER_ID = CURR.USER_ID)) V
  WHERE V.PRIVILEGE_TYPE IS NOT NULL
  """.replace("\n", " "),

  normal_columns = [
  ]
  )

def_table_schema(
  owner = 'sean.yyj',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'SCHEMA_PRIVILEGES',
  table_id       = '21349',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  view_definition = """
  WITH DB_PRIV AS (
    select A.user_id USER_ID,
           A.database_name DATABASE_NAME,
           A.priv_alter PRIV_ALTER,
           A.priv_create PRIV_CREATE,
           A.priv_delete PRIV_DELETE,
           A.priv_drop PRIV_DROP,
           A.priv_grant_option PRIV_GRANT_OPTION,
           A.priv_insert PRIV_INSERT,
           A.priv_update PRIV_UPDATE,
           A.priv_select PRIV_SELECT,
           A.priv_index PRIV_INDEX,
           A.priv_create_view PRIV_CREATE_VIEW,
           A.priv_show_view PRIV_SHOW_VIEW,
           A.priv_others PRIV_OTHERS
    from oceanbase.__all_database_privilege_history A,
        (select user_id, database_name, max(schema_version) schema_version from oceanbase.__all_database_privilege_history group by user_id, database_name, database_name collate utf8mb4_bin) B
    where A.user_id = B.user_id and A.database_name collate utf8mb4_bin = B.database_name collate utf8mb4_bin and A.schema_version = B.schema_version and A.is_deleted = 0
  )
  SELECT CAST(CONCAT('''', V.USER_NAME, '''', '@', '''', V.HOST, '''') AS CHAR(81)) AS GRANTEE ,
         CAST('def' AS CHAR(512)) AS TABLE_CATALOG ,
         CAST(V.DATABASE_NAME AS CHAR(128)) collate utf8mb4_name_case AS TABLE_SCHEMA ,
         CAST(V.PRIVILEGE_TYPE AS CHAR(64)) AS PRIVILEGE_TYPE ,
         CAST(V.IS_GRANTABLE AS CHAR(3)) AS IS_GRANTABLE
  FROM
    (SELECT DP.DATABASE_NAME DATABASE_NAME,
            U.USER_NAME AS USER_NAME,
            U.HOST AS HOST,
            CASE
                WHEN V1.C1 = 1
                     AND DP.PRIV_ALTER = 1 THEN 'ALTER'
                WHEN V1.C1 = 2
                     AND DP.PRIV_CREATE = 1 THEN 'CREATE'
                WHEN V1.C1 = 4
                     AND DP.PRIV_DELETE = 1 THEN 'DELETE'
                WHEN V1.C1 = 5
                     AND DP.PRIV_DROP = 1 THEN 'DROP'
                WHEN V1.C1 = 7
                     AND DP.PRIV_INSERT = 1 THEN 'INSERT'
                WHEN V1.C1 = 8
                     AND DP.PRIV_UPDATE = 1 THEN 'UPDATE'
                WHEN V1.C1 = 9
                     AND DP.PRIV_SELECT = 1 THEN 'SELECT'
                WHEN V1.C1 = 10
                     AND DP.PRIV_INDEX = 1 THEN 'INDEX'
                WHEN V1.C1 = 11
                     AND DP.PRIV_CREATE_VIEW = 1 THEN 'CREATE VIEW'
                WHEN V1.C1 = 12
                     AND DP.PRIV_SHOW_VIEW = 1 THEN 'SHOW VIEW'
                WHEN V1.C1 = 22
                     AND (DP.PRIV_OTHERS & (1 << 6)) != 0 THEN 'REFERENCES'
                WHEN V1.C1 = 23
                     AND (DP.PRIV_OTHERS & (1 << 0)) != 0 THEN 'EXECUTE'
                WHEN V1.C1 = 37
                     AND (DP.PRIV_OTHERS & (1 << 1)) != 0 THEN 'ALTER ROUTINE'
                WHEN V1.C1 = 38
                     AND (DP.PRIV_OTHERS & (1 << 2)) != 0 THEN 'CREATE ROUTINE'
                WHEN V1.C1 = 44
                     AND (DP.PRIV_OTHERS & (1 << 9)) != 0 THEN 'TRIGGER'
                WHEN V1.C1 = 45
                     AND (DP.PRIV_OTHERS & (1 << 10)) != 0 THEN 'LOCK TABLES'
                WHEN V1.C1 = 49
                     AND (DP.PRIV_OTHERS & (1 << 13)) != 0 THEN 'EVENT'
                ELSE NULL
            END PRIVILEGE_TYPE ,
            CASE
                WHEN DP.PRIV_GRANT_OPTION = 1 THEN 'YES'
                WHEN DP.PRIV_GRANT_OPTION = 0 THEN 'NO'
            END IS_GRANTABLE
     FROM DB_PRIV DP,
                      oceanbase.__all_user U,
       (SELECT 1 AS C1
        UNION ALL SELECT 2 AS C1
        UNION ALL SELECT 4 AS C1
        UNION ALL SELECT 5 AS C1
        UNION ALL SELECT 7 AS C1
        UNION ALL SELECT 8 AS C1
        UNION ALL SELECT 9 AS C1
        UNION ALL SELECT 10 AS C1
        UNION ALL SELECT 11 AS C1
        UNION ALL SELECT 12 AS C1
        UNION ALL SELECT 22 AS C1
        UNION ALL SELECT 23 AS C1
        UNION ALL SELECT 37 AS C1
        UNION ALL SELECT 38 AS C1
        UNION ALL SELECT 44 AS C1
        UNION ALL SELECT 45 AS C1
        UNION ALL SELECT 49 AS C1) V1,
       (SELECT USER_ID
        FROM oceanbase.__all_user
        WHERE CONCAT(USER_NAME, '@', HOST) = CURRENT_USER()) CURR
     LEFT JOIN
       (SELECT USER_ID
        FROM DB_PRIV
        WHERE DATABASE_NAME = 'mysql'
          AND PRIV_SELECT = 1) DB ON CURR.USER_ID = DB.USER_ID
     WHERE DP.USER_ID = U.USER_ID
       AND DP.DATABASE_NAME != '__recyclebin'
       AND DP.DATABASE_NAME != '__public'
       AND DP.DATABASE_NAME != 'SYS'
       AND DP.DATABASE_NAME != 'LBACSYS'
       AND DP.DATABASE_NAME != 'ORAAUDITOR'
       AND (DB.USER_ID IS NOT NULL
            OR 512 & CURRENT_USER_PRIV() = 512
            OR DP.USER_ID = CURR.USER_ID)) V
  WHERE V.PRIVILEGE_TYPE IS NOT NULL
  """.replace("\n", " "),

  normal_columns = [
  ]
  )

def_table_schema(
  owner = 'bin.lb',
  tablegroup_id = 'OB_INVALID_ID',
  database_id   = 'OB_INFORMATION_SCHEMA_ID',
  table_name    = 'CHECK_CONSTRAINTS',
  table_id      = '21350',
  table_type    = 'SYSTEM_VIEW',
  gm_columns    = [],
  rowkey_columns = [],
  normal_columns  = [],
  in_tenant_space = True,
  view_definition = """
    SELECT CAST('def' AS CHAR(64)) AS CONSTRAINT_CATALOG,
           CAST(d.database_name AS CHAR(128)) collate utf8mb4_name_case AS CONSTRAINT_SCHEMA,
           CAST(c.constraint_name AS CHAR(64)) AS CONSTRAINT_NAME,
           CAST(c.check_expr AS CHAR(2048)) AS CHECK_CLAUSE
    FROM oceanbase.__all_database d
    JOIN oceanbase.__all_table t ON d.database_id = t.database_id
    JOIN oceanbase.__all_constraint c ON t.table_id = c.table_id
    WHERE d.database_id > 500000 and d.in_recyclebin = 0
      AND t.table_type = 3
      AND c.constraint_type = 3
      AND t.table_mode >> 12 & 15 in (0,1)
      and t.index_attributes_set & 16 = 0
      AND (0 = sys_privilege_check('table_acc', 1)
           OR 0 = sys_privilege_check('table_acc', 1, d.database_name, t.table_name))
  """.replace("\n", " ")
  )

def_table_schema(
  owner = 'bin.lb',
  tablegroup_id = 'OB_INVALID_ID',
  database_id   = 'OB_INFORMATION_SCHEMA_ID',
  table_name    = 'REFERENTIAL_CONSTRAINTS',
  table_id      = '21351',
  table_type    = 'SYSTEM_VIEW',
  gm_columns    = [],
  rowkey_columns = [],
  normal_columns  = [],
  in_tenant_space = True,
  view_definition = """

    select
    CAST('def' AS CHAR(64)) AS CONSTRAINT_CATALOG,
    CAST(cd.database_name AS CHAR(128)) collate utf8mb4_name_case AS CONSTRAINT_SCHEMA,
    CAST(f.foreign_key_name AS CHAR(128)) AS CONSTRAINT_NAME,
    CAST('def' AS CHAR(64)) AS UNIQUE_CONSTRAINT_CATALOG,
    CAST(pd.database_name AS CHAR(128)) collate utf8mb4_name_case AS UNIQUE_CONSTRAINT_SCHEMA,
    CAST(CASE WHEN f.ref_cst_type = 1 THEN 'PRIMARY'
         ELSE NULL END AS CHAR(128)) AS UNIQUE_CONSTRAINT_NAME,
    CAST('NONE' AS CHAR(64)) AS MATCH_OPTION,
    CAST(CASE WHEN f.update_action = 1 THEN 'RESTRICT'
              WHEN f.update_action = 2 THEN 'CASCADE'
              WHEN f.update_action = 3 THEN 'SET NULL'
              WHEN f.update_action = 4 THEN 'NO ACTION'
              WHEN f.update_action = 5 THEN 'SET_DEFAULT'
         ELSE NULL END AS CHAR(64)) AS UPDATE_RULE,
    CAST(CASE WHEN f.delete_action = 1 THEN 'RESTRICT'
              WHEN f.delete_action = 2 THEN 'CASCADE'
              WHEN f.delete_action = 3 THEN 'SET NULL'
              WHEN f.delete_action = 4 THEN 'NO ACTION'
              WHEN f.delete_action = 5 THEN 'SET_DEFAULT'
         ELSE NULL END AS CHAR(64)) AS DELETE_RULE,
    CAST(ct.table_name AS CHAR(256)) AS TABLE_NAME,
    CAST(pt.table_name AS CHAR(256)) AS REFERENCED_TABLE_NAME
    FROM oceanbase.__all_foreign_key f
    JOIN oceanbase.__all_table ct on f.child_table_id = ct.table_id and f.is_parent_table_mock = 0 and f.ref_cst_type = 1
    JOIN oceanbase.__all_database cd on ct.database_id = cd.database_id
    JOIN oceanbase.__all_table pt on f.parent_table_id = pt.table_id
    JOIN oceanbase.__all_database pd on pt.database_id = pd.database_id
    WHERE cd.database_id > 500000 and cd.in_recyclebin = 0
      AND ct.table_type = 3
      AND ct.table_mode >> 12 & 15 in (0,1)
      AND ct.index_attributes_set & 16 = 0
      AND (0 = sys_privilege_check('table_acc', 1)
           OR 0 = sys_privilege_check('table_acc', 1, cd.database_name, ct.table_name))

    union all

    select
    CAST('def' AS CHAR(64)) AS CONSTRAINT_CATALOG,
    CAST(cd.database_name AS CHAR(128)) collate utf8mb4_name_case AS CONSTRAINT_SCHEMA,
    CAST(f.foreign_key_name AS CHAR(128)) AS CONSTRAINT_NAME,
    CAST('def' AS CHAR(64)) AS UNIQUE_CONSTRAINT_CATALOG,
    CAST(pd.database_name AS CHAR(128)) AS UNIQUE_CONSTRAINT_SCHEMA,
    CAST(CASE WHEN it.table_type = 3 THEN 'PRIMARY'
              WHEN it.index_type in (2, 4, 8) THEN SUBSTR(it.table_name, 7 + INSTR(SUBSTR(it.table_name, 7), '_'))
         ELSE NULL END AS CHAR(128)) AS UNIQUE_CONSTRAINT_NAME,
    CAST('NONE' AS CHAR(64)) AS MATCH_OPTION,
    CAST(CASE WHEN f.update_action = 1 THEN 'RESTRICT'
              WHEN f.update_action = 2 THEN 'CASCADE'
              WHEN f.update_action = 3 THEN 'SET NULL'
              WHEN f.update_action = 4 THEN 'NO ACTION'
              WHEN f.update_action = 5 THEN 'SET_DEFAULT'
         ELSE NULL END AS CHAR(64)) AS UPDATE_RULE,
    CAST(CASE WHEN f.delete_action = 1 THEN 'RESTRICT'
              WHEN f.delete_action = 2 THEN 'CASCADE'
              WHEN f.delete_action = 3 THEN 'SET NULL'
              WHEN f.delete_action = 4 THEN 'NO ACTION'
              WHEN f.delete_action = 5 THEN 'SET_DEFAULT'
         ELSE NULL END AS CHAR(64)) AS DELETE_RULE,
    CAST(ct.table_name AS CHAR(256)) AS TABLE_NAME,
    CAST(pt.table_name AS CHAR(256)) AS REFERENCED_TABLE_NAME
    FROM oceanbase.__all_foreign_key f
    JOIN oceanbase.__all_table ct on f.child_table_id = ct.table_id and f.is_parent_table_mock = 0 and f.ref_cst_type in (2, 5)
    JOIN oceanbase.__all_database cd on ct.database_id = cd.database_id
    JOIN oceanbase.__all_table pt on f.parent_table_id = pt.table_id
    JOIN oceanbase.__all_database pd on pt.database_id = pd.database_id
    JOIN oceanbase.__all_table it on f.ref_cst_id = it.table_id
    WHERE cd.database_id > 500000 and cd.in_recyclebin = 0
      AND ct.table_type = 3
      AND (0 = sys_privilege_check('table_acc', 1)
           OR 0 = sys_privilege_check('table_acc', 1, cd.database_name, ct.table_name))

    union all

    select
    CAST('def' AS CHAR(64)) AS CONSTRAINT_CATALOG,
    CAST(cd.database_name AS CHAR(128)) collate utf8mb4_name_case AS CONSTRAINT_SCHEMA,
    CAST(f.foreign_key_name AS CHAR(128)) AS CONSTRAINT_NAME,
    CAST('def' AS CHAR(64)) AS UNIQUE_CONSTRAINT_CATALOG,
    CAST(pd.database_name AS CHAR(128)) collate utf8mb4_name_case AS UNIQUE_CONSTRAINT_SCHEMA,
    CAST(NULL AS CHAR(128)) AS UNIQUE_CONSTRAINT_NAME,
    CAST('NONE' AS CHAR(64)) AS MATCH_OPTION,
    CAST(CASE WHEN f.update_action = 1 THEN 'RESTRICT'
              WHEN f.update_action = 2 THEN 'CASCADE'
              WHEN f.update_action = 3 THEN 'SET NULL'
              WHEN f.update_action = 4 THEN 'NO ACTION'
              WHEN f.update_action = 5 THEN 'SET_DEFAULT'
         ELSE NULL END AS CHAR(64)) AS UPDATE_RULE,
    CAST(CASE WHEN f.delete_action = 1 THEN 'RESTRICT'
              WHEN f.delete_action = 2 THEN 'CASCADE'
              WHEN f.delete_action = 3 THEN 'SET NULL'
              WHEN f.delete_action = 4 THEN 'NO ACTION'
              WHEN f.delete_action = 5 THEN 'SET_DEFAULT'
         ELSE NULL END AS CHAR(64)) AS DELETE_RULE,
    CAST(ct.table_name AS CHAR(256)) AS TABLE_NAME,
    CAST(pt.mock_fk_parent_table_name AS CHAR(256)) AS REFERENCED_TABLE_NAME
    FROM oceanbase.__all_foreign_key f
    JOIN oceanbase.__all_table ct on f.child_table_id = ct.table_id and f.is_parent_table_mock = 1
    JOIN oceanbase.__all_database cd on ct.database_id = cd.database_id
    JOIN oceanbase.__all_mock_fk_parent_table pt on f.parent_table_id = pt.mock_fk_parent_table_id
    JOIN oceanbase.__all_database pd on pt.database_id = pd.database_id
    WHERE cd.database_id > 500000 and cd.in_recyclebin = 0
      AND ct.table_type = 3
      AND (0 = sys_privilege_check('table_acc', 1)
           OR 0 = sys_privilege_check('table_acc', 1, cd.database_name, ct.table_name))
  """.replace("\n", " ")
  )

def_table_schema(
  owner = 'bin.lb',
  tablegroup_id = 'OB_INVALID_ID',
  database_id   = 'OB_INFORMATION_SCHEMA_ID',
  table_name    = 'TABLE_CONSTRAINTS',
  table_id      = '21352',
  table_type    = 'SYSTEM_VIEW',
  gm_columns    = [],
  rowkey_columns = [],
  normal_columns  = [],
  in_tenant_space = True,
  view_definition = """

    SELECT
           CAST('def' AS CHAR(64)) AS CONSTRAINT_CATALOG,
           CAST(d.database_name AS CHAR(128)) collate utf8mb4_name_case AS CONSTRAINT_SCHEMA,
           CAST('PRIMARY' AS CHAR(256)) AS CONSTRAINT_NAME,
           CAST(d.database_name AS CHAR(128)) collate utf8mb4_name_case AS TABLE_SCHEMA,
           CAST(t.table_name AS CHAR(256)) collate utf8mb4_name_case AS TABLE_NAME,
           CAST('PRIMARY KEY' AS CHAR(11)) AS CONSTRAINT_TYPE,
           CAST('YES' AS CHAR(3)) AS ENFORCED
    FROM oceanbase.__all_database d
    JOIN oceanbase.__all_table t ON d.database_id = t.database_id
    WHERE (d.database_id = 201003 OR d.database_id > 500000) AND d.in_recyclebin = 0
      AND t.table_type = 3
      AND t.table_mode >> 16 & 1 = 0
      AND t.table_mode >> 12 & 15 in (0,1)
      AND t.index_attributes_set & 16 = 0
      AND (0 = sys_privilege_check('table_acc', 1)
           OR 0 = sys_privilege_check('table_acc', 1, d.database_name, t.table_name))

    union all

    SELECT
           CAST('def' AS CHAR(64)) AS CONSTRAINT_CATALOG,
           CAST(d.database_name AS CHAR(128)) collate utf8mb4_name_case AS CONSTRAINT_SCHEMA,
           CAST(SUBSTR(it.table_name, 7 + INSTR(SUBSTR(it.table_name, 7), '_')) AS CHAR(256)) AS CONSTRAINT_NAME,
           CAST(d.database_name AS CHAR(128)) collate utf8mb4_name_case AS TABLE_SCHEMA,
           CAST(ut.table_name AS CHAR(256)) collate utf8mb4_name_case AS TABLE_NAME,
           CAST(CASE WHEN it.index_type = 41 THEN 'PRIMARY KEY'
                ELSE 'UNIQUE' END AS CHAR(11)) AS CONSTRAINT_TYPE,
           CAST('YES' AS CHAR(3)) AS ENFORCED
    FROM oceanbase.__all_database d
    JOIN oceanbase.__all_table it ON d.database_id = it.database_id
    JOIN oceanbase.__all_table ut ON it.data_table_id = ut.table_id
    WHERE d.database_id > 500000 AND d.in_recyclebin = 0
      AND it.table_type = 5
      AND it.index_type IN (2, 4, 8, 41)
      AND (0 = sys_privilege_check('table_acc', 1)
           OR 0 = sys_privilege_check('table_acc', 1, d.database_name, ut.table_name))

    union all

    SELECT
           CAST('def' AS CHAR(64)) AS CONSTRAINT_CATALOG,
           CAST(d.database_name AS CHAR(128)) collate utf8mb4_name_case AS CONSTRAINT_SCHEMA,
           CAST(c.constraint_name AS CHAR(256)) AS CONSTRAINT_NAME,
           CAST(d.database_name AS CHAR(128)) collate utf8mb4_name_case AS TABLE_SCHEMA,
           CAST(t.table_name AS CHAR(256)) collate utf8mb4_name_case AS TABLE_NAME,
           CAST('CHECK' AS CHAR(11)) AS CONSTRAINT_TYPE,
           CAST(CASE WHEN c.enable_flag = 1 THEN 'YES'
                ELSE 'NO' END AS CHAR(3)) AS ENFORCED
    FROM oceanbase.__all_database d
    JOIN oceanbase.__all_table t ON d.database_id = t.database_id
    JOIN oceanbase.__all_constraint c ON t.table_id = c.table_id
    WHERE d.database_id > 500000 AND d.in_recyclebin = 0
      AND t.table_type = 3
      AND c.constraint_type = 3
      AND (0 = sys_privilege_check('table_acc', 1)
           OR 0 = sys_privilege_check('table_acc', 1, d.database_name, t.table_name))

    union all

    SELECT
           CAST('def' AS CHAR(64)) AS CONSTRAINT_CATALOG,
           CAST(f.constraint_schema AS CHAR(128)) collate utf8mb4_name_case AS CONSTRAINT_SCHEMA,
           CAST(f.constraint_name AS CHAR(256)) AS CONSTRAINT_NAME,
           CAST(f.constraint_schema AS CHAR(128)) collate utf8mb4_name_case AS TABLE_SCHEMA,
           CAST(f.table_name AS CHAR(256)) collate utf8mb4_name_case AS TABLE_NAME,
           CAST('FOREIGN KEY' AS CHAR(11)) AS CONSTRAINT_TYPE,
           CAST('YES' AS CHAR(3)) AS ENFORCED
    FROM information_schema.REFERENTIAL_CONSTRAINTS f

  """.replace("\n", " ")
  )

# 21353: GV$OB_TRANSACTION_SCHEDULERS # removed (single-tenant GV/V collapse; folded into V$OB_TRANSACTION_SCHEDULERS)

def_table_schema(
  owner = 'wuyuefei.wyf',
  table_name      = 'V$OB_TRANSACTION_SCHEDULERS',
  table_id        = '21354',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """SELECT
    session_id AS SESSION_ID,
    trans_id AS TX_ID,
    CASE
      WHEN state = 0 THEN 'INVALID'
      WHEN state = 1 THEN 'IDLE'
      WHEN state = 2 THEN 'EXPLICIT_ACTIVE'
      WHEN state = 3 THEN 'IMPLICIT_ACTIVE'
      WHEN state = 4 THEN 'ROLLBACK_SAVEPOINT'
      WHEN state = 5 THEN 'IN_TERMINATE'
      WHEN state = 6 THEN 'ABORTED'
      WHEN state = 7 THEN 'ROLLED_BACK'
      WHEN state = 8 THEN 'COMMIT_TIMEOUT'
      WHEN state = 9 THEN 'COMMIT_UNKNOWN'
      WHEN state = 10 THEN 'COMMITTED'
      ELSE 'UNKNOWN'
      END AS STATE,
    write_state AS WRITE_STATE,
    CASE
      WHEN isolation_level = -1 THEN 'INVALID'
      WHEN isolation_level = 0 THEN 'READ UNCOMMITTED'
      WHEN isolation_level = 1 THEN 'READ COMMITTED'
      WHEN isolation_level = 2 THEN 'REPEATABLE READ'
      WHEN isolation_level = 3 THEN 'SERIALIZABLE'
      ELSE 'UNKNOWN'
      END AS ISOLATION_LEVEL,
    snapshot_version AS SNAPSHOT_VERSION,
    CASE
      WHEN access_mode = -1 THEN 'INVALID'
      WHEN access_mode = 0 THEN 'READ_WRITE'
      WHEN access_mode = 1 THEN 'READ_ONLY'
      ELSE 'UNKNOWN'
      END AS ACCESS_MODE,
    tx_op_sn AS TX_OP_SN,
    active_time AS ACTIVE_TIME,
    expire_time AS EXPIRE_TIME,
    CASE
      WHEN can_early_lock_release = 0 THEN 'FALSE'
      WHEN can_early_lock_release = 1 THEN 'TRUE'
      ELSE 'UNKNOWN'
      END AS CAN_EARLY_LOCK_RELEASE
    FROM oceanbase.__all_virtual_trans_scheduler
""".replace("\n", " ")
  )

def_table_schema(
    owner           = 'webber.wb',
    tablegroup_id   = 'OB_INVALID_ID',
    table_name      = 'TRIGGERS',
    table_id        = '21355',
    database_id     = 'OB_INFORMATION_SCHEMA_ID',
    table_type      = 'SYSTEM_VIEW',
    rowkey_columns  = [],
    normal_columns  = [],
    gm_columns      = [],
    in_tenant_space = True,
    view_definition = """SELECT CAST('def' AS CHAR(512)) AS TRIGGER_CATALOG,
      CAST(db.database_name AS CHAR(64)) collate utf8mb4_name_case AS TRIGGER_SCHEMA,
      CAST(trg.trigger_name AS CHAR(64)) AS TRIGGER_NAME,
      CAST((case when trg.trigger_events=1 then 'INSERT'
                when trg.trigger_events=2 then 'UPDATE'
                when trg.trigger_events=4 then 'DELETE' end)
            AS CHAR(6)) AS EVENT_MANIPULATION,
      CAST('def' AS CHAR(512)) AS EVENT_OBJECT_CATALOG,
      CAST(db.database_name AS CHAR(64)) collate utf8mb4_name_case AS EVENT_OBJECT_SCHEMA,
      CAST(t.table_name AS CHAR(64)) collate utf8mb4_name_case AS EVENT_OBJECT_TABLE,
      CAST(trg.action_order AS SIGNED) AS ACTION_ORDER,
      CAST(NULL AS CHAR(4194304)) AS ACTION_CONDITION,
      CAST(NVL(trg.trigger_body, trg.trigger_body_v2) AS CHAR(4194304)) AS ACTION_STATEMENT,
      CAST('ROW' AS CHAR(9)) AS ACTION_ORIENTATION,
      CAST((case when trg.TIMING_POINTS=4 then 'BEFORE'
                when trg.TIMING_POINTS=8 then 'AFTER' end)
            AS CHAR(6)) AS ACTION_TIMING,
      CAST(NULL AS CHAR(64)) AS ACTION_REFERENCE_OLD_TABLE,
      CAST(NULL AS CHAR(64)) AS ACTION_REFERENCE_NEW_TABLE,
      CAST('OLD' AS CHAR(3)) AS ACTION_REFERENCE_OLD_ROW,
      CAST('NEW' AS CHAR(3)) AS ACTION_REFERENCE_NEW_ROW,
      CAST(trg.gmt_create AS DATETIME(2)) AS CREATED,
      CAST(sql_mode_convert(trg.sql_mode) AS CHAR(8192)) AS SQL_MODE,
      CAST(trg.trigger_priv_user AS CHAR(93)) AS DEFINER,
      CAST((select charset from oceanbase.__all_virtual_collation
          where id = substring_index(substring_index(trg.package_exec_env, ',', 2), ',', -1)) AS CHAR(32)
            ) AS CHARACTER_SET_CLIENT,
      CAST((select collation from oceanbase.__all_virtual_collation
            where collation_type = substring_index(substring_index(trg.package_exec_env, ',', 3), ',', -1)) AS CHAR(32)
            ) AS COLLATION_CONNECTION,
      CAST((select collation from oceanbase.__all_virtual_collation
            where collation_type = substring_index(substring_index(trg.package_exec_env, ',', 4), ',', -1)) AS CHAR(32)
            ) AS DATABASE_COLLATION
      FROM oceanbase.__all_trigger trg
          JOIN oceanbase.__all_database db on trg.database_id = db.database_id
          JOIN oceanbase.__all_table t on trg.base_object_id = t.table_id
      WHERE db.database_name != '__recyclebin' and db.in_recyclebin = 0
      and t.table_mode >> 12 & 15 in (0,1)
      and can_access_trigger(db.database_name, t.table_name)
      and t.index_attributes_set & 16 = 0
""".replace("\n", " ")
  )

def_table_schema(
  owner = 'yibo.tyf',
  tablegroup_id = 'OB_INVALID_ID',
  database_id   = 'OB_INFORMATION_SCHEMA_ID',
  table_name    = 'PARTITIONS',
  table_id      = '21356',
  table_type    = 'SYSTEM_VIEW',
  gm_columns    = [],
  rowkey_columns = [],
  normal_columns  = [],
  in_tenant_space = True,
  view_definition = """SELECT
  CAST('def' as CHAR(4096)) AS TABLE_CATALOG,
  DB.DATABASE_NAME collate utf8mb4_name_case AS TABLE_SCHEMA,
  T.TABLE_NAME collate utf8mb4_name_case AS TABLE_NAME,
  P.PART_NAME AS PARTITION_NAME,
  SP.SUB_PART_NAME AS SUBPARTITION_NAME,
  CAST(PART_POSITION AS UNSIGNED) AS PARTITION_ORDINAL_POSITION,
  CAST(SUB_PART_POSITION AS UNSIGNED) AS SUBPARTITION_ORDINAL_POSITION,
  CAST(CASE WHEN T.PART_LEVEL = 0
            THEN NULL
            ELSE (CASE T.PART_FUNC_TYPE
                    WHEN 0 THEN 'HASH'
                    WHEN 1 THEN 'KEY'
                    WHEN 2 THEN 'KEY'
                    WHEN 3 THEN 'RANGE'
                    WHEN 4 THEN 'RANGE COLUMNS'
                    WHEN 5 THEN 'LIST'
                    WHEN 6 THEN 'LIST COLUMNS'
                    WHEN 7 THEN 'RANGE'
                  END)
       END AS CHAR(13)) PARTITION_METHOD,
  CAST(CASE WHEN (T.PART_LEVEL = 0 OR T.PART_LEVEL = 1)
            THEN NULL
            ELSE (CASE T.SUB_PART_FUNC_TYPE
                    WHEN 0 THEN 'HASH'
                    WHEN 1 THEN 'KEY'
                    WHEN 2 THEN 'KEY'
                    WHEN 3 THEN 'RANGE'
                    WHEN 4 THEN 'RANGE COLUMNS'
                    WHEN 5 THEN 'LIST'
                    WHEN 6 THEN 'LIST COLUMNS'
                    WHEN 7 THEN 'RANGE'
                  END)
       END AS CHAR(13)) SUBPARTITION_METHOD,
  CAST(CASE WHEN (T.PART_LEVEL = 0)
            THEN NULL
            ELSE T.PART_FUNC_EXPR
       END AS CHAR(2048)) PARTITION_EXPRESSION,
  CAST(CASE WHEN (T.PART_LEVEL = 0 OR T.PART_LEVEL = 1)
            THEN NULL
            ELSE T.SUB_PART_FUNC_EXPR
       END AS CHAR(2048)) SUBPARTITION_EXPRESSION,
  CAST(CASE WHEN (T.PART_LEVEL = 0)
            THEN NULL
            ELSE (CASE WHEN LENGTH(P.HIGH_BOUND_VAL) > 0
                       THEN P.HIGH_BOUND_VAL
                       ELSE P.LIST_VAL
                  END)
       END AS CHAR(4096)) AS PARTITION_DESCRIPTION,
  CAST(CASE WHEN (T.PART_LEVEL = 0 OR T.PART_LEVEL = 1)
            THEN NULL
            ELSE (CASE WHEN LENGTH(SP.HIGH_BOUND_VAL) > 0
                       THEN SP.HIGH_BOUND_VAL
                       ELSE SP.LIST_VAL
                  END)
       END AS CHAR(4096)) AS SUBPARTITION_DESCRIPTION,
  CAST(TS.ROW_CNT AS UNSIGNED) AS TABLE_ROWS,
  CAST(TS.AVG_ROW_LEN AS UNSIGNED) AS AVG_ROW_LENGTH,
  CAST(COALESCE(TS.MACRO_BLK_CNT * 2 * 1024 * 1024, 0) AS UNSIGNED) AS DATA_LENGTH,
  CAST(NULL AS UNSIGNED) AS MAX_DATA_LENGTH,
  CAST(COALESCE((
    SELECT
      SUM(G.MACRO_BLK_CNT * 2 * 1024 * 1024) AS INDEX_LENGTH
    FROM
      OCEANBASE.__ALL_TABLE E
      LEFT JOIN OCEANBASE.__ALL_PART F ON F.PART_NAME = P.PART_NAME
      AND E.TABLE_ID = F.TABLE_ID
      LEFT JOIN OCEANBASE.__ALL_SUB_PART SF ON SF.SUB_PART_NAME = SP.SUB_PART_NAME
      AND E.TABLE_ID = SF.TABLE_ID
      AND F.PART_ID = SF.PART_ID
      JOIN OCEANBASE.__ALL_TABLE_STAT G ON E.TABLE_ID = G.TABLE_ID
      AND G.PARTITION_ID = CASE E.PART_LEVEL
        WHEN 0 THEN E.TABLE_ID
        WHEN 1 THEN F.PART_ID
        WHEN 2 THEN SF.SUB_PART_ID
      END
    WHERE
      E.INDEX_TYPE in (1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12)
      AND E.TABLE_TYPE = 5
      AND E.DATA_TABLE_ID = T.TABLE_ID
  ), 0) AS UNSIGNED) AS INDEX_LENGTH,
  CAST(NULL AS UNSIGNED) AS DATA_FREE,
  CASE T.PART_LEVEL
    WHEN 0 THEN T.GMT_CREATE
    WHEN 1 THEN P.GMT_CREATE
    WHEN 2 THEN SP.GMT_CREATE
  END AS CREATE_TIME,
  CAST(NULL AS DATETIME) AS UPDATE_TIME,
  CAST(NULL AS DATETIME) AS CHECK_TIME,
  CAST(NULL AS SIGNED) AS CHECKSUM,
  CAST(CASE T.PART_LEVEL
         WHEN 0 THEN NULL
         WHEN 1 THEN P.COMMENT
         WHEN 2 THEN SP.COMMENT
       END AS CHAR(1024)) AS PARTITION_COMMENT,
  CAST('default' AS CHAR(256)) NODEGROUP,
  CAST(NULL AS CHAR(268)) AS TABLESPACE_NAME
FROM
  OCEANBASE.__ALL_TABLE T
  JOIN OCEANBASE.__ALL_DATABASE DB ON T.DATABASE_ID = DB.DATABASE_ID
    AND T.TABLE_MODE >> 12 & 15 in (0,1)
    AND T.INDEX_ATTRIBUTES_SET & 16 = 0
  LEFT JOIN (
      SELECT
        TABLE_ID,
        PART_ID,
        PART_NAME,
        HIGH_BOUND_VAL,
        LIST_VAL,
        TABLESPACE_ID,
        GMT_CREATE,
        COMMENT,
        PARTITION_TYPE,
        ROW_NUMBER() OVER(PARTITION BY TABLE_ID ORDER BY PART_IDX) AS PART_POSITION
      FROM OCEANBASE.__ALL_PART
  ) P ON T.TABLE_ID = P.TABLE_ID
  LEFT JOIN (
    SELECT
        TABLE_ID,
        PART_ID,
        SUB_PART_ID,
        SUB_PART_NAME,
        HIGH_BOUND_VAL,
        LIST_VAL,
        TABLESPACE_ID,
        GMT_CREATE,
        COMMENT,
        PARTITION_TYPE,
        ROW_NUMBER() OVER(PARTITION BY TABLE_ID,PART_ID ORDER BY SUB_PART_IDX) AS SUB_PART_POSITION
    FROM OCEANBASE.__ALL_SUB_PART
  ) SP ON T.TABLE_ID = SP.TABLE_ID AND P.PART_ID = SP.PART_ID
  LEFT JOIN OCEANBASE.__ALL_TABLE_STAT TS ON TS.TABLE_ID = T.TABLE_ID AND TS.PARTITION_ID = CASE T.PART_LEVEL WHEN 0 THEN T.TABLE_ID WHEN 1 THEN P.PART_ID WHEN 2 THEN SP.SUB_PART_ID END
WHERE T.TABLE_TYPE IN (3,6,8,9,14)
      AND (P.PARTITION_TYPE = 0 OR P.PARTITION_TYPE is NULL)
      AND (SP.PARTITION_TYPE = 0 OR SP.PARTITION_TYPE is NULL)
      AND (0 = sys_privilege_check('table_acc', 1)
           OR 0 = sys_privilege_check('table_acc', 1, DB.DATABASE_NAME, T.TABLE_NAME))
  """.replace("\n", " ")
  )

# 21357: DBA_OB_ARBITRATION_SERVICE (abandoned)
# 21358: CDB_OB_LS_ARB_REPLICA_TASKS (abandoned)
# 21359: DBA_OB_LS_ARB_REPLICA_TASKS (abandoned)
# 21360: CDB_OB_LS_ARB_REPLICA_TASK_HISTORY (abandoned)
# 21361: DBA_OB_LS_ARB_REPLICA_TASK_HISTORY (abandoned)

# 21362: V$OB_ARCHIVE_DEST_STATUS (removed: backup/restore/log-archive deleted)
# 21363: DBA_OB_LS_LOG_ARCHIVE_PROGRESS # abandoned
# 21364: CDB_OB_LS_LOG_ARCHIVE_PROGRESS # abandoned
# 21365: DBA_OB_LS_LOG_RESTORE_STAT
# 21366: CDB_OB_LS_LOG_RESTORE_STAT

# 21367: GV$OB_KV_HOTKEY_STAT
# 21368: V$OB_KV_HOTKEY_STAT

# 21369: removed (legacy resource manager deleted)

# 21370: GV$OB_TABLET_STATS # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_tablet_stat)

# 21371: V$OB_TABLET_STATS # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_tablet_stat)

# 21372: DBA_OB_ACCESS_POINT (abandoned)
# 21373: CDB_OB_ACCESS_POINT (abandoned)

# 21375: DBA_OB_DATA_DICTIONARY_IN_LOG (removed with CDC data dictionary)

# 21376: GV$OB_OPT_STAT_GATHER_MONITOR # removed (single-tenant GV/V collapse; folded into V$OB_OPT_STAT_GATHER_MONITOR)

def_table_schema(
    owner = 'jiangxiu.wt',
    table_name     = 'V$OB_OPT_STAT_GATHER_MONITOR',
    table_id       = '21377',
    table_type = 'SYSTEM_VIEW',
    rowkey_columns  = [],
    normal_columns  = [],
    gm_columns      = [],
    in_tenant_space = True,
    view_definition = """SELECT
          CAST(SESSION_ID AS SIGNED) AS SESSION_ID,
          CAST(TRACE_ID AS CHAR(64)) AS TRACE_ID,
          CAST(TASK_ID AS CHAR(36)) AS TASK_ID,
          CAST((CASE WHEN TYPE = 0 THEN 'MANUAL GATHER' ELSE
                (CASE WHEN TYPE = 1 THEN 'AUTO GATHER' ELSE
                  (CASE WHEN TYPE = 2 THEN 'ASYNC GATHER' ELSE 'UNDEFINED GATHER' END) END) END) AS CHAR(16)) AS TYPE,
          CAST(TASK_START_TIME AS DATETIME(6)) AS TASK_START_TIME,
          CAST(TASK_DURATION_TIME AS SIGNED) AS TASK_DURATION_TIME,
          CAST(TASK_TABLE_COUNT AS SIGNED) AS TASK_TABLE_COUNT,
          CAST(COMPLETED_TABLE_COUNT AS SIGNED) AS COMPLETED_TABLE_COUNT,
          CAST(RUNNING_TABLE_OWNER AS CHAR(128)) AS RUNNING_TABLE_OWNER,
          CAST(RUNNING_TABLE_NAME AS CHAR(256)) AS RUNNING_TABLE_NAME,
          CAST(RUNNING_TABLE_DURATION_TIME AS SIGNED) AS RUNNING_TABLE_DURATION_TIME,
          CAST(SPARE2 AS CHAR(256)) AS RUNNING_TABLE_PROGRESS
          FROM oceanbase.__all_virtual_opt_stat_gather_monitor
""".replace("\n", " ")
)



# 21380: GV$OB_THREAD # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_thread)

# 21381: V$OB_THREAD # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_thread)

# 21382: GV$OB_ARBITRATION_MEMBER_INFO (abandoned)
# 21383: V$OB_ARBITRATION_MEMBER_INFO (abandoned)
# 21384: DBA_OB_ZONE_STORAGE (abandoned)

# 21385: GV$OB_SERVER_STORAGE # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_server_storage;)

# 21386: V$OB_SERVER_STORAGE # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_server_storage;)

# 21387: GV$OB_ARBITRATION_SERVICE_STATUS (abandoned)
# 21388: V$OB_ARBITRATION_SERVICE_STATUS (abandoned)

# 21389: DBA_WR_ACTIVE_SESSION_HISTORY
# 21389: DBA_WR_ACTIVE_SESSION_HISTORY # removed
# 21390: CDB_WR_ACTIVE_SESSION_HISTORY
# 21390: CDB_WR_ACTIVE_SESSION_HISTORY # removed
# 21391: DBA_WR_SNAPSHOT
# 21391: DBA_WR_SNAPSHOT # removed
# 21392: CDB_WR_SNAPSHOT
# 21392: CDB_WR_SNAPSHOT # removed
# 21393: DBA_WR_STATNAME
# 21393: DBA_WR_STATNAME # removed
# 21394: CDB_WR_STATNAME
# 21394: CDB_WR_STATNAME # removed

# 21395: DBA_WR_SYSSTAT
# 21395: DBA_WR_SYSSTAT # removed
# 21396: CDB_WR_SYSSTAT
# 21396: CDB_WR_SYSSTAT # removed
# 21397: GV$OB_KV_CONNECTIONS abandoned
# 21398: V$OB_KV_CONNECTIONS abandoned


# 21399: GV$OB_LOCKS # removed (single-tenant GV/V collapse; folded into V$OB_LOCKS)
def_table_schema(
  owner           = 'yangyifei.yyf',
  table_name      = 'V$OB_LOCKS',
  table_id        = '21400',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT
    TRANS_ID AS TRANS_ID,
    SESSION_ID AS SESSION_ID,
    CASE WHEN TYPE = 1 THEN 'TR'
         WHEN TYPE = 2 THEN 'TX'
         WHEN TYPE = 3 THEN 'TM'
         ELSE 'UNDEFINED' END
    AS TYPE,
    HOLDER_TRANS_ID AS ID1,
    HOLDER_SESSION_ID AS ID2,
    CASE WHEN TYPE = 1 THEN CONCAT(CONCAT(TABLET_ID, '-'), ROWKEY)
         WHEN TYPE = 2 OR TYPE = 3 THEN NULL
         ELSE 'ERROR' END
    AS ID3,
    'NONE' AS LMODE,
    LOCK_MODE AS REQUEST,
    TIME_AFTER_RECV AS CTIME,
    1 AS BLOCK
    FROM
    oceanbase.__ALL_VIRTUAL_LOCK_WAIT_STAT

    UNION ALL

    SELECT
    TRANS_ID AS TRANS_ID,
    SESSION_ID AS SESSION_ID,
    'TR' AS TYPE,
    TRANS_ID AS ID1,
    SESSION_ID AS ID2,
    CONCAT(CONCAT(TABLET_ID, '-'), ROWKEY) AS ID3,
    'X' AS LMODE,
    'NONE' AS REQUEST,
    TIME_AFTER_RECV AS CTIME,
    0 AS BLOCK
    FROM
    oceanbase.__ALL_VIRTUAL_TRANS_LOCK_STAT
    WHERE ROWKEY IS NOT NULL AND ROWKEY <> ''

    UNION ALL

    SELECT
    TRANS_ID AS TRANS_ID,
    SESSION_ID AS SESSION_ID,
    'TX' AS TYPE,
    TRANS_ID AS ID1,
    SESSION_ID AS ID2,
    NULL AS ID3,
    'X' AS LMODE,
    'NONE' AS REQUEST,
    MIN(TIME_AFTER_RECV) AS CTIME,
    0 AS BLOCK
    FROM
    oceanbase.__ALL_VIRTUAL_TRANS_LOCK_STAT
    GROUP BY TRANS_ID, SESSION_ID

    UNION ALL

    SELECT
    OBJ_LOCK.CREATE_TRANS_ID AS TRANS_ID,
    TRX_WRITE_PART.SESSION_ID AS SESSION_ID,
    CASE WHEN OBJ_LOCK.OBJ_TYPE IN ('TABLE', 'TABLET') THEN 'TM'
         WHEN OBJ_LOCK.OBJ_TYPE = 'DBMS_LOCK' THEN 'UL'
         ELSE 'UNKONWN' END
    AS TYPE,
    OBJ_LOCK.CREATE_TRANS_ID AS ID1,
    TRX_WRITE_PART.SESSION_ID AS ID2,
    OBJ_LOCK.OBJ_ID AS ID3,
    OBJ_LOCK.LOCK_MODE AS LMODE,
    'NONE' AS REQUEST,
    OBJ_LOCK.TIME_AFTER_CREATE AS CTIME,
    0 AS BLOCK
    FROM
    oceanbase.__ALL_VIRTUAL_OBJ_LOCK AS OBJ_LOCK
    LEFT JOIN
    oceanbase.V$OB_TRANSACTION_WRITE_STATE TRX_WRITE_PART
    ON
    TRX_WRITE_PART.TX_ID = OBJ_LOCK.CREATE_TRANS_ID
    WHERE
    OBJ_LOCK.OBJ_TYPE IN ('TABLE', 'TABLET', 'DBMS_LOCK') AND
    OBJ_LOCK.EXTRA_INFO LIKE '%tx_ctx%'
""".replace("\n", " ")
)
# 21401: CDB_OB_LOG_RESTORE_SOURCE # abandoned
# 21402: DBA_OB_LOG_RESTORE_SOURCE # abandoned

# 21403: DBA_OB_EXTERNAL_TABLE_FILE

def_table_schema(
  owner           = 'gjw228474',
  table_name      = 'V$OB_TIMESTAMP_SERVICE',
  table_id        = '21404',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT
      TS_TYPE,
      TS_VALUE
    FROM
      oceanbase.__all_virtual_timestamp_service as a
""".replace("\n", " ")
)


# 21417: DBA_OB_EXTERNAL_TABLE_FILES # abandoned in seekdb

# 21418: ALL_OB_EXTERNAL_TABLE_FILES # abandoned in seekdb

# 21419: GV$OB_PX_P2P_DATAHUB # removed (single-tenant GV/V collapse; folded into V$OB_PX_P2P_DATAHUB)

def_table_schema(
    owner = 'mingdou.tmd',
    table_name     = 'V$OB_PX_P2P_DATAHUB',
    table_id       = '21420',
    table_type = 'SYSTEM_VIEW',
    gm_columns = [],
    in_tenant_space = True,
    rowkey_columns = [],
    view_definition = """
        SELECT
          CAST(TRACE_ID AS CHAR(64)) AS TRACE_ID,
          CAST(DATAHUB_ID AS SIGNED) AS DATAHUB_ID,
          CAST(MESSAGE_TYPE AS CHAR(256)) AS MESSAGE_TYPE,
          CAST(HOLD_SIZE AS SIGNED) as HOLD_SIZE,
          CAST(TIMEOUT_TS AS DATETIME) as TIMEOUT_TS,
          CAST(START_TIME AS DATETIME) as START_TIME
        FROM oceanbase.__all_virtual_px_p2p_datahub

""".replace("\n", " "),

    normal_columns = [
    ]
  )

def_table_schema(
    owner = 'yibo.tyf',
    table_name     = 'DBA_OB_TABLE_STAT_STALE_INFO',
    table_id       = '21423',
    table_type = 'SYSTEM_VIEW',
    rowkey_columns  = [],
    normal_columns  = [],
    gm_columns      = [],
    in_tenant_space = True,
    view_definition = """
WITH V AS
(SELECT
  NVL(T.TABLE_ID, VT.TABLE_ID) AS TABLE_ID,
  NVL(T.TABLET_ID, VT.TABLET_ID) AS TABLET_ID,
  NVL(T.INSERTS, 0) + NVL(VT.INSERT_ROW_COUNT, 0) - NVL(T.LAST_INSERTS, 0) AS INSERTS,
  NVL(T.UPDATES, 0) + NVL(VT.UPDATE_ROW_COUNT, 0) - NVL(T.LAST_UPDATES, 0) AS UPDATES,
  NVL(T.DELETES, 0) + NVL(VT.DELETE_ROW_COUNT, 0) - NVL(T.LAST_DELETES, 0) AS DELETES
  FROM
  OCEANBASE.__ALL_MONITOR_MODIFIED T
  FULL JOIN
  OCEANBASE.__ALL_VIRTUAL_DML_STATS VT
  ON T.TABLE_ID = VT.TABLE_ID
  AND T.TABLET_ID = VT.TABLET_ID
)
SELECT
  CAST(TM.DATABASE_NAME AS CHAR(128)) AS DATABASE_NAME,
  CAST(TM.TABLE_NAME AS CHAR(128)) AS TABLE_NAME,
  CAST(TM.PART_NAME AS CHAR(128)) AS PARTITION_NAME,
  CAST(TM.SUB_PART_NAME AS CHAR(128)) AS SUBPARTITION_NAME,
  CAST(TS.ROW_CNT AS SIGNED) AS LAST_ANALYZED_ROWS,
  TS.LAST_ANALYZED AS LAST_ANALYZED_TIME,
  CAST(TM.INSERTS AS SIGNED) AS INSERTS,
  CAST(TM.UPDATES AS SIGNED) AS UPDATES,
  CAST(TM.DELETES AS SIGNED) AS DELETES,
  CAST(NVL(CAST(UP.VALCHAR AS SIGNED), CAST(GP.SPARE4 AS SIGNED)) AS SIGNED) STALE_PERCENT,
  CAST(CASE NVL((TM.INSERTS + TM.UPDATES + TM.DELETES) > TS.ROW_CNT * NVL(CAST(UP.VALCHAR AS SIGNED), CAST(GP.SPARE4 AS SIGNED)) / 100,
                (TM.INSERTS + TM.UPDATES + TM.DELETES) > 0)
        WHEN 0 THEN 'NO'
        WHEN 1 THEN 'YES'
       END AS CHAR(3)) AS IS_STALE
FROM
(SELECT
  T.TABLE_ID,
  CASE T.PART_LEVEL WHEN 0 THEN T.TABLE_ID WHEN 1 THEN P.PART_ID WHEN 2 THEN SP.SUB_PART_ID END AS PARTITION_ID,
  DB.DATABASE_NAME,
  T.TABLE_NAME,
  P.PART_NAME,
  SP.SUB_PART_NAME,
  NVL(V.INSERTS, 0) AS INSERTS,
  NVL(V.UPDATES, 0) AS UPDATES,
  NVL(V.DELETES, 0) AS DELETES
FROM OCEANBASE.__ALL_TABLE T
JOIN OCEANBASE.__ALL_DATABASE DB
  ON DB.DATABASE_ID = T.DATABASE_ID
LEFT JOIN OCEANBASE.__ALL_PART P
  ON T.TABLE_ID = P.TABLE_ID
LEFT JOIN OCEANBASE.__ALL_SUB_PART SP
  ON T.TABLE_ID = SP.TABLE_ID AND P.PART_ID = SP.PART_ID
LEFT JOIN V
ON T.TABLE_ID = V.TABLE_ID
AND V.TABLET_ID = CASE T.PART_LEVEL WHEN 0 THEN T.TABLET_ID WHEN 1 THEN P.TABLET_ID WHEN 2 THEN SP.TABLET_ID END
WHERE T.TABLE_TYPE IN (0, 3, 6) AND T.TABLE_MODE >> 12 & 15 in (0,1) AND T.INDEX_ATTRIBUTES_SET & 16 = 0
UNION ALL
SELECT
  MIN(T.TABLE_ID),
  -1 AS PARTITION_ID,
  DB.DATABASE_NAME,
  T.TABLE_NAME,
  NULL AS PART_NAME,
  NULL AS SUB_PART_NAME,
  SUM(NVL(V.INSERTS, 0)) AS INSERTS,
  SUM(NVL(V.UPDATES, 0)) AS UPDATES,
  SUM(NVL(V.DELETES, 0)) AS DELETES
FROM OCEANBASE.__ALL_TABLE T
JOIN OCEANBASE.__ALL_DATABASE DB
  ON DB.DATABASE_ID = T.DATABASE_ID
JOIN OCEANBASE.__ALL_PART P
  ON T.TABLE_ID = P.TABLE_ID
LEFT JOIN V
ON T.TABLE_ID = V.TABLE_ID AND V.TABLET_ID = P.TABLET_ID
WHERE T.TABLE_TYPE IN (0, 3, 6) AND T.PART_LEVEL = 1 AND T.TABLE_MODE >> 12 & 15 in (0,1) AND T.INDEX_ATTRIBUTES_SET & 16 = 0
GROUP BY DB.DATABASE_NAME,
         T.TABLE_NAME
UNION ALL
SELECT
  MIN(T.TABLE_ID),
  MIN(P.PART_ID) AS PARTITION_ID,
  DB.DATABASE_NAME,
  T.TABLE_NAME,
  P.PART_NAME,
  NULL AS SUB_PART_NAME,
  SUM(NVL(V.INSERTS, 0)) AS INSERTS,
  SUM(NVL(V.UPDATES, 0)) AS UPDATES,
  SUM(NVL(V.DELETES, 0)) AS DELETES
FROM OCEANBASE.__ALL_TABLE T
JOIN OCEANBASE.__ALL_DATABASE DB
  ON DB.DATABASE_ID = T.DATABASE_ID
JOIN OCEANBASE.__ALL_PART P
  ON T.TABLE_ID = P.TABLE_ID
JOIN OCEANBASE.__ALL_SUB_PART SP
  ON T.TABLE_ID = SP.TABLE_ID AND P.PART_ID = SP.PART_ID
LEFT JOIN V
ON T.TABLE_ID = V.TABLE_ID AND V.TABLET_ID = SP.TABLET_ID
WHERE T.TABLE_TYPE IN (0, 3, 6) AND T.PART_LEVEL = 2 AND T.TABLE_MODE >> 12 & 15 in (0,1) AND T.INDEX_ATTRIBUTES_SET & 16 = 0
GROUP BY DB.DATABASE_NAME,
        T.TABLE_NAME,
        P.PART_NAME
UNION ALL
SELECT
  MIN(T.TABLE_ID),
  -1 AS PARTITION_ID,
  DB.DATABASE_NAME,
  T.TABLE_NAME,
  NULL AS PART_NAME,
  NULL AS SUB_PART_NAME,
  SUM(NVL(V.INSERTS, 0)) AS INSERTS,
  SUM(NVL(V.UPDATES, 0)) AS UPDATES,
  SUM(NVL(V.DELETES, 0)) AS DELETES
FROM OCEANBASE.__ALL_TABLE T
JOIN OCEANBASE.__ALL_DATABASE DB
  ON DB.DATABASE_ID = T.DATABASE_ID
JOIN OCEANBASE.__ALL_PART P
  ON T.TABLE_ID = P.TABLE_ID
JOIN OCEANBASE.__ALL_SUB_PART SP
  ON T.TABLE_ID = SP.TABLE_ID AND P.PART_ID = SP.PART_ID
LEFT JOIN V
ON T.TABLE_ID = V.TABLE_ID AND V.TABLET_ID = SP.TABLET_ID
WHERE T.TABLE_TYPE IN (0, 3, 6) AND T.PART_LEVEL = 2 AND T.TABLE_MODE >> 12 & 15 in (0,1) AND T.INDEX_ATTRIBUTES_SET & 16 = 0
GROUP BY DB.DATABASE_NAME,
        T.TABLE_NAME
) TM
LEFT JOIN OCEANBASE.__ALL_TABLE_STAT TS
  ON TM.TABLE_ID = TS.TABLE_ID AND TM.PARTITION_ID = TS.PARTITION_ID
LEFT JOIN OCEANBASE.__ALL_OPTSTAT_USER_PREFS UP
  ON TM.TABLE_ID = UP.TABLE_ID AND UP.PNAME = 'STALE_PERCENT'
JOIN OCEANBASE.__ALL_OPTSTAT_GLOBAL_PREFS GP
  ON GP.SNAME = 'STALE_PERCENT'
""".replace("\n", " ")
)

# 21425: CDB_OB_EXTERNAL_TABLE_FILES # abandoned in seekdb

# 21426: DBA_DB_LINKS # abandoned in seekdb

# 21443: DBA_WR_CONTROL # removed
# 21444: CDB_WR_CONTROL
# 21444: CDB_WR_CONTROL # removed

# 21445: DBA_OB_LS_HISTORY (abandoned)
# 21446: CDB_OB_LS_HISTORY (abandoned)

# 21448: CDB_OB_TENANT_EVENT_HISTORY (abandoned)
# 21459: GV$OB_SESSION # removed (single-tenant GV/V collapse; folded into V$OB_SESSION)
def_table_schema(
  owner = 'jingfeng.jf',
  table_name      = 'V$OB_SESSION',
  table_id        = '21460',
  gm_columns      = [],
  rowkey_columns  = [],
  table_type      = 'SYSTEM_VIEW',
  in_tenant_space = True,
  view_definition = """select
                         id as ID,
                         user as USER,
                         tenant as TENANT,
                         host as HOST,
                         db as DB,
                         command as COMMAND,
                         sql_id as SQL_ID,
                         cast(time as SIGNED) as TIME,
                         state as STATE,
                         info as INFO,
                         user_client_ip as USER_CLIENT_IP,
                         user_host as USER_HOST,
                         trans_id as TRANS_ID,
                         thread_id as THREAD_ID,
                         trace_id as TRACE_ID,
                         ref_count as REF_COUNT,
                         backtrace as BACKTRACE,
                         trans_state as TRANS_STATE,
                         user_client_port as USER_CLIENT_PORT,
                         cast(total_cpu_time as SIGNED) as TOTAL_CPU_TIME
                     from oceanbase.__all_virtual_session_info
""".replace("\n", " "),
  normal_columns  = []
  )

# 21459:GV$OB_SESSION
# 21460:V$OB_SESSION
# 21461: GV$OB_PL_CACHE_OBJECT
# 21462: V$OB_PL_CACHE_OBJECT

# 21461: GV$OB_PL_CACHE_OBJECT # removed (single-tenant GV/V collapse; folded into V$OB_PL_CACHE_OBJECT)

def_table_schema(
    owner = 'hr351303',
    table_name     = 'V$OB_PL_CACHE_OBJECT',
    table_id       = '21462',
    table_type = 'SYSTEM_VIEW',
    gm_columns = [],
    in_tenant_space = True,
    rowkey_columns = [],
    view_definition = """
    SELECT
           PLAN_ID AS CACHE_OBJECT_ID,
           STATEMENT AS PARAMETERIZE_TEXT,
           QUERY_SQL AS OBJECT_TEXT,
           FIRST_LOAD_TIME,
           LAST_ACTIVE_TIME,
           AVG_EXE_USEC,
           SLOWEST_EXE_TIME,
           SLOWEST_EXE_USEC,
           HIT_COUNT,
           PLAN_SIZE AS CACHE_OBJ_SIZE,
           EXECUTIONS,
           ELAPSED_TIME,
           OBJECT_TYPE,
           PL_SCHEMA_ID AS OBJECT_ID,
           COMPILE_TIME,
           SCHEMA_VERSION,
           PL_EVICT_VERSION,
           PS_STMT_ID,
           DB_ID,
           PL_CG_MEM_HOLD,
           SYS_VARS,
           PARAM_INFOS,
           SQL_ID,
           OUTLINE_VERSION,
           OUTLINE_ID,
           OUTLINE_DATA AS CONCURRENT_DATA
    FROM oceanbase.__all_virtual_plan_stat WHERE OBJECT_STATUS = 0 AND TYPE > 5 AND TYPE < 11 AND is_in_pc=true
""".replace("\n", " "),


    normal_columns = [
    ]
  )
# 21463: CDB_OB_RECOVER_TABLE_JOBS # abandoned
# 21464: DBA_OB_RECOVER_TABLE_JOBS # abandoned
# 21465: CDB_OB_RECOVER_TABLE_JOB_HISTORY # abandoned
# 21466: DBA_OB_RECOVER_TABLE_JOB_HISTORY # abandoned
# 21467: CDB_OB_IMPORT_TABLE_JOBS # abandoned
# 21468: DBA_OB_IMPORT_TABLE_JOBS # abandoned
# 21469: CDB_OB_IMPORT_TABLE_JOB_HISTORY # abandoned
# 21470: DBA_OB_IMPORT_TABLE_JOB_HISTORY # abandoned
# 21471: CDB_OB_IMPORT_TABLE_TASKS # abandoned
# 21472: DBA_OB_IMPORT_TABLE_TASKS # abandoned
# 21473: CDB_OB_IMPORT_TABLE_TASK_HISTORY # abandoned
# 21474: DBA_OB_IMPORT_TABLE_TASK_HISTORY # abandoned

# 21475: CDB_OB_IMPORT_STMT_EXEC_HISTORY
# 21476: DBA_OB_IMPORT_STMT_EXEC_HISTORY

# 21477: GV$OB_TENANT_RUNTIME_INFO # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_dump_info)

# 21478: V$OB_TENANT_RUNTIME_INFO # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_dump_info)

# 21479: removed (legacy resource isolation deleted)

# 21480: removed (legacy resource isolation deleted)

# 21481: DBA_WR_SYSTEM_EVENT # removed
# 21482: CDB_WR_SYSTEM_EVENT # removed
# 21483: DBA_WR_EVENT_NAME # removed
# 21484: CDB_WR_EVENT_NAME # removed
def_table_schema(
    owner = 'guoyun.lgy',
    table_name     = 'DBA_OB_FORMAT_OUTLINES',
    table_id       = '21485',
    table_type = 'SYSTEM_VIEW',
    gm_columns = [],
    in_tenant_space = True,
    rowkey_columns = [],
    view_definition = """
    SELECT
      B.GMT_CREATE AS CREATE_TIME,
      B.GMT_MODIFIED AS MODIFY_TIME,
      A.DATABASE_ID,
      A.OUTLINE_ID,
      A.DATABASE_NAME,
      A.OUTLINE_NAME,
      A.VISIBLE_SIGNATURE,
      A.FORMAT_SQL_TEXT,
      A.OUTLINE_TARGET,
      A.OUTLINE_SQL,
      A.FORMAT_SQL_ID,
      A.OUTLINE_CONTENT
    FROM oceanbase.__all_virtual_outline A, oceanbase.__all_outline B
    WHERE A.OUTLINE_ID = B.OUTLINE_ID AND B.FORMAT_OUTLINE != 0
""".replace("\n", " "),
    normal_columns = [
   ]
  )

def_table_schema(
  owner = 'mingye.swj',
  database_id    = 'OB_MYSQL_SCHEMA_ID',
  table_name      = 'procs_priv',
  table_id        = '21486',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT cast(b.host as char(60)) as Host,
           cast(a.database_name as char(64)) as Db,
           cast(b.user_name as char(32)) as User,
           cast(a.routine_name as char(64)) as Routine_name,
           case when a.routine_type = 1 then 'PROCEDURE' else 'FUNCTION' end as Routine_type,
           cast(concat(a.grantor, '@', a.grantor_host) as char(93)) as Grantor,
           substr(concat(case when (a.all_priv & 1) > 0 then ',Execute' else '' end,
                          case when (a.all_priv & 2) > 0 then ',Alter Routine' else '' end,
                          case when (a.all_priv & 4) > 0 then ',Grant' else '' end), 2) as Proc_priv,
           cast(a.gmt_modified as date) as Timestamp
    FROM oceanbase.__all_routine_privilege a, oceanbase.__all_user b
    WHERE a.user_id = b.user_id;
""".replace("\n", " ")
)

# 21487: GV$OB_SQLSTAT # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_sqlstat)
# 21488: V$OB_SQLSTAT # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_sqlstat)
# 21489: DBA_WR_SQLSTAT # removed
# 21490: CDB_WR_SQLSTAT # removed
# 21491: GV$OB_SESS_TIME_MODEL # removed
# 21492: V$OB_SESS_TIME_MODEL # removed
# 21493: GV$OB_SYS_TIME_MODEL # removed
# 21494: V$OB_SYS_TIME_MODEL # removed
# 21495: DBA_WR_SYS_TIME_MODEL # removed

# 21496: CDB_WR_SYS_TIME_MODEL # removed

def_table_schema(
    owner = 'zhenling.zzg',
    table_name     = 'DBA_OB_AUX_STATISTICS',
    table_id       = '21497',
    table_type = 'SYSTEM_VIEW',
    rowkey_columns  = [],
    normal_columns  = [],
    gm_columns      = [],
    in_tenant_space = True,
    view_definition = """
  	select
      LAST_ANALYZED,
      CPU_SPEED AS `CPU_SPEED(MHZ)`,
      DISK_SEQ_READ_SPEED AS `DISK_SEQ_READ_SPEED(MB/S)`,
      DISK_RND_READ_SPEED AS `DISK_RND_READ_SPEED(MB/S)`,
      NETWORK_SPEED AS `NETWORK_SPEED(MB/S)`
    from oceanbase.__all_aux_stat;
""".replace("\n", " ")
)

def_table_schema(
  owner           = 'dingjincheng.djc',
  table_name      = 'DBA_OB_SYS_VARIABLES',
  table_id        = '21500',
  table_type      = 'SYSTEM_VIEW',
  gm_columns      = [],
  rowkey_columns  = [],
  normal_columns  = [],
  in_tenant_space = True,
  view_definition =
  """
    SELECT
    a.GMT_CREATE AS CREATE_TIME,
    a.GMT_MODIFIED AS MODIFY_TIME,
    a.NAME as NAME,
    a.VALUE as VALUE,
    a.MIN_VAL as MIN_VALUE,
    a.MAX_VAL as MAX_VALUE,
    CASE a.FLAGS & 0x3
        WHEN 1 THEN "GLOBAL_ONLY"
        WHEN 2 THEN "SESSION_ONLY"
        WHEN 3 THEN "GLOBAL | SESSION"
        ELSE NULL
    END as SCOPE,
    a.INFO as INFO,
    b.DEFAULT_VALUE as DEFAULT_VALUE,
    CAST (CASE WHEN a.VALUE = b.DEFAULT_VALUE
          THEN 'YES'
          ELSE 'NO'
          END AS CHAR(3)) AS ISDEFAULT
  FROM oceanbase.__all_sys_variable a
  join oceanbase.__all_virtual_sys_variable_default_value b
  where a.name = b.variable_name;
  """.replace("\n", " ")
  )


# 21505: DBA_WR_SQLTEXT # removed
# 21506: CDB_WR_SQLTEXT # removed

# 21507: GV$OB_ACTIVE_SESSION_HISTORY # removed


# 21508: V$OB_ACTIVE_SESSION_HISTORY # removed

# 21509: DBA_OB_TRUSTED_ROOT_CERTIFICATE (abandoned)

#### sys tenant only view
# 21510: DBA_OB_CLONE_PROGRESS (abandoned)

def_table_schema(
  owner = 'jim.wjh',
  database_id    = 'OB_MYSQL_SCHEMA_ID',
  table_name      = 'role_edges',
  table_id        = '21511',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT cast(from_user.host AS char(255)) FROM_HOST,
         cast(from_user.user_name AS char(128)) FROM_USER,
         cast(to_user.host AS char(255)) TO_HOST,
         cast(to_user.user_name AS char(128)) TO_USER,
         cast(CASE role_map.admin_option WHEN 1 THEN 'Y' ELSE 'N' END AS char(1)) WITH_ADMIN_OPTION
  FROM oceanbase.__all_role_grantee_map role_map,
       oceanbase.__all_user from_user,
       oceanbase.__all_user to_user
  WHERE role_map.grantee_id = to_user.user_id
    AND role_map.role_id = from_user.user_id;
""".replace("\n", " ")
)

def_table_schema(
  owner = 'jim.wjh',
  database_id    = 'OB_MYSQL_SCHEMA_ID',
  table_name      = 'default_roles',
  table_id        = '21512',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT cast(to_user.host AS char(255)) HOST,
         cast(to_user.user_name AS char(128)) USER,
         cast(from_user.host AS char(255)) DEFAULT_ROLE_HOST,
         cast(from_user.user_name AS char(128)) DEFAULT_ROLE_USER
  FROM oceanbase.__all_role_grantee_map role_map,
       oceanbase.__all_user from_user,
       oceanbase.__all_user to_user
  WHERE role_map.grantee_id = to_user.user_id
    AND role_map.role_id = from_user.user_id
    AND role_map.disable_flag = 0;
""".replace("\n", " ")
)


def_table_schema(
  owner = 'mingye.swj',
  database_id    = 'OB_MYSQL_SCHEMA_ID',
  table_name      = 'columns_priv',
  table_id        = '21516',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT cast(b.host as char(255)) as Host,
           cast(a.database_name as char(128)) as Db,
           cast(b.user_name as char(128)) as User,
           cast(a.table_name as char(128)) as Table_name,
           cast(a.column_name as char(128)) as Column_name,
           substr(concat(case when (a.all_priv & 1) > 0 then ',Select' else '' end,
                          case when (a.all_priv & 2) > 0 then ',Insert' else '' end,
                          case when (a.all_priv & 4) > 0 then ',Update' else '' end,
                          case when (a.all_priv & 8) > 0 then ',References' else '' end), 2) as Column_priv,
           cast(a.gmt_modified as datetime) as Timestamp
    FROM oceanbase.__all_column_privilege a, oceanbase.__all_user b
    WHERE a.user_id = b.user_id
""".replace("\n", " ")
)

# 21517:GV$OB_LS_SNAPSHOTS (abandoned)
# 21518:V$OB_LS_SNAPSHOTS (abandoned)

#### sys tenant only view
# 21519: DBA_OB_CLONE_HISTORY (abandoned)
# 21520: GV$OB_SHARED_STORAGE_QUOTA (abandoned)
# 21521: V$OB_SHARED_STORAGE_QUOTA (abandoned)
# 21523: DBA_OB_LS_REPLICA_TASK_HISTORY (abandoned)
# 21524: CDB_OB_LS_REPLICA_TASK_HISTORY (abandoned)

# 21522: CDB_UNUSED_COL_TABS
# 21541: GV$OB_SESSION_PS_INFO # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_session_ps_info)

# 21542: V$OB_SESSION_PS_INFO # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_session_ps_info)

# 21543: GV$OB_TRACEPOINT_INFO # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_tracepoint_info)

# 21544: V$OB_TRACEPOINT_INFO # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_tracepoint_info)
# 21545: V$OB_COMPATIBILITY_CONTROL (removed)

# 21546: removed (legacy resource manager deleted)

# 21548: DBA_OB_SERVICES (abandoned)
# 21549: CDB_OB_SERVICES (abandoned)

# 21550: GV$OB_TENANT_RESOURCE_LIMIT # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_resource_limit)

# 21551: V$OB_TENANT_RESOURCE_LIMIT # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_resource_limit)

# 21552: GV$OB_TENANT_RESOURCE_LIMIT_DETAIL # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_resource_limit_detail)

# 21553: V$OB_TENANT_RESOURCE_LIMIT_DETAIL # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_resource_limit_detail)

def_table_schema(
  owner           = 'yangyifei.yyf',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_LOCK_WAITS',
  table_id        = '21554',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT '0' as REQUESTING_TRX_ID,
           '0' as REQUESTED_LOCK_ID,
           '0' as BLOCKING_TRX_ID,
           '0' as BLOCKING_LOCK_ID
""".replace("\n", " ")
)

def_table_schema(
  owner           = 'yangyifei.yyf',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_LOCKS',
  table_id        = '21555',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT '0' as LOCK_ID,
           '0' as LOCK_TRX_ID,
           '0' as LOCK_MODE,
           '0' as LOCK_TYPE,
           '0' as LOCK_TABLE,
           '0' as LOCK_INDEX,
           0 as LOCK_SPACE,
           0 as LOCK_PAGE,
           0 as LOCK_REC,
           '0' as LOCK_DATA
""".replace("\n", " ")
)

def_table_schema(
  owner           = 'yangyifei.yyf',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_TRX',
  table_id        = '21556',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT '0' as TRX_ID,
           '0' as TRX_STATE,
           now() as TRX_STARTED,
           '0' as TRX_REQUESTED_LOCK_ID,
           now() as TRX_WAIT_STARTED,
           0 as TRX_WEIGHT,
           0 as TRX_MYSQL_THREAD_ID,
           '0' as TRX_QUERY,
           '0' as TRX_OPERATION_STATE,
           0 as TRX_TABLE_IN_USE,
           0 as TRX_TABLES_LOCKED,
           0 as TRX_LOCK_STRUCTS,
           0 as TRX_LOCK_MEMORY_BYTES,
           0 as TRX_ROWS_LOCKED,
           0 as TRX_ROWS_MODIFIED,
           0 as TRX_CONCURRENCY_TICKETS,
           '0' as TRX_ISOLATION_LEVEL,
           0 as TRX_UNIQUE_CHECKS,
           0 as TRX_FOREIGN_KEY_CHECKS,
           '0' as TRX_LAST_FOREIGN_KEY_ERROR,
           0 as TRX_ADAPTIVE_HASH_LATCHED,
           0 as TRX_ADAPTIVE_HASH_TIMEOUT,
           0 as TRX_IS_READ_ONLY,
           0 as TRX_AUTOCOMMIT_NON_LOCKING
""".replace("\n", " ")
)

def_table_schema(
  owner           = 'yangyifei.yyf',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'NDB_TRANSID_MYSQL_CONNECTION_MAP',
  table_id        = '21557',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT 0 as MYSQL_CONNECTION_ID,
           0 as NODE_ID,
           0 as NDB_TRANSID
""".replace("\n", " ")
)

def_table_schema(
  owner           = 'wyh329796',
  table_name      = 'V$OB_GROUP_IO_STAT',
  table_id        = '21558',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    A.GROUP_ID AS GROUP_ID,
    A.GROUP_NAME AS GROUP_NAME,
    A.MODE AS MODE,
    A.MIN_IOPS AS MIN_IOPS,
    A.MAX_IOPS AS MAX_IOPS,
    A.NORM_IOPS AS NORM_IOPS,
    A.REAL_IOPS AS REAL_IOPS,
    A.MAX_NET_BANDWIDTH AS MAX_NET_BANDWIDTH,
    A.MAX_NET_BANDWIDTH_DISPLAY AS MAX_NET_BANDWIDTH_DISPLAY,
    A.REAL_NET_BANDWIDTH AS REAL_NET_BANDWIDTH,
    A.REAL_NET_BANDWIDTH_DISPLAY AS REAL_NET_BANDWIDTH_DISPLAY
  FROM
    OCEANBASE.__ALL_VIRTUAL_GROUP_IO_STAT AS A
""".replace("\n", " ")
)

# 21559: GV$OB_GROUP_IO_STAT # removed (single-tenant GV/V collapse; folded into V$OB_GROUP_IO_STAT)

# 21560: DBA_OB_STORAGE_IO_USAGE (abandoned)
# 21561: CDB_OB_STORAGE_IO_USAGE (abandoned)

def_table_schema(
  owner = 'chaser.ch',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'TABLESPACES',
  table_id        = '21562',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as CHAR) as TABLESPACE_NAME,
    CAST(NULL as CHAR) as ENGINE,
    CAST(NULL as CHAR) as TABLESPACE_TYPE,
    CAST(NULL as CHAR) as LOGFILE_GROUP_NAME,
    CAST(NULL as UNSIGNED) as EXTENT_SIZE,
    CAST(NULL as UNSIGNED) as AUTOEXTEND_SIZE,
    CAST(NULL as UNSIGNED) as MAXIMUM_SIZE,
    CAST(NULL as UNSIGNED) as NODEGROUP_ID,
    CAST(NULL as CHAR) as TABLESPACE_COMMENT
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'chaser.ch',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_BUFFER_PAGE',
  table_id        = '21563',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as UNSIGNED) as POOL_ID,
    CAST(NULL as UNSIGNED) as BLOCK_ID,
    CAST(NULL as UNSIGNED) as SPACE,
    CAST(NULL as UNSIGNED) as PAGE_NUMBER,
    CAST(NULL as CHAR) as PAGE_TYPE,
    CAST(NULL as UNSIGNED) as FLUSH_TYPE,
    CAST(NULL as UNSIGNED) as FIX_COUNT,
    CAST(NULL as CHAR) as IS_HASHED,
    CAST(NULL as UNSIGNED) as NEWEST_MODIFICATION,
    CAST(NULL as UNSIGNED) as OLDEST_MODIFICATION,
    CAST(NULL as UNSIGNED) as ACCESS_TIME,
    CAST(NULL as CHAR) as TABLE_NAME,
    CAST(NULL as CHAR) as INDEX_NAME,
    CAST(NULL as UNSIGNED) as NUMBER_RECORDS,
    CAST(NULL as UNSIGNED) as DATA_SIZE,
    CAST(NULL as UNSIGNED) as COMPRESSED_SIZE,
    CAST(NULL as CHAR) as PAGE_STATE,
    CAST(NULL as CHAR) as IO_FIX,
    CAST(NULL as CHAR) as IS_OLD,
    CAST(NULL as UNSIGNED) as FREE_PAGE_CLOCK
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'chaser.ch',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_BUFFER_PAGE_LRU',
  table_id        = '21564',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as UNSIGNED) as POOL_ID,
    CAST(NULL as UNSIGNED) as LRU_POSITION,
    CAST(NULL as UNSIGNED) as SPACE,
    CAST(NULL as UNSIGNED) as PAGE_NUMBER,
    CAST(NULL as CHAR) as PAGE_TYPE,
    CAST(NULL as UNSIGNED) as FLUSH_TYPE,
    CAST(NULL as UNSIGNED) as FIX_COUNT,
    CAST(NULL as CHAR) as IS_HASHED,
    CAST(NULL as UNSIGNED) as NEWEST_MODIFICATION,
    CAST(NULL as UNSIGNED) as OLDEST_MODIFICATION,
    CAST(NULL as UNSIGNED) as ACCESS_TIME,
    CAST(NULL as CHAR) as TABLE_NAME,
    CAST(NULL as CHAR) as INDEX_NAME,
    CAST(NULL as UNSIGNED) as NUMBER_RECORDS,
    CAST(NULL as UNSIGNED) as DATA_SIZE,
    CAST(NULL as UNSIGNED) as COMPRESSED_SIZE,
    CAST(NULL as CHAR) as COMPRESSED,
    CAST(NULL as CHAR) as IO_FIX,
    CAST(NULL as CHAR) as IS_OLD,
    CAST(NULL as UNSIGNED) as FREE_PAGE_CLOCK
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'chaser.ch',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_BUFFER_POOL_STATS',
  table_id        = '21565',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as UNSIGNED) as POOL_ID,
    CAST(NULL as UNSIGNED) as POOL_SIZE,
    CAST(NULL as UNSIGNED) as FREE_BUFFERS,
    CAST(NULL as UNSIGNED) as DATABASE_PAGES,
    CAST(NULL as UNSIGNED) as OLD_DATABASE_PAGES,
    CAST(NULL as UNSIGNED) as MODIFIED_DATABASE_PAGES,
    CAST(NULL as UNSIGNED) as PENDING_DECOMPRESS,
    CAST(NULL as UNSIGNED) as PENDING_READS,
    CAST(NULL as UNSIGNED) as PENDING_FLUSH_LRU,
    CAST(NULL as UNSIGNED) as PENDING_FLUSH_LIST,
    CAST(NULL as UNSIGNED) as PAGES_MADE_YOUNG,
    CAST(NULL as UNSIGNED) as PAGES_NOT_MADE_YOUNG,
    CAST(NULL as DECIMAL) as PAGES_MADE_YOUNG_RATE,
    CAST(NULL as DECIMAL) as PAGES_MADE_NOT_YOUNG_RATE,
    CAST(NULL as UNSIGNED) as NUMBER_PAGES_READ,
    CAST(NULL as UNSIGNED) as NUMBER_PAGES_CREATED,
    CAST(NULL as UNSIGNED) as NUMBER_PAGES_WRITTEN,
    CAST(NULL as DECIMAL) as PAGES_READ_RATE,
    CAST(NULL as DECIMAL) as PAGES_CREATE_RATE,
    CAST(NULL as DECIMAL) as PAGES_WRITTEN_RATE,
    CAST(NULL as UNSIGNED) as NUMBER_PAGES_GET,
    CAST(NULL as UNSIGNED) as HIT_RATE,
    CAST(NULL as UNSIGNED) as YOUNG_MAKE_PER_THOUSAND_GETS,
    CAST(NULL as UNSIGNED) as NOT_YOUNG_MAKE_PER_THOUSAND_GETS,
    CAST(NULL as UNSIGNED) as NUMBER_PAGES_READ_AHEAD,
    CAST(NULL as UNSIGNED) as NUMBER_READ_AHEAD_EVICTED,
    CAST(NULL as DECIMAL) as READ_AHEAD_RATE,
    CAST(NULL as DECIMAL) as READ_AHEAD_EVICTED_RATE,
    CAST(NULL as UNSIGNED) as LRU_IO_TOTAL,
    CAST(NULL as UNSIGNED) as LRU_IO_CURRENT,
    CAST(NULL as UNSIGNED) as UNCOMPRESS_TOTAL,
    CAST(NULL as UNSIGNED) as UNCOMPRESS_CURRENT
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'chaser.ch',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_CMP',
  table_id        = '21566',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as SIGNED) as PAGE_SIZE,
    CAST(NULL as SIGNED) as COMPRESS_OPS,
    CAST(NULL as SIGNED) as COMPRESS_OPS_OK,
    CAST(NULL as SIGNED) as COMPRESS_TIME,
    CAST(NULL as SIGNED) as UNCOMPRESS_OPS,
    CAST(NULL as SIGNED) as UNCOMPRESS_TIME
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'chaser.ch',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_CMP_PER_INDEX',
  table_id        = '21567',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as CHAR) as DATABASE_NAME,
    CAST(NULL as CHAR) as TABLE_NAME,
    CAST(NULL as CHAR) as INDEX_NAME,
    CAST(NULL as SIGNED) as COMPRESS_OPS,
    CAST(NULL as SIGNED) as COMPRESS_OPS_OK,
    CAST(NULL as SIGNED) as COMPRESS_TIME,
    CAST(NULL as SIGNED) as UNCOMPRESS_OPS,
    CAST(NULL as SIGNED) as UNCOMPRESS_TIME
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'chaser.ch',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_CMP_PER_INDEX_RESET',
  table_id        = '21568',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as CHAR) as DATABASE_NAME,
    CAST(NULL as CHAR) as TABLE_NAME,
    CAST(NULL as CHAR) as INDEX_NAME,
    CAST(NULL as SIGNED) as COMPRESS_OPS,
    CAST(NULL as SIGNED) as COMPRESS_OPS_OK,
    CAST(NULL as SIGNED) as COMPRESS_TIME,
    CAST(NULL as SIGNED) as UNCOMPRESS_OPS,
    CAST(NULL as SIGNED) as UNCOMPRESS_TIME
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'chaser.ch',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_CMP_RESET',
  table_id        = '21569',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as SIGNED) as PAGE_SIZE,
    CAST(NULL as SIGNED) as COMPRESS_OPS,
    CAST(NULL as SIGNED) as COMPRESS_OPS_OK,
    CAST(NULL as SIGNED) as COMPRESS_TIME,
    CAST(NULL as SIGNED) as UNCOMPRESS_OPS,
    CAST(NULL as SIGNED) as UNCOMPRESS_TIME
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'chaser.ch',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_CMPMEM',
  table_id        = '21570',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as SIGNED) as PAGE_SIZE,
    CAST(NULL as SIGNED) as BUFFER_POOL_INSTANCE,
    CAST(NULL as SIGNED) as PAGES_USED,
    CAST(NULL as SIGNED) as PAGES_FREE,
    CAST(NULL as SIGNED) as RELOCATION_OPS,
    CAST(NULL as SIGNED) as RELOCATION_TIME
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'chaser.ch',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_CMPMEM_RESET',
  table_id        = '21571',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as SIGNED) as PAGE_SIZE,
    CAST(NULL as SIGNED) as BUFFER_POOL_INSTANCE,
    CAST(NULL as SIGNED) as PAGES_USED,
    CAST(NULL as SIGNED) as PAGES_FREE,
    CAST(NULL as SIGNED) as RELOCATION_OPS,
    CAST(NULL as SIGNED) as RELOCATION_TIME
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'chaser.ch',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_SYS_DATAFILES',
  table_id        = '21572',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as UNSIGNED) as SPACE,
    CAST(NULL as CHAR) as PATH
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'chaser.ch',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_SYS_INDEXES',
  table_id        = '21573',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as UNSIGNED) as INDEX_ID,
    CAST(NULL as CHAR) as NAME,
    CAST(NULL as UNSIGNED) as TABLE_ID,
    CAST(NULL as SIGNED) as TYPE,
    CAST(NULL as SIGNED) as N_FIELDS,
    CAST(NULL as SIGNED) as PAGE_NO,
    CAST(NULL as SIGNED) as SPACE,
    CAST(NULL as SIGNED) as MERGE_THRESHOLD
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'chaser.ch',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_SYS_TABLES',
  table_id        = '21574',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as UNSIGNED) as TABLE_ID,
    CAST(NULL as CHAR) as NAME,
    CAST(NULL as SIGNED) as FLAG,
    CAST(NULL as SIGNED) as N_COLS,
    CAST(NULL as SIGNED) as SPACE,
    CAST(NULL as CHAR) as FILE_FORMAT,
    CAST(NULL as CHAR) as ROW_FORMAT,
    CAST(NULL as UNSIGNED) as ZIP_PAGE_SIZE,
    CAST(NULL as CHAR) as SPACE_TYPE
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'chaser.ch',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_SYS_TABLESPACES',
  table_id        = '21575',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as UNSIGNED) as SPACE,
    CAST(NULL as CHAR) as NAME,
    CAST(NULL as UNSIGNED) as FLAG,
    CAST(NULL as CHAR) as FILE_FORMAT,
    CAST(NULL as CHAR) as ROW_FORMAT,
    CAST(NULL as UNSIGNED) as PAGE_SIZE,
    CAST(NULL as UNSIGNED) as ZIP_PAGE_SIZE,
    CAST(NULL as CHAR) as SPACE_TYPE,
    CAST(NULL as UNSIGNED) as FS_BLOCK_SIZE,
    CAST(NULL as UNSIGNED) as FILE_SIZE,
    CAST(NULL as UNSIGNED) as ALLOCATED_SIZE
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'chaser.ch',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_SYS_TABLESTATS',
  table_id        = '21576',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as UNSIGNED) as TABLE_ID,
    CAST(NULL as CHAR) as NAME,
    CAST(NULL as CHAR) as STATS_INITIALIZED,
    CAST(NULL as UNSIGNED) as NUM_ROWS,
    CAST(NULL as UNSIGNED) as CLUST_INDEX_SIZE,
    CAST(NULL as UNSIGNED) as OTHER_INDEX_SIZE,
    CAST(NULL as UNSIGNED) as MODIFIED_COUNTER,
    CAST(NULL as UNSIGNED) as AUTOINC,
    CAST(NULL as SIGNED) as REF_COUNT
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'chaser.ch',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_SYS_VIRTUAL',
  table_id        = '21577',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as UNSIGNED) as TABLE_ID,
    CAST(NULL as UNSIGNED) as POS,
    CAST(NULL as UNSIGNED) as BASE_POS
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'chaser.ch',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_TEMP_TABLE_INFO',
  table_id        = '21578',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as UNSIGNED) as TABLE_ID,
    CAST(NULL as CHAR) as NAME,
    CAST(NULL as UNSIGNED) as N_COLS,
    CAST(NULL as UNSIGNED) as SPACE,
    CAST(NULL as CHAR) as PER_TABLE_TABLESPACE,
    CAST(NULL as CHAR) as IS_COMPRESSED
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'chaser.ch',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_METRICS',
  table_id        = '21579',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as CHAR) as NAME,
    CAST(NULL as CHAR) as SUBSYSTEM,
    CAST(NULL as SIGNED) as COUNT,
    CAST(NULL as SIGNED) as MAX_COUNT,
    CAST(NULL as SIGNED) as MIN_COUNT,
    CAST(NULL as DECIMAL) as AVG_COUNT,
    CAST(NULL as SIGNED) as COUNT_RESET,
    CAST(NULL as SIGNED) as MAX_COUNT_RESET,
    CAST(NULL as SIGNED) as MIN_COUNT_RESET,
    CAST(NULL as DECIMAL) as AVG_COUNT_RESET,
    CAST(NULL as DATETIME) as TIME_ENABLED,
    CAST(NULL as DATETIME) as TIME_DISABLED,
    CAST(NULL as SIGNED) as TIME_ELAPSED,
    CAST(NULL as DATETIME) as TIME_RESET,
    CAST(NULL as CHAR) as STATUS,
    CAST(NULL as CHAR) as TYPE,
    CAST(NULL as CHAR) as COMMENT
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'huangrenhuang.hrh',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'EVENTS',
  table_id        = '21580',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """SELECT
      CAST("def" AS CHARACTER(64)) AS EVENT_CATALOG,
      CAST(T.cowner AS CHARACTER(128)) AS EVENT_SCHEMA,
      CAST(SUBSTRING_INDEX(T.job_name, '.', -1) AS CHARACTER(64)) AS EVENT_NAME,
      CAST(T.powner AS CHARACTER(93)) AS DEFINER,
      CAST("SYSTEM" AS CHARACTER(64)) AS TIME_ZONE,
      CAST("SQL" AS CHARACTER(8)) AS EVENT_BODY,
      CAST(T.what AS CHARACTER(65536)) AS EVENT_DEFINITION,
      CAST(CASE WHEN T.repeat_interval IS NOT NULL THEN "RECURRING" ELSE "ONE TIME" END AS CHARACTER(9)) AS EVENT_TYPE,
      CAST(CASE WHEN T.repeat_interval IS NOT NULL THEN "NULL" ELSE T.start_date END AS DATETIME)  AS EXECUTE_AT,
      CAST(CASE WHEN T.repeat_interval IS NOT NULL THEN SUBSTRING_INDEX(SUBSTRING_INDEX(T.repeat_interval, 'INTERVAL=', -1), ';', 1) ELSE NULL END AS CHARACTER(256)) AS INTERVAL_VALUE,
      CAST(CASE WHEN T.repeat_interval IS NOT NULL THEN SUBSTRING_INDEX(SUBSTRING_INDEX(T.repeat_interval, 'FREQ=', -1),'LY', 1) ELSE NULL END AS CHARACTER(18))  AS INTERVAL_FIELD,
      CAST("ONLY_FULL_GROUP_BY,STRICT_TRANS_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_AUTO_CREATE_USER,NO_ENGINE_SUBSTITUTION" AS CHARACTER(8192))  AS SQL_MODE,
      CAST(CASE WHEN T.repeat_interval IS NOT NULL THEN T.start_date ELSE NULL END AS DATETIME) AS STARTS,
      CAST(CASE WHEN T.repeat_interval IS NOT NULL AND T.end_date != '4000-01-01 00:00:00' THEN T.end_date ELSE NULL END AS DATETIME) AS ENDS,
      CAST(CASE WHEN T.enabled = 1 THEN "ENABLED" ELSE "DISABLED" END AS CHARACTER(18)) AS STATUS,
      CAST(CASE WHEN T.auto_drop = 1 THEN "NOT PRESERVE" ELSE "PRESERVE" END AS CHARACTER(12)) AS ON_COMPLETION,
      CAST(T.gmt_create AS DATETIME) AS CREATED,
      CAST(T.gmt_modified AS DATETIME) AS LAST_ALTERED,
      CAST(T.last_date AS DATETIME) AS LAST_EXECUTED,
      CAST(T.comments AS CHARACTER(4096)) AS EVENT_COMMENT,
      CAST(NULL AS UNSIGNED) AS ORIGINATOR,
      CAST(NULL AS CHARACTER(32)) AS CHARACTER_SET_CLIENT,
      CAST(NULL AS CHARACTER(32)) AS COLLATION_CONNECTION,
      CAST(NULL AS CHARACTER(32)) AS DATABASE_COLLATION
    FROM oceanbase.__all_scheduler_job T WHERE T.JOB_NAME != '__dummy_guard' AND T.JOB > 0 AND T.JOB_CLASS = 'MYSQL_EVENT_JOB_CLASS'
""".replace("\n", " ")
)

# 21581: V$OB_NIC_INFO # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_nic_info)

def_table_schema(
  owner = 'linyi.cl',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'ROLE_TABLE_GRANTS',
  table_id       = '21582',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  view_definition = """
  with recursive role_graph (from_user, from_host, to_user, to_host, is_enabled)
  as (
      select user_name, host, cast('' as char(128)), cast('' as char(128)), false
      from oceanbase.__all_user
      where concat(user_name, '@', host)=current_user()
      union all
      select role_edges.from_user, role_edges.from_host, role_edges.to_user, role_edges.to_host,
             if ((role_graph.is_enabled
                  or is_enabled_role(role_edges.from_user, role_edges.from_host)),
                  true,
                  false)
      from mysql.role_edges role_edges join role_graph
      on role_edges.to_user = role_graph.from_user and role_edges.to_host = role_graph.from_host
  )
  select distinct
    cast(tp.grantor as char(97)) as GRANTOR,
    cast(tp.grantor_host as char(256)) as GRANTOR_HOST,
    cast(u.user_name as char(32)) as GRANTEE,
    cast(u.host as char(255)) as GRANTEE_HOST,
    cast('def' as char(3)) as TABLE_CATALOG,
    cast(tp.database_name as char(64)) as TABLE_SCHEMA,
    cast(tp.table_name as char(64)) as TABLE_NAME,
    substr(concat(case when tp.priv_alter > 0 then ',Alter' else '' end,
            case when tp.priv_create > 0 then ',Create' else '' end,
            case when tp.priv_delete > 0 then ',Delete' else '' end,
            case when tp.priv_drop > 0 then ',Drop' else '' end,
            case when tp.priv_grant_option > 0 then ',Grant' else '' end,
            case when tp.priv_insert > 0 then ',Insert' else '' end,
            case when tp.priv_update > 0 then ',Update' else '' end,
            case when tp.priv_select > 0 then ',Select' else '' end,
            case when tp.priv_index > 0 then ',Index' else '' end,
            case when tp.priv_create_view > 0 then ',Create View' else '' end,
            case when tp.priv_show_view > 0 then ',Show View' else '' end,
            case when (tp.priv_others & 64) > 0 then ',References' else '' end),2) as PRIVILEGE_TYPE,
    cast(if (tp.priv_grant_option > 0,'YES','NO') as char(3)) AS IS_GRANTABLE
  from (select distinct from_user, from_host, to_user, to_host, is_enabled from role_graph) rg
      join oceanbase.__all_table_privilege tp join oceanbase.__all_user u
  on tp.user_id = u.user_id and rg.from_user = u.user_name and rg.from_host = u.host
  where rg.is_enabled and rg.to_user <> ''
  """.replace("\n", " "),

  normal_columns = []
  )

def_table_schema(
  owner = 'linyi.cl',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'ROLE_COLUMN_GRANTS',
  table_id       = '21583',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  view_definition = """
  with recursive role_graph (from_user, from_host, to_user, to_host, is_enabled)
  as (
      select user_name, host, cast('' as char(128)), cast('' as char(128)), false
      from oceanbase.__all_user
      where concat(user_name, '@', host)=current_user()
      union all
      select role_edges.from_user, role_edges.from_host, role_edges.to_user, role_edges.to_host,
            if ((role_graph.is_enabled or is_enabled_role(role_edges.from_user, role_edges.from_host)), true, false)
      from mysql.role_edges role_edges join role_graph
      on role_edges.to_user = role_graph.from_user and role_edges.to_host = role_graph.from_host
  )
  select distinct
    NULL as GRANTOR,
    NULL as GRANTOR_HOST,
    cast(u.user_name as char(32)) as GRANTEE,
    cast(u.host as char(255)) as GRANTEE_HOST,
    cast('def' as char(3)) as TABLE_CATALOG,
    cast(cp.database_name as char(64)) as TABLE_SCHEMA,
    cast(cp.table_name as char(64)) as TABLE_NAME,
    cast(cp.column_name as char(64)) as COLUMN_NAME,
    substr(concat(case when (cp.all_priv & 1) > 0 then ',Select' else '' end,
                  case when (cp.all_priv & 2) > 0 then ',Insert' else '' end,
                  case when (cp.all_priv & 4) > 0 then ',Update' else '' end,
                  case when (cp.all_priv & 8) > 0 then ',References' else '' end), 2) as PRIVILEGE_TYPE,
    cast(if (tp.priv_grant_option > 0,'YES','NO') as char(3)) AS IS_GRANTABLE
  from  ((select distinct from_user, from_host, to_user, to_host, is_enabled from role_graph) rg
        join oceanbase.__all_user u join oceanbase.__all_column_privilege cp
        on cp.user_id = u.user_id and rg.from_user = u.user_name and rg.from_host = u.host
            and rg.is_enabled and rg.to_user <> '')
        left join
        oceanbase.__all_table_privilege tp
        on cp.database_name = tp.database_name and cp.table_name = tp.table_name
            and cp.user_id = tp.user_id
  """.replace("\n", " "),

  normal_columns = []
  )

def_table_schema(
  owner = 'linyi.cl',
  database_id    = 'OB_INFORMATION_SCHEMA_ID',
  table_name     = 'ROLE_ROUTINE_GRANTS',
  table_id       = '21584',
  table_type = 'SYSTEM_VIEW',
  gm_columns = [],
  rowkey_columns = [],
  in_tenant_space = True,

  view_definition = """
  with recursive role_graph (from_user, from_host, to_user, to_host, is_enabled)
  as (
    select user_name, host, cast('' as char(128)), cast('' as char(128)), false
    from oceanbase.__all_user
    where concat(user_name, '@', host)=current_user()
    union all
    select role_edges.from_user, role_edges.from_host, role_edges.to_user, role_edges.to_host,
          if ((role_graph.is_enabled or is_enabled_role(role_edges.from_user, role_edges.from_host)), true, false)
    from mysql.role_edges role_edges join role_graph
    on role_edges.to_user = role_graph.from_user and role_edges.to_host = role_graph.from_host
  )
  select distinct
    cast(rp.grantor as char(97)) as GRANTOR,
    cast(rp.grantor_host as char(256)) as GRANTOR_HOST,
    cast(u.user_name as char(32)) as GRANTEE,
    cast(u.host as char(255)) as GRANTEE_HOST,
    cast('def' as char(3)) AS SPECIFIC_CATALOG,
    cast(rp.database_name as char(64)) AS SPECIFIC_SCHEMA,
    cast(rp.routine_name as char(64)) AS SPECIFIC_NAME,
    cast('def' as char(3))  AS ROUTINE_CATALOG,
    cast(rp.database_name as char(64)) AS ROUTINE_SCHEMA,
    cast(rp.routine_name as char(64)) AS ROUTINE_NAME,
    substr(concat(case when (rp.all_priv & 1) > 0 then ',Execute' else '' end,
                  case when (rp.all_priv & 2) > 0 then ',Alter Routine' else '' end,
                  case when (rp.all_priv & 4) > 0 then ',Grant' else '' end), 2) AS PRIVILEGE_TYPE,
    cast(if ((rp.all_priv & 4) > 0,'YES','NO') as char(3)) AS `IS_GRANTABLE`
  from   (select distinct from_user, from_host, to_user, to_host, is_enabled from role_graph) rg
         join oceanbase.__all_routine_privilege rp join oceanbase.__all_user u
  on     rp.user_id = u.user_id and rg.from_user = u.user_name and rg.from_host = u.host
  where  rg.to_user <> '' and rg.is_enabled
  """.replace("\n", " "),

  normal_columns = []
  )

def_table_schema(
  owner = 'wangbai.wx',
  database_id    = 'OB_MYSQL_SCHEMA_ID',
  table_name     = 'func',
  table_id       = '21585',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    SELECT name, ret, dl, type
    FROM oceanbase.__all_func
""".replace("\n", " ")
)

# 21586: GV$OB_NIC_INFO # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_nic_info)
# 21587: GV$OB_QUERY_RESPONSE_TIME_HISTOGRAM # removed (single-tenant GV/V collapse; folded into V$OB_QUERY_RESPONSE_TIME_HISTOGRAM)

def_table_schema(
  owner = 'jiajingzhe.jjz',
  table_name      = 'V$OB_QUERY_RESPONSE_TIME_HISTOGRAM',
  table_id        = '21588',
  table_type      = 'SYSTEM_VIEW',
  gm_columns      = [],
  rowkey_columns  = [],
  normal_columns  = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    sql_type as SQL_TYPE,
    cast ((response_time/1000000 ) as decimal(24,6)) as RESPONSE_TIME,
    count as COUNT,
    cast ((total/1000000)  as decimal(24,6))  as TOTAL
  FROM oceanbase.__all_virtual_query_response_time
""".replace("\n", " ")
  )

# 21591: DBA_OB_SERVER_SPACE_USAGE (abandoned)
# 21592: CDB_OB_SERVER_SPACE_USAGE (abandoned)
# 21593: DBA_OB_SPACE_USAGE
# 21594: CDB_OB_SPACE_USAGE (abandoned)

def_table_schema(
  owner = 'gaishun.gs',
  table_name      = 'DBA_OB_TABLE_SPACE_USAGE',
  table_id        = '21595',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
    select
      subquery.TABLE_ID AS TABLE_ID,
      subquery.DATABASE_NAME AS DATABASE_NAME,
      at_name.TABLE_NAME AS TABLE_NAME,
      subquery.OCCUPY_SIZE AS OCCUPY_SIZE,
      subquery.REQUIRED_SIZE AS REQUIRED_SIZE
    from
    (
      select
        CASE
          WHEN at.table_type in (12, 13) THEN at.data_table_id
          ELSE at.table_id
        END as TABLE_ID,
        ad.database_name as DATABASE_NAME,
        sum(avtps.occupy_size) as OCCUPY_SIZE,
        sum(avtps.required_size) as REQUIRED_SIZE
      from
      oceanbase.__all_virtual_tablet_pointer_status avtps
      INNER JOIN oceanbase.__all_tablet_to_table attl
        ON      attl.tablet_id = avtps.tablet_id
      INNER JOIN oceanbase.__all_table at
        ON      at.table_id = attl.table_id
          and   at.table_id > 500000
      INNER JOIN oceanbase.__all_database ad
        ON      ad.database_id = at.database_id
      group by table_id
    ) as subquery
    INNER JOIN oceanbase.__all_table at_name
      ON    subquery.TABLE_ID = at_name.table_id
    order by table_id
""".replace("\n", " ")
)

# 21599: GV$OB_SS_LOCAL_CACHE abandoned
# 21600: V$OB_SS_LOCAL_CACHE abandoned

# 21600: GV$OB_KV_GROUP_COMMIT_STATUS abandoned


# 21601: V$OB_KV_GROUP_COMMIT_STATUS abandoned


def_table_schema(
  owner = 'zhenjiang.xzj',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_SYS_FIELDS',
  table_id        = '21603',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as UNSIGNED) as INDEX_ID,
    CAST(NULL as CHAR) as NAME,
    CAST(NULL as UNSIGNED) as POS
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'zhenjiang.xzj',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_SYS_FOREIGN',
  table_id        = '21604',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as CHAR) as ID,
    CAST(NULL as CHAR) as FOR_NAME,
    CAST(NULL as CHAR) as REF_NAME,
    CAST(NULL as UNSIGNED) as N_COLS,
    CAST(NULL as UNSIGNED) as TYPE
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

def_table_schema(
  owner = 'zhenjiang.xzj',
  tablegroup_id   = 'OB_INVALID_ID',
  database_id     = 'OB_INFORMATION_SCHEMA_ID',
  table_name      = 'INNODB_SYS_FOREIGN_COLS',
  table_id        = '21605',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
    CAST(NULL as CHAR) as ID,
    CAST(NULL as CHAR) as FOR_COL_NAME,
    CAST(NULL as CHAR) as REF_COL_NAME,
    CAST(NULL as UNSIGNED) as POS
  FROM
    DUAL
  WHERE
    0 = 1
""".replace("\n", " ")
)

# 21606: GV$OB_VARIABLES_BY_SESSION
# 21602: GV$OB_KV_CLIENT_INFO abandoned


# 21603: V$OB_KV_CLIENT_INFO abandoned

# 21609: V$OB_VARIABLES_BY_SESSION
# 21610: GV$OB_RES_MGR_SYSSTAT # removed

# 21611: V$OB_RES_MGR_SYSSTAT # removed

# 21612: DBA_WR_SQL_PLAN # removed

# 21613: CDB_WR_SQL_PLAN # removed

# 21614: DBA_WR_RES_MGR_SYSSTAT # removed

# 21615: CDB_WR_RES_MGR_SYSSTAT # removed

# 21616: DBA_OB_SPM_EVO_RESULT abandoned
# 21617: CDB_OB_SPM_EVO_RESULT abandoned

# 21618: DBA_OB_KV_REDIS_TABLE abandoned


# 21619: CDB_OB_KV_REDIS_TABLE abandoned

# 21620: removed (legacy function IO classification deleted)

# 21621: removed (legacy function IO classification deleted)

def_table_schema(
  owner = 'wuyuefei.wyf',
  table_name      = 'DBA_OB_TEMP_FILES',
  table_id        = '21622',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """SELECT
    FILE_ID,
    TRACE_ID,
    DIR_ID,
    DATA_BYTES,
    START_OFFSET,
    TOTAL_WRITES,
    UNALIGNED_WRITES,
    TOTAL_READS,
    UNALIGNED_READS,
    TOTAL_READ_BYTES,
    LAST_ACCESS_TIME,
    LAST_MODIFY_TIME,
    BIRTH_TIME
  FROM oceanbase.__all_virtual_temp_file
""".replace("\n", " ")
  )

# 21624: GV$OB_LOGSTORE_SERVICE_STATUS
# 21625: V$OB_LOGSTORE_SERVICE_STATUS
# 21626: GV$OB_LOGSTORE_SERVICE_INFO
# 21627: V$OB_LOGSTORE_SERVICE_INFO


def_table_schema(
    owner           = 'xinning.lf',
    tablegroup_id   = 'OB_INVALID_ID',
    table_name      = 'proc',
    table_id        = '21628',
    database_id     = 'OB_MYSQL_SCHEMA_ID',
    table_type      = 'SYSTEM_VIEW',
    rowkey_columns  = [],
    normal_columns  = [],
    gm_columns      = [],
    in_tenant_space = True,
    view_definition = """
    SELECT
      D.DATABASE_NAME AS DB,
      R.ROUTINE_NAME AS NAME,
      CAST((CASE R.ROUTINE_TYPE
        WHEN 1 THEN 'PROCEDURE'
        WHEN 2 THEN 'FUNCTION' END) AS CHAR(10)) AS TYPE,
      R.ROUTINE_NAME AS SPECIFIC_NAME,
      CAST('SQL' AS CHAR(4)) AS LANGUAGE,
      CAST((CASE WHEN (R.FLAG & 32768) = 32768 THEN 'NO_SQL'
                WHEN (R.FLAG & 65536) = 65536 THEN 'READS_SQL_DATA'
                WHEN (R.FLAG & 131072) = 131072 THEN 'MODIFIES_SQL_DATA'
                ELSE 'CONTAINS_SQL' END) AS CHAR(32)) AS SQL_DATA_ACCESS,
      CAST((CASE WHEN (R.FLAG & 4) = 4 THEN 'YES' ELSE 'NO' END) AS CHAR(4)) AS IS_DETERMINISTIC,
      CAST((CASE WHEN (R.FLAG & 16) = 16 THEN 'INVOKER' ELSE 'DEFINER' END) AS CHAR(10)) AS SECURITY_TYPE,
      MYSQL_PROC_INFO('PARAM_LIST', R.ROUTINE_BODY, R.EXEC_ENV, R.ROUTINE_ID, NULL, NULL, NULL, NULL, NULL) AS PARAM_LIST,
      CASE R.ROUTINE_TYPE
        WHEN 1 THEN ''
        WHEN 2 THEN MYSQL_PROC_INFO('RETURNS', R.ROUTINE_BODY, R.EXEC_ENV, R.ROUTINE_ID, RP.PARAM_TYPE, RP.PARAM_LENGTH, RP.PARAM_PRECISION, RP.PARAM_SCALE, RP.PARAM_COLL_TYPE)
        END AS RETURNS,
      MYSQL_PROC_INFO('BODY', R.ROUTINE_BODY, R.EXEC_ENV, R.ROUTINE_ID, NULL, NULL, NULL, NULL, NULL) AS BODY,
      CAST(CONCAT('''', REPLACE(R.PRIV_USER, '@', '''@''' ), '''') AS CHAR(77)) AS DEFINER,
      R.GMT_CREATE AS CREATED,
      R.GMT_MODIFIED AS MODIFIED,
      CAST(MYSQL_PROC_INFO('SQL_MODE', R.ROUTINE_BODY, R.EXEC_ENV, R.ROUTINE_ID, NULL, NULL, NULL, NULL, NULL) AS CHAR(8192)) AS SQL_MODE,
      NVL(R.COMMENT, '') AS COMMENT,
      CAST(MYSQL_PROC_INFO('CHARACTER_SET_CLIENT', R.ROUTINE_BODY, R.EXEC_ENV, R.ROUTINE_ID, NULL, NULL, NULL, NULL, NULL) AS CHAR(128)) AS CHARACTER_SET_CLIENT,
      CAST(MYSQL_PROC_INFO('COLLATION_CONNECTION', R.ROUTINE_BODY, R.EXEC_ENV, R.ROUTINE_ID, NULL, NULL, NULL, NULL, NULL) AS CHAR(128)) AS COLLATION_CONNECTION,
      CAST(MYSQL_PROC_INFO('DB_COLLATION', R.ROUTINE_BODY, R.EXEC_ENV, R.ROUTINE_ID, NULL, NULL, NULL, NULL, NULL) AS CHAR(128)) AS DB_COLLATION,
      MYSQL_PROC_INFO('BODY', R.ROUTINE_BODY, R.EXEC_ENV, R.ROUTINE_ID, NULL, NULL, NULL, NULL, NULL) AS BODY_UTF8
      FROM
        ((SELECT * FROM oceanbase.__all_routine) R
          LEFT JOIN oceanbase.__all_database D ON R.DATABASE_ID = D.DATABASE_ID
          LEFT JOIN oceanbase.__all_routine_param RP ON R.routine_id = RP.routine_id AND RP.param_position = 0)
      WHERE
        D.IN_RECYCLEBIN = 0
      AND
        R.ROUTINE_TYPE IN (1, 2)
  """.replace("\n", " ")
)

# 21629: DBA_OB_OBJECT_BALANCE_WEIGHT
# 21630: CDB_OB_OBJECT_BALANCE_WEIGHT

# 21631: GV$OB_STANDBY_LOG_TRANSPORT_STAT
# 21632: V$OB_STANDBY_LOG_TRANSPORT_STAT
# 21633: removed

# 21635: GV$OB_PLUGINS # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_plugin_info)
# 21636: V$OB_PLUGINS # removed (single-tenant GV/V collapse; use oceanbase.__all_virtual_plugin_info)

def_table_schema(
  owner           = 'yangjiali.yjl',
  table_name      = 'DBA_OB_VECTOR_INDEX_TASKS',
  table_id        = '21640',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
      table_id as TABLE_ID,
      tablet_id as TABLET_ID,
      task_id as TASK_ID,
      gmt_create as START_TIME,
      gmt_modified as MODIFY_TIME,
      case trigger_type
        when 0 then "USER"
        when 1 then "MANUAL"
        else "INVALID" END AS TRIGGER_TYPE,
      case status
        when 0 then "PREPARED"
        when 1 then "RUNNING"
        when 2 then "PENDING"
        when 3 then "FINISHED"
        else "INVALID" END AS STATUS,
      task_type as TASK_TYPE,
      target_scn as TASK_SCN,
      ret_code as RET_CODE,
      trace_id as TRACE_ID
  FROM oceanbase.__all_vector_index_task
""".replace("\n", " ")
)

def_table_schema(
  owner           = 'yangjiali.yjl',
  table_name      = 'DBA_OB_VECTOR_INDEX_TASK_HISTORY',
  table_id        = '21642',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
  SELECT
      table_id as TABLE_ID,
      tablet_id as TABLET_ID,
      task_id as TASK_ID,
      gmt_create as START_TIME,
      gmt_modified as MODIFY_TIME,
      case trigger_type
        when 0 then "AUTO"
        when 1 then "MANUAL"
        else "INVALID" END AS TRIGGER_TYPE,
      case status
        when 0 then "PREPARED"
        when 1 then "RUNNING"
        when 2 then "PENDING"
        when 3 then "FINISHED"
        else "INVALID" END AS STATUS,
      task_type as TASK_TYPE,
      target_scn as TASK_SCN,
      ret_code as RET_CODE,
      trace_id as TRACE_ID
  FROM oceanbase.__all_vector_index_task_history
""".replace("\n", " ")
)

# 21643: CDB_OB_VECTOR_INDEX_TASK_HISTORY abandoned
# 21644: GV$OB_STORAGE_CACHE_TASKS abandoned
# 21645: V$OB_STORAGE_CACHE_TASKS abandoned
# 21646: GV$OB_TABLET_LOCAL_CACHE abandoned
# 21647: V$OB_TABLET_LOCAL_CACHE abandoned

def_table_schema(
    owner = 'zhl413386',
    table_name     = 'DBA_OB_CCL_RULES',
    table_id       = '21648',
    table_type = 'SYSTEM_VIEW',
    gm_columns = [],
    in_tenant_space = True,
    rowkey_columns = [],
    view_definition = """
          SELECT
          CCL_RULE_ID,
          CCL_RULE_NAME,
          AFFECT_USER_NAME,
          AFFECT_HOST,
          AFFECT_FOR_ALL_DATABASES,
          AFFECT_FOR_ALL_TABLES,
          AFFECT_DATABASE,
          AFFECT_TABLE,
          AFFECT_DML,
          AFFECT_SCOPE,
          CCL_KEYWORDS,
          MAX_CONCURRENCY
        FROM oceanbase.__all_ccl_rule
""".replace("\n", " "),

    normal_columns = [
    ]
  )

# 21650: GV$OB_SQL_CCL_STATUS # removed (single-tenant GV/V collapse; folded into V$OB_SQL_CCL_STATUS)

def_table_schema(
    owner = 'zhl413386',
    table_name     = 'V$OB_SQL_CCL_STATUS',
    table_id       = '21651',
    table_type = 'SYSTEM_VIEW',
    gm_columns = [],
    in_tenant_space = True,
    rowkey_columns = [],
    view_definition = """
          SELECT
          1 as CON_ID,
          CCL_RULE_ID,
          FORMAT_SQLID,
          CURRENT_CONCURRENCY,
          MAX_CONCURRENCY
        FROM oceanbase.__all_virtual_ccl_status

""".replace("\n", " "),

    normal_columns = [
    ]
  )




# 21661: GV$OB_VECTOR_MEMORY # removed (single-tenant GV/V collapse; folded into V$OB_VECTOR_MEMORY)

def_table_schema(
  owner = 'tonghui.ht',
  table_name      = 'V$OB_VECTOR_MEMORY',
  table_id        = '21662',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition = """
SELECT
    (VECTOR_MEM_HOLD + RAW_MALLOC_SIZE + INDEX_METADATA_SIZE) as VECTOR_MEM_HOLD,
    (VECTOR_MEM_USED + RAW_MALLOC_SIZE + INDEX_METADATA_SIZE) as VECTOR_MEM_USED,
    VECTOR_MEM_LIMIT
FROM
    oceanbase.__all_virtual_vector_mem_info
""".replace("\n", " ")
  )

def_table_schema(
  owner           = 'shenyunlong.syl',
  table_name      = 'DBA_OB_AI_MODELS',
  table_id        = '21663',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition =
  """
    SELECT
      MODEL_ID,
      NAME,
      case type
        when 1 then 'DENSE_EMBEDDING'
        when 2 then 'SPARSE_EMBEDDING'
        when 3 then 'COMPLETION'
        when 4 then 'RERANK'
        else 'INVALID'
      END AS TYPE,
      MODEL_NAME
    FROM oceanbase.__all_ai_model;
  """.replace("\n", " ")
)

def_table_schema(
  owner           = 'shenyunlong.syl',
  table_name      = 'DBA_OB_AI_MODEL_ENDPOINTS',
  table_id        = '21664',
  table_type      = 'SYSTEM_VIEW',
  rowkey_columns  = [],
  normal_columns  = [],
  gm_columns      = [],
  in_tenant_space = True,
  view_definition =
  """
    SELECT
      ENDPOINT_ID,
      ENDPOINT_NAME,
      AI_MODEL_NAME,
      SCOPE,
      URL,
      ACCESS_KEY,
      PROVIDER,
      REQUEST_MODEL_NAME,
      PARAMETERS,
      REQUEST_TRANSFORM_FN,
      RESPONSE_TRANSFORM_FN
    FROM oceanbase.__all_ai_model_endpoint WHERE ENDPOINT_ID != -1;
  """.replace("\n", " ")
)

def_table_schema(
  owner           = 'jingyu.cr',
  table_name      = 'DBA_OB_ROOTSERVICE_JOBS',
  table_id        = '21667',
  table_type      = 'SYSTEM_VIEW',
  gm_columns      = [],
  rowkey_columns  = [],
  normal_columns  = [],
  view_definition =
  """
    SELECT
      job_id AS JOB_ID,
      usec_to_time(gmt_create) AS GMT_CREATE,
      usec_to_time(gmt_modified) AS GMT_MODIFIED,
      job_type AS JOB_TYPE,
      job_status AS JOB_STATUS,
      result_code AS RESULT_CODE
    FROM oceanbase.__all_virtual_rootservice_job
  """.replace("\n", " ")
  )

def_table_schema(
  owner = 'xiebaoma.xbm',
  table_name      = 'DBA_OB_CHANGE_STREAM_REFRESH_STAT',
  table_id        = '21668',
  table_type      = 'SYSTEM_VIEW',
  gm_columns      = [],
  rowkey_columns  = [],
  normal_columns  = [],
  in_tenant_space = True,
  view_definition =
  """
    SELECT
      REFRESH_SCN,
      MIN_DEP_LSN,
      PENDING_TX_COUNT,
      FETCH_TX,
      FETCH_LSN,
      FETCH_SCN
    FROM oceanbase.__all_virtual_change_stream_refresh_stat
  """.replace("\n", " ")
)

# Reserved position (placeholder before this line)
# Placeholder suggestion for this section: Use the actual view name for placeholder
################################################################################
# End of MySQL System View (20000, 25000]
################################################################################
################################### Placeholder Notice ###################################
# Placeholder example: Write the comment at the beginning of the line, indicating which TABLE_ID to occupy and the corresponding name
# TABLE_ID: TABLE_NAME
#
# FARM will base the placeholder validation development branch TABLE_ID and TABLE_NAME match check, if they do not match, FARM will intercept and report an error
#
# Note:
# 0. Placeholder before 'reserved position'
# 1. Always start by occupying the master, ensuring the master branch is a superset of all other branches, to avoid NAME and ID conflicts
# 2. After the master placeholder is set, do not change NAME on the development branch, otherwise FARM will consider it an ID placeholder conflict. If this scenario occurs, you need to modify the master placeholder first
# 3. It is recommended to use the accurate TABLE_NAME for placeholder, TABLE_ID and TABLE_NAME are one-to-one corresponding within the system
# 4. Some tables are defined based on the schema of other base tables (e.g., gen_xx_table_def()), their actual table names are relatively complex, to facilitate placeholder usage, it is recommended to use the base table name for placeholders
#    - Example 1: def_table_schema(**gen_mysql_sys_agent_virtual_table_def('12393', all_def_keywords['__all_virtual_long_ops_status']))
#      * Base table name placeholder: # 12393: __all_virtual_long_ops_status
#      * Real table name placeholder: # 12393: __all_virtual_virtual_long_ops_status_mysql_sys_agent
#      * Base table name placeholder: # 15009: __all_virtual_sql_audit
#      * Real table name placeholder: # 15009: ALL_VIRTUAL_SQL_AUDIT
#    - Example 3: def_table_schema(**gen_sys_agent_virtual_table_def('15111', all_def_keywords['__all_routine_param']))
#      * Base table name placeholder: # 15111: __all_routine_param
#      * Real table name placeholder: # 15111: ALL_VIRTUAL_ROUTINE_PARAM_SYS_AGENT
# 5. Index table placeholder requirements TABLE_NAME should be used as follows: base table (data table) name, index name (index_name), actual index table name
#    For example: 100001 The placeholder method for the index table can be:
#       * # 100001: __idx_3_idx_data_table_id
#       * # 100001: idx_data_table_id
#       * # 100001: __all_table
################################################################################

################################################################################
# Extended System View (25000, 30000]
# Data Dictionary View (25000, 28000]
# Performance View (28000, 30000]
################################################################################

# 28275: GV$OB_RESULT_CACHE_OBJECTS
# 28276: V$OB_RESULT_CACHE_OBJECTS
# Reserved position (placeholder before this line)
# Placeholder suggestion for this section: Use the actual view name for placeholder
################################################################################
#### End of Performance View (28000, 30000]
################################################################################
################################### Placeholder Notice ###################################
# Placeholder example: Write comments at the beginning of the line to indicate which TABLE_ID is to be occupied and what the corresponding name is
# TABLE_ID: TABLE_NAME
#
# FARM will base the placeholder validation development branch TABLE_ID and TABLE_NAME matching check, if they do not match, FARM will intercept and report an error
#
# Note:
# 0. Placeholder before 'reserved position'
# 1. Always start by occupying the master, ensuring the master branch is a superset of all other branches, to avoid NAME and ID conflicts
# 2. After the master placeholder is set, do not change NAME on the development branch, otherwise FARM will consider it an ID placeholder conflict. If this scenario occurs, you need to modify the master placeholder first
# 3. It is recommended to use the accurate TABLE_NAME for placeholder, TABLE_ID and TABLE_NAME are one-to-one corresponding within the system
# 4. Some tables are defined based on the schema of other base tables (e.g., gen_xx_table_def()), their actual table names are relatively complex, to facilitate placeholder usage, it is recommended to use the base table name for placeholders
#    - Example 1: def_table_schema(**gen_mysql_sys_agent_virtual_table_def('12393', all_def_keywords['__all_virtual_long_ops_status']))
#      * Base table name placeholder: # 12393: __all_virtual_long_ops_status
#      * Real table name placeholder: # 12393: __all_virtual_virtual_long_ops_status_mysql_sys_agent
#      * Base table name placeholder: # 15009: __all_virtual_sql_audit
#      * Real table name placeholder: # 15009: ALL_VIRTUAL_SQL_AUDIT
#    - Example 3: def_table_schema(**gen_sys_agent_virtual_table_def('15111', all_def_keywords['__all_routine_param']))
#      * Base table name placeholder: # 15111: __all_routine_param
#      * Real table name placeholder: # 15111: ALL_VIRTUAL_ROUTINE_PARAM_SYS_AGENT
# 5. Index table placeholder requirements TABLE_NAME should be used as follows: base table (data table) name, index name (index_name), actual index table name
#    For example: 100001 The placeholder method for the index table can be:
#       * # 100001: __idx_3_idx_data_table_id
#       * # 100001: idx_data_table_id
#       * # 100001: __all_table
################################################################################


################################################################################
# Lob Table (50000, 70000)
################################################################################
# lob table id is correspond to its data_table_id, related schemas will be generated automatically.

################################################################################
# Sys table Index (100000, 200000)
# Index for core table (100000, 101000)
# Index for other sys table (101000, 200000)
################################################################################
# Index for core table (100000, 101000)
def_sys_index_table(
  index_name = 'idx_data_table_id',
  index_table_id = 100001,
  index_columns = ['data_table_id'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_table'])

def_sys_index_table(
  index_name = 'idx_db_tb_name',
  index_table_id = 100002,
  index_columns = ['database_id', 'table_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_table'])

def_sys_index_table(
  index_name = 'idx_tb_name',
  index_table_id = 100003,
  index_columns = ['table_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_table'])

def_sys_index_table(
  index_name = 'idx_tb_column_name',
  index_table_id = 100004,
  index_columns = ['table_id', 'column_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_column'])

def_sys_index_table(
  index_name = 'idx_column_name',
  index_table_id = 100005,
  index_columns = ['column_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_column'])

def_sys_index_table(
  index_name = 'idx_ddl_type',
  index_table_id = 100006,
  index_columns = ['operation_type', 'schema_version'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_ddl_operation'])


# Index for other sys table (100000, 101000)
def_sys_index_table(
  index_name = 'idx_data_table_id',
  index_table_id = 101001,
  index_columns = ['data_table_id'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_table_history'])
# 101002: __all_log_archive_piece_files # abandoned
# 101003: __all_backup_set_files # abandoned

def_sys_index_table(
  index_name = 'idx_task_key',
  index_table_id = 101004,
  index_columns = ['target_object_id', 'object_id', 'schema_version'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_UNIQUE_LOCAL',
  keywords = all_def_keywords['__all_ddl_task_status'])

def_sys_index_table(
  index_name = 'idx_ur_name',
  index_table_id = 101005,
  index_columns = ['user_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_user'])

def_sys_index_table(
  index_name = 'idx_db_name',
  index_table_id = 101006,
  index_columns = ['database_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_database'])

def_sys_index_table(
  index_name = 'idx_tg_name',
  index_table_id = 101007,
  index_columns = ['tablegroup_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_tablegroup'])

# 101008: idx_tenant_deleted(abandoned)

def_sys_index_table(
  index_name = 'idx_recyclebin_db_type',
  index_table_id = 101011,
  index_columns = ['database_id','type'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_recyclebin'])

def_sys_index_table(
  index_name = 'idx_part_name',
  index_table_id = 101012,
  index_columns = ['part_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_part'])

def_sys_index_table(
  index_name = 'idx_sub_part_name',
  index_table_id = 101013,
  index_columns = ['sub_part_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_sub_part'])

def_sys_index_table(
  index_name = 'idx_def_sub_part_name',
  index_table_id = 101014,
  index_columns = ['sub_part_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_def_sub_part'])

# 101017: idx_rs_job_type (abandoned)

def_sys_index_table(
  index_name = 'idx_fk_child_tid',
  index_table_id = 101018,
  index_columns = ['child_table_id'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_foreign_key'])

def_sys_index_table(
  index_name = 'idx_fk_parent_tid',
  index_table_id = 101019,
  index_columns = ['parent_table_id'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_foreign_key'])

def_sys_index_table(
  index_name = 'idx_fk_name',
  index_table_id = 101020,
  index_columns = ['foreign_key_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_foreign_key'])

def_sys_index_table(
  index_name = 'idx_fk_his_child_tid',
  index_table_id = 101021,
  index_columns = ['child_table_id', 'schema_version'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_foreign_key_history'])

def_sys_index_table(
  index_name = 'idx_fk_his_parent_tid',
  index_table_id = 101022,
  index_columns = ['parent_table_id', 'schema_version'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_foreign_key_history'])

def_sys_index_table(
  index_name = 'idx_ddl_checksum_task',
  index_table_id = 101025,
  index_columns = ['ddl_task_id'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_ddl_checksum'])

def_sys_index_table(
  index_name = 'idx_db_routine_name',
  index_table_id = 101026,
  index_columns = ['database_id', 'routine_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_routine'])

def_sys_index_table(
  index_name = 'idx_routine_name',
  index_table_id = 101027,
  index_columns = ['routine_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_routine'])

def_sys_index_table(
  index_name = 'idx_routine_pkg_id',
  index_table_id = 101028,
  index_columns = ['package_id'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_routine'])

def_sys_index_table(
  index_name = 'idx_routine_param_name',
  index_table_id = 101029,
  index_columns = ['param_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_routine_param'])

def_sys_index_table(
  index_name = 'idx_db_pkg_name',
  index_table_id = 101030,
  index_columns = ['database_id', 'package_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_package'])

def_sys_index_table(
  index_name = 'idx_pkg_name',
  index_table_id = 101031,
  index_columns = ['package_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_package'])

def_sys_index_table(
  index_name = 'idx_snapshot_tablet',
  index_table_id = 101032,
  index_columns = ['tablet_id'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_acquired_snapshot'])

def_sys_index_table(
  index_name = 'idx_cst_name',
  index_table_id = 101033,
  index_columns = ['constraint_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_constraint'])

def_sys_index_table(
  index_name = 'idx_grantee_role_id',
  index_table_id = 101040,
  index_columns = ['role_id'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_role_grantee_map'])

def_sys_index_table(
  index_name = 'idx_grantee_his_role_id',
  index_table_id = 101041,
  index_columns = ['role_id', 'schema_version'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_role_grantee_map_history'])

def_sys_index_table(
  index_name = 'idx_trigger_base_obj_id',
  index_table_id = 101054,
  index_columns = ['base_object_id'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_trigger'])

def_sys_index_table(
  index_name = 'idx_db_trigger_name',
  index_table_id = 101055,
  index_columns = ['database_id', 'trigger_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_trigger'])

def_sys_index_table(
  index_name = 'idx_trigger_name',
  index_table_id = 101056,
  index_columns = ['trigger_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_trigger'])

def_sys_index_table(
  index_name = 'idx_trigger_his_base_obj_id',
  index_table_id = 101057,
  index_columns = ['base_object_id', 'schema_version'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_trigger_history'])

def_sys_index_table(
  index_name = 'idx_objauth_grantor',
  index_table_id = 101058,
  index_columns = ['grantor_id'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_objauth'])

def_sys_index_table(
  index_name = 'idx_objauth_grantee',
  index_table_id = 101059,
  index_columns = ['grantee_id'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_objauth'])

def_sys_index_table(
  index_name = 'idx_dependency_ref_obj',
  index_table_id = 101063,
  index_columns = ['ref_obj_id', 'ref_obj_type'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_dependency'])

def_sys_index_table(
  index_name = 'idx_ddl_error_object',
  index_table_id = 101064,
  index_columns = ['object_id', 'target_object_id'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_ddl_error_message'])

def_sys_index_table(
  index_name = 'idx_table_stat_his_savtime',
  index_table_id = 101065,
  index_columns = ['savtime'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_table_stat_history'])

def_sys_index_table(
  index_name = 'idx_column_stat_his_savtime',
  index_table_id = 101066,
  index_columns = ['savtime'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_column_stat_history'])

def_sys_index_table(
  index_name = 'idx_histogram_stat_his_savtime',
  index_table_id = 101067,
  index_columns = ['savtime'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_histogram_stat_history'])

def_sys_index_table(
  index_name = 'idx_tablet_to_table_id',
  index_table_id = 101069,
  index_columns = ['table_id'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_tablet_to_table'])

def_sys_index_table(
  index_name = 'idx_ctx_namespace',
  index_table_id = 101071,
  index_columns = ['namespace'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_context'])

# 101072: idx_spm_item_sql_id abandoned
# 101073: idx_spm_item_value abandoned

def_sys_index_table(
  index_name = 'idx_directory_name',
  index_table_id = 101074,
  index_columns = ['directory_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_directory'])

def_sys_index_table(
  index_name = 'idx_job_powner',
  index_table_id = 101075,
  index_columns = ['powner'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_job'])

def_sys_index_table(
  index_name = 'idx_seq_obj_db_name',
  index_table_id = 101076,
  index_columns = ['database_id', 'sequence_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_sequence_object'])

def_sys_index_table(
  index_name = 'idx_seq_obj_name',
  index_table_id = 101077,
  index_columns = ['sequence_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_sequence_object'])

def_sys_index_table(
  index_name = 'idx_recyclebin_ori_name',
  index_table_id = 101078,
  index_columns = ['original_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_recyclebin'])

def_sys_index_table(
  index_name = 'idx_tb_priv_db_name',
  index_table_id = 101079,
  index_columns = ['database_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_table_privilege'])

def_sys_index_table(
  index_name = 'idx_tb_priv_tb_name',
  index_table_id = 101080,
  index_columns = ['table_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_table_privilege'])

def_sys_index_table(
  index_name = 'idx_db_priv_db_name',
  index_table_id = 101081,
  index_columns = ['database_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_database_privilege'])

# 101089: idx_tenant_snapshot_name (abandoned)

def_sys_index_table(
  index_name = 'idx_dbms_lock_allocated_lockhandle',
  index_table_id = 101090,
  index_columns = ['lockhandle'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_dbms_lock_allocated'])

def_sys_index_table(
  index_name = 'idx_dbms_lock_allocated_expiration',
  index_table_id = 101091,
  index_columns = ['expiration'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_dbms_lock_allocated'])

# 101093: idx_kv_ttl_task_table_id (abandoned)
# 101094: idx_kv_ttl_task_history_upd_time (abandoned)

# 101095: idx_mview_refresh_run_stats_num_mvs_current (removed)

# 101096: idx_mview_refresh_stats_end_time (removed)
# 101097: idx_mview_refresh_stats_mview_end_time (removed)

# 101099: idx_client_to_server_session_info_client_session_id (removed)

def_sys_index_table(
  index_name = 'idx_column_privilege_name',
  index_table_id = 101100,
  index_columns = ['user_id', 'database_name', 'table_name', 'column_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_column_privilege'])

def_sys_index_table(
  index_name = 'idx_catalog_name',
  index_table_id = 101113,
  index_columns = ['catalog_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_catalog'])

def_sys_index_table(
  index_name = 'idx_catalog_priv_catalog_name',
  index_table_id = 101114,
  index_columns = ['catalog_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_catalog_privilege'])

def_sys_index_table(
  index_name = 'idx_ccl_rule_id',
  index_table_id = 101115,
  index_columns = ['ccl_rule_id'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_ccl_rule'])

def_sys_index_table(
  index_name = 'idx_endpoint_name',
  index_table_id = 101116,
  index_columns = ['endpoint_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_UNIQUE_LOCAL',
  keywords = all_def_keywords['__all_ai_model_endpoint'])

def_sys_index_table(
  index_name = 'idx_ai_model_name',
  index_table_id = 101117,
  index_columns = ['ai_model_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_ai_model_endpoint'])

def_sys_index_table(
  index_name = 'idx_location_name',
  index_table_id = 101118,
  index_columns = ['location_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_location'])
def_sys_index_table(
  index_name = 'idx_objauth_mysql_user_id',
  index_table_id = 101119,
  index_columns = ['user_id'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_objauth_mysql'])
def_sys_index_table(
  index_name = 'idx_objauth_mysql_obj_name',
  index_table_id = 101120,
  index_columns = ['obj_name'],
  index_using_type = 'USING_BTREE',
  index_type = 'INDEX_TYPE_NORMAL_LOCAL',
  keywords = all_def_keywords['__all_objauth_mysql'])


# Reserved position (placeholder before this line)
# Index table placeholder suggestion: based on the base table (data table) name for placeholder, other methods include: index name (index_name), index table name
################################################################################
# End of Sys table Index (100000, 200000)
#     Index for core table (100000, 101000)
#     Index for other sys table (101000, 200000)
################################################################################
################################### Placeholder Notice ###################################
# Placeholder example: Write the comment at the beginning of the line, indicating which TABLE_ID to occupy and the corresponding name
# TABLE_ID: TABLE_NAME
#
# FARM will base the placeholder validation development branch TABLE_ID and TABLE_NAME matching check, if they do not match, FARM will intercept and report an error
#
# Note:
# 0. Placeholder before 'reserved position'
# 1. Always start by reserving the master, ensuring the master branch is a superset of all other branches to avoid NAME and ID conflicts
# 2. After the master placeholder is set, do not change NAME on the development branch, otherwise FARM will consider it an ID placeholder conflict. If this scenario occurs, you need to modify the master placeholder first
# 3. It is recommended to use the accurate TABLE_NAME for placeholder, TABLE_ID and TABLE_NAME are one-to-one corresponding within the system
# 4. Some tables are defined based on the schema of other base tables (e.g., gen_xx_table_def()), their actual table names are relatively complex, to facilitate placeholder usage, it is recommended to use the base table name for placeholders
#    - Example 1: def_table_schema(**gen_mysql_sys_agent_virtual_table_def('12393', all_def_keywords['__all_virtual_long_ops_status']))
#      * Base table name placeholder: # 12393: __all_virtual_long_ops_status
#      * Real table name placeholder: # 12393: __all_virtual_virtual_long_ops_status_mysql_sys_agent
#      * Base table name placeholder: # 15009: __all_virtual_sql_audit
#      * Real table name placeholder: # 15009: ALL_VIRTUAL_SQL_AUDIT
#    - Example 3: def_table_schema(**gen_sys_agent_virtual_table_def('15111', all_def_keywords['__all_routine_param']))
#      * Base table name placeholder: # 15111: __all_routine_param
#      * Real table name placeholder: # 15111: ALL_VIRTUAL_ROUTINE_PARAM_SYS_AGENT
# 5. Index table placeholder requirements TABLE_NAME should be used as follows: base table (data table) name, index name (index_name), actual index table name
#    For example: 100001 The placeholder method for the index table can be:
#       * # 100001: __idx_3_idx_data_table_id
#       * # 100001: idx_data_table_id
#       * # 100001: __all_table
################################################################################

################################################################################
# Agent table Index
# End Agent table Index
################################################################################
