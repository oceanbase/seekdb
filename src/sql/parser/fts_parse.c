/**
 * Copyright (c) 2024 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#include "fts_parse.h"
#include "ftsblex_lex.h"
#include "ftsparser_tab.h"  // Bison generated header file


extern int obsql_fts_yyparse(void* yyscanner);

void fts_parse_docment(const char *input, const int length, void * pool, FtsParserResult *ss)
{
    void *scanner = NULL;
    ss->ret_ = 0;
    ss->err_info_.str_ = NULL;
    ss->err_info_.len_ = 0;
    ss->root_ = NULL;
    ss->list_.head = NULL;
    ss->list_.tail = NULL;
    ss->charset_info_ = NULL;
    ss->malloc_pool_ = pool;
    obsql_fts_yylex_init_extra(ss, &scanner);
    YY_BUFFER_STATE bufferState = obsql_fts_yy_scan_bytes(input, length, scanner);  // read string
    ss->yyscanner_ = scanner;
    obsql_fts_yyparse(ss);  // call the parser
    obsql_fts_yy_delete_buffer(bufferState, scanner);  // delete buffer
    obsql_fts_yylex_destroy(scanner);
}
