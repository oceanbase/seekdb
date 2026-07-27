#!/bin/env python3
# -*- coding: utf-8 -*-

# Copyright 2014 - 2018 Alibaba Inc. All Rights Reserved.
# Author:
#  config file is ob_inner_table_schema_def.py
#  shell> python3 generate_inner_table_schema.py
#

import argparse
import copy
from collections import OrderedDict
from ob_inner_table_init_data import *
import io
import re
import os
import glob
import sys

script_dir = os.path.dirname(os.path.abspath(__file__))
share_output_dir = script_dir
observer_output_dir = os.path.join(os.path.dirname(os.path.dirname(script_dir)), 'observer', 'virtual_table')
generated_schema_cpp_files = []
schema_cpp_handles = {}
quiet_mode = False
verbose_mode = False

kv_core_table_id         = int(1)
max_core_table_id        = int(100)
max_sys_table_id         = int(10000)
max_ob_virtual_table_id  = int(15000)
max_ora_virtual_table_id = int(20000)
max_mysql_sys_view_id    = int(25000)
max_sys_view_id          = int(30000)
base_lob_meta_table_id   = int(50000)
base_lob_piece_table_id  = int(60000)
max_lob_table_id         = int(70000)
min_sys_index_id         = int(100000)
max_core_index_id        = int(101000)
max_sys_index_id         = int(200000)

min_shadow_column_id     = int(32767)

def is_core_table(table_id):
  table_id = int(table_id)
  return table_id > 0 and table_id < max_core_table_id

def is_sys_table(table_id):
  table_id = int(table_id)
  return table_id > 0 and table_id < max_sys_table_id

def is_mysql_virtual_table(table_id):
  table_id = int(table_id)
  return table_id > max_sys_table_id and table_id < max_ob_virtual_table_id

def is_extended_virtual_table(table_id):
  table_id = int(table_id)
  return table_id > max_ob_virtual_table_id and table_id < max_ora_virtual_table_id

def is_extended_sys_view(table_id):
  table_id = int(table_id)
  return table_id > max_mysql_sys_view_id and table_id < max_sys_view_id

def is_virtual_table(table_id):
  table_id = int(table_id)
  return table_id > max_sys_table_id and table_id < max_ora_virtual_table_id

def is_sys_view(table_id):
  table_id = int(table_id)
  return table_id > max_ora_virtual_table_id and table_id < max_sys_view_id

def is_lob_table(table_id):
  table_id = int(table_id)
  return (table_id > base_lob_meta_table_id) and (table_id < max_lob_table_id)

def is_core_index_table(table_id):
  table_id = int(table_id)
  return (table_id > min_sys_index_id) and (table_id < max_core_index_id)

def is_sys_index_table(table_id):
  table_id = int(table_id)
  return (table_id > min_sys_index_id) and (table_id < max_sys_index_id)

new_keywords = {}
cpp_f = None
h_f = None
fileds = None
default_filed_values = None
table_name_ids = []
table_name_postfix_ids = []
table_name_postfix_table_names = []
index_name_ids = []
table_index=[]
lob_aux_ids = []
runtime_space_tables = []
runtime_space_table_names = []
cluster_distributed_vtables = []
column_def_enum_array = []
all_def_keywords = OrderedDict()
all_agent_virtual_tables = []
all_iterate_virtual_tables = []
all_iterate_private_virtual_tables = []
all_sqlite_tables = []
all_sqlite_virtual_tables = []
cluster_private_tables = []
core_related_tables = []
all_only_sys_table_name = {}
mysql_compat_agent_tables = {}
column_collation = 'CS_TYPE_INVALID'
# Virtual tables accessible only from the system runtime or through system views.
restrict_access_virtual_tables = []
is_extended_sys_table = False
sys_index_tables = []
copyright = """/*
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
"""

def share_out_path(*parts):
  return os.path.join(share_output_dir, *parts)

def observer_out_path(*parts):
  return os.path.join(observer_output_dir, *parts)

def log_info(*args, **kwargs):
  if not quiet_mode:
    print(*args, **kwargs)

def log_debug(*args, **kwargs):
  if verbose_mode and not quiet_mode:
    print(*args, **kwargs)

def parse_args(argv):
  parser = argparse.ArgumentParser(description='Generate inner table schema sources')
  parser.add_argument('--def-file',
                      default=os.path.join(script_dir, 'ob_inner_table_schema_def.py'),
                      help='Path to the schema definition Python file')
  parser.add_argument('--quiet',
                      action='store_true',
                      help='Suppress non-error output')
  parser.add_argument('--verbose',
                      action='store_true',
                      help='Enable detailed debug output')
  return parser.parse_args(argv)

def configure_paths(args):
  global quiet_mode
  global verbose_mode
  quiet_mode = args.quiet
  verbose_mode = args.verbose
  os.makedirs(share_output_dir, exist_ok=True)
  os.makedirs(observer_output_dir, exist_ok=True)
  
def print_method_start(table_name):
  global cpp_f
  head = """int ObInnerTableSchema::{0}_schema(ObTableSchema &table_schema)
{{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
"""
  # use {{ for string.foramt. string.format will translate {{ to {
  cpp_f.write(head.format(table_name.replace('$', '_').lower().strip('_')))

def add_method_end():
  global cpp_f
  end = """
  table_schema.set_max_used_column_id(column_id);
  return ret;
}

"""
  cpp_f.write(end)

def add_index_method_end(num_index):
  global cpp_f
  end = """
  table_schema.set_max_used_column_id(column_id + {0});
  return ret;
}}

"""
  cpp_f.write(end.format(str(num_index)))

def print_default_column(column_name, rowkey_id, index_id, part_key_pos, column_type, column_collation_type, column_length, column_precision, column_scale, is_nullable, is_autoincrement, default_value, column_id, is_hidden, is_storing_column):
  global cpp_f
  set_op = "";
  if "NULL" == default_value or "null" == default_value:
    set_op = 'set_null()'
  elif column_type == 'ObIntType':
    set_op = 'set_int({0})'.format(default_value)
  elif column_type == 'ObUInt64Type':
    set_op = 'set_uint64({0})'.format(default_value)
  elif column_type == 'ObTinyIntType':
    set_op = 'set_tinyint({0})'.format(default_value)
  elif column_type == 'ObVarcharType':
      if column_collation_type == "CS_TYPE_BINARY":
        set_op = 'set_varbinary(ObString::make_string("{0}"))'.format(default_value)
      else:
        set_op = 'set_varchar(ObString::make_string("{0}"))'.format(default_value)
  elif column_type == 'ObTimestampType':
    if (default_value == 'CURRENT_TIMESTAMP') or (default_value == 'current_timestmap'):
      set_op = 'set_timestamp(ObTimeUtility::current_time())'
    else:
      set_op = 'set_timestamp({0})'.format(default_value)
  elif column_type == 'ObLongTextType':
    set_op = 'set_lob_value(ObLongTextType, "{0}", static_cast<int32_t>(strlen("{0}")))'.format(default_value)
    if column_collation_type == "CS_TYPE_BINARY":
      set_op += '; {0}_default.set_collation_type(CS_TYPE_BINARY);'.format(column_name.lower())
  else:
    raise IOError("ERROR column format: column_name={0} column_type={1}\n".format(column_name, column_type))
  if is_hidden == 'true' or is_storing_column == 'true':
    if column_id != 0:
      ## index
      line = """
  if (OB_SUCC(ret)) {{
    ObObj {12}_default;
    {12}_default.{13};
    ADD_COLUMN_SCHEMA_T_WITH_COLUMN_FLAGS("{0}", //column_name
      column_id + {1}, //column_id
      {2}, //rowkey_id
      {3}, //index_id
      {4}, //part_key_pos
      {5}, //column_type
      {6}, //column_collation_type
      {7}, //column_length
      {8}, //column_precision
      {9}, //column_scale
      {10}, //is_nullable
      {11}, //is_autoincrement
      {12}_default,
      {12}_default, //default_value
      {14}, //is_hidden
      {15}); //is_storing_column 
  }}
"""
      cpp_f.write(line.format(column_name, column_id, rowkey_id, index_id, part_key_pos, column_type, column_collation_type, column_length, column_precision, column_scale, is_nullable, is_autoincrement, column_name.lower(), set_op, is_hidden, is_storing_column))
    else:
      line = """
  if (OB_SUCC(ret)) {{
    ObObj {11}_default;
    {11}_default.{12};
    ADD_COLUMN_SCHEMA_T_WITH_COLUMN_FLAGS("{0}", //column_name
      ++column_id, //column_id
      {1}, //rowkey_id
      {2}, //index_id
      {3}, //part_key_pos
      {4}, //column_type
      {5}, //column_collation_type
      {6}, //column_length
      {7}, //column_precision
      {8}, //column_scale
      {9}, //is_nullable
      {10}, //is_autoincrement
      {11}_default,
      {11}_default, //default_value
      {13}, //is_hidden
      {14}); //is_storing_column
  }}
"""
      cpp_f.write(line.format(column_name, rowkey_id, index_id, part_key_pos, column_type, column_collation_type, column_length, column_precision, column_scale, is_nullable, is_autoincrement, column_name.lower(),set_op, is_hidden, is_storing_column))
  else:
    if column_id != 0:
      ## index
      line = """
  if (OB_SUCC(ret)) {{
    ObObj {12}_default;
    {12}_default.{13};
    ADD_COLUMN_SCHEMA_T("{0}", //column_name
      column_id + {1}, //column_id
      {2}, //rowkey_id
      {3}, //index_id
      {4}, //part_key_pos
      {5}, //column_type
      {6}, //column_collation_type
      {7}, //column_length
      {8}, //column_precision
      {9}, //column_scale
      {10}, //is_nullable
      {11}, //is_autoincrement
      {12}_default,
      {12}_default); //default_value
  }}
"""
      cpp_f.write(line.format(column_name, column_id, rowkey_id, index_id, part_key_pos, column_type, column_collation_type, column_length, column_precision, column_scale, is_nullable, is_autoincrement, column_name.lower(), set_op))
    else:
      line = """
  if (OB_SUCC(ret)) {{
    ObObj {11}_default;
    {11}_default.{12};
    ADD_COLUMN_SCHEMA_T("{0}", //column_name
      ++column_id, //column_id
      {1}, //rowkey_id
      {2}, //index_id
      {3}, //part_key_pos
      {4}, //column_type
      {5}, //column_collation_type
      {6}, //column_length
      {7}, //column_precision
      {8}, //column_scale
      {9}, //is_nullable
      {10}, //is_autoincrement
      {11}_default,
      {11}_default); //default_value
  }}
"""
      cpp_f.write(line.format(column_name, rowkey_id, index_id, part_key_pos, column_type, column_collation_type, column_length, column_precision, column_scale, is_nullable, is_autoincrement, column_name.lower(),set_op))
    
def print_column(column_name, rowkey_id, index_id, part_key_pos, column_type, column_collation_type, column_length, column_precision, column_scale, is_nullable, is_autoincrement, column_id, is_hidden, is_storing_column):
  global cpp_f

  if is_hidden == 'true' or is_storing_column == 'true':
    if column_id != 0:
      ## index
      line = """
  if (OB_SUCC(ret)) {{
    ADD_COLUMN_SCHEMA_WITH_COLUMN_FLAGS("{0}", //column_name
      column_id + {1}, //column_id
      {2}, //rowkey_id
      {3}, //index_id
      {4}, //part_key_pos
      {5}, //column_type
      {6}, //column_collation_type
      {7}, //column_length
      {8}, //column_precision
      {9}, //column_scale
      {10},//is_nullable
      {11},//is_autoincrement
      {12},//is_hidden
      {13});//is_storing_column
  }}
"""
      cpp_f.write(line.format(column_name, column_id, rowkey_id, index_id, part_key_pos, column_type, column_collation_type, column_length, column_precision, column_scale, is_nullable, is_autoincrement,is_hidden ,is_storing_column))
    else:
      line = """
  if (OB_SUCC(ret)) {{
    ADD_COLUMN_SCHEMA_WITH_COLUMN_FLAGS("{0}", //column_name
      ++column_id, //column_id
      {1}, //rowkey_id
      {2}, //index_id
      {3}, //part_key_pos
      {4}, //column_type
      {5}, //column_collation_type
      {6}, //column_length
      {7}, //column_precision
      {8}, //column_scale
      {9}, //is_nullable
      {10},//is_autoincrement
      {11},//is_hidden
      {12});//is_storing_column 
  }}
"""
      cpp_f.write(line.format(column_name, rowkey_id, index_id, part_key_pos, column_type, column_collation_type, column_length, column_precision, column_scale, is_nullable, is_autoincrement,is_hidden ,is_storing_column))
  else:
    if column_id != 0:
      ## index
      line = """
  if (OB_SUCC(ret)) {{
    ADD_COLUMN_SCHEMA("{0}", //column_name
      column_id + {1}, //column_id
      {2}, //rowkey_id
      {3}, //index_id
      {4}, //part_key_pos
      {5}, //column_type
      {6}, //column_collation_type
      {7}, //column_length
      {8}, //column_precision
      {9}, //column_scale
      {10},//is_nullable
      {11}); //is_autoincrement
  }}
"""
      cpp_f.write(line.format(column_name, column_id, rowkey_id, index_id, part_key_pos, column_type, column_collation_type, column_length, column_precision, column_scale, is_nullable, is_autoincrement))
    else:
      line = """
  if (OB_SUCC(ret)) {{
    ADD_COLUMN_SCHEMA("{0}", //column_name
      ++column_id, //column_id
      {1}, //rowkey_id
      {2}, //index_id
      {3}, //part_key_pos
      {4}, //column_type
      {5}, //column_collation_type
      {6}, //column_length
      {7}, //column_precision
      {8}, //column_scale
      {9}, //is_nullable
      {10}); //is_autoincrement
  }}
"""
      cpp_f.write(line.format(column_name, rowkey_id, index_id, part_key_pos, column_type, column_collation_type, column_length, column_precision, column_scale, is_nullable, is_autoincrement))

    
def print_discard_column(column_name):
  global cpp_f
  line = """
  if (OB_SUCC(ret)) {{
    ++column_id; // for {0}
  }}
"""
  cpp_f.write(line.format(column_name))


def print_timestamp_column(column_name, rowkey_id, index_id, part_key_pos, column_type, column_collation_type, column_length, column_precision, column_scale, is_nullable, is_autoincrement, column_id, is_on_update_for_timestamp,is_hidden, is_storing_column):
  global cpp_f
  if rowkey_id > 0:
    is_nullable = "false"

  if is_hidden == 'true' or is_storing_column == 'true':
    if column_id != 0:
      if int(column_scale) > 0 :
        line = """
  if (OB_SUCC(ret)) {{
    ObObj gmt_default;
    ObObj gmt_default_null;

    gmt_default.set_ext(ObActionFlag::OP_DEFAULT_NOW_FLAG);
    gmt_default_null.set_null();
    ADD_COLUMN_SCHEMA_TS_T_WITH_COLUMN_FLAGS("{0}", //column_name
      column_id + {1}, //column_id
      {2}, //rowkey_id
      {3}, //index_id
      {4}, //part_key_pos
      {5}, //column_type
      {6}, //column_collation_type
      {7}, //column_length
      {8}, //column_precision
      {9}, //column_scale
      {10}, //is_nullable
      {11}, //is_autoincrement
      {12}, //is_on_update_for_timestamp
      gmt_default_null,
      gmt_default,
      {13}, //is_hidden
      {14}); //is_storing_column
  }}
"""
      else :
        line = """
  if (OB_SUCC(ret)) {{
    ADD_COLUMN_SCHEMA_TS_WITH_COLUMN_FLAGS("{0}", //column_name
      column_id + {1}, //column_id
      {2}, //rowkey_id
      {3}, //index_id
      {4}, //part_key_pos
      {5}, //column_type
      {6}, //column_collation_type
      {7}, //column_length
      {8}, //column_precision
      {9}, //column_scale
      {10}, //is_nullable
      {11}, //is_autoincrement
      {12}, //is_on_update_for_timestamp
      {13}, //is_hidden
      {14});//is_storing_column 
  }}
"""
      cpp_f.write(line.format(column_name, column_id, rowkey_id, index_id, part_key_pos, column_type, column_collation_type, column_length, column_precision, column_scale, is_nullable, is_autoincrement, is_on_update_for_timestamp,is_hidden, is_storing_column))
    else:
      if int(column_scale) > 0 :
        line = """
  if (OB_SUCC(ret)) {{
    ObObj gmt_default;
    ObObj gmt_default_null;

    gmt_default.set_ext(ObActionFlag::OP_DEFAULT_NOW_FLAG);
    gmt_default_null.set_null();
    ADD_COLUMN_SCHEMA_TS_T_WITH_COLUMN_FLAGS("{0}", //column_name
      ++column_id, //column_id
      {1}, //rowkey_id
      {2}, //index_id
      {3}, //part_key_pos
      {4}, //column_type
      {5}, //column_collation_type
      {6}, //column_length
      {7}, //column_precision
      {8}, //column_scale
      {9}, //is_nullable
      {10}, //is_autoincrement
      {11}, //is_on_update_for_timestamp
      gmt_default_null,
      gmt_default,
      {12}, //is_hidden
      {13})//is_storing_column
  }}
"""
      else :
        line = """
  if (OB_SUCC(ret)) {{
    ADD_COLUMN_SCHEMA_TS_WITH_COLUMN_FLAGS("{0}", //column_name
      ++column_id, //column_id
      {1}, //rowkey_id
      {2}, //index_id
      {3}, //part_key_pos
      {4}, //column_type
      {5}, //column_collation_type
      {6}, //column_length
      {7}, //column_precision
      {8}, //column_scale
      {9}, //is_nullable
      {10}, //is_autoincrement
      {11}, //is_on_update_for_timestamp
      {12}, //is_hidden
      {13});//is_storing_column 
  }}
"""
      cpp_f.write(line.format(column_name, rowkey_id, index_id, part_key_pos, column_type, column_collation_type, column_length, column_precision, column_scale, is_nullable, is_autoincrement, is_on_update_for_timestamp, is_hidden, is_storing_column))
  else:
    if column_id != 0:
      if int(column_scale) > 0 :
        line = """
  if (OB_SUCC(ret)) {{
    ObObj gmt_default;
    ObObj gmt_default_null;

    gmt_default.set_ext(ObActionFlag::OP_DEFAULT_NOW_FLAG);
    gmt_default_null.set_null();
    ADD_COLUMN_SCHEMA_TS_T("{0}", //column_name
      column_id + {1}, //column_id
      {2}, //rowkey_id
      {3}, //index_id
      {4}, //part_key_pos
      {5}, //column_type
      {6}, //column_collation_type
      {7}, //column_length
      {8}, //column_precision
      {9}, //column_scale
      {10}, //is_nullable
      {11}, //is_autoincrement
      {12}, //is_on_update_for_timestamp
      gmt_default_null,
      gmt_default);
  }}
"""
      else :
        line = """
  if (OB_SUCC(ret)) {{
    ADD_COLUMN_SCHEMA_TS("{0}", //column_name
      column_id + {1}, //column_id
      {2}, //rowkey_id
      {3}, //index_id
      {4}, //part_key_pos
      {5}, //column_type
      {6}, //column_collation_type
      {7}, //column_length
      {8}, //column_precision
      {9}, //column_scale
      {10}, //is_nullable
      {11}, //is_autoincrement
      {12}); //is_on_update_for_timestamp
  }}
"""
      cpp_f.write(line.format(column_name, column_id, rowkey_id, index_id, part_key_pos, column_type, column_collation_type, column_length, column_precision, column_scale, is_nullable, is_autoincrement, is_on_update_for_timestamp))
    else:
      if int(column_scale) > 0 :
        line = """
  if (OB_SUCC(ret)) {{
    ObObj gmt_default;
    ObObj gmt_default_null;

    gmt_default.set_ext(ObActionFlag::OP_DEFAULT_NOW_FLAG);
    gmt_default_null.set_null();
    ADD_COLUMN_SCHEMA_TS_T("{0}", //column_name
      ++column_id, //column_id
      {1}, //rowkey_id
      {2}, //index_id
      {3}, //part_key_pos
      {4}, //column_type
      {5}, //column_collation_type
      {6}, //column_length
      {7}, //column_precision
      {8}, //column_scale
      {9}, //is_nullable
      {10}, //is_autoincrement
      {11}, //is_on_update_for_timestamp
      gmt_default_null,
      gmt_default)
  }}
"""
      else :
        line = """
  if (OB_SUCC(ret)) {{
    ADD_COLUMN_SCHEMA_TS("{0}", //column_name
      ++column_id, //column_id
      {1}, //rowkey_id
      {2}, //index_id
      {3}, //part_key_pos
      {4}, //column_type
      {5}, //column_collation_type
      {6}, //column_length
      {7}, //column_precision
      {8}, //column_scale
      {9}, //is_nullable
      {10}, //is_autoincrement
      {11}); //is_on_update_for_timestamp
  }}
"""
      cpp_f.write(line.format(column_name, rowkey_id, index_id, part_key_pos, column_type, column_collation_type, column_length, column_precision, column_scale, is_nullable, is_autoincrement, is_on_update_for_timestamp))


def add_gm_columns(columns):
  global cpp_f
  column_length = 'sizeof(int64_t)'
  is_nullable = 'false'
  line = "error"
  for column in columns:
    if column == 'gmt_create':
      line = """
  if (OB_SUCC(ret)) {
    ObObj gmt_create_default;
    ObObj gmt_create_default_null;

    gmt_create_default.set_ext(ObActionFlag::OP_DEFAULT_NOW_FLAG);
    gmt_create_default_null.set_null();
    ADD_COLUMN_SCHEMA_TS_T("gmt_create", //column_name
      ++column_id, //column_id
      0, //rowkey_id
      0, //index_id
      0, //part_key_pos
      ObTimestampType,  //column_type
      CS_TYPE_BINARY,//collation_type
      0, //column length
      -1, //column_precision
      6, //column_scale
      true,//is nullable
      false, //is_autoincrement
      false, //is_on_update_for_timestamp
      gmt_create_default_null,
      gmt_create_default)
  }
"""
    elif column == 'gmt_modified':
      line = """
  if (OB_SUCC(ret)) {
    ObObj gmt_modified_default;
    ObObj gmt_modified_default_null;

    gmt_modified_default.set_ext(ObActionFlag::OP_DEFAULT_NOW_FLAG);
    gmt_modified_default_null.set_null();
    ADD_COLUMN_SCHEMA_TS_T("gmt_modified", //column_name
      ++column_id, //column_id
      0, //rowkey_id
      0, //index_id
      0, //part_key_pos
      ObTimestampType,  //column_type
      CS_TYPE_BINARY,//collation_type
      0, //column length
      -1, //column_precision
      6, //column_scale
      true,//is nullable
      false, //is_autoincrement
      true, //is_on_update_for_timestamp
      gmt_modified_default_null,
      gmt_modified_default)
  }
"""
    cpp_f.write(line)

def add_column(column, rowkey_id, index_id, part_key_pos, column_id=0, is_hidden='false', is_storing_column='false'):
  global column_collation
  global is_extended_sys_table
  column_name = None
  column_type = None
  column_collation_type = 'CS_TYPE_INVALID';
  column_length = None
  column_precision = -1
  column_scale = -1
  is_nullable = "false"
  is_autoincrement = "false"
  is_on_update_for_timestamp = "false"
  default_value = None
  if len(column) >= 2:
    column_name = column[0]
    column_type = column[1]
    if column_type == 'int':
      column_type = 'ObIntType'
      column_length = 'sizeof(int64_t)'
    elif column_type[:6] == 'bigint':
      s = column_type.split(':')
      column_type = 'ObIntType'
      column_length = 'sizeof(int64_t)'
      column_precision = 20 if len(s) == 1 else s[1]
      column_scale = 0
    elif column_type[:6] == 'number':
      s = column_type.split(':')
      column_type = 'ObFloatType' if len(s) == 1 else 'ObNumberType'
      column_length = 'sizeof(float)' if len(s) == 1 else 38
      column_precision = -1 if len(s) == 1 else s[1]
      if len(s) == 3:
        column_scale = s[2]
      elif len(s) == 2:
        column_scale = 0
    elif column_type == 'uint':
      column_type = 'ObUInt64Type'
      column_length = 'sizeof(uint64_t)'
    elif column_type == 'uint32':
      column_type = 'ObUInt32Type'
      column_length = 'sizeof(uint32_t)'
    elif column_type == 'double':
      column_type = 'ObDoubleType'
      column_length = 'sizeof(double)'
    elif column_type == 'bool':
      column_type = 'ObTinyIntType'
      column_length = '1'
    elif column_type[:9] == 'timestamp':
      s = column_type.split(':')
      column_type = 'ObTimestampType'
      column_length = 'sizeof(ObPreciseDateTime)'
      if len(s) != 1:
        column_scale = s[1]
        column_length = 0
    elif column_type[:7]  == 'varchar':
      s = column_type.split(':')
      column_type = 'ObVarcharType'
      column_length = s[1]
      if True == is_extended_sys_table:
        column_precision = 2
      column_collation_type = column_collation;
    elif column_type[:9]  == 'varbinary':
      s = column_type.split(':')
      column_type = 'ObVarcharType'
      column_length = s[1]
      column_collation_type = 'CS_TYPE_BINARY'
    elif column_type == 'otimestamp':
      column_type = 'ObTimestampLTZType'
      column_length = 0
    elif column_type == 'longtext':
      column_type = 'ObLongTextType'
      column_length = 0
    elif column_type == 'longblob':
      column_type = 'ObLongTextType'
      column_length = 0
      column_collation_type = 'CS_TYPE_BINARY'
    elif column_type == 'datetime':
      column_type = 'ObDateTimeType'
      column_length = 0

  if len(column) >= 3:
    is_nullable = column[2]

  if len(column) >= 4:
    default_value = column[3]

  if len(column) >= 5:
    is_autoincrement = column[4]

  if len(column) >= 6:
    is_on_update_for_timestamp = column[5]

  log_debug(column_name, rowkey_id, index_id, part_key_pos, column_type, column_length, is_nullable, is_autoincrement, default_value)
  if column_name.find("[discard]") == 0:
    print_discard_column(column_name)
  elif column_type == 'ObTimestampType':
    print_timestamp_column(column_name, rowkey_id, index_id, part_key_pos, column_type, column_collation_type, column_length, column_precision, column_scale, is_nullable, is_autoincrement, column_id, is_on_update_for_timestamp, is_hidden, is_storing_column)
  elif default_value is None:
    print_column(column_name, rowkey_id, index_id, part_key_pos, column_type, column_collation_type, column_length, column_precision, column_scale, is_nullable, is_autoincrement, column_id, is_hidden, is_storing_column)
  else:
    print_default_column(column_name, rowkey_id, index_id, part_key_pos, column_type, column_collation_type, column_length, column_precision, column_scale, is_nullable, is_autoincrement, default_value, column_id, is_hidden, is_storing_column)

def no_direct_access(keywords):
  global restrict_access_virtual_tables
  tid = table_name2tid(keywords['table_name'] + ('name_postfix' in keywords and keywords['name_postfix'] or ''))
  if tid not in restrict_access_virtual_tables:
    restrict_access_virtual_tables.append(tid)
  return keywords

def find_column_def(keywords, column_name, is_shadow_pk_column):
  i = 1
  if is_shadow_pk_column:
    # for shadow_pk_column, need to find the mapping rowkey column in main table,
    # and use the rowkey column's column_id and data_type.
    for col in keywords['rowkey_columns']:
      if 'shadow_pk_' + str(i - 1) == column_name:
        return (i + min_shadow_column_id, (column_name, col[1], 'true'))
      else:
        i += 1
  else:
    for col in keywords['rowkey_columns']:
      if col[0].upper() == column_name.upper():
        return (i, col)
      else:
        i += 1
    for col in keywords['normal_columns']:
      if col[0].upper() == column_name.upper():
        return (i, col)
      else:
        i += 1

# For specified index column, rowkey_position != 0 and index_position != 0.
def add_index_column(keywords, rowkey_id, index_id, column):
  (idx, column_def) = find_column_def(keywords, column, False)
  add_column(column_def, rowkey_id, index_id, 0, idx)
  return idx

# For shadow pk column, rowkey_position != 0 and index_position = 0.
def add_shadow_pk_column(keywords, rowkey_id, column):
  (idx, column_def) = find_column_def(keywords, column, True)
  add_column(column_def, rowkey_id, 0, 0, idx, is_hidden='true')
  return idx

def add_index_columns(columns, **keywords):
  rowkey_id = 1
  is_unique_index = keywords['index_type'] == 'INDEX_TYPE_UNIQUE_LOCAL' or keywords['index_type'] == 'INDEX_TYPE_HEAP_ORGANIZED_TABLE_PRIMARY'
  max_used_column_idx = 1
  if is_unique_index:
    # specified index columns.
    for column in columns:
      column_idx = add_index_column(keywords, rowkey_id, rowkey_id, column)
      max_used_column_idx = max(max_used_column_idx, column_idx)
      rowkey_id += 1

    # generate shadow pk column whose number equals to rowkeys' number.
    shadow_pk_col_idx = 0
    for col in keywords['rowkey_columns']:
      shadow_pk_col_name = 'shadow_pk_' + str(shadow_pk_col_idx)
      column_idx = add_shadow_pk_column(keywords, rowkey_id, shadow_pk_col_name)
      max_used_column_idx = max(max_used_column_idx, column_idx)
      rowkey_id += 1
      shadow_pk_col_idx += 1

    # rowkey columns of main table are normal in the unique index table.
    # i.e., rowkey_position = 0, index_position = 0.
    for col in keywords['rowkey_columns']:
      if col[0].upper() not in [x.upper() for x in columns]:
        (idx, column_def) = find_column_def(keywords, col[0], False)
        add_column(column_def, 0, 0, 0, idx)
        max_used_column_idx = max(max_used_column_idx, idx)
  else:
    for column in columns:
      column_idx = add_index_column(keywords, rowkey_id, rowkey_id, column)
      max_used_column_idx = max(max_used_column_idx, column_idx)
      rowkey_id += 1
    for col in keywords['rowkey_columns']:
      if col[0].upper() not in [x.upper() for x in columns]:
        column_idx = add_index_column(keywords, rowkey_id, 0, col[0])
        max_used_column_idx = max(max_used_column_idx, column_idx)
        rowkey_id += 1
  return max_used_column_idx

def add_rowkey_columns(columns, *args):
  rowkey_id = 1
  index_id = 0
  if 0 < len(args):
    partition_columns = args[0]

  for column in columns:
    column_name = column[0]
    if column_name in partition_columns:
      part_key_pos = partition_columns.index(column_name)
      add_column(column, rowkey_id, index_id, part_key_pos + 1)
    else:
      add_column(column, rowkey_id, index_id, 0)
    rowkey_id += 1

def add_normal_columns(columns, *args):
  rowkey_id = 0
  index_id = 0
  partition_columns = []
  if 0 < len(args):
    partition_columns = args[0]

  for column in columns:
    column_name = column[0]
    if column_name in partition_columns:
      part_key_pos = partition_columns.index(column_name)
      add_column(column, rowkey_id, index_id, part_key_pos + 1)
    else:
      add_column(column, rowkey_id, index_id, 0)

def add_storing_column(keywords, column_name):
  (idx, column_def) = find_column_def(keywords, column_name, False)
  add_column(column_def, 0, 0, 0, idx, is_hidden='false' ,is_storing_column='true')
  return idx
def add_storing_columns(columns, max_used_column_idx, **keywords):
  for column_name in columns:
    max_used_column_idx = max(max_used_column_idx, add_storing_column(keywords, column_name))
  return max_used_column_idx

def add_field(kw, value):
  global cpp_f
  line = "  table_schema.set_{0}({1});\n".format(kw, value)
  cpp_f.write(line)

def add_char_field(kw, value):
  global cpp_f
  field = "table_schema.{0}".format(kw)
  line = """
  if (OB_SUCC(ret)) {{
    if (OB_FAIL(table_schema.set_{0}({2}))) {{
      LOG_ERROR("fail to set {0}", K(ret));
    }}
  }}
"""

  cpp_f.write(line.format(kw, field, value))

def add_list_partition_expr_field(value):
  global cpp_f
  type_str = ''
  expr_str = ''
  if 2 != len(value):
    raise IOError("partition_expr should in format [type, expr]");
  elif 'list' != value[0] and 'list_columns' != value[0]:
    raise IOError("partition_type is invalid", value[0]);
  else:
    expr_str = '"%s"' % value[1]
    if 'list' == value[0]:
      type_str = 'PARTITION_FUNC_TYPE_LIST'
    elif 'list_columns' == value[0]:
      type_str = 'PARTITION_FUNC_TYPE_LIST_COLUMNS'
    else:
      raise IOError("partition_expr type %s only list or list columns now" % value[0]);

    cpp_f.write("  if (OB_SUCC(ret)) {\n")
    line = "    table_schema.get_part_option().set_part_num(1);\n"
    cpp_f.write(line)
    line = "    table_schema.set_part_level(PARTITION_LEVEL_ONE);\n"
    cpp_f.write(line)
    line = "    table_schema.get_part_option().set_part_func_type(%s);\n" % type_str
    cpp_f.write(line)
    line = "    if (OB_FAIL(table_schema.get_part_option().set_part_expr(%s))) {\n" % expr_str
    cpp_f.write(line)
    line = "      LOG_WARN(\"set_part_expr failed\", K(ret));\n";
    cpp_f.write(line)
    line = "    } else if (OB_FAIL(table_schema.mock_list_partition_array())) {\n"
    cpp_f.write(line)
    line = "      LOG_WARN(\"mock list partition array failed\", K(ret));\n";
    cpp_f.write(line)
    cpp_f.write("    }\n")
    cpp_f.write("  }\n")


def add_partition_expr_field(value, table_id):
  global cpp_f
  type_str = ''
  expr_str = ''
  if (len(value) != 3):
    raise IOError("partition_expr should in format [type, expr, part_num]");
  else:
    if 'hash' == value[0]:
      type_str = 'PARTITION_FUNC_TYPE_HASH'
      expr_str = '"hash (%s)"' % value[1]
    elif 'key' == value[0]:
      type_str = 'PARTITION_FUNC_TYPE_KEY'
      expr_str = '"key (%s)"' % value[1]
    else:
      raise IOError("partition_expr type %s only support hash or key now" % value[0]);
    cpp_f.write("  if (OB_SUCC(ret)) {\n")
    line = "    table_schema.get_part_option().set_part_func_type(%s);\n" % type_str
    cpp_f.write(line)
    line = "    if (OB_FAIL(table_schema.get_part_option().set_part_expr(%s))) {\n" % expr_str
    cpp_f.write(line)
    line = "      LOG_WARN(\"set_part_expr failed\", K(ret));\n";
    cpp_f.write(line)
    cpp_f.write("    }\n")
    line = "    table_schema.get_part_option().set_part_num(%s);\n" % value[2]
    cpp_f.write(line)
    line = "    table_schema.set_part_level(PARTITION_LEVEL_ONE);\n"
    cpp_f.write(line)
    cpp_f.write("  }\n")

def calculate_rowkey_column_num(keywords):
  rowkey_columns = keywords['rowkey_columns']
  keywords['rowkey_column_num'] = len(rowkey_columns)

def check_fileds(fields, keywords):
  for field in fields:
    if field not in keywords and 'index_name' not in keywords:
      if not field in index_only_fields:
        raise IOError("no field {0} found in def_table_schema, table_name={1}".format(field, keywords["table_name"]))

  non_field_keywords = ('index', 'enable_column_def_enum', 'base_def_keywords',
                        'self_tid', 'mapping_tid', 'real_vt', 'meta_record_in_sys',
                        'is_core_related')
  for kw in keywords:
    if not kw.startswith("base_table_name") and kw not in fields and 'index_name' not in keywords and kw not in non_field_keywords and keywords['table_type'] != 'AUX_LOB_META' and keywords['table_type'] != 'AUX_LOB_PIECE':
      raise IOError("unknown field {0} found in def_table_schema, table_name={1}".format(kw, keywords["table_name"]))

def fill_default_values(default_filed_values, keywords, missing_fields, index_value=('', [])):
  for key in default_filed_values:
    if key not in keywords:
      if key == 'index_status':
        if index_value[0] != '':
          keywords[key] = default_filed_values[key]
      elif key == 'data_table_id':
        tid = table_name2tid(keywords['table_name'] + ('name_postfix' in keywords and keywords['name_postfix'] or ''))
        add_field(field, tid)
      else:
        keywords[key] = default_filed_values[key]
        missing_fields[key] = True

def copy_keywords(keywords):
  tname = keywords["table_name"];
  tid = keywords["table_id"];
  base_tname = ''
  base_tname1 = ''
  base_tname2 = ''

  if "base_table_name" in keywords:
    base_tname = keywords["base_table_name"];
  else:
    keywords["base_table_name"] = ''

  if "base_table_name1" in keywords:
    base_tname1 = keywords["base_table_name1"];
  else:
    keywords["base_table_name1"] = ''

  if "base_table_name2" in keywords:
    base_tname2 = keywords["base_table_name2"];
  else:
    keywords["base_table_name2"] = ''

  log_debug("copy_keywords in: table_id=", tid, ",  table_name=" + tname, ", base_table_name=" + base_tname, ", base_table_name1=" + base_tname1, ", base_table_name2=" + base_tname2)
  # Default base_table_name equals its table name
  # base_table_name[1,2] records the original base table name in the scenario of multi-layer schema nested definitions
  if base_tname == '':
    base_tname = tname;
    keywords["base_table_name"] = tname;
  elif base_tname1 == '' and tname != base_tname:
    base_tname1 = tname;
    keywords["base_table_name1"] = tname;
  elif base_tname2 == '' and base_tname1 != '' and tname != base_tname and tname != base_tname1:
    base_tname2 = tname;
    keywords["base_table_name2"] = tname;
  elif base_tname1 != '' and base_tname2 != '' and tname != base_tname and tname != base_tname1 and tname != base_tname2:
    log_info("ERROR: should not be here. need design new base_table_name")
  # Execute copy
  new_keywords = copy.deepcopy(keywords)

  log_debug("copy_keywords out: table_id=", tid, ",  table_name=" + tname, ", base_table_name=" + base_tname, ", base_table_name1=" + base_tname1, ", base_table_name2=" + base_tname2)

  return new_keywords

def gen_history_table_def(table_id, keywords):
  new_keywords = copy_keywords(keywords)

  new_keywords["table_id"] = table_id
  new_keywords["table_name"] = "%s_history" % new_keywords["table_name"]
  rowkey_columns = new_keywords["rowkey_columns"]
  rowkey_columns.append(("schema_version", "int"))

  cols = new_keywords["normal_columns"]
  to_del = None
  for i in range(len(cols)):
    col = cols[i]
    if "schema_version" == col[0]:
      to_del = col
      continue
    l = list(col)
    if (len(l) < 3):
      l.append('true')
    else:
      l[2] = 'true'
    cols[i] = tuple(l)
  if to_del is not None:
    cols.remove(to_del)
  cols.insert(0, ('is_deleted', 'int'))

  return new_keywords

def gen_history_table_def_of_task(table_id, keywords):
  new_keywords = copy_keywords(keywords)

  new_keywords["table_id"] = table_id
  new_keywords["table_name"] = "%s_history" % new_keywords["table_name"]

  cols = new_keywords["normal_columns"]
  cols.append(('create_time', 'timestamp', 'false'))
  cols.append(('finish_time', 'timestamp', 'false'))

  return new_keywords


def def_all_lob_aux_table():
  global lob_aux_ids
  # build lob meta for 30000 ~ 39999
  for line in lob_aux_ids:
    if line[3] == "AUX_LOB_META":
      def_table_schema(**gen_inner_lob_aux_table_def(line[1], line[2], line[3], line[4], line[5], line[6]))
  # build lob piece for 40000 ~ 49000
  for line in lob_aux_ids:
    if line[3] == "AUX_LOB_PIECE":
      def_table_schema(**gen_inner_lob_aux_table_def(line[1], line[2], line[3], line[4], line[5], line[6]))

def gen_inner_lob_aux_table_def(data_table_name, table_id, table_type, keywords, is_in_runtime_space = False, cluster_private = False):
  keywords["table_id"] = table_id
  keywords["table_name"] = data_table_name
  keywords["base_table_name"] = data_table_name
  keywords["base_table_name1"] = ''
  keywords["base_table_name2"] = ''

  new_keywords = copy_keywords(keywords)

  new_keywords["table_id"] = table_id
  new_keywords["table_type"] = table_type
  dtid = table_name2tid(data_table_name)
  new_keywords["data_table_id"] = dtid
  if is_in_runtime_space:
    new_keywords["in_runtime_space"] = is_in_runtime_space

  if cluster_private:
    new_keywords["is_cluster_private"] = cluster_private

  if table_type == "AUX_LOB_META":
    new_keywords["table_name"] = data_table_name + "_aux_lob_meta"
  else :
    new_keywords["table_name"] = data_table_name + "_aux_lob_piece"
  return new_keywords

def gen_iterate_core_inner_table_def(table_id, table_name, table_type, keywords):
  new_keywords = copy_keywords(keywords)

  new_keywords["table_id"] = table_id
  new_keywords["table_name"] = table_name
  new_keywords["table_type"] = table_type
  new_keywords["gm_columns"] = []

  if 'partition_expr' in new_keywords:
    del new_keywords["partition_expr"]
  if 'partition_columns' in new_keywords:
    del new_keywords["partition_columns"]
  if 'index' in new_keywords:
    del new_keywords["index"]

  new_keywords["vtable_route_policy"] = 'local'
  new_keywords["in_runtime_space"] = True
  return new_keywords

def replace_agent_table_columns_def(columns):
  for i in range(0, len(columns)):
    column = list(columns[i])
    column[0] = column[0].upper()
    t = column[1]
    if t in ("int", "uint", "bool", "bigint", "double"):
      t = "number:38"
    elif t in ("timestamp", "timestamp:6"):
      t = "otimestamp"
    elif t == "otimestamp":
      pass
    elif t in ("longtext", "longblob"):
      pass
    elif t.startswith("varchar:") or t.startswith("varbinary:"):
      if len(column) >= 4 and "false" == column[2] and "" == column[3]:
        column[2] = "true"
    elif t.startswith("number:"):
      pass
    else:
      raise Exception("unsupported type", t)
    column[1] = t
    columns[i] = column[0:3] # ignore default value

def __gen_agent_vt_base_on_mysql(table_id, keywords, table_name_suffix):
  in_runtime_space = 'in_runtime_space' in keywords and keywords['in_runtime_space']
  is_cluster_private = 'is_cluster_private' in keywords and keywords['is_cluster_private']
  if in_runtime_space and is_cluster_private:
    raise Exception("real table must be not cluster_private")
  new_keywords = copy_keywords(keywords)

  new_keywords["table_type"] = 'VIRTUAL_TABLE'
  new_keywords["in_runtime_space"] = True
  new_keywords["table_id"] = table_id
  new_keywords["database_id"] = "OB_EXTENDED_SYS_DATABASE_ID"
  new_keywords["collation_type"] = "ObCollationType::CS_TYPE_UTF8MB4_BIN"
  name = keywords["table_name"]
  if name.startswith("__all_virtual_"):
    new_keywords["table_name"] = name.replace("__all_", "all_").upper() + table_name_suffix
  else:
    new_keywords["table_name"] = name.replace("__all_", "all_virtual_").upper() + table_name_suffix
  replace_agent_table_columns_def(new_keywords["rowkey_columns"])
  replace_agent_table_columns_def(new_keywords["normal_columns"])
  for column in new_keywords["gm_columns"]:
    new_keywords["normal_columns"].append([column.upper(), "otimestamp"])
  new_keywords["gm_columns"] = []
  if 'index' in new_keywords:
    new_idx = {}
    for (k, v) in new_keywords['index'].items():
      v['index_columns'] = [ c.upper() for c in v['index_columns'] ]
      new_idx[k] = v
    new_keywords['index'] = new_idx
  if is_sys_table(keywords['table_id']):
    new_keywords['index_using_type'] = 'USING_BTREE'

  new_keywords["base_def_keywords"] = keywords
  return new_keywords

def gen_sys_agent_virtual_table_def(table_id, keywords):
  global all_agent_virtual_tables
  new_keywords = __gen_agent_vt_base_on_mysql(table_id, keywords, "_SYS_AGENT")
  new_keywords["partition_expr"] = []
  new_keywords["partition_columns"] = []
  new_keywords["vtable_route_policy"] = "local"
  all_only_sys_table_name[keywords["table_name"]] = True
  all_agent_virtual_tables.append(new_keywords)
  return new_keywords

def __gen_mysql_vt(table_id, keywords, table_name_suffix):
  if 'in_runtime_space' in keywords and keywords['in_runtime_space']:
    raise Exception("base table must not be in runtime space")
  elif 'SYSTEM_TABLE' != keywords['table_type'] and 'VIRTUAL_TABLE' != keywords['table_type']:
    raise Exception("unsupported table type", keywords['table_type'])
  new_keywords = copy_keywords(keywords)

  new_keywords["table_type"] = 'VIRTUAL_TABLE'
  new_keywords["in_runtime_space"] = True
  new_keywords["table_id"] = table_id
  new_keywords["database_id"] = "OB_SYS_DATABASE_ID"
  name = keywords["table_name"]
  if name.startswith("__all_"):
    new_keywords["table_name"] = name.replace("__all_", "__all_virtual_") + table_name_suffix
  if is_sys_table(keywords['table_id']):
    new_keywords['index_using_type'] = 'USING_BTREE'

  new_keywords["base_def_keywords"] = keywords
  return new_keywords


def gen_agent_virtual_table_def(table_id, keywords):
  global all_agent_virtual_tables
  new_keywords = __gen_agent_vt_base_on_mysql(table_id, keywords, "_AGENT")
  new_keywords["partition_expr"] = []
  new_keywords["partition_columns"] = []
  if 'vtable_route_policy' in new_keywords:
    del(new_keywords["vtable_route_policy"])
  all_agent_virtual_tables.append(new_keywords)
  return new_keywords

def gen_cluster_config_def(table_id, table_name, keywords):
  new_keywords = copy_keywords(keywords)

  new_keywords["table_id"] = table_id
  new_keywords["table_name"] = table_name
  return new_keywords

def generate_cluster_private_table(f):
  global cluster_private_tables
  all_tables = [x for x in cluster_private_tables]
  all_tables.sort(key = lambda x: x['table_name'])
  cluster_private_switch = '\n'
  for kw in all_tables:
    if 'index_name' in kw:
      cluster_private_switch += 'case ' + table_name2index_tid(kw['table_name'] + kw['name_postfix'], kw['index_name']) + ':\n'
    else:
      cluster_private_switch += 'case ' + table_name2tid(kw['table_name'] + kw['name_postfix']) + ':\n'
  f.write('\n\n#ifdef CLUSTER_PRIVATE_TABLE_SWITCH\n' + cluster_private_switch + '\n#endif\n')

def generate_sqlite_create_table_statements(f):
  """
  Generate SQLite CREATE TABLE statement string constants
  These strings can be used in corresponding .cpp files
  """
  global all_sqlite_tables
  
  if not all_sqlite_tables:
    return
  
  f.write('\n\n#ifdef SQLITE_CREATE_TABLE_STATEMENTS\n')
  f.write('#ifndef SQLITE_CREATE_TABLE_STATEMENTS_DEFINED\n')
  f.write('#define SQLITE_CREATE_TABLE_STATEMENTS_DEFINED\n')
  f.write('// Auto-generated SQLite CREATE TABLE statements\n')
  f.write('// DO NOT EDIT THIS FILE MANUALLY\n')
  f.write('// Usage: Include this file and use the constant strings in your .cpp file\n\n')
  
  for table_def in all_sqlite_tables:
    table_name = table_def['table_name']
    columns = table_def['columns']
    primary_key = table_def['primary_key']
    
    # Generate CREATE TABLE statement string
    create_sql_lines = []
    create_sql_lines.append("CREATE TABLE IF NOT EXISTS {0} (".format(table_name))
    
    column_defs = []
    for i, col in enumerate(columns):
      col_name, col_type, nullable, default_val = col
      col_def = "  {0} {1} {2}".format(col_name, col_type, nullable)
      if default_val is not None:
        col_def += " DEFAULT {0}".format(default_val)
      # Add comma after all columns except the last one
      if i < len(columns) - 1 or primary_key:
        col_def += ","
      column_defs.append(col_def)
    
    create_sql_lines.extend(column_defs)
    
    if primary_key:
      pk_cols = ", ".join(primary_key)
      create_sql_lines.append("  PRIMARY KEY ({0})".format(pk_cols))
    
    create_sql_lines.append(");")
    
    # Generate constant name (converted from table name)
    # __all_merge_info -> SQLITE_CREATE_TABLE_ALL_MERGE_INFO
    const_name = 'SQLITE_CREATE_TABLE_' + table_name.replace('__all_', '').upper().replace('_', '_')
    
    # Generate C++ string constant (using inline to avoid duplicate definitions)
    f.write('// {0}\n'.format(table_name))
    f.write('inline const char *{0} = \n'.format(const_name))
    for i, line in enumerate(create_sql_lines):
      if i == 0:
        f.write('  "{0}\\n"\n'.format(line))
      elif i == len(create_sql_lines) - 1:
        f.write('  "{0}";\n\n'.format(line))
      else:
        f.write('  "{0}\\n"\n'.format(line))
  
  f.write('#endif // SQLITE_CREATE_TABLE_STATEMENTS_DEFINED\n')
  f.write('#endif // SQLITE_CREATE_TABLE_STATEMENTS\n')

def generate_sqlite_virtual_table_registration(f):
  """
  Generate SQLite virtual table registration code snippets
  These code snippets will be included in ob_virtual_table_iterator_factory.cpp
  Note: All SQLite virtual tables are now defined in ob_all_virtual_sqlite_tables.h
  """
  global all_sqlite_virtual_tables
  
  # Generate #ifdef even if empty, for clearer code structure
  f.write('\n\n#ifdef SQLITE_VIRTUAL_TABLE_CREATE_ITER\n')
  
  if not all_sqlite_virtual_tables:
    f.write('\n  // No SQLite virtual tables defined\n')
    f.write('#endif // SQLITE_VIRTUAL_TABLE_CREATE_ITER\n')
    return
  
  # Add unified header file inclusion hint
  f.write('\n  // All SQLite virtual tables are defined in ob_all_virtual_sqlite_tables.h\n')
  f.write('  // Include it in ob_virtual_table_iterator_factory.cpp:\n')
  f.write('  // (sqlite virtual tables header is pulled in by the factory cpp, not here)\n\n')
  
  # Use BEGIN_CREATE_VT_ITER_SWITCH_LAMBDA and END_CREATE_VT_ITER_SWITCH_LAMBDA macros
  f.write('  BEGIN_CREATE_VT_ITER_SWITCH_LAMBDA\n')
  
  for kw in all_sqlite_virtual_tables:
    table_name = kw['table_name']
    table_id = kw['table_id']
    
    # Generate class name: __all_virtual_merge_info -> ObAllVirtualMergeInfo
    class_name_parts = table_name.replace('__all_virtual_', '').split('_')
    class_name = 'ObAllVirtual' + ''.join([x.capitalize() for x in class_name_parts])
    
    # Generate table_id constant name: use table_name2tid function
    tid_name = table_name2tid(table_name)
    
    f.write('    case {0}: {{\n'.format(tid_name))
    f.write('      {0} *{1} = NULL;\n'.format(class_name, class_name.lower()))
    f.write('      if (OB_FAIL(NEW_VIRTUAL_TABLE({0}, {1}))) {{\n'.format(class_name, class_name.lower()))
    f.write('        SERVER_LOG(ERROR, "{0} construct failed", K(ret));\n'.format(class_name))
    f.write('      } else {\n')
    f.write('        vt_iter = static_cast<ObVirtualTableIterator *>({0});\n'.format(class_name.lower()))
    f.write('      }\n')
    f.write('      break;\n')
    f.write('    }\n')
  
  f.write('  END_CREATE_VT_ITER_SWITCH_LAMBDA\n\n')
  f.write('#endif // SQLITE_VIRTUAL_TABLE_CREATE_ITER\n')

def generate_sqlite_virtual_table_cpp_files():
  """
  Generate unified .h and .cpp files for all SQLite virtual tables
  All SQLite virtual tables are placed in ob_all_virtual_sqlite_tables.cpp/h
  """
  global all_sqlite_virtual_tables
  
  if not all_sqlite_virtual_tables:
    return
  
  # Generate unified header file
  generate_unified_sqlite_virtual_table_h()
  
  # Generate unified implementation file
  generate_unified_sqlite_virtual_table_cpp()

def generate_unified_sqlite_virtual_table_h():
  """
  Generate unified SQLite virtual table header file
  All SQLite virtual table class definitions are in this file
  """
  global all_sqlite_virtual_tables
  h_file = observer_out_path('ob_all_virtual_sqlite_tables.h')
  h_dir = os.path.dirname(h_file)
  if not os.path.exists(h_dir):
    os.makedirs(h_dir)
  
  with open(h_file, 'w') as f:
    f.write(copyright)
    f.write('\n#ifndef OB_ALL_VIRTUAL_SQLITE_TABLES_H_\n')
    f.write('#define OB_ALL_VIRTUAL_SQLITE_TABLES_H_\n\n')
    f.write('// Auto-generated unified header for all SQLite virtual tables\n')
    f.write('// DO NOT EDIT THIS FILE MANUALLY\n\n')
    f.write('#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"\n')
    f.write('#include "share/storage/ob_sqlite_connection_pool.h"\n')
    f.write('#include "lib/container/ob_se_array.h"\n\n')
    f.write('namespace oceanbase\n')
    f.write('{\n')
    f.write('namespace observer\n')
    f.write('{\n\n')
    
    # Generate class definition for each virtual table
    for kw in all_sqlite_virtual_tables:
      table_name = kw['table_name']
      class_name_parts = table_name.replace('__all_virtual_', '').split('_')
      class_name = 'ObAllVirtual' + ''.join([x.capitalize() for x in class_name_parts])
      
      columns = kw.get('normal_columns', [])
      rowkey_columns = kw.get('rowkey_columns', [])
      # SQLite virtual table columns should match the base table exactly, no additional columns (e.g., gm_columns)
      all_columns = [col[0] for col in rowkey_columns] + [col[0] for col in columns]
      
      f.write('class {0} : public common::ObVirtualTableScannerIterator\n'.format(class_name))
      f.write('{\n')
      f.write('public:\n')
      f.write('  {0}();\n'.format(class_name))
      f.write('  virtual ~{0}();\n\n'.format(class_name))
      f.write('  virtual int inner_open() override;\n')
      f.write('  virtual int inner_get_next_row(common::ObNewRow *&row) override;\n')
      f.write('  virtual void reset() override;\n\n')
      f.write('private:\n')
      f.write('  int fill_cells();\n')
      f.write('  int get_next_row_from_sqlite();\n\n')
      f.write('private:\n')
      f.write('  bool is_inited_;\n')
      f.write('  share::ObSQLiteConnectionGuard guard_;\n')
      f.write('  share::ObSQLiteStmt *stmt_;\n')
      f.write('  int64_t row_idx_;\n')
      
      for col_name in all_columns:
        col_type = 'int64_t'
        # First check the original SQLite type (from base table's _sqlite_columns)
        base_kw_check = kw.get('base_def_keywords', {})
        base_sqlite_cols_check = base_kw_check.get('_sqlite_columns', [])
        sqlite_type_check = None
        for sqlite_col in base_sqlite_cols_check:
          if sqlite_col[0] == col_name:
            sqlite_type_check = sqlite_col[1]
            break
        
        # Determine C++ type based on SQLite type
        # All columns of SQLite virtual table should be in base table's _sqlite_columns
        if sqlite_type_check == 'BLOB':
          col_type = 'common::ObString'
        elif sqlite_type_check == 'TEXT':
          col_type = 'common::ObString'
        elif sqlite_type_check == 'INTEGER':
          # INTEGER type needs further determination of int or uint (from OceanBase type)
          col_type = 'int64_t'  # Default, may be overridden to uint64_t later
          for col in rowkey_columns:
            if col[0] == col_name and 'uint' in col[1]:
              col_type = 'uint64_t'
              break
          if col_type == 'int64_t':
            for col in columns:
              if col[0] == col_name and 'uint' in col[1]:
                col_type = 'uint64_t'
                break
        elif sqlite_type_check is None:
          # Column not in _sqlite_columns, this is an error condition
          raise Exception("Column {0} not found in base table {1} _sqlite_columns".format(col_name, base_table_name_check))
        else:
          # Unknown SQLite type
          raise Exception("Unknown SQLite type {0} for column {1} in table {2}".format(sqlite_type_check, col_name, base_table_name_check))
      
        var_name = col_name + '_'
        f.write('  {0} {1};\n'.format(col_type, var_name))
      
      f.write('\n  DISALLOW_COPY_AND_ASSIGN({0});\n'.format(class_name))
      f.write('};\n\n')
    
    f.write('} // namespace observer\n')
    f.write('} // namespace oceanbase\n\n')
    f.write('#endif /* OB_ALL_VIRTUAL_SQLITE_TABLES_H_ */\n')

def generate_unified_sqlite_virtual_table_cpp():
  """
  Generate unified SQLite virtual table implementation file
  All SQLite virtual table implementations are in this file
  """
  global all_sqlite_virtual_tables
  cpp_file = observer_out_path('ob_all_virtual_sqlite_tables.cpp')
  cpp_dir = os.path.dirname(cpp_file)
  if not os.path.exists(cpp_dir):
    os.makedirs(cpp_dir)
  
  with open(cpp_file, 'w') as f:
    f.write(copyright)
    f.write('\n#define USING_LOG_PREFIX SERVER\n\n')
    f.write('#include "observer/virtual_table/ob_all_virtual_sqlite_tables.h"\n')
    f.write('#include "share/ob_server_struct.h"\n')
    f.write('#include "share/storage/ob_sqlite_connection.h"\n')
    f.write('#include "lib/oblog/ob_log.h"\n')
    f.write('#include "lib/time/ob_time_utility.h"\n')
    f.write('#include "lib/utility/ob_print_utils.h"\n')
    f.write('#include <sqlite/sqlite3.h>\n\n')
    f.write('namespace oceanbase\n')
    f.write('{\n')
    f.write('using namespace common;\n')
    f.write('using namespace share;\n\n')
    f.write('namespace observer\n')
    f.write('{\n\n')
    
    # Generate implementation for each virtual table
    for kw in all_sqlite_virtual_tables:
      table_name = kw['table_name']
      base_table_name = kw['base_def_keywords']['table_name']
      sqlite_db_pool = kw.get('sqlite_db_pool', 'GCTX.meta_db_pool_')
      
      class_name_parts = table_name.replace('__all_virtual_', '').split('_')
      class_name = 'ObAllVirtual' + ''.join([x.capitalize() for x in class_name_parts])
      
      columns = kw.get('normal_columns', [])
      rowkey_columns = kw.get('rowkey_columns', [])
      # SQLite virtual table columns should match the base table exactly, no additional columns (e.g., gm_columns)
      all_columns = [col[0] for col in rowkey_columns] + [col[0] for col in columns]
      select_columns = ', '.join(all_columns)
      
      # Generate implementation for a single class (reuse original logic)
      generate_single_sqlite_virtual_table_impl(f, table_name, class_name, base_table_name, sqlite_db_pool, kw, all_columns, select_columns)
    
    f.write('} // namespace observer\n')
    f.write('} // namespace oceanbase\n')

def generate_single_sqlite_virtual_table_impl(f, table_name, class_name, base_table_name, sqlite_db_pool, kw, all_columns, select_columns):
  """
  Generate implementation code for a single SQLite virtual table
  """
  global all_def_keywords
  # Constructor
  f.write('{0}::{0}()\n'.format(class_name))
  f.write('  : is_inited_(false),\n')
  f.write('    guard_(),\n')
  f.write('    stmt_(nullptr),\n')
  f.write('    row_idx_(0)')
  
  columns = kw.get('normal_columns', [])
  rowkey_columns = kw.get('rowkey_columns', [])
  # SQLite virtual table doesn't need gm_columns, columns should match base table exactly
  
  for col_name in all_columns:
    var_name = col_name + '_'
    # Check original SQLite type to determine default value
    base_table_name_check = base_table_name.replace('__all_virtual_', '__all_')
    base_kw_check = all_def_keywords.get(base_table_name_check, {})
    base_sqlite_cols_check = base_kw_check.get('_sqlite_columns', [])
    sqlite_type_check = None
    for sqlite_col in base_sqlite_cols_check:
      if sqlite_col[0] == col_name:
        sqlite_type_check = sqlite_col[1]
        break
    
    # Determine default value based on SQLite type
    # All columns of SQLite virtual table should be in base table's _sqlite_columns
    if sqlite_type_check == 'BLOB' or sqlite_type_check == 'TEXT':
      # BLOB and TEXT types use empty string, generate name_() instead of name_(ObString())
      default_val = ''
    elif sqlite_type_check == 'INTEGER':
      default_val = '0'
      for col in columns:
        if col[0] == col_name:
          if len(col) > 3:
            default_val = col[3]
          break
      # Check if default_val is a large integer that needs ULL suffix
      # Values >= 2^63 (9223372036854775808) need ULL suffix for uint64_t
      # Also check for OB_MAX_SCN_TS_NS equivalent value (18446744073709551615)
      try:
        if default_val and default_val.isdigit():
          # In Python 2, int() can handle arbitrary size, in Python 3 use int()
          # Compare as string length first for efficiency
          if len(default_val) > 18:  # 2^63 has 19 digits
            default_val = default_val + 'ULL'
          elif len(default_val) == 19:
            # Check if >= 9223372036854775808 (2^63)
            if default_val >= '9223372036854775808':
              default_val = default_val + 'ULL'
      except (ValueError, OverflowError):
        pass  # Not a number, use as-is
    elif sqlite_type_check is None:
      # Column not in _sqlite_columns, this is an error condition
      raise Exception("Column {0} not found in base table {1} _sqlite_columns".format(col_name, base_table_name_check))
    else:
      # Unknown SQLite type
      raise Exception("Unknown SQLite type {0} for column {1} in table {2}".format(sqlite_type_check, col_name, base_table_name_check))
    f.write(',\n    {0}({1})'.format(var_name, default_val))
  
  f.write('\n{\n')
  f.write('}\n\n')
  
  # Destructor
  f.write('{0}::~{0}()\n'.format(class_name))
  f.write('{\n')
  f.write('  reset();\n')
  f.write('}\n\n')
  
  # reset
  f.write('void {0}::reset()\n'.format(class_name))
  f.write('{\n')
  f.write('  if (stmt_) {\n')
  f.write('    guard_->finalize_query(stmt_);\n')
  f.write('    stmt_ = nullptr;\n')
  f.write('  }\n')
  f.write('  is_inited_ = false;\n')
  f.write('  row_idx_ = 0;\n')
  f.write('  ObVirtualTableScannerIterator::reset();\n')
  f.write('}\n\n')
  
  # inner_open
  f.write('int {0}::inner_open()\n'.format(class_name))
  f.write('{\n')
  f.write('  int ret = OB_SUCCESS;\n\n')
  f.write('  ObSQLiteConnectionGuard guard({0});\n'.format(sqlite_db_pool))
  f.write('  if (!guard) {\n')
  f.write('    ret = OB_ERR_UNEXPECTED;\n')
  f.write('    SERVER_LOG(WARN, "failed to acquire connection", K(ret));\n')
  f.write('  } else {\n')
  f.write('    guard_ = std::move(guard);\n')
  f.write('    const char *select_sql =\n')
  f.write('      "SELECT {0} "\n'.format(select_columns))
  f.write('      "FROM {0};";\n\n'.format(base_table_name))
  f.write('    if (OB_FAIL(guard_->prepare_query(select_sql, nullptr, stmt_))) {\n')
  f.write('      SERVER_LOG(WARN, "failed to prepare query", K(ret));\n')
  f.write('    } else {\n')
  f.write('      is_inited_ = true;\n')
  f.write('      row_idx_ = 0;\n')
  f.write('    }\n')
  f.write('  }\n\n')
  f.write('  return ret;\n')
  f.write('}\n\n')
  
  # inner_get_next_row
  f.write('int {0}::inner_get_next_row(common::ObNewRow *&row)\n'.format(class_name))
  f.write('{\n')
  f.write('  int ret = OB_SUCCESS;\n\n')
  f.write('  if (!is_inited_) {\n')
  f.write('    ret = OB_NOT_INIT;\n')
  f.write('    SERVER_LOG(WARN, "not initialized", K(ret));\n')
  f.write('  } else {\n')
  f.write('    ret = get_next_row_from_sqlite();\n')
  f.write('    if (OB_ITER_END == ret) {\n')
  f.write('      // End of result set\n')
  f.write('    } else if (OB_FAIL(ret)) {\n')
  f.write('      SERVER_LOG(WARN, "failed to get next row from sqlite", K(ret));\n')
  f.write('    } else if (OB_FAIL(fill_cells())) {\n')
  f.write('      SERVER_LOG(WARN, "failed to fill cells", K(ret));\n')
  f.write('    } else {\n')
  f.write('      row = &cur_row_;\n')
  f.write('    }\n')
  f.write('  }\n\n')
  f.write('  return ret;\n')
  f.write('}\n\n')
  
  # get_next_row_from_sqlite
  f.write('int {0}::get_next_row_from_sqlite()\n'.format(class_name))
  f.write('{\n')
  f.write('  int ret = OB_SUCCESS;\n\n')
  f.write('  if (OB_ISNULL(stmt_)) {\n')
  f.write('    ret = OB_ERR_UNEXPECTED;\n')
  f.write('    SERVER_LOG(WARN, "statement is null", K(ret), KP(stmt_));\n')
  f.write('  } else {\n')
  f.write('    ObSQLiteRowReader reader;\n')
  f.write('    ret = guard_->step_query(stmt_, reader);\n')
  f.write('    if (OB_SUCC(ret)) {\n')
  
  # Read data
  for col_name in all_columns:
      var_name = col_name + '_'
      read_method = 'get_int64'
      # First check the original SQLite type (from base table's _sqlite_columns)
      base_table_name = base_table_name.replace('__all_virtual_', '__all_')
      base_kw = all_def_keywords.get(base_table_name, {})
      base_sqlite_cols = base_kw.get('_sqlite_columns', [])
      sqlite_type = None
      for sqlite_col in base_sqlite_cols:
        if sqlite_col[0] == col_name:
          sqlite_type = sqlite_col[1]
          break
      
      # Determine read method based on SQLite type
      # All columns of SQLite virtual table should be in base table's _sqlite_columns
      if sqlite_type == 'BLOB':
        read_method = 'get_blob'
      elif sqlite_type == 'TEXT':
        read_method = 'get_text'
      elif sqlite_type == 'INTEGER':
        read_method = 'get_int64'
      elif sqlite_type is None:
        # Column not in _sqlite_columns, this is an error condition
        raise Exception("Column {0} not found in base table {1} _sqlite_columns".format(col_name, base_table_name))
      else:
        # Unknown SQLite type
        raise Exception("Unknown SQLite type {0} for column {1} in table {2}".format(sqlite_type, col_name, base_table_name))
      
      if read_method == 'get_blob':
        f.write('      int {0}_len = 0;\n'.format(col_name))
        f.write('      const void *{0}_ptr = reader.get_blob(&{1}_len);\n'.format(col_name, col_name))
        f.write('      if (OB_ISNULL({0}_ptr)) {{\n'.format(col_name))
        f.write('        {0}.reset();\n'.format(var_name))
        f.write('      } else {\n')
        f.write('        {0}.assign_ptr(static_cast<const char *>({1}_ptr), {1}_len);\n'.format(var_name, col_name))
        f.write('      }\n')
      elif read_method == 'get_text':
        f.write('      int {0}_len = 0;\n'.format(col_name))
        f.write('      const char *{0}_str = reader.get_text(&{1}_len);\n'.format(col_name, col_name))
        f.write('      if (OB_ISNULL({0}_str)) {{\n'.format(col_name))
        f.write('        {0}.reset();\n'.format(var_name))
        f.write('      } else {\n')
        f.write('        {0}.assign_ptr({1}_str, {1}_len);\n'.format(var_name, col_name))
        f.write('      }\n')
      else:
        f.write('      {0} = reader.{1}();\n'.format(var_name, read_method))
  
  f.write('    } else if (OB_ITER_END == ret) {\n')
  f.write('      // End of result set\n')
  f.write('    } else {\n')
  f.write('      SERVER_LOG(WARN, "failed to step query", K(ret));\n')
  f.write('    }\n')
  f.write('  }\n\n')
  f.write('  return ret;\n')
  f.write('}\n\n')
  
  # fill_cells
  f.write('int {0}::fill_cells()\n'.format(class_name))
  f.write('{\n')
  f.write('  int ret = OB_SUCCESS;\n')
  f.write('  const int64_t col_count = output_column_ids_.count();\n')
  f.write('  ObObj *cells = cur_row_.cells_;\n\n')
  f.write('  if (OB_ISNULL(cells)) {\n')
  f.write('    ret = OB_ERR_UNEXPECTED;\n')
  f.write('    SERVER_LOG(WARN, "cells is null", K(ret));\n')
  f.write('  } else {\n')
  f.write('    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {\n')
  f.write('      uint64_t col_id = output_column_ids_.at(i);\n')
  f.write('      switch (col_id) {\n')
  
  col_idx = 0
  for col_name in all_columns:
    var_name = col_name + '_'
    f.write('        case OB_APP_MIN_COLUMN_ID + {0}: {{\n'.format(col_idx))
    f.write('          // {0}\n'.format(col_name))
    
    set_method = 'set_int'
    # First check the original SQLite type (from base table's _sqlite_columns)
    base_table_name_check = base_table_name.replace('__all_virtual_', '__all_')
    base_kw_check = all_def_keywords.get(base_table_name_check, {})
    base_sqlite_cols_check = base_kw_check.get('_sqlite_columns', [])
    sqlite_type_check = None
    for sqlite_col in base_sqlite_cols_check:
      if sqlite_col[0] == col_name:
        sqlite_type_check = sqlite_col[1]
        break
    
    # Determine set method based on SQLite type
    # All columns of SQLite virtual table should be in base table's _sqlite_columns
    if sqlite_type_check == 'BLOB':
      set_method = 'set_varbinary'
    elif sqlite_type_check == 'TEXT':
      set_method = 'set_varchar'
    elif sqlite_type_check == 'INTEGER':
      # INTEGER type needs further determination of int or uint (from OceanBase type)
      set_method = 'set_int'  # Default, may be overridden to set_uint64
      for col in rowkey_columns:
        if col[0] == col_name and 'uint' in col[1]:
          set_method = 'set_uint64'
          break
      if set_method == 'set_int':
        for col in columns:
          if col[0] == col_name and 'uint' in col[1]:
            set_method = 'set_uint64'
            break
    elif sqlite_type_check is None:
      # Column not in _sqlite_columns, this is an error condition
      raise Exception("Column {0} not found in base table {1} _sqlite_columns".format(col_name, base_table_name_check))
    else:
      # Unknown SQLite type
      raise Exception("Unknown SQLite type {0} for column {1} in table {2}".format(sqlite_type_check, col_name, base_table_name_check))
    
    if set_method == 'set_varbinary':
      f.write('          cells[i].set_varbinary({0});\n'.format(var_name))
    elif set_method == 'set_varchar':
      f.write('          cells[i].set_varchar({0});\n'.format(var_name))
      f.write('          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));\n')
    else:
      f.write('          cells[i].{0}({1});\n'.format(set_method, var_name))
    
    f.write('          break;\n')
    f.write('        }\n')
    col_idx += 1
  
  f.write('        default: {\n')
  f.write('          ret = OB_ERR_UNEXPECTED;\n')
  f.write('          SERVER_LOG(WARN, "invalid column id", K(ret), K(col_id));\n')
  f.write('          break;\n')
  f.write('        }\n')
  f.write('      }\n')
  f.write('    }\n')
  f.write('  }\n\n')
  f.write('  return ret;\n')
  f.write('}\n\n')

def generate_sqlite_virtual_table_h(table_name, class_name, kw):
  """
  Generate virtual table .h file
  """
  import os
  # Script is in src/share/inner_table/ directory, need to go up two levels to project root
  script_dir = os.path.dirname(os.path.abspath(__file__))
  project_root = os.path.dirname(os.path.dirname(os.path.dirname(script_dir)))
  h_file = os.path.join(project_root, 'src/observer/virtual_table/ob_all_virtual_' + table_name.replace('__all_virtual_', '') + '.h')
  
  # Ensure directory exists
  h_dir = os.path.dirname(h_file)
  if not os.path.exists(h_dir):
    os.makedirs(h_dir)
  
  columns = kw.get('normal_columns', [])
  rowkey_columns = kw.get('rowkey_columns', [])
  # SQLite virtual table columns should match the base table exactly, no additional columns (e.g., gm_columns)
  all_columns = [col[0] for col in rowkey_columns] + [col[0] for col in columns]
  
  with open(h_file, 'w') as f:
    f.write(copyright)
    f.write('\n#ifndef OB_ALL_VIRTUAL_{0}_H_\n'.format(table_name.upper().replace('__', '_').replace('_', '_')))
    f.write('#define OB_ALL_VIRTUAL_{0}_H_\n\n'.format(table_name.upper().replace('__', '_').replace('_', '_')))
    f.write('#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"\n')
    f.write('#include "share/storage/ob_sqlite_connection_pool.h"\n')
    f.write('#include "lib/container/ob_se_array.h"\n\n')
    f.write('namespace oceanbase\n')
    f.write('{\n')
    f.write('namespace observer\n')
    f.write('{\n\n')
    f.write('class {0} : public common::ObVirtualTableScannerIterator\n'.format(class_name))
    f.write('{\n')
    f.write('public:\n')
    f.write('  {0}();\n'.format(class_name))
    f.write('  virtual ~{0}();\n\n'.format(class_name))
    f.write('  virtual int inner_open() override;\n')
    f.write('  virtual int inner_get_next_row(common::ObNewRow *&row) override;\n')
    f.write('  virtual void reset() override;\n\n')
    f.write('private:\n')
    f.write('  int fill_cells();\n')
    f.write('  int get_next_row_from_sqlite();\n\n')
    f.write('private:\n')
    f.write('  bool is_inited_;\n')
    f.write('  share::ObSQLiteConnectionGuard guard_;\n')
    f.write('  share::ObSQLiteStmt *stmt_;\n')
    f.write('  int64_t row_idx_;\n')
    
    # Generate member variables
    for col_name in all_columns:
        # Determine C++ type based on column type
        col_type = 'int64_t'  # Default
        # First check the original SQLite type (from base table's _sqlite_columns)
        base_table_name_check = base_table_name.replace('__all_virtual_', '__all_')
        base_kw_check = all_def_keywords.get(base_table_name_check, {})
        base_sqlite_cols_check = base_kw_check.get('_sqlite_columns', [])
        sqlite_type_check = None
        for sqlite_col in base_sqlite_cols_check:
          if sqlite_col[0] == col_name:
            sqlite_type_check = sqlite_col[1]
            break
        
        # Determine C++ type based on SQLite type
        # All columns of SQLite virtual table should be in base table's _sqlite_columns
        if sqlite_type_check == 'BLOB':
          col_type = 'common::ObString'
        elif sqlite_type_check == 'TEXT':
          col_type = 'common::ObString'
        elif sqlite_type_check == 'INTEGER':
          # INTEGER type needs further determination of int or uint (from OceanBase type)
          col_type = 'int64_t'  # Default, may be overridden to uint64_t later
          for col in rowkey_columns:
            if col[0] == col_name and 'uint' in col[1]:
              col_type = 'uint64_t'
              break
          if col_type == 'int64_t':
            for col in columns:
              if col[0] == col_name and 'uint' in col[1]:
                col_type = 'uint64_t'
                break
        elif sqlite_type_check is None:
          # Column not in _sqlite_columns, this is an error condition
          raise Exception("Column {0} not found in base table {1} _sqlite_columns".format(col_name, base_table_name_check))
        else:
          # Unknown SQLite type
          raise Exception("Unknown SQLite type {0} for column {1} in table {2}".format(sqlite_type_check, col_name, base_table_name_check))
        
        var_name = col_name + '_'
        f.write('  {0} {1};\n'.format(col_type, var_name))
    
    f.write('\n  DISALLOW_COPY_AND_ASSIGN({0});\n'.format(class_name))
    f.write('};\n\n')
    f.write('} // namespace observer\n')
    f.write('} // namespace oceanbase\n\n')
    f.write('#endif /* OB_ALL_VIRTUAL_{0}_H_ */\n'.format(table_name.upper().replace('__', '_').replace('_', '_')))

def generate_sqlite_virtual_table_cpp(table_name, class_name, base_table_name, sqlite_db_pool, kw):
  """
  Generate virtual table .cpp file
  """
  import os
  # Script is in src/share/inner_table/ directory, need to go up two levels to project root
  script_dir = os.path.dirname(os.path.abspath(__file__))
  project_root = os.path.dirname(os.path.dirname(os.path.dirname(script_dir)))
  cpp_file = os.path.join(project_root, 'src/observer/virtual_table/ob_all_virtual_' + table_name.replace('__all_virtual_', '') + '.cpp')
  
  # Ensure directory exists
  cpp_dir = os.path.dirname(cpp_file)
  if not os.path.exists(cpp_dir):
    os.makedirs(cpp_dir)
  
  columns = kw.get('normal_columns', [])
  rowkey_columns = kw.get('rowkey_columns', [])
  # SQLite virtual table columns should match the base table exactly, no additional columns (e.g., gm_columns)
  all_columns = [col[0] for col in rowkey_columns] + [col[0] for col in columns]
  
  # Generate SELECT SQL (in column order)
  select_columns = ', '.join(all_columns)
  
  with open(cpp_file, 'w') as f:
    f.write(copyright)
    f.write('\n#define USING_LOG_PREFIX SERVER\n\n')
    f.write('#include "observer/virtual_table/ob_all_virtual_{0}.h"\n'.format(table_name.replace('__all_virtual_', '')))
    f.write('#include "share/ob_server_struct.h"\n')
    f.write('#include "share/storage/ob_sqlite_connection.h"\n')
    f.write('#include "lib/oblog/ob_log.h"\n')
    f.write('#include "lib/time/ob_time_utility.h"\n')
    f.write('#include "lib/utility/ob_print_utils.h"\n')
    f.write('#include <sqlite/sqlite3.h>\n\n')
    f.write('namespace oceanbase\n')
    f.write('{\n')
    f.write('using namespace common;\n')
    f.write('using namespace share;\n\n')
    f.write('namespace observer\n')
    f.write('{\n\n')
    
    # Constructor
    f.write('{0}::{0}()\n'.format(class_name))
    f.write('  : is_inited_(false),\n')
    f.write('    guard_(),\n')
    f.write('    stmt_(nullptr),\n')
    f.write('    row_idx_(0)')
    
    for col_name in all_columns:
        var_name = col_name + '_'
        # choose the default value by SQLite type
        # First check the original SQLite type (from base table's _sqlite_columns)
        base_table_name_check = base_table_name.replace('__all_virtual_', '__all_')
        base_kw_check = all_def_keywords.get(base_table_name_check, {})
        base_sqlite_cols_check = base_kw_check.get('_sqlite_columns', [])
        sqlite_type_check = None
        for sqlite_col in base_sqlite_cols_check:
          if sqlite_col[0] == col_name:
            sqlite_type_check = sqlite_col[1]
            break
        
        if sqlite_type_check == 'BLOB' or sqlite_type_check == 'TEXT':
          # BLOB and TEXT types use empty string, generate name_() instead of name_(ObString())
          default_val = ''
        elif sqlite_type_check == 'INTEGER':
          default_val = '0'
          for col in columns:
            if col[0] == col_name:
              if len(col) > 3:
                default_val = col[3]
              break
          # Check if default_val is a large integer that needs ULL suffix
          try:
            if default_val and default_val.isdigit():
              if len(default_val) > 18:  # 2^63 has 19 digits
                default_val = default_val + 'ULL'
              elif len(default_val) == 19:
                if default_val >= '9223372036854775808':
                  default_val = default_val + 'ULL'
          except (ValueError, OverflowError):
            pass  # Not a number, use as-is
        elif sqlite_type_check is None:
          # Column not in _sqlite_columns, this is an error condition
          raise Exception("Column {0} not found in base table {1} _sqlite_columns".format(col_name, base_table_name_check))
        else:
          # Unknown SQLite type
          raise Exception("Unknown SQLite type {0} for column {1} in table {2}".format(sqlite_type_check, col_name, base_table_name_check))
        f.write(',\n    {0}({1})'.format(var_name, default_val))
    
    f.write('\n{\n')
    f.write('}\n\n')
    
    # Destructor
    f.write('{0}::~{0}()\n'.format(class_name))
    f.write('{\n')
    f.write('  reset();\n')
    f.write('}\n\n')
    
    # reset
    f.write('void {0}::reset()\n'.format(class_name))
    f.write('{\n')
    f.write('  if (stmt_) {\n')
    f.write('    guard_->finalize_query(stmt_);\n')
    f.write('    stmt_ = nullptr;\n')
    f.write('  }\n')
    f.write('  is_inited_ = false;\n')
    f.write('  row_idx_ = 0;\n')
    f.write('  ObVirtualTableScannerIterator::reset();\n')
    f.write('}\n\n')
    
    # inner_open
    f.write('int {0}::inner_open()\n'.format(class_name))
    f.write('{\n')
    f.write('  int ret = OB_SUCCESS;\n\n')
    f.write('  ObSQLiteConnectionGuard guard({0});\n'.format(sqlite_db_pool))
    f.write('  if (!guard) {\n')
    f.write('    ret = OB_ERR_UNEXPECTED;\n')
    f.write('    SERVER_LOG(WARN, "failed to acquire connection", K(ret));\n')
    f.write('  } else {\n')
    f.write('    guard_ = std::move(guard);\n')
    f.write('    const char *select_sql =\n')
    f.write('      "SELECT {0} "\n'.format(select_columns))
    f.write('      "FROM {0};";\n\n'.format(base_table_name))
    f.write('    if (OB_FAIL(guard_->prepare_query(select_sql, nullptr, stmt_))) {\n')
    f.write('      SERVER_LOG(WARN, "failed to prepare query", K(ret));\n')
    f.write('    } else {\n')
    f.write('      is_inited_ = true;\n')
    f.write('      row_idx_ = 0;\n')
    f.write('    }\n')
    f.write('  }\n\n')
    f.write('  return ret;\n')
    f.write('}\n\n')
    
    # inner_get_next_row
    f.write('int {0}::inner_get_next_row(common::ObNewRow *&row)\n'.format(class_name))
    f.write('{\n')
    f.write('  int ret = OB_SUCCESS;\n\n')
    f.write('  if (!is_inited_) {\n')
    f.write('    ret = OB_NOT_INIT;\n')
    f.write('    SERVER_LOG(WARN, "not initialized", K(ret));\n')
    f.write('  } else {\n')
    f.write('    ret = get_next_row_from_sqlite();\n')
    f.write('    if (OB_ITER_END == ret) {\n')
    f.write('      // End of result set\n')
    f.write('    } else if (OB_FAIL(ret)) {\n')
    f.write('      SERVER_LOG(WARN, "failed to get next row from sqlite", K(ret));\n')
    f.write('    } else if (OB_FAIL(fill_cells())) {\n')
    f.write('      SERVER_LOG(WARN, "failed to fill cells", K(ret));\n')
    f.write('    } else {\n')
    f.write('      row = &cur_row_;\n')
    f.write('    }\n')
    f.write('  }\n\n')
    f.write('  return ret;\n')
    f.write('}\n\n')
    
    # get_next_row_from_sqlite
    f.write('int {0}::get_next_row_from_sqlite()\n'.format(class_name))
    f.write('{\n')
    f.write('  int ret = OB_SUCCESS;\n\n')
    f.write('  if (OB_ISNULL(stmt_)) {\n')
    f.write('    ret = OB_ERR_UNEXPECTED;\n')
    f.write('    SERVER_LOG(WARN, "statement is null", K(ret), KP(stmt_));\n')
    f.write('  } else {\n')
    f.write('    ObSQLiteRowReader reader;\n')
    f.write('    ret = guard_->step_query(stmt_, reader);\n')
    f.write('    if (OB_SUCC(ret)) {\n')
    
    # Read data
    for col_name in all_columns:
      var_name = col_name + '_'
      read_method = 'get_int64'
      # First check the original SQLite type (from base table's _sqlite_columns)
      base_table_name_check = base_table_name.replace('__all_virtual_', '__all_')
      base_kw_check = all_def_keywords.get(base_table_name_check, {})
      base_sqlite_cols_check = base_kw_check.get('_sqlite_columns', [])
      sqlite_type = None
      for sqlite_col in base_sqlite_cols_check:
        if sqlite_col[0] == col_name:
          sqlite_type = sqlite_col[1]
          break
      
      # Determine read method based on SQLite type
      # All columns of SQLite virtual table should be in base table's _sqlite_columns
      if sqlite_type == 'BLOB':
        read_method = 'get_blob'
      elif sqlite_type == 'TEXT':
        read_method = 'get_text'
      elif sqlite_type == 'INTEGER':
        read_method = 'get_int64'
      elif sqlite_type is None:
        # Column not in _sqlite_columns, this is an error condition
        raise Exception("Column {0} not found in base table {1} _sqlite_columns".format(col_name, base_table_name_check))
      else:
        # Unknown SQLite type
        raise Exception("Unknown SQLite type {0} for column {1} in table {2}".format(sqlite_type, col_name, base_table_name_check))
      
      if read_method == 'get_blob':
        f.write('      int {0}_len = 0;\n'.format(col_name))
        f.write('      const void *{0}_ptr = reader.get_blob(&{1}_len);\n'.format(col_name, col_name))
        f.write('      if (OB_ISNULL({0}_ptr)) {{\n'.format(col_name))
        f.write('        {0}.reset();\n'.format(var_name))
        f.write('      } else {\n')
        f.write('        {0}.assign_ptr(static_cast<const char *>({1}_ptr), {1}_len);\n'.format(var_name, col_name))
        f.write('      }\n')
      elif read_method == 'get_text':
        f.write('      int {0}_len = 0;\n'.format(col_name))
        f.write('      const char *{0}_str = reader.get_text(&{1}_len);\n'.format(col_name, col_name))
        f.write('      if (OB_ISNULL({0}_str)) {{\n'.format(col_name))
        f.write('        {0}.reset();\n'.format(var_name))
        f.write('      } else {\n')
        f.write('        {0}.assign_ptr({1}_str, {1}_len);\n'.format(var_name, col_name))
        f.write('      }\n')
      else:
        f.write('      {0} = reader.{1}();\n'.format(var_name, read_method))
    
    f.write('    } else if (OB_ITER_END == ret) {\n')
    f.write('      // End of result set\n')
    f.write('    } else {\n')
    f.write('      SERVER_LOG(WARN, "failed to step query", K(ret));\n')
    f.write('    }\n')
    f.write('  }\n\n')
    f.write('  return ret;\n')
    f.write('}\n\n')
    
    # fill_cells
    f.write('int {0}::fill_cells()\n'.format(class_name))
    f.write('{\n')
    f.write('  int ret = OB_SUCCESS;\n')
    f.write('  const int64_t col_count = output_column_ids_.count();\n')
    f.write('  ObObj *cells = cur_row_.cells_;\n\n')
    f.write('  if (OB_ISNULL(cells)) {\n')
    f.write('    ret = OB_ERR_UNEXPECTED;\n')
    f.write('    SERVER_LOG(WARN, "cells is null", K(ret));\n')
    f.write('  } else {\n')
    f.write('    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {\n')
    f.write('      uint64_t col_id = output_column_ids_.at(i);\n')
    f.write('      switch (col_id) {\n')
    
    col_idx = 0
    for col_name in all_columns:
      var_name = col_name + '_'
      f.write('        case OB_APP_MIN_COLUMN_ID + {0}: {{\n'.format(col_idx))
    f.write('          // {0}\n'.format(col_name))
    
    set_method = 'set_int'
    # First check the original SQLite type (from base table's _sqlite_columns)
    base_table_name_check = base_table_name.replace('__all_virtual_', '__all_')
    base_kw_check = all_def_keywords.get(base_table_name_check, {})
    base_sqlite_cols_check = base_kw_check.get('_sqlite_columns', [])
    sqlite_type_check = None
    for sqlite_col in base_sqlite_cols_check:
      if sqlite_col[0] == col_name:
        sqlite_type_check = sqlite_col[1]
        break
    
    # Determine set method based on SQLite type
    # All columns of SQLite virtual table should be in base table's _sqlite_columns
    if sqlite_type_check == 'BLOB':
      set_method = 'set_varbinary'
    elif sqlite_type_check == 'TEXT':
      set_method = 'set_varchar'
    elif sqlite_type_check == 'INTEGER':
      # INTEGER type needs further determination of int or uint (from OceanBase type)
      set_method = 'set_int'  # Default, may be overridden to set_uint64
      for col in rowkey_columns:
        if col[0] == col_name and 'uint' in col[1]:
          set_method = 'set_uint64'
          break
      if set_method == 'set_int':
        for col in columns:
          if col[0] == col_name and 'uint' in col[1]:
            set_method = 'set_uint64'
            break
    elif sqlite_type_check is None:
      # Column not in _sqlite_columns, this is an error condition
      raise Exception("Column {0} not found in base table {1} _sqlite_columns".format(col_name, base_table_name_check))
    else:
      # Unknown SQLite type
      raise Exception("Unknown SQLite type {0} for column {1} in table {2}".format(sqlite_type_check, col_name, base_table_name_check))
    
    if set_method == 'set_varchar':
      f.write('          cells[i].set_varchar({0});\n'.format(var_name))
      f.write('          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));\n')
    else:
      f.write('          cells[i].{0}({1});\n'.format(set_method, var_name))
      
      f.write('          break;\n')
      f.write('        }\n')
      col_idx += 1
    
    f.write('        default: {\n')
    f.write('          ret = OB_ERR_UNEXPECTED;\n')
    f.write('          SERVER_LOG(WARN, "invalid column id", K(ret), K(col_id));\n')
    f.write('          break;\n')
    f.write('        }\n')
    f.write('      }\n')
    f.write('    }\n')
    f.write('  }\n\n')
    f.write('  return ret;\n')
    f.write('}\n\n')
    
    f.write('} // namespace observer\n')
    f.write('} // namespace oceanbase\n')

def generate_sys_index_table_misc_data(f):
  global sys_index_tables

  data_table_dict = {}
  for kw in sys_index_tables:
    if kw['table_name'] not in data_table_dict:
      data_table_dict[kw['table_name']] = []
    data_table_dict[kw['table_name']].append(kw)

  sys_index_table_id_switch = '\n'
  for kw in sys_index_tables:
    sys_index_table_id_switch += 'case ' + table_name2index_tid(kw['table_name'], kw['index_name']) + ':\n'
  f.write('\n\n#ifdef SYS_INDEX_TABLE_ID_SWITCH\n' + sys_index_table_id_switch + '\n#endif\n')

  sys_index_data_table_id_switch = '\n'
  for data_table_name in list(data_table_dict.keys()):
    sys_index_data_table_id_switch += 'case ' + table_name2tid(data_table_name) + ':\n'
  f.write('\n\n#ifdef SYS_INDEX_DATA_TABLE_ID_SWITCH\n' + sys_index_data_table_id_switch + '\n#endif\n')

  sys_index_data_table_id_to_index_ids_switch = '\n'
  for data_table_name, sys_indexs in list(data_table_dict.items()):
    sys_index_data_table_id_to_index_ids_switch += 'case ' + table_name2tid(data_table_name) + ': {\n'
    for kw in sys_indexs:
      sys_index_data_table_id_to_index_ids_switch += '  if (FAILEDx(index_tids.push_back(' + table_name2index_tid(kw['table_name'], kw['index_name']) +  '))) {\n'
      sys_index_data_table_id_to_index_ids_switch += '    LOG_WARN(\"fail to push back index tid\", KR(ret));\n'
      sys_index_data_table_id_to_index_ids_switch += '  }\n'
    sys_index_data_table_id_to_index_ids_switch += '  break;\n'
    sys_index_data_table_id_to_index_ids_switch += '}\n'
  f.write('\n\n#ifdef SYS_INDEX_DATA_TABLE_ID_TO_INDEX_IDS_SWITCH\n' + sys_index_data_table_id_to_index_ids_switch + '\n#endif\n')

  sys_index_data_table_id_to_index_schema_switch = '\n'
  for data_table_name, sys_indexs in list(data_table_dict.items()):
    sys_index_data_table_id_to_index_schema_switch += 'case ' + table_name2tid(data_table_name) + ': {\n'
    for kw in sys_indexs:
      method_name = kw['table_name'].replace('$', '_').strip('_').lower() + '_' + kw['index_name'].lower() + '_schema'
      sys_index_data_table_id_to_index_schema_switch += '  index_schema.reset();\n'
      sys_index_data_table_id_to_index_schema_switch += '  if (FAILEDx(ObInnerTableSchema::' + method_name +'(index_schema))) {\n'
      sys_index_data_table_id_to_index_schema_switch += '    LOG_WARN(\"fail to create index schema\", KR(ret), K(data_table_id));\n'
      sys_index_data_table_id_to_index_schema_switch += '  } else if (OB_FAIL(append_table_(index_schema, tables))) {\n'
      sys_index_data_table_id_to_index_schema_switch += '    LOG_WARN(\"fail to append\", KR(ret), K(data_table_id));\n'
      sys_index_data_table_id_to_index_schema_switch += '  }\n'
    sys_index_data_table_id_to_index_schema_switch += '  break;\n'
    sys_index_data_table_id_to_index_schema_switch += '}\n'

  f.write('\n\n#ifdef SYS_INDEX_DATA_TABLE_ID_TO_INDEX_SCHEMAS_SWITCH\n' + sys_index_data_table_id_to_index_schema_switch + '\n#endif\n')

  add_sys_index_id = '\n'
  for kw in sys_index_tables:
    index_id = table_name2index_tid(kw['table_name'], kw['index_name'])
    add_sys_index_id += '  } else if (OB_FAIL(table_ids.push_back(' + index_id +'))) {\n'
    add_sys_index_id += '    LOG_WARN(\"add index id failed\", KR(ret));\n'
  f.write('\n\n#ifdef ADD_SYS_INDEX_ID\n' + add_sys_index_id + '\n#endif\n')


def def_sys_index_table(index_name, index_table_id, index_columns, index_using_type, index_type, keywords):
  global cpp_f
  global cpp_f_tmp
  global StringIO
  global sys_index_tables

  kw = copy_keywords(keywords)

  if 'index' in kw:
    raise Exception("should not have index", kw['table_name'])
  if not is_sys_table(kw['table_id']):
    raise Exception("only support sys table", kw['table_name'])
  if not is_sys_index_table(index_table_id):
    raise Exception("index table id is invalid", index_table_id)
  if is_core_table(kw['table_id']) and not is_core_index_table(index_table_id):
    raise Exception("index table id for core table should be less than 101000", index_table_id, kw['table_id'])

  index_def = ''
  cpp_f_tmp = cpp_f
  cpp_f = io.StringIO()
  kw['index_name'] = index_name
  kw['index_columns'] = index_columns
  kw['index_table_id'] = index_table_id
  kw['index_using_type'] = index_using_type
  kw['index_type'] = index_type
  kw['table_type'] = 'USER_INDEX'
  kw['index_status'] = 'INDEX_STATUS_AVAILABLE'
  dtid = table_name2tid(kw['table_name'])
  kw['data_table_id'] = dtid
  kw['partition_columns'] = []
  kw['partition_expr'] = []
  kw['storing_columns'] =[]
  sys_index_tables.append(kw)
  def_table_schema(**kw)
  index_def = cpp_f.getvalue()
  cpp_f = cpp_f_tmp
  cpp_f.write(index_def)


def gen_sqlite_table_def(table_name, columns, primary_key):
  """
  Define SQLite table structure and register to all_def_keywords
  
  Parameters:
  - table_name: SQLite table name (e.g., '__all_merge_info')
  - columns: Column definition list, format: [('col_name', 'sqlite_type', 'nullable', 'default'), ...]
  - primary_key: Primary key column list, e.g., ['zone']
  
  Returns:
  - SQLite table definition keywords (registered to all_def_keywords)
  """
  global all_sqlite_tables
  global all_def_keywords
  
  # Convert column definitions: SQLite type → OceanBase type
  ob_columns = []
  rowkey_columns = []
  
  for col in columns:
    col_name, sqlite_type, nullable, default_val = col
    
    # SQLite type → OceanBase type mapping
    if sqlite_type == 'INTEGER':
      # Determine int or uint based on column name
      if 'scn' in col_name.lower() or 'version' in col_name.lower():
        ob_type = 'uint'  # SCN/version numbers are usually unsigned
      else:
        ob_type = 'int'
    elif sqlite_type == 'TEXT':
      ob_type = 'varchar:MAX_IP_ADDR_LENGTH' if 'ip' in col_name.lower() else 'varchar:256'
    elif sqlite_type == 'BLOB':
      ob_type = 'varbinary:OB_MAX_VARBINARY_LENGTH'  # BLOB maps to varbinary
    else:
      ob_type = 'int'  # Default
    
    # Build OceanBase column definition
    ob_col = [col_name, ob_type]
    if nullable == 'NOT NULL':
      ob_col.append('false')
    else:
      ob_col.append('true')
    
    if default_val is not None:
      ob_col.append(default_val)
    
    ob_columns.append(tuple(ob_col))
    
    # Build rowkey_columns
    if col_name in primary_key:
      rowkey_columns.append((col_name, ob_type))
  
  # Build keywords
  kw = {
    'table_name': table_name,
    'table_id': '0',  # SQLite table doesn't need real table_id, use '0' as placeholder
    'table_type': 'SYSTEM_TABLE',  # SQLite table as system table
    'rowkey_columns': rowkey_columns,
    'normal_columns': ob_columns,
    'gm_columns': [],  # SQLite tables usually don't have gmt_create/gmt_modified
    'in_runtime_space': False,  # SQLite table is system-only.
    'is_cluster_private': True,  # SQLite table is cluster private
    # Save original SQLite definition for generating CREATE TABLE
    '_sqlite_columns': columns,
    '_sqlite_primary_key': primary_key,
  }
  
  # Register to all_def_keywords
  all_def_keywords[table_name] = copy_keywords(kw)
  
  # Save to all_sqlite_tables for generating CREATE TABLE
  all_sqlite_tables.append({
    'table_name': table_name,
    'columns': columns,
    'primary_key': primary_key,
  })
  
  return kw

def gen_sqlite_virtual_table_def(table_id, table_name, keywords):
  """
  Generate virtual table definition for SQLite table (refer to gen_iterate_private_virtual_table_def)
  
  Parameters:
  - table_id: Virtual table's table_id
  - table_name: Virtual table name (e.g., '__all_virtual_merge_info')
  - keywords: Base table's keywords (obtained from all_def_keywords[base_table_name])
  
  Returns:
  - Virtual table definition keywords
  
  Notes:
  - SQLite virtual table is system-only (in_runtime_space = False)
  """
  global all_sqlite_virtual_tables
  
  kw = copy_keywords(keywords)
  kw['table_id'] = table_id
  kw['table_name'] = table_name
  
  # Remove internal fields, these should not be passed to def_table_schema()
  if '_sqlite_columns' in kw:
    del kw['_sqlite_columns']
  if '_sqlite_primary_key' in kw:
    del kw['_sqlite_primary_key']
  if 'sqlite_db_pool' in kw:
    del kw['sqlite_db_pool']
  
  # Convert to virtual table type
  kw['table_type'] = 'VIRTUAL_TABLE'
  kw['index_using_type'] = 'USING_BTREE'
  kw['partition_columns'] = []
  kw['partition_expr'] = []
  
  # SQLite virtual table doesn't need additional columns (e.g., gm_columns), columns should match base table exactly
  # Ensure gm_columns is empty
  kw['gm_columns'] = []
  
  # For virtual tables, columns in rowkey_columns should not be duplicated in normal_columns
  # because def_table_schema handles rowkey_columns and normal_columns separately
  # If columns in rowkey_columns are already in normal_columns, remove them from normal_columns to avoid duplication
  # Refer to gen_iterate_virtual_table_def approach
  if 'rowkey_columns' in kw and kw['rowkey_columns']:
    rowkey_column_names = [col[0] for col in kw['rowkey_columns']]
    # Remove columns in rowkey_columns from normal_columns to avoid duplication
    kw['normal_columns'] = [col for col in kw.get('normal_columns', []) if col[0] not in rowkey_column_names]
  
  # All virtual tables use local routing (svr_ip/svr_port removed)
  kw['partition_columns'] = []
  kw['vtable_route_policy'] = 'local'
  
  # Save base table information
  kw['base_def_keywords'] = keywords
  # SQLite virtual table is system-only.
  kw['in_runtime_space'] = False
  
  # Set owner
  kw['owner'] = 'nijia.nj'
  
  # Save SQLite related information (for subsequent code generation, but not passed to def_table_schema)
  sqlite_db_pool = 'GCTX.meta_db_pool_'
  
  # Save to all_sqlite_virtual_tables (includes sqlite_db_pool for code generation)
  save_kw = copy.deepcopy(kw)
  save_kw['sqlite_db_pool'] = sqlite_db_pool
  all_sqlite_virtual_tables.append(save_kw)
  
  # Returned kw does not include sqlite_db_pool, as def_table_schema doesn't need this field
  return kw




def get_column_def_enum(**keywords):
  global column_def_enum_array
  columns = []
  normal_columns = keywords['normal_columns']
  rowkey_columns = keywords['rowkey_columns']

  columns.extend([x[0] for x in rowkey_columns])
  columns.extend([x[0] for x in normal_columns])

  table_name = keywords['table_name'] + keywords['name_postfix']
  t = [x.upper().replace('#', '_') for x in columns]
  if len(t) > 0 and 'enable_column_def_enum' in keywords and keywords['enable_column_def_enum']:
    t[0] = '%s = common::OB_APP_MIN_COLUMN_ID' % t[0]
    content = '''
struct %s {
  enum {
    %s
  };
};
''' % (table_name.replace('$', '_').upper().strip('_') + "_CDE", ",\n    ".join(t))
    column_def_enum_array.append(content)

def kw2schema_version(kw):
  tid = kw['table_id']
  name_postfix = "_EXTENDED" if (is_extended_sys_view(tid) or is_extended_virtual_table(tid)) else ""
  if 'index_columns' in kw:
    return "OB_IDX_" + str(kw['table_id']) + '_' + kw['index_name'].upper() + name_postfix + "_SCHEMA_VERSION"
  else:
    return "OB_" + kw['table_name'].replace('$', '_').upper().strip('_') + name_postfix + "_SCHEMA_VERSION"

def table_name2tid(name):
  return "OB_" + name.replace('$', '_').upper().strip('_') + "_TID"

def table_name2index_tid(table_name, idx_name):
  return "OB_" + table_name.replace('$', '_').upper().strip('_') + '_' + str(idx_name).upper() + "_TID";

def table_name2tname(name):
  return "OB_" + name.replace('$', '_').upper().strip('_') + "_TNAME"

def table_name2tname_ora(name):
  return "OB_" + name.replace('$', '_').upper().strip('_') + "_ORA_TNAME"

def table_name2index_tname(table_name, idx_name):
  return "OB_" + table_name.replace('$', '_').upper().strip('_') + '_' + str(idx_name).upper() + "_TNAME";

def kw2tid(kw):
  name_postfix = kw['name_postfix'] if 'name_postfix' in kw else ""
  if 'index_columns' in kw:
    return table_name2index_tid(kw['table_name']+ name_postfix, kw['index_name'])
  else:
    return table_name2tid(kw['table_name']+ name_postfix)

__current_range_idx = -1 
__def_cnt = 0 
__split_size = 50
def check_split_file(tid):
  global __current_range_idx
  global __def_cnt
  global cpp_f
  #sometimes cpp_f may modify to STRINGIO object
  if (isinstance(cpp_f, io.IOBase) and not isinstance(cpp_f, io.StringIO)) or cpp_f == None:
    log_debug("current schema cnt => %d" % __def_cnt)
    range_idx = tid // __split_size
    if range_idx > __current_range_idx:
      if cpp_f != None:
        end_generate_cpp()
      fname = "ob_inner_table_schema.%d_%d.cpp" % (range_idx * __split_size + 1, (range_idx + 1) * __split_size)
      log_debug("generate new file with name %s" % fname)
      start_generate_cpp(fname)
      __current_range_idx = range_idx
    elif range_idx < __current_range_idx:
      log_debug("unexcept table id seq")
      sys.exit(1)
    __def_cnt += 1

def def_table_schema(**keywords):
  tid = int(keywords['table_id'])
  check_split_file(tid)

  global fields
  global default_filed_values
  missing_fields = {}
  global table_name_ids
  global table_name_postfix_ids
  global table_name_postfix_table_names
  global index_name_ids
  global runtime_space_tables
  global runtime_space_table_names
  global cluster_distributed_vtables
  global StringIO
  global ob_virtual_index_table_id
  global ora_virtual_index_table_id
  global index_only_id
  global index_idx
  global cpp_f
  global cpp_f_tmp
  global all_def_keywords
  global column_collation
  global is_extended_sys_table
  global cluster_private_tables
  global core_related_tables
  global lob_aux_data_def
  global lob_aux_meta_def

  if 'index_name' not in keywords:
    if 'name_postfix' in keywords:
      all_def_keywords[keywords['table_name'] + keywords['name_postfix']] = copy_keywords(keywords)
    else:
      all_def_keywords[keywords['table_name']] = copy_keywords(keywords)
      keywords = copy.deepcopy(all_def_keywords[keywords['table_name']])
  else:
    if 'name_postfix' in keywords:
      all_def_keywords[keywords['table_name'] + keywords['name_postfix'] + '_' + keywords['index_name']] = copy_keywords(keywords)
    else:
      all_def_keywords[keywords['table_name'] + '_' + keywords['index_name']] = copy_keywords(keywords)

  index_defs = []
  index_def = ''
  calculate_rowkey_column_num(keywords)
  is_extended_sys_table = False
  column_collation = 'CS_TYPE_INVALID'

  ##virtual table will set index_using_type to USING_HASH by default
  if is_virtual_table(keywords['table_id']):
    if 'index_using_type' not in keywords:
      keywords['index_using_type'] = 'USING_HASH'

  if not is_mysql_virtual_table(tid) and not is_extended_virtual_table(tid):
    if 'partition_expr' in keywords and 0 != len(keywords['partition_expr']):
      raise Exception("partition_expr only works for virtual table after 4.0", tid)
    elif 'partition_columns' in keywords and 0 != len(keywords['partition_columns']):
      raise Exception("partition_columns only works for virtual table after 4.0", tid)

  if not is_mysql_virtual_table(tid) and not is_extended_virtual_table(tid):
    if 'partition_expr' in keywords and 0 != len(keywords['partition_expr']):
      raise Exception("partition_expr only works for virtual table after 4.0", tid)
    elif 'partition_columns' in keywords and 0 != len(keywords['partition_columns']):
      raise Exception("partition_columns only works for virtual table after 4.0", tid)
  if is_sys_view(tid):
    pattern = re.compile(r'^\s*SELECT\s+\*', re.IGNORECASE)
    if 'view_definition' in keywords and 0 != len(keywords['view_definition']) and pattern.match(keywords['view_definition'].upper().replace("\n", " ")):
      log_debug((keywords['view_definition']))
      raise Exception("The system view definition cannot start with select *. Please specify the column name explicitly, ", tid)

  fill_default_values(default_filed_values, keywords, missing_fields)
  check_fileds(fields, keywords)

  get_column_def_enum(**keywords)

  if 'index_name' in keywords:
    print_method_start(keywords['table_name'] + keywords['name_postfix'] + '_' + keywords['index_name'])
    if True == is_extended_virtual_table(int(keywords['table_id'])):
      if 'real_vt' in keywords and True == keywords['real_vt']:
        index_name_ids.append([keywords['index_name'], int(keywords['index_table_id']), keywords['table_name'] + keywords['name_postfix'], keywords['table_id'], keywords['base_table_name'], keywords['base_table_name1']])
      else:
        index_name_ids.append([keywords['index_name'], int(ora_virtual_index_table_id), keywords['table_name'] + keywords['name_postfix'], keywords['table_id'], keywords['base_table_name'], keywords['base_table_name1']])
        ora_virtual_index_table_id -= 1
    elif True == is_mysql_virtual_table(int(keywords['table_id'])):
      index_name_ids.append([keywords['index_name'], int(ob_virtual_index_table_id), keywords['table_name'] + keywords['name_postfix'], keywords['table_id'], keywords['base_table_name'], keywords['base_table_name1']])
      ob_virtual_index_table_id -= 1
    elif True == is_sys_table(int(keywords['table_id'])):
      if 'index_table_id' not in keywords:
        raise Exception("must specific index_table_id", int(keywords['table_id']))
      index_name_ids.append([keywords['index_name'], int(keywords['index_table_id']), keywords['table_name'] + keywords['name_postfix'], keywords['table_id'], keywords['base_table_name'], keywords['base_table_name1']])
  else:
    print_method_start(keywords['table_name'] + keywords['name_postfix'])
    table_name_postfix_ids.append((keywords['table_name']+ keywords['name_postfix'], int(keywords['table_id'])))
    table_name_postfix_table_names.append((keywords['table_name']+ keywords['name_postfix'], keywords['table_name']))

    table_name_ids.append((keywords['table_name'], int(keywords['table_id']), keywords['base_table_name'], keywords['base_table_name1'], keywords['base_table_name2']))

  if 'is_core_related' in keywords and keywords['is_core_related']:
    core_related_tables.append(int(keywords['table_id']))

  log_debug("\table_id=",  keywords['table_id'], ", table_name=" + keywords['table_name'], ", base_table_name=", keywords['base_table_name'], ", base_table_name1=" + keywords['base_table_name1'], ", base_table_name2=" + keywords['base_table_name2'])

  log_debug("\nSTART TO GENERATE: " + keywords['table_name']+ keywords['name_postfix'])
  if True == is_extended_virtual_table(int(keywords['table_id'])):
    column_collation = 'CS_TYPE_UTF8MB4_BIN'
    is_extended_sys_table = True
  if 'index_name' in keywords:
    local_fields = fields + index_only_fields
  elif is_lob_table(keywords['table_id']):
    local_fields = fields + lob_fields
  else:
    local_fields = fields

  # Generate partition expr for virtual table.
  # We support addr_to_partition_id(ip, port) for MySQL virtual tables,
  # and hash(ip, port) for extended virtual tables.
  table_id = int(keywords['table_id']);
  if keywords['partition_columns'] and (is_mysql_virtual_table(table_id) or is_extended_virtual_table(table_id)) and False == keywords['is_real_virtual_table']:
    cols = keywords['partition_columns']

    # vtable with definition of partition_colums must be distributed
    if 'vtable_route_policy' not in keywords or 'distributed' != keywords['vtable_route_policy'].lower():
      raise Exception("vtable route policy must be distributed", keywords['table_name'])

    if len(cols) != 2:
      raise Exception("only support ip, port partition columns for virtual table", cols)
    types = []
    for col in cols:
      types.append([x[1] for x in keywords['rowkey_columns'] + keywords['normal_columns'] if x[0] == col][0])
    (ip, port) = types
    if is_mysql_virtual_table(table_id):
      if not ip.startswith("varchar:") or not port.startswith("int"):
        raise Exception("unexpected type of ip and port", cols, types);
      keywords['partition_expr'] = ['list_columns', ', '.join(cols)]
    else:
      if not ip.startswith("varchar:") or not port.startswith("number"):
        raise Exception("unexpected type of ip and port", cols, types);
      keywords['partition_expr'] = ['list', ', '.join(cols)]

  # owner must be defined
  if 'owner' not in keywords or 0 == len(keywords['owner'].strip()):
    raise Exception('owner must be specified')

  # vtable_route_policy' value must be valid
  if 'vtable_route_policy' in keywords:
    route_policy = keywords['vtable_route_policy'].lower()

    tid_str = ""
    if 'index_columns' in keywords:
      tid_str = table_name2index_tid(keywords['table_name']+ keywords['name_postfix'], keywords['index_name'])
    else:
      tid_str = table_name2tid(keywords['table_name']+ keywords['name_postfix'])

    if 'local' != route_policy and 'distributed' != route_policy:
      raise Exception("vtable route policy is invalid", route_policy)
    elif not is_mysql_virtual_table(tid) and not is_extended_virtual_table(tid) and 'local' != route_policy:
      raise Exception("vtabl route policy is only work for virtual table", tid)
    else:
      if 'local' == route_policy:
        if 'partition_columns' in keywords and 0 != len(keywords['partition_columns']):
          raise Exception("partition columns is not valid for local virtual table", keywords.get('partition_columns', []))
      else:
        # distributed
        if 'partition_columns' not in keywords or 2 != len(keywords['partition_columns']):
          raise Exception("partition columns is not valid for distributed virtual table", keywords.get('partition_columns', []))
        if not ('in_runtime_space' in keywords and keywords['in_runtime_space']):
          cluster_distributed_vtables.append(tid_str)

  ## Set sys table's (including index and lob tables) tablet_id.
  if is_sys_table(tid) or is_lob_table(tid) or is_sys_index_table(tid):
    keywords['tablet_id'] = tid_str

  for field in local_fields :
    value = keywords[field]
    if field == 'gm_columns':
      if 'index_table_id' in keywords:
        for column_name in value:
          print_discard_column(column_name)
      else:
        add_gm_columns(value)
    elif field == 'rowkey_columns':
      if 'index_name' not in keywords:
        if 'partition_columns' in keywords:
          add_rowkey_columns(value, keywords['partition_columns'])
        else:
          add_rowkey_columns(value)
    elif field == 'normal_columns':
      if 'index_name' not in keywords:
        if keywords['table_type'] != 'TABLE_TYPE_VIEW':
          if 'partition_columns' in keywords:
            add_normal_columns(value, keywords['partition_columns'])
          else:
            add_normal_columns(value)
    elif field == 'partition_columns':
      continue;
    elif field == 'table_id':
      if 'index_columns' in keywords:
        tid = table_name2index_tid(keywords['table_name']+ keywords['name_postfix'], keywords['index_name'])
      else:
        tid = table_name2tid(keywords['table_name']+ keywords['name_postfix'])
      add_field(field, tid)
    elif field == 'database_id' and field not in missing_fields:
      database_id = value
      add_field(field, database_id)
    elif field == 'table_name':
      if 'index_name' in keywords :
        add_char_field(field, table_name2index_tname(keywords['table_name'] + keywords['name_postfix'], keywords['index_name']))
      else:
        if keywords["name_postfix"] != '_ORA':
          add_char_field(field, table_name2tname(keywords['table_name']))
        else:
          add_char_field(field, table_name2tname_ora(keywords['table_name']))
    elif field in ('compress_func_name'):
      add_char_field(field, '{0}'.format(value))
    elif field in ('comment_str', 'part_func_expr', 'sub_part_func_expr'):
      add_char_field(field, '"{0}"'.format(value))
    elif field == 'in_runtime_space':
      if keywords[field]:
        if 'index_name' in keywords :
          runtime_space_tables.append(table_name2index_tid(keywords['table_name']+ keywords['name_postfix'], keywords['index_name']))
          runtime_space_table_names.append(table_name2index_tname(keywords['table_name'] + keywords['name_postfix'], keywords['index_name']))
        else:
          runtime_space_tables.append(table_name2tid(keywords['table_name']+ keywords['name_postfix']))
          runtime_space_table_names.append(table_name2tname(keywords['table_name'] + keywords['name_postfix']))
    elif field == 'view_definition':
      if keywords[field]:
        add_char_field(field, 'R"__({0})__"'.format(value))
    elif field == 'partition_expr':
      if keywords[field]:
        add_list_partition_expr_field(value)
    elif field == 'index':
      if type(value) == dict:
        # index defined in table definition
        cpp_f_tmp = cpp_f
        index_idx = 0
        del keywords['index']
        if 'index_using_type' in keywords:
            dt_using_type = keywords['index_using_type']
        for k, v in list(value.items()):
          cpp_f = io.StringIO()
          index_idx += 1
          keywords['index_name'] = k
          keywords['index_columns'] = v['index_columns']
          if 'index_table_id' in v:
              keywords['index_table_id'] = v['index_table_id']
          if 'index_using_type' in v:
              keywords['index_using_type'] = v['index_using_type']
          keywords['table_type'] = 'USER_INDEX'
          keywords['index_status'] = 'INDEX_STATUS_AVAILABLE'
          keywords['index_type'] = 'INDEX_TYPE_NORMAL_LOCAL';
          dtid = table_name2tid(keywords['table_name']+ keywords['name_postfix'])
          keywords['data_table_id'] = dtid
          if (is_virtual_table(keywords['table_id'])):
            keywords['storing_columns'] = [col[0] for col in keywords['normal_columns'] if col[0] not in v['index_columns']]
          def_table_schema(**keywords)
          index_def = cpp_f.getvalue()
          index_defs.append(index_def)

        keywords['index'] = value
        if dt_using_type is not None:
            keywords['index_using_type'] = dt_using_type
        else:
            del keywords['index_using_type']
        cpp_f = cpp_f_tmp
    elif field == 'index_columns':
      # only index generation will enter here
      max_used_column_idx = add_index_columns(value, **keywords)
    elif field == 'storing_columns':
      # only virtual table index generation will enter here
      max_used_column_idx = add_storing_columns(value, max_used_column_idx, **keywords)
    elif field in ('index_name', 'name_postfix',
                   'is_cluster_private', 'is_real_virtual_table',
                   'owner', 'vtable_route_policy'):
      # do nothing
      log_debug("skip")
    else:
      add_field(field, value)

  ## add lob aux table except for __all_core_table
  if keywords['table_type'] == 'SYSTEM_TABLE' and int(keywords['table_id']) > 1:
    is_in_runtime_space = False
    cluster_private = False
    if 'in_runtime_space' in keywords:
      is_in_runtime_space = keywords['in_runtime_space']
    if 'is_cluster_private' in keywords:
      cluster_private = keywords['is_cluster_private']
    meta_tid = int(keywords['table_id']) + base_lob_meta_table_id
    lob_aux_ids.append([keywords['table_id'], keywords['table_name'], meta_tid, 'AUX_LOB_META', lob_aux_meta_def, is_in_runtime_space, cluster_private])
    mtid = table_name2tid(keywords['table_name'] + '_aux_lob_meta')
    add_field('aux_lob_meta_tid', mtid)
    piece_tid = int(keywords['table_id']) + base_lob_piece_table_id
    lob_aux_ids.append([keywords['table_id'], keywords['table_name'], piece_tid, 'AUX_LOB_PIECE', lob_aux_data_def, is_in_runtime_space, cluster_private])
    ptid = table_name2tid(keywords['table_name'] + '_aux_lob_piece')
    add_field('aux_lob_piece_tid', ptid)
  
  if "index_name" in keywords and not type(keywords['index']) == dict:
    add_index_method_end(max_used_column_idx)
  else:
    add_method_end()

  if 'is_cluster_private' in keywords and keywords['is_cluster_private'] \
     and 'in_runtime_space' in keywords and keywords['in_runtime_space'] \
     and (is_sys_table(table_id) or is_sys_index_table(table_id) or is_lob_table(table_id)):
    if is_sys_table(table_id) and 'meta_record_in_sys' not in keywords:
      raise Exception("meta_record_in_sys must be defined when is_cluster_private = true")
    kw = copy_keywords(keywords)
    cluster_private_tables.append(kw)

  if 'index_name' in keywords:
    del keywords['index_name']
  if 'index_columns' in keywords:
    del keywords['index_columns']
  if 'index_status' in keywords:
    del keywords['index_status']
  if 'data_table_id' in keywords:
    del keywords['data_table_id']
  if 'index_type' in keywords:
    del keywords['index_type']
  if 'is_cluster_private' in keywords:
    del keywords['is_cluster_private']
  for index_def in index_defs:
    cpp_f.write(index_def)

def clean_files(globstr):
  log_debug("clean files by glob [%s]" % globstr)
  for f in glob.glob(os.path.join(share_output_dir, globstr)):
      log_debug("remove  %s ..." % f)
      try:
        os.remove(f)
      except FileNotFoundError:
        # Multiple build targets can trigger generation concurrently.
        # If another generator already removed this file, treat it as clean.
        pass

def start_generate_cpp(cpp_file_name):
  global cpp_f
  cpp_f = open(share_out_path(cpp_file_name), 'w')
  head = copyright + """
#define USING_LOG_PREFIX SHARE_SCHEMA
#include "ob_inner_table_schema.h"

#include "share/schema/ob_schema_macro_define.h"
#include "share/schema/ob_table_schema.h"
#include "share/scn.h"

namespace oceanbase
{
using namespace share::schema;
using namespace common;
namespace share
{

"""
  cpp_f.write(head)

def start_generate_h(h_file_name):
  global h_f
  h_f = open(share_out_path(h_file_name), 'w')
  head = copyright + """
#ifndef _OB_INNER_TABLE_SCHEMA_H_
#define _OB_INNER_TABLE_SCHEMA_H_

#include "share/ob_define.h"
#include "ob_inner_table_schema_constants.h"
#include "share/ob_version_parser.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObTableSchema;
}
}

namespace share
{
"""
  h_f.write(head)

def start_generate_constants_h(h_file_name):
  global constants_h_f
  global id_to_name_f
  constants_h_f = open(share_out_path(h_file_name), 'w')
  id_to_name_f = open(share_out_path("table_id_to_name"), 'w')
  head = copyright + """
#ifndef _OB_INNER_TABLE_SCHEMA_CONSTANTS_H_
#define _OB_INNER_TABLE_SCHEMA_CONSTANTS_H_

#include "share/ob_define.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObTableSchema;
}
}

namespace share
{
"""
  constants_h_f.write(head)

def print_class_head_h():
  global column_def_enum_array
  h_f.write("\n".join(column_def_enum_array))

  class_head="""
class ObInnerTableSchema
{
"""
  h_f.write(class_head)

def end_generate_cpp():
  global cpp_f
  end = """
} // end namespace share
} // end namespace oceanbase
"""
  cpp_f.write(end)
  cpp_f.close()
# While generating constants.h, generate the table_id_to_name file
def generate_constants_h_content():
  global constants_h_f
  global id_to_name_f
  last_table_id = 0;

  id_to_name_f.write("########## Table ID to Table Name mapping ##########\n")
  id_to_name_f.write("# For easy analysis of occupancy, the same ID may map to multiple Names\n\n")
  ################# Generate xx_TID definition ################
  table_id_line = 'const uint64_t OB_{0}_TID = {1}; // "{2}"\n'
  for (table_name, table_id) in table_name_postfix_ids:
    constants_h_f.write(table_id_line.format(table_name.replace('$', '_').upper().strip('_'), table_id, table_name))
    if table_id <= last_table_id:
        raise Exception("invalid table id", table_name, table_id, last_table_id)
    last_table_id = table_id
  for line in index_name_ids:
    constants_h_f.write(table_id_line.format(line[2].replace('$', '_').upper().strip('_')+'_'+line[0].upper(), line[1], line[2]))

  constants_h_f.write("\n")
  ###################################################
  ################# Generate xx_TNAME definition ################
  # Generate table_id_to_name file simultaneously
  table_name_line = 'const char *const OB_{0}_TNAME = "{1}";\n'
  for (table_name_postfix, table_name) in table_name_postfix_table_names:
    constants_h_f.write(table_name_line.format(table_name_postfix.replace('$', '_').upper().strip('_'), table_name))

  table_id_to_name_line = '# {0}: {1}\n'
  base_table_id_to_name_line = '# {0}: {1}  # BASE_TABLE_NAME\n'
  base_table_id_to_name_line1 = '# {0}: {1}  # BASE_TABLE_NAME1\n'
  base_table_id_to_name_line2 = '# {0}: {1}  # BASE_TABLE_NAME2\n'
  for (table_name, table_id, base_table_name, base_table_name1, base_table_name2) in table_name_ids:
    # lob table is not recorded in the table_id_to_name file
    if not is_lob_table(table_id):
      id_to_name_f.write(table_id_to_name_line.format(table_id, table_name))
      # If base_table_name is different, then output base_table_name
      if base_table_name != table_name:
        id_to_name_f.write(base_table_id_to_name_line.format(table_id, base_table_name))
      if base_table_name1 != '' and base_table_name1 != table_name:
        id_to_name_f.write(base_table_id_to_name_line1.format(table_id, base_table_name1))
      if base_table_name2 != '' and base_table_name2 != table_name:
        id_to_name_f.write(base_table_id_to_name_line2.format(table_id, base_table_name2))

  index_table_name_format = "__idx_{0}_{1}"
  index_name_line = 'const char *const OB_{0}_TNAME = "{1}";\n'
  index_id_to_name_line = '# {0}: {1}\n'
  base_index_id_to_name_line = '# {0}: {1}  # INDEX_NAME\n'
  data_tname_id_to_name_line = '# {0}: {1}  # DATA_BASE_TABLE_NAME\n'
  data_tname_id_to_name_line1 = '# {0}: {1}  # DATA_BASE_TABLE_NAME1\n'

  for line in index_name_ids:
    index_name = line[0]
    table_id = line[1]
    data_table_id =  int(line[3]) & (0xFFFFFFFFFF)
    data_table_name = line[4]
    data_table_name1 = line[5]
    index_table_name = index_table_name_format.format(str(data_table_id), index_name)

    constants_h_f.write(index_name_line.format(line[2].replace('$', '_').upper().strip('_')+'_'+line[0].upper(), index_table_name))
    id_to_name_f.write(index_id_to_name_line.format(table_id, index_table_name))
    id_to_name_f.write(base_index_id_to_name_line.format(table_id, index_name))
    id_to_name_f.write(data_tname_id_to_name_line.format(table_id, data_table_name))
    if data_table_name1 != '':
      id_to_name_f.write(data_tname_id_to_name_line1.format(table_id, data_table_name1))

  constants_h_f.write("\n")
  ###################################################
  ########### Generate all_privilege_init_data ###########
  gen_all_privilege_init_data(constants_h_f);
  ###################################################


def generate_h_content():
  global table_name_ids
  global h_f
  core_table_count = 0
  sys_table_count = 0
  virtual_table_count = 0
  sys_view_count = 0

  print_class_head_h()
  new_table_name_postfix_ids = sorted(table_name_postfix_ids, key = lambda table : table[1])
  new_index_name_ids = sorted(index_name_ids, key = lambda index : index[1])

  h_f.write("\npublic:\n")
  method_line = "  static int {0}_schema(share::schema::ObTableSchema &table_schema);\n"
  for (table_name, table_id) in new_table_name_postfix_ids:
    h_f.write(method_line.format(table_name.replace('$', '_').lower().strip('_'), table_id))
  for line in new_index_name_ids:
    h_f.write(method_line.format(line[2].replace('$', '_').strip('_').lower()+'_'+line[0].lower(), line[1]))
  line = """
private:
  DISALLOW_COPY_AND_ASSIGN(ObInnerTableSchema);
};
"""
  h_f.write(line)

  h_f.write("\n")
  h_f.write("typedef int (*schema_create_func)(share::schema::ObTableSchema &table_schema);\n")
  h_f.write("\n")

  method_name = "  ObInnerTableSchema::{0}_schema,\n"
  h_f.write("const schema_create_func all_core_table_schema_creator [] = {\n")
  for (table_name, table_id) in table_name_postfix_ids:
    if table_id == kv_core_table_id:
      h_f.write(method_name.format(table_name.replace('$', '_').lower().strip('_'), table_name))
      core_table_count = core_table_count + 1
  h_f.write("  NULL,};\n\n")

  h_f.write("const schema_create_func core_table_schema_creators [] = {\n")
  for (table_name, table_id) in new_table_name_postfix_ids:
    if is_core_table(table_id) and table_id != kv_core_table_id:
      h_f.write(method_name.format(table_name.replace('$', '_').lower().strip('_'), table_name))
      core_table_count = core_table_count + 1
  h_f.write("  NULL,};\n\n")

  h_f.write("const schema_create_func core_related_table_schema_creators [] = {\n")
  for (table_name, table_id) in new_table_name_postfix_ids:
    if int(table_id) in core_related_tables and not is_virtual_table(table_id):
      h_f.write(method_name.format(table_name.replace('$', '_').lower().strip('_'), table_name))
  h_f.write("  NULL,};\n\n")

  h_f.write("const schema_create_func sys_table_schema_creators [] = {\n")
  for (table_name, table_id) in new_table_name_postfix_ids:
    if is_sys_table(table_id) and not is_core_table(table_id):
      h_f.write(method_name.format(table_name.replace('$', '_').lower().strip('_'), table_name))
      sys_table_count = sys_table_count + 1
  h_f.write("  NULL,};\n\n")

  h_f.write("const schema_create_func virtual_table_schema_creators [] = {\n")
  for (table_name, table_id) in new_table_name_postfix_ids:
    if is_mysql_virtual_table(table_id):
      h_f.write(method_name.format(table_name.replace('$', '_').lower().strip('_'), table_name))
      virtual_table_count = virtual_table_count + 1
  for index_l in new_index_name_ids:
    if is_mysql_virtual_table(index_l[1]):
      h_f.write(method_name.format(index_l[2].replace('$', '_').strip('_').lower()+'_'+index_l[0].lower(), index_l[2]))
      virtual_table_count = virtual_table_count + 1
  for (table_name, table_id) in new_table_name_postfix_ids:
    if is_extended_virtual_table(table_id):
      h_f.write(method_name.format(table_name.replace('$', '_').lower().strip('_'), table_name))
      virtual_table_count = virtual_table_count + 1
  for index_l in new_index_name_ids:
    if is_extended_virtual_table(index_l[1]):
      h_f.write(method_name.format(index_l[2].replace('$', '_').strip('_').lower()+'_'+index_l[0].lower(), index_l[2]))
      virtual_table_count = virtual_table_count + 1
  h_f.write("  NULL,};\n\n")

  h_f.write("const schema_create_func sys_view_schema_creators [] = {\n")
  for (table_name, table_id) in new_table_name_postfix_ids:
    if is_sys_view(table_id):
      h_f.write(method_name.format(table_name.replace('$', '_').lower().strip('_'), table_name))
      sys_view_count = sys_view_count + 1
  h_f.write("  NULL,};\n\n")

  h_f.write("const schema_create_func core_index_table_schema_creators [] = {\n")
  for index_l in index_name_ids:
    if is_core_index_table(index_l[1]):
      h_f.write(method_name.format(index_l[2].replace('$', '_').strip('_').lower()+'_'+index_l[0].lower(), index_l[2]))
  h_f.write("  NULL,};\n\n")

  h_f.write("const schema_create_func sys_index_table_schema_creators [] = {\n")
  for index_l in index_name_ids:
    if not is_core_index_table(index_l[1]) and is_sys_index_table(index_l[1]):
      h_f.write(method_name.format(index_l[2].replace('$', '_').strip('_').lower()+'_'+index_l[0].lower(), index_l[2]))
  h_f.write("  NULL,};\n\n")

  # just to make test happy
  h_f.write("const schema_create_func information_schema_table_schema_creators[] = {\n")
  h_f.write("  NULL,};\n\n")
  h_f.write("const schema_create_func mysql_table_schema_creators[] = {\n")
  h_f.write("  NULL,};\n\n")


  h_f.write("const uint64_t runtime_space_tables [] = {")
  for name in runtime_space_tables:
    h_f.write("\n  {0},".format(name))
  h_f.write("  };\n\n")

  h_f.write("const char* const runtime_space_table_names [] = {")
  for name in runtime_space_table_names:
    h_f.write("\n  {0},".format(name))
  h_f.write("  };\n\n")

  h_f.write("const uint64_t cluster_distributed_vtables [] = {")
  for name in cluster_distributed_vtables:
    h_f.write("\n  {0},".format(name))
  h_f.write("  };\n\n")

  global restrict_access_virtual_tables
  h_f.write("const uint64_t restrict_access_virtual_tables[] = {\n  "
      + ",\n  ".join(restrict_access_virtual_tables) + "  };\n\n")
  h_f.write("""
static inline bool is_restrict_access_virtual_table(const uint64_t tid)
{
  bool found = false;
  for (int64_t i = 0; i < ARRAYSIZEOF(restrict_access_virtual_tables) && !found; i++) {
    if (tid == restrict_access_virtual_tables[i]) {
      found = true;
    }
  }
  return found;
}

""")

  h_f.write("static inline bool is_runtime_table(const uint64_t tid)\n");
  h_f.write("{\n");
  h_f.write("  bool in_runtime_space = false;\n");
  h_f.write("  for (int64_t i = 0; i < ARRAYSIZEOF(runtime_space_tables); ++i) {\n");
  h_f.write("    if (tid == runtime_space_tables[i]) {\n");
  h_f.write("      in_runtime_space = true;\n");
  h_f.write("      break;\n");
  h_f.write("    }\n");
  h_f.write("  }\n");
  h_f.write("  return in_runtime_space;\n");
  h_f.write("}\n\n");

  h_f.write("static inline bool is_runtime_table_name(const common::ObString &tname)\n");
  h_f.write("{\n");
  h_f.write("  bool in_runtime_space = false;\n");
  h_f.write("  for (int64_t i = 0; i < ARRAYSIZEOF(runtime_space_table_names); ++i) {\n");
  h_f.write("    if (0 == tname.case_compare(runtime_space_table_names[i])) {\n");
  h_f.write("      in_runtime_space = true;\n");
  h_f.write("      break;\n");
  h_f.write("    }\n");
  h_f.write("  }\n");
  h_f.write("  return in_runtime_space;\n");
  h_f.write("}\n\n");

  h_f.write("static inline bool is_system_virtual_table(const uint64_t tid)\n");
  h_f.write("{\n");
  h_f.write("  return common::is_virtual_table(tid) && !is_runtime_table(tid);\n");
  h_f.write("}\n\n");

  h_f.write("static inline bool is_runtime_virtual_table(const uint64_t tid)\n");
  h_f.write("{\n");
  h_f.write("  return common::is_virtual_table(tid) && is_runtime_table(tid);\n");
  h_f.write("}\n\n");

  h_f.write("static inline bool is_cluster_distributed_vtables(const uint64_t tid)\n");
  h_f.write("{\n");
  h_f.write("  bool bret = false;\n");
  h_f.write("  for (int64_t i = 0; !bret && i < ARRAYSIZEOF(cluster_distributed_vtables); ++i) {\n");
  h_f.write("    if (tid == cluster_distributed_vtables[i]) {\n");
  h_f.write("      bret = true;\n");
  h_f.write("    }\n");
  h_f.write("  }\n");
  h_f.write("  return bret;\n");
  h_f.write("}\n\n");

  # define lob aux mapping
  h_f.write("/* lob aux table mapping for sys table */\n")
  h_f.write("struct LOBMapping\n")
  h_f.write("{\n")
  h_f.write("  uint64_t data_table_tid_;\n")
  h_f.write("  uint64_t lob_meta_tid_;\n")
  h_f.write("  uint64_t lob_piece_tid_;\n")
  h_f.write("  schema_create_func lob_meta_func_;\n")
  h_f.write("  schema_create_func lob_piece_func_;\n")
  h_f.write("};\n\n")
  h_f.write("LOBMapping const lob_aux_table_mappings [] = {\n")
  for i in range(0, len(lob_aux_ids), 2):
    meta_info = lob_aux_ids[i]
    piece_info = lob_aux_ids[i + 1]
    dtid = table_name2tid(meta_info[1])
    mtid = table_name2tid(meta_info[1] + '_aux_lob_meta')
    ptid = table_name2tid(piece_info[1] + '_aux_lob_piece')
    h_f.write("  {\n")
    h_f.write("    {0},\n".format(dtid))
    h_f.write("    {0},\n".format(mtid))
    h_f.write("    {0},\n".format(ptid))
    meta_method_name = meta_info[1] + "_AUX_LOB_META"
    piece_method_name = piece_info[1] + "_AUX_LOB_PIECE"
    h_f.write("    ObInnerTableSchema::{0}_schema,\n".format(meta_method_name.replace('$', '_').lower().strip('_'), meta_method_name))
    h_f.write("    ObInnerTableSchema::{0}_schema\n".format(piece_method_name.replace('$', '_').lower().strip('_'), piece_method_name))
    h_f.write("  },\n\n")
  h_f.write("};\n\n")

  h_f.write("static inline bool get_sys_table_lob_aux_table_id(const uint64_t tid, uint64_t& meta_tid, uint64_t& piece_tid)\n");
  h_f.write("{\n");
  h_f.write("  bool bret = false;\n");
  h_f.write("  meta_tid = OB_INVALID_ID;\n");
  h_f.write("  piece_tid = OB_INVALID_ID;\n");
  h_f.write("  if (OB_ALL_CORE_TABLE_TID == tid) {\n");
  h_f.write("    // __all_core_table do not need lob aux table, return false\n");
  h_f.write("  } else if (is_system_table(tid)) {\n");
  h_f.write("    bret = true;\n");
  h_f.write("    meta_tid = tid + OB_MIN_SYS_LOB_META_TABLE_ID;\n");
  h_f.write("    piece_tid = tid + OB_MIN_SYS_LOB_PIECE_TABLE_ID;\n");
  h_f.write("  }\n");
  h_f.write("  return bret;\n");
  h_f.write("}\n\n");
  h_f.write("typedef common::hash::ObHashMap<uint64_t, LOBMapping> inner_lob_map_t;\n")
  h_f.write("extern inner_lob_map_t inner_lob_map;\n")
  h_f.write("extern bool inited_lob;\n")
  h_f.write("static inline int get_sys_table_lob_aux_schema(const uint64_t tid,\n");
  h_f.write("                                               share::schema::ObTableSchema& meta_schema,\n");
  h_f.write("                                               share::schema::ObTableSchema& piece_schema)\n");
  h_f.write("{\n");
  h_f.write("  int ret = OB_SUCCESS;\n");
  h_f.write("  LOBMapping item;\n");
  h_f.write("  if (OB_FAIL(inner_lob_map.get_refactored(tid, item))) {\n");
  h_f.write("    SERVER_LOG(WARN, \"fail to get lob mapping item\", K(ret), K(tid), K(inited_lob));\n");
  h_f.write("  } else if (OB_FAIL(item.lob_meta_func_(meta_schema))) {\n");
  h_f.write("    SERVER_LOG(WARN, \"fail to build lob meta schema\", K(ret), K(tid));\n");
  h_f.write("  } else if (OB_FAIL(item.lob_piece_func_(piece_schema))) {\n");
  h_f.write("    SERVER_LOG(WARN, \"fail to build lob piece schema\", K(ret), K(tid));\n");
  h_f.write("  }\n");
  h_f.write("  return ret;\n");
  h_f.write("}\n\n");

  runtime_table_count = 1 + core_table_count + sys_table_count + virtual_table_count + sys_view_count
  core_schema_version = 1
  bootstrap_version = core_schema_version + runtime_table_count + 2
  h_f.write("const int64_t OB_CORE_TABLE_COUNT = %d;\n" % core_table_count)
  h_f.write("const int64_t OB_SYS_TABLE_COUNT = %d;\n" % sys_table_count)
  h_f.write("const int64_t OB_VIRTUAL_TABLE_COUNT = %d;\n" % virtual_table_count)
  h_f.write("const int64_t OB_SYS_VIEW_COUNT = %d;\n" % sys_view_count)
  h_f.write("const int64_t OB_RUNTIME_TABLE_COUNT = %d;\n" % runtime_table_count)
  h_f.write("const int64_t OB_CORE_SCHEMA_VERSION = %d;\n" % core_schema_version)
  h_f.write("const int64_t OB_BOOTSTRAP_SCHEMA_VERSION = %d;\n" % bootstrap_version)

  for (table_name, table_id, base_table_name, base_table_name1, base_table_name2) in table_name_ids:
    if table_id >= max_sys_index_id:
      raise IOError("invalid table_id: {0} table_name:{1}".format(table_id, table_name))

def end_generate_h():
  global h_f
  end = """
} // end namespace share
} // end namespace oceanbase
#endif /* _OB_INNER_TABLE_SCHEMA_H_ */
"""
  h_f.write(end)
  h_f.close()

def end_generate_constants_h():
  global constants_h_f
  global id_to_name_f
  end = """
} // end namespace share
} // end namespace oceanbase
#endif /* _OB_INNER_TABLE_SCHEMA_CONSTANTS_H_ */
"""
  constants_h_f.write(end)
  constants_h_f.close()
  id_to_name_f.close()

def write_lob_mapping_cpp(h_file_name):
  global cpp_f
  cpp_f = open(share_out_path(h_file_name), 'w')
  head = copyright + """
#define USING_LOG_PREFIX SHARE_SCHEMA
#include "ob_inner_table_schema.h"

namespace oceanbase
{
namespace share
{
"""
  cpp_f.write(head)

  cpp_f.write("inner_lob_map_t inner_lob_map;\n")
  cpp_f.write("bool lob_mapping_init()\n")
  cpp_f.write("{\n")
  cpp_f.write("  int ret = OB_SUCCESS;\n")
  bucket_cnt = len(lob_aux_ids)/2
  cpp_f.write("  if (OB_FAIL(inner_lob_map.create(%d, ObModIds::OB_INNER_LOB_HASH_SET))) {\n" % bucket_cnt);
  cpp_f.write("    SERVER_LOG(WARN, \"fail to create inner lob map\", K(ret));\n")
  cpp_f.write("  } else {\n")
  cpp_f.write("    for (int64_t i = 0; OB_SUCC(ret) && i < ARRAYSIZEOF(lob_aux_table_mappings); ++i) {\n")
  cpp_f.write("      if (OB_FAIL(inner_lob_map.set_refactored(lob_aux_table_mappings[i].data_table_tid_, lob_aux_table_mappings[i]))) {\n")
  cpp_f.write("        SERVER_LOG(WARN, \"fail to set inner lob map\", K(ret), K(i));\n")
  cpp_f.write("      }\n")
  cpp_f.write("    }\n")
  # cpp_f.write("    if (OB_SUCC(ret)) {\n")
  # cpp_f.write("      has_init = true;\n")
  # cpp_f.write("    }\n")
  cpp_f.write("  }\n")
  cpp_f.write("  return (ret == OB_SUCCESS);\n")
  cpp_f.write("} // end define lob_mappings\n\n")

  cpp_f.write("bool inited_lob = lob_mapping_init();\n")
  end = """
} // end namespace share
} // end namespace oceanbase
"""
  cpp_f.write(end)
  cpp_f.close()


def start_generate_misc_data(fname):
  f = open(share_out_path(fname), 'w')
  f.write(copyright)
  return f


if __name__ == "__main__":
  args = parse_args(sys.argv[1:])
  configure_paths(args)
  global ob_virtual_index_table_id
  ob_virtual_index_table_id = max_ob_virtual_table_id - 1
  ora_virtual_index_table_id = max_ora_virtual_table_id - 1

  clean_files("ob_inner_table_schema.*")
  exec(compile(open("ob_inner_table_schema_def.py", "rb").read(), "ob_inner_table_schema_def.py", 'exec'))
  def_all_lob_aux_table()
  end_generate_cpp()

  start_generate_h("ob_inner_table_schema.h")
  generate_h_content()
  end_generate_h()

  start_generate_constants_h("ob_inner_table_schema_constants.h")
  generate_constants_h_content()
  end_generate_constants_h()

  ## write virtual table for init virtual table information
  write_lob_mapping_cpp("ob_inner_table_schema.lob.cpp")
  f = start_generate_misc_data("ob_inner_table_schema_misc.ipp")
  generate_cluster_private_table(f)
  generate_sys_index_table_misc_data(f)
  generate_sqlite_create_table_statements(f)
  generate_sqlite_virtual_table_registration(f)

  f.close()

  # Generate SQLite virtual table C++ files
  generate_sqlite_virtual_table_cpp_files()
  log_info("Successfully generate C++ files for SQLite virtual tables.")
