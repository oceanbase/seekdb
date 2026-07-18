/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "lib/allocator/page_arena.h"
#include "lib/ob_errno.h"
#include "lib/string/ob_string.h"
#include "sql/parser/ob_parser.h"
#include "sql/resolver/cmd/ob_alter_system_stmt.h"
#include "plugin/interface/ob_plugin_ftparser_intf.h"
#include "storage/fts/ob_fts_parser_property.h"
#include "storage/fts/ob_fts_plugin_helper.h"
#include "storage/fts/dict/ob_ft_cache.h"
#include "storage/fts/dict/ob_ft_dict_hub.h"

// 测试仅需检查词典描述构造，不触发分词和缓存加载；开放 private 以覆盖该内部边界。
#define private public
#include "storage/fts/ob_ik_ft_parser.h"
#include "sql/engine/expr/ob_expr_tokenize.h"
#undef private

#include <cstring>
#include <gtest/gtest.h>

using namespace oceanbase::common;
using namespace oceanbase::storage;

namespace oceanbase
{
namespace storage
{

class TestFtsCustomDict : public ::testing::Test
{
public:
  static void SetUpTestCase()
  {
    // 初始化 IK 解析器，供词典属性解析用例复用。
    ik_parser_.set_name_and_version(share::ObPluginName("ik"), 1);
  }

protected:
  static ObFTParser ik_parser_;
};

ObFTParser TestFtsCustomDict::ik_parser_;

// 递归确认 parser 是否按预期生成指定类型的语法节点。
static bool has_parse_node_type(const ParseNode *node, const ObItemType type)
{
  bool found = false;
  if (nullptr != node) {
    found = (type == node->type_);
    for (int32_t i = 0; !found && i < node->num_child_; ++i) {
      found = has_parse_node_type(node->children_[i], type);
    }
  }
  return found;
}

TEST_F(TestFtsCustomDict, ik_parser_property_uses_configured_dictionary_tables)
{
  // 旧 schema 使用 quanitfier_table；解析后仍必须得到用户配置的真实表名。
  const ObString props = R"({"dict_table":"app.main_dict","stopword_table":"app.stop_dict","quanitfier_table":"app.quan_dict"})";
  ObFTParserProperty property;

  ASSERT_EQ(OB_SUCCESS, property.parse_for_parser_helper(ik_parser_, props));
  ASSERT_EQ(ObString("app.main_dict"), property.dict_table_);
  ASSERT_EQ(ObString("app.stop_dict"), property.stopword_table_);
  ASSERT_EQ(ObString("app.quan_dict"), property.quantifier_table_);
}

TEST_F(TestFtsCustomDict, ik_parser_property_keeps_dictionary_names_after_later_parse)
{
  // 运行时属性会跨越临时 JSON 对象生命周期，后续解析不能覆盖先前保存的词典表名。
  ObFTParserProperty first_property;
  ObFTParserProperty second_property;
  const ObString first_props = ObString::make_string(
      R"({"dict_table":"app.first_dict","stopword_table":"app.first_stop","quantifier_table":"app.first_quan"})");
  const ObString second_props = ObString::make_string(
      R"({"dict_table":"app.second_dict","stopword_table":"app.second_stop","quantifier_table":"app.second_quan"})");

  ASSERT_EQ(OB_SUCCESS, first_property.parse_for_parser_helper(ik_parser_, first_props));
  ASSERT_EQ(OB_SUCCESS, second_property.parse_for_parser_helper(ik_parser_, second_props));
  ASSERT_EQ(ObString("app.first_dict"), first_property.dict_table_);
  ASSERT_EQ(ObString("app.first_stop"), first_property.stopword_table_);
  ASSERT_EQ(ObString("app.first_quan"), first_property.quantifier_table_);
}

TEST_F(TestFtsCustomDict, quantifier_table_is_written_with_correct_key)
{
  // 新建索引序列化属性时只写正确拼写，避免继续扩散历史错误键。
  ObFTParserJsonProps json_props;
  ObArenaAllocator allocator;
  ObString json;

  ASSERT_EQ(OB_SUCCESS, json_props.init());
  ASSERT_EQ(OB_SUCCESS, json_props.config_set_quantifier_table(ObString("app.quan_dict")));
  ASSERT_EQ(OB_SUCCESS, json_props.to_format_json(allocator, json));
  ASSERT_NE(nullptr, std::strstr(json.ptr(), "quantifier_table"));
  ASSERT_EQ(nullptr, std::strstr(json.ptr(), "quanitfier_table"));
}

TEST_F(TestFtsCustomDict, dictionary_cache_key_distinguishes_range_id)
{
  // 同一词典的不同 DAT range 不能命中同一份缓存数据。
  ObDictCacheKey first_key(1001, ObFTDictType::DICT_IK_MAIN, 0);
  ObDictCacheKey second_key(1001, ObFTDictType::DICT_IK_MAIN, 1);

  ASSERT_FALSE(first_key == second_key);
}

TEST_F(TestFtsCustomDict, user_dictionary_descriptor_has_isolated_cache_identity)
{
  // 相同类型的两个用户词典必须按 tenant/table/version 得到不同缓存身份。
  ObFTDictDesc first_dict(ObString("app.dict_a"), ObFTDictType::DICT_IK_MAIN,
                          CHARSET_UTF8MB4, CS_TYPE_UTF8MB4_GENERAL_CI,
                          1001, 2001, 1, false);
  ObFTDictDesc second_dict(ObString("app.dict_b"), ObFTDictType::DICT_IK_MAIN,
                           CHARSET_UTF8MB4, CS_TYPE_UTF8MB4_GENERAL_CI,
                           1001, 2002, 1, false);

  ASSERT_FALSE(first_dict.is_builtin());
  ASSERT_FALSE(second_dict.is_builtin());
  ASSERT_NE(first_dict.get_cache_identity(), second_dict.get_cache_identity());
}

TEST_F(TestFtsCustomDict, user_dictionary_without_table_id_uses_name_as_cache_identity)
{
  // 当前运行时暂未传入 table ID 时，不同表名仍必须映射到不同 DAT 缓存。
  ObFTDictDesc first_dict(ObString("app.dict_a"), ObFTDictType::DICT_IK_MAIN,
                          CHARSET_UTF8MB4, CS_TYPE_UTF8MB4_GENERAL_CI,
                          0, 0, 0, false);
  ObFTDictDesc second_dict(ObString("app.dict_b"), ObFTDictType::DICT_IK_MAIN,
                           CHARSET_UTF8MB4, CS_TYPE_UTF8MB4_GENERAL_CI,
                           0, 0, 0, false);

  ASSERT_NE(first_dict.get_cache_identity(), second_dict.get_cache_identity());
}

TEST_F(TestFtsCustomDict, ik_parser_builds_user_dictionary_descriptors_from_properties)
{
  // IK 初始化必须使用属性中的真实表名，不能继续退化为固定的内置词典名称。
  ObArenaAllocator allocator;
  ObFTParserProperty property;
  ObFTDictDesc main_dict("", ObFTDictType::DICT_TYPE_INVALID, CHARSET_INVALID, CS_TYPE_INVALID);
  ObFTDictDesc quantifier_dict("", ObFTDictType::DICT_TYPE_INVALID, CHARSET_INVALID, CS_TYPE_INVALID);
  ObFTDictDesc stopword_dict("", ObFTDictType::DICT_TYPE_INVALID, CHARSET_INVALID, CS_TYPE_INVALID);
  ObIKFTParser parser(allocator, nullptr);

  property.dict_table_ = ObString("app.main_dict");
  property.quantifier_table_ = ObString("app.quantifier_dict");
  property.stopword_table_ = ObString("app.stopword_dict");

  ASSERT_EQ(OB_SUCCESS, parser.build_dict_descs_(property, main_dict, quantifier_dict, stopword_dict));
  ASSERT_EQ(ObString("app.main_dict"), main_dict.name_);
  ASSERT_EQ(ObString("app.quantifier_dict"), quantifier_dict.name_);
  ASSERT_EQ(ObString("app.stopword_dict"), stopword_dict.name_);
  ASSERT_FALSE(main_dict.is_builtin());
  ASSERT_FALSE(quantifier_dict.is_builtin());
  ASSERT_FALSE(stopword_dict.is_builtin());
}

TEST_F(TestFtsCustomDict, ik_parser_keeps_default_auxiliary_dictionaries_builtin)
{
  // 未显式配置量词和停用词时，默认名称必须继续走内嵌 IK 词典，不能扫描空的系统表。
  ObArenaAllocator allocator;
  ObFTParserProperty property;
  ObFTDictDesc main_dict("", ObFTDictType::DICT_TYPE_INVALID, CHARSET_INVALID, CS_TYPE_INVALID);
  ObFTDictDesc quantifier_dict("", ObFTDictType::DICT_TYPE_INVALID, CHARSET_INVALID, CS_TYPE_INVALID);
  ObFTDictDesc stopword_dict("", ObFTDictType::DICT_TYPE_INVALID, CHARSET_INVALID, CS_TYPE_INVALID);
  ObIKFTParser parser(allocator, nullptr);

  property.dict_table_ = ObString::make_string("app.main_dict");
  property.quantifier_table_ = ObString(ObFTSLiteral::FT_DEFAULT_IK_QUANTIFIER_UTF8_TABLE);
  property.stopword_table_ = ObString(ObFTSLiteral::FT_DEFAULT_IK_STOPWORD_UTF8_TABLE);
  ASSERT_EQ(OB_SUCCESS, parser.build_dict_descs_(property, main_dict, quantifier_dict, stopword_dict));
  ASSERT_FALSE(main_dict.is_builtin());
  ASSERT_TRUE(quantifier_dict.is_builtin());
  ASSERT_TRUE(stopword_dict.is_builtin());
}

TEST_F(TestFtsCustomDict, ik_parser_uses_refresh_generation_for_user_dictionary)
{
  // refresh 后构造的 descriptor 必须携带稳定 table ID 与新 generation。
  ObArenaAllocator allocator;
  ObFTDictHub hub;
  ObFTParserProperty property;
  ObFTDictDesc main_dict("", ObFTDictType::DICT_TYPE_INVALID, CHARSET_INVALID, CS_TYPE_INVALID);
  ObFTDictDesc quantifier_dict("", ObFTDictType::DICT_TYPE_INVALID, CHARSET_INVALID, CS_TYPE_INVALID);
  ObFTDictDesc stopword_dict("", ObFTDictType::DICT_TYPE_INVALID, CHARSET_INVALID, CS_TYPE_INVALID);
  ObIKFTParser parser(allocator, &hub);
  int64_t version = 0;

  ASSERT_EQ(OB_SUCCESS, hub.init());
  ASSERT_EQ(OB_SUCCESS, hub.advance_refresh_version(2001, version));
  property.dict_table_ = ObString("app.main_dict");
  property.dict_table_id_ = 2001;
  ASSERT_EQ(OB_SUCCESS, parser.build_dict_descs_(property, main_dict, quantifier_dict, stopword_dict));
  ASSERT_EQ(2001U, main_dict.table_id_);
  ASSERT_EQ(1, main_dict.version_);
  ASSERT_EQ(OB_SUCCESS, hub.destroy());
}

TEST_F(TestFtsCustomDict, ik_parser_keeps_legacy_dictionary_name_when_table_id_is_absent)
{
  // 历史属性 JSON 未持久化 table ID 时，仍要能按表名初始化用户词典，不能因刷新代次查询失败。
  ObArenaAllocator allocator;
  ObFTDictHub hub;
  ObFTParserProperty property;
  ObFTDictDesc main_dict("", ObFTDictType::DICT_TYPE_INVALID, CHARSET_INVALID, CS_TYPE_INVALID);
  ObFTDictDesc quantifier_dict("", ObFTDictType::DICT_TYPE_INVALID, CHARSET_INVALID, CS_TYPE_INVALID);
  ObFTDictDesc stopword_dict("", ObFTDictType::DICT_TYPE_INVALID, CHARSET_INVALID, CS_TYPE_INVALID);
  ObIKFTParser parser(allocator, &hub);

  ASSERT_EQ(OB_SUCCESS, hub.init());
  property.dict_table_ = ObString("app.legacy_dict");
  ASSERT_EQ(OB_INVALID_ID, property.dict_table_id_);
  ASSERT_EQ(OB_SUCCESS, parser.build_dict_descs_(property, main_dict, quantifier_dict, stopword_dict));
  ASSERT_EQ(ObString("app.legacy_dict"), main_dict.name_);
  ASSERT_EQ(0U, main_dict.table_id_);
  ASSERT_EQ(0, main_dict.version_);
  ASSERT_EQ(OB_SUCCESS, hub.destroy());
}

TEST_F(TestFtsCustomDict, create_dictionary_table_accepts_fulltext_dict_option)
{
  // Task3 要求 FULLTEXT_DICT='Y' 能作为 CREATE TABLE 的表选项被 MySQL parser 接受。
  ObArenaAllocator allocator(ObModIds::TEST);
  sql::ObParser parser(allocator, DEFAULT_MYSQL_MODE);
  ParseResult parse_result;
  const ObString sql = ObString::make_string(
      "CREATE TABLE my_dict (word varchar(100) PRIMARY KEY) "
      "ORGANIZATION INDEX DEFAULT CHARSET=utf8mb4 FULLTEXT_DICT='Y'");

  const int ret = parser.parse(sql, parse_result);
  // 语法失败时输出 parser 的原始错误信息，便于定位具体 token。
  SCOPED_TRACE(nullptr == parse_result.error_msg_ ? "" : parse_result.error_msg_);
  SCOPED_TRACE(parse_result.start_col_);
  ASSERT_EQ(OB_SUCCESS, -ret);
  ASSERT_TRUE(has_parse_node_type(parse_result.result_tree_, T_FULLTEXT_DICT));
  parser.free_result(parse_result);
}

TEST_F(TestFtsCustomDict, refresh_fulltext_dictionary_statement_is_parsed)
{
  // 刷新命令必须保留词典表目标，后续 resolver 才能定位并失效对应缓存。
  ObArenaAllocator allocator(ObModIds::TEST);
  sql::ObParser parser(allocator, DEFAULT_MYSQL_MODE);
  ParseResult parse_result;
  const ObString sql = ObString::make_string("ALTER SYSTEM REFRESH FULLTEXT DICT app.my_dict");

  const int ret = parser.parse(sql, parse_result);
  SCOPED_TRACE(nullptr == parse_result.error_msg_ ? "" : parse_result.error_msg_);
  SCOPED_TRACE(parse_result.start_col_);
  ASSERT_EQ(OB_SUCCESS, -ret);
  ASSERT_TRUE(has_parse_node_type(parse_result.result_tree_, T_REFRESH_FULLTEXT_DICT));
  parser.free_result(parse_result);
}

TEST_F(TestFtsCustomDict, refresh_fulltext_dictionary_statement_keeps_target_identity)
{
  // resolver 必须把稳定的 tenant/table ID 交给 executor，不能依赖会变化的裸表名。
  sql::ObRefreshFulltextDictStmt stmt;

  stmt.set_tenant_id(1001);
  stmt.set_dict_table_id(2001);

  ASSERT_TRUE(stmt.is_valid());
  ASSERT_EQ(1001U, stmt.get_tenant_id());
  ASSERT_EQ(2001U, stmt.get_dict_table_id());
}

TEST_F(TestFtsCustomDict, refresh_fulltext_dictionary_statement_keeps_canonical_table_name)
{
  // resolver 在完成 schema 查找前也要保留解析后的表名，供错误提示和后续规范化使用。
  sql::ObRefreshFulltextDictStmt stmt;
  const ObString table_name("app.my_dict");

  ASSERT_EQ(OB_SUCCESS, stmt.set_dict_table_name(table_name));
  ASSERT_EQ(table_name, stmt.get_dict_table_name());
}

TEST_F(TestFtsCustomDict, dictionary_refresh_generation_is_monotonic)
{
  // refresh 后必须产生更大的代次，使后续分词切换到新的 DAT cache key。
  ObFTDictHub hub;
  int64_t version = 0;

  ASSERT_EQ(OB_SUCCESS, hub.init());
  ASSERT_EQ(OB_SUCCESS, hub.get_refresh_version(2001, version));
  ASSERT_EQ(0, version);
  ASSERT_EQ(OB_SUCCESS, hub.advance_refresh_version(2001, version));
  ASSERT_EQ(1, version);
  ASSERT_EQ(OB_SUCCESS, hub.get_refresh_version(2001, version));
  ASSERT_EQ(1, version);
  ASSERT_EQ(OB_SUCCESS, hub.destroy());
}

TEST_F(TestFtsCustomDict, dictionary_table_id_is_persisted_in_parser_properties)
{
  // resolver 写入的内部 table ID 必须能随属性 JSON 保存和恢复。
  ObFTParserJsonProps props;
  ObArenaAllocator allocator;
  ObString json;
  uint64_t table_id = OB_INVALID_ID;

  ASSERT_EQ(OB_SUCCESS, props.init());
  ASSERT_EQ(OB_SUCCESS, props.config_set_dict_table_id(2001));
  ASSERT_EQ(OB_SUCCESS, props.to_format_json(allocator, json));
  ASSERT_NE(nullptr, std::strstr(json.ptr(), "dict_table_id"));
  ASSERT_EQ(OB_SUCCESS, props.config_get_dict_table_id(table_id));
  ASSERT_EQ(2001U, table_id);
}

TEST_F(TestFtsCustomDict, all_dictionary_table_ids_are_persisted_in_parser_properties)
{
  // 主词典、停用词和量词必须各自持有稳定 ID，不能在 refresh 时混用 generation。
  ObFTParserJsonProps props;
  uint64_t stopword_table_id = OB_INVALID_ID;
  uint64_t quantifier_table_id = OB_INVALID_ID;

  ASSERT_EQ(OB_SUCCESS, props.init());
  ASSERT_EQ(OB_SUCCESS, props.config_set_stopword_table_id(2002));
  ASSERT_EQ(OB_SUCCESS, props.config_set_quantifier_table_id(2003));
  ASSERT_EQ(OB_SUCCESS, props.config_get_stopword_table_id(stopword_table_id));
  ASSERT_EQ(OB_SUCCESS, props.config_get_quantifier_table_id(quantifier_table_id));
  ASSERT_EQ(2002U, stopword_table_id);
  ASSERT_EQ(2003U, quantifier_table_id);
}

TEST_F(TestFtsCustomDict, parser_properties_identify_all_referenced_dictionary_tables)
{
  // DDL 保护必须同时识别主词典、停用词和量词，避免遗漏任一种引用关系。
  ObFTParserJsonProps props;

  ASSERT_EQ(OB_SUCCESS, props.init());
  ASSERT_EQ(OB_SUCCESS, props.config_set_dict_table_id(2001));
  ASSERT_EQ(OB_SUCCESS, props.config_set_stopword_table_id(2002));
  ASSERT_EQ(OB_SUCCESS, props.config_set_quantifier_table_id(2003));
  ASSERT_TRUE(props.references_dict_table(2001));
  ASSERT_TRUE(props.references_dict_table(2002));
  ASSERT_TRUE(props.references_dict_table(2003));
  ASSERT_FALSE(props.references_dict_table(2999));
}

TEST_F(TestFtsCustomDict, ik_plugin_parameter_preserves_dictionary_table_ids)
{
  // SQL 到 IK 插件的参数边界必须保留三类词典 ID，刷新代次才能在运行时生效。
  plugin::ObFTIKParam param;

  ASSERT_EQ(OB_INVALID_ID, param.main_dict_table_id_);
  ASSERT_EQ(OB_INVALID_ID, param.quantifier_dict_table_id_);
  ASSERT_EQ(OB_INVALID_ID, param.stopword_dict_table_id_);
  param.main_dict_table_id_ = 2001;
  param.quantifier_dict_table_id_ = 2002;
  param.stopword_dict_table_id_ = 2003;
  ASSERT_EQ(2001U, param.main_dict_table_id_);
  ASSERT_EQ(2002U, param.quantifier_dict_table_id_);
  ASSERT_EQ(2003U, param.stopword_dict_table_id_);
}

TEST_F(TestFtsCustomDict, ik_property_rebuild_adds_default_stopword_dictionary)
{
  // TOKENIZE 与 DDL 共用属性重建；未配置停用词时必须补齐默认停用词词典。
  ObFTParserJsonProps props;
  ObString stopword_table;

  ASSERT_EQ(OB_SUCCESS, props.init());
  ASSERT_EQ(OB_SUCCESS, props.rebuild_props_for_ddl(ObString::make_string("ik.1"),
                                                     CS_TYPE_UTF8MB4_BIN, false));
  ASSERT_EQ(OB_SUCCESS, props.config_get_stopword_table(stopword_table));
  ASSERT_EQ(ObString(ObFTSLiteral::FT_DEFAULT_IK_STOPWORD_UTF8_TABLE), stopword_table);
}

TEST_F(TestFtsCustomDict, ik_property_rebuild_migrates_legacy_quantifier_key)
{
  // 旧属性重建后仅允许保留正确键，避免 TOKENIZE 和新索引继续写入历史拼写。
  ObFTParserJsonProps props;
  ObArenaAllocator allocator;
  ObString json;

  ASSERT_EQ(OB_SUCCESS, props.init());
  ASSERT_EQ(OB_SUCCESS, props.parse_from_valid_str(
                            ObString::make_string(R"({"quanitfier_table":"app.legacy_quan"})")));
  ASSERT_EQ(OB_SUCCESS, props.rebuild_props_for_ddl(ObString::make_string("ik.1"),
                                                     CS_TYPE_UTF8MB4_BIN, false));
  ASSERT_EQ(OB_SUCCESS, props.to_format_json(allocator, json));
  ASSERT_NE(nullptr, std::strstr(json.ptr(), "quantifier_table"));
  ASSERT_EQ(nullptr, std::strstr(json.ptr(), "quanitfier_table"));
}

TEST_F(TestFtsCustomDict, tokenize_reforms_legacy_quantifier_key_to_standard_key)
{
  // TOKENIZE 的 additional_args 必须复用 DDL 属性重建逻辑，避免保留历史错误键。
  sql::ObExprTokenize::TokenizeParam param;

  param.parser_name_ = ObString::make_string("ik.1");
  ASSERT_EQ(OB_SUCCESS, param.reform_parser_properties(
                            ObString::make_string(R"({"quanitfier_table":"app.legacy_quan"})")));
  ASSERT_NE(nullptr, std::strstr(param.properties_.ptr(), "quantifier_table"));
  ASSERT_EQ(nullptr, std::strstr(param.properties_.ptr(), "quanitfier_table"));
}

TEST_F(TestFtsCustomDict, ik_property_rebuild_accepts_internal_dictionary_table_ids)
{
  // resolver 写入的内部词典表 ID 是运行时刷新和 DDL 保护所需元数据，重建属性时不得拒绝。
  ObFTParserJsonProps props;
  uint64_t table_id = OB_INVALID_ID;

  ASSERT_EQ(OB_SUCCESS, props.init());
  ASSERT_EQ(OB_SUCCESS, props.config_set_dict_table(ObString::make_string("app.main_dict")));
  ASSERT_EQ(OB_SUCCESS, props.config_set_dict_table_id(2001));
  ASSERT_EQ(OB_SUCCESS, props.rebuild_props_for_ddl(ObString::make_string("ik.1"),
                                                     CS_TYPE_UTF8MB4_BIN, false));
  ASSERT_EQ(OB_SUCCESS, props.config_get_dict_table_id(table_id));
  ASSERT_EQ(2001U, table_id);
}

} // namespace storage
} // namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -rf test_task3.log");
  OB_LOGGER.set_file_name("test_task3.log", true);
  OB_LOGGER.set_log_level("DEBUG");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
