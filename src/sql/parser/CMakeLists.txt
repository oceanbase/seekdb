# common
set(ob_inner_sql_parser_object_list
  ${ob_inner_sql_parser_object_list}
  sql_parser_mysql_mode_lex.c
  sql_parser_mysql_mode_lex.h
  sql_parser_mysql_mode_tab.c
  sql_parser_mysql_mode_tab.h
  ftsblex_lex.c
  ftsblex_lex.h
  ftsparser_tab.c
  ftsparser_tab.h
  fts_base.h
  fts_base.c
  fts_parse.h
  fts_parse.c
  non_reserved_keywords_mysql_mode.c
  ob_item_type_str.h
  ob_non_reserved_keywords.c
  ob_non_reserved_keywords.h
  ob_sql_parser.cpp
  ob_sql_parser.h
  parse_define.h
  parse_malloc.cpp
  parse_malloc.h
  parse_node.c
  parse_node.h
  parse_node_hash.cpp
  parse_node_hash.h
  ob_char_type.h
  ob_fast_parser.h
  ob_fast_parser.cpp
  ob_parser_charset_utils.cpp
  sql_parser_base.c
  sql_parser_base.h
  sql_parser_base.h
  parser_utility.cpp
  parser_utility.h
  type_name.c
  ob_parse_simd.cpp
)

# parser objects for server parser
set(ob_extra_sql_parser_object_list
  ob_parser.cpp
  ob_parser.h
  ob_parser_utils.h
)

# ob_sql_server_parser_object is the static library for observer
add_library(ob_sql_server_parser_objects OBJECT
  ${ob_inner_sql_parser_object_list}
  ${ob_extra_sql_parser_object_list})

target_link_libraries(ob_sql_server_parser_objects PUBLIC ob_base)

if (WITH_COVERAGE)
  # 排除少量sql解析文件，不参与coverage编译，clang针对于特殊语法文件会core
  config_remove_coverage_flag(ob_sql_server_parser_objects)
endif()

add_library(ob_sql_server_parser_static
  STATIC
  EXCLUDE_FROM_ALL)

target_link_libraries(ob_sql_server_parser_static
  PUBLIC
  ob_sql_server_parser_objects)
