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
