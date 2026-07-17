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

#include <stdio.h>
#include "sql/parser/ob_non_reserved_keywords.h"
#include "pl/parser/pl_parser_mysql_mode_tab.h"

static t_node *mysql_pl_none_reserved_keywords_root = NULL;

/* List of non-reserved keywords */
/*Initially, a trie tree will be built for these words. For each lookup, the tree remains fixed.
 *If the new keyword contains characters other than uppercase and lowercase letters, '_', and numbers, please contact @YeTi to modify this tree.
 *The implementation does not need to guarantee lexicographical order, but it is still advisable to maintain lexicographical order for ease of maintenance and lookup.*/
static const NonReservedKeyword Mysql_pl_none_reserved_keywords[] =
{
  {"after", AFTER},
  {"at", AT},
  {"begin", BEGIN_KEY},
  {"before", BEFORE},
  {"binary_integer", BINARY_INTEGER},
  {"body", BODY},
  {"by", BY},
  {"case", CASE},
  {"catalog_name", CATALOG_NAME},
  {"class_origin", CLASS_ORIGIN},
  {"close", CLOSE},
  {"column_name", COLUMN_NAME},
  {"comment", COMMENT},
  {"commit", COMMIT},
  {"compile", COMPILE},
  {"completion", COMPLETION},
  {"condition", CONDITION},
  {"constraint_catalog", CONSTRAINT_CATALOG},
  {"constraint_name", CONSTRAINT_NAME},
  {"constraint_schema", CONSTRAINT_SCHEMA},
  {"continue", CONTINUE},
  //{"count", COUNT},
  {"current_user", CURRENT_USER},
  {"cursor", CURSOR},
  {"cursor_name", CURSOR_NAME},
  {"declare", DECLARE},
  {"default", DEFAULT},
  {"definer", DEFINER},
  {"delete", DELETE},
  {"deterministic", DETERMINISTIC},
  {"disable", DISABLE},
  {"each", EACH},
  {"else", ELSE},
  {"elseif", ELSEIF},
  {"enable", ENABLE},
  {"end", END_KEY},
  {"ends", ENDS},
  {"event", EVENT},
  {"every", EVERY},
  {"exists", EXISTS},
  {"exit", EXIT},
  {"extend", EXTEND},
  {"for", FOR},
  {"found", FOUND},
  {"from", FROM},
  {"handler", HANDLER},
  {"if", IF},
  {"in", IN},
  {"insert", INSERT},
  {"is", IS},
  {"inout", INOUT},
  {"iterate", ITERATE},
  {"leave", LEAVE},
  {"limit", LIMIT},
  {"long", LONG},
  {"loop", LOOP},
  {"message_text", MESSAGE_TEXT},
  {"mysql_errno", MYSQL_ERRNO},
  {"national", NATIONAL},
  {"nchar", NCHAR},
  {"next", NEXT},
  {"not", NOT},
  {"nvarchar", NVARCHAR},
  {"of", OF},
  {"on", ON},
  {"open", OPEN},
  {"out", OUT},
  {"package", PACKAGE},
  {"precedes", PRECEDES},
  {"table_name", TABLE_NAME},
  {"then", THEN},
  {"type", TYPE},
  {"record", RECORD},
  {"repeat", REPEAT},
  {"resignal", RESIGNAL},
  {"return", RETURN},
  {"returns", RETURNS},
  {"reuse", REUSE},
  {"rollback", ROLLBACK},
  {"row", ROW},
  {"rowtype", ROWTYPE},
  {"role", ROLE},
  {"schedule", SCHEDULE},
  {"schema_name", SCHEMA_NAME},
  {"signal", SIGNAL},
  {"sqlexception", SQLEXCEPTION},
  {"sqlstate", SQLSTATE},
  {"sqlwarning", SQLWARNING},
  {"starts", STARTS},
  {"subclass_origin", SUBCLASS_ORIGIN},
  {"preserve", PRESERVE},
  {"until", UNTIL},
  {"update", UPDATE},
  {"user", USER},
  {"using", USING},
  {"when", WHEN},
  {"while", WHILE},
  {"language", LANGUAGE},
  {"sql", SQL},
  {"no", NO},
  {"contains", CONTAINS},
  {"reads", READS},
  {"modifies", MODIFIES},
  {"data", DATA},
  {"constraint_origin", CONSTRAINT_ORIGIN},
  {"invoker", INVOKER},
  {"security", SECURITY},
  {"tinyint", TINYINT},
  {"smallint", SMALLINT},
  {"mediumint", MEDIUMINT},
  {"middleint", MEDIUMINT},
  {"integer", INTEGER},
  {"int", INTEGER},
  {"int1", TINYINT},
  {"int2", SMALLINT},
  {"int3", MEDIUMINT},
  {"int4", INTEGER},
  {"int8", BIGINT},
  {"bigint", BIGINT},
  {"fetch", FETCH},
  {"float", FLOAT},
  {"float4", FLOAT},
  {"float8", DOUBLE},
  {"follows", FOLLOWS},
  {"double", DOUBLE},
  {"precision", PRECISION},
  {"dec", DEC},
  {"decimal", DECIMAL},
  {"numeric", NUMERIC},
  {"real", DOUBLE},
  {"bit", BIT},
  {"datetime", DATETIME},
  {"timestamp", TIMESTAMP},
  {"time", TIME},
  {"date", DATE},
  {"year", YEAR},
  {"settings", SETTINGS},
  {"month", MONTH},
  {"day", DAY},
  {"hour", HOUR},
  {"minute", MINUTE},
  {"second", SECOND},
  {"character", CHARACTER},
  {"char", CHARACTER},
  {"text", TEXT},
  {"value", VALUE},
  {"varchar", VARCHAR},
  {"varcharacter", VARCHAR},
  {"varying", VARYING},
  {"varbinary", VARBINARY},
  {"unsigned", UNSIGNED},
  {"signed", SIGNED},
  {"zerofill", ZEROFILL},
  {"collate", COLLATE},
  {"charset", CHARSET},
  {"bool", BOOL},
  {"boolean", BOOLEAN},
  {"enum", ENUM},
  {"tinytext", TINYTEXT},
  {"mediumtext", MEDIUMTEXT},
  {"longtext", LONGTEXT},
  {"blob", BLOB},
  {"tinyblob", TINYBLOB},
  {"mediumblob", MEDIUMBLOB},
  {"longblob", LONGBLOB},
  {"fixed", FIXED},
  {"authid", AUTHID},
  {"or", OR},
  {"replace", REPLACE},
  {"pragma", PRAGMA},
  {"interface", INTERFACE},
  {"c", C},
  {"submit", SUBMIT},
  {"job", JOB},
  {"cancel", CANCEL},
  {"xa", XA},
  {"recover", RECOVER},
  {"polygon", POLYGON},
  {"multipoint", MULTIPOINT},
  {"point", POINT},
  {"linestring", LINESTRING},
  {"geometry", GEOMETRY},
  {"multilinestring", MULTILINESTRING},
  {"multipolygon", MULTIPOLYGON},
  {"geometrycollection", GEOMETRYCOLLECTION},
  {"geomcollection", GEOMCOLLECTION},
  {"roaringbitmap", ROARINGBITMAP},
  {"interval", INTERVAL},
  {"to", TO},
  {"serial", SERIAL}
};

const NonReservedKeyword *mysql_pl_non_reserved_keyword_lookup(const char *word)
{
  return find_word(word, mysql_pl_none_reserved_keywords_root, Mysql_pl_none_reserved_keywords);
}

//return 0 if succ, return 1 if fail
int create_mysql_pl_trie_tree()
{
  int ret = 0;
  if (0 != (ret = create_trie_tree(Mysql_pl_none_reserved_keywords, LENGTH_OF(Mysql_pl_none_reserved_keywords), &mysql_pl_none_reserved_keywords_root))) {
    (void)printf("ERROR create trie tree failed! \n");
  }
  return ret;
}

void  __attribute__((constructor)) init_mysql_pl_non_reserved_keywords_tree()
{
  int ret = 0;
  if (0 != (ret = create_mysql_pl_trie_tree())) {
    (void)printf("ERROR build mysql_pl_non_reserved_keywords tree failed=>%d", ret);
  }
}
