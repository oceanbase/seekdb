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
 
#include "ob_parser_charset_utils.h"
#include "lib/charset/ob_charset.h"

#ifdef __cplusplus
extern "C"
{
#endif

namespace  oceanbase{

int obcharset_is_gb_charset_of_collation(ObCollationType collation_type, bool *is_gb) {
    int ret = OB_SUCCESS;
    *is_gb = false;
    if (collation_type == CS_TYPE_GBK_CHINESE_CI ||
        collation_type == CS_TYPE_GBK_BIN ||
        collation_type == CS_TYPE_GB2312_CHINESE_CI ||
        collation_type == CS_TYPE_GB2312_BIN ||
        collation_type == CS_TYPE_GB18030_CHINESE_CI ||
        collation_type == CS_TYPE_GB18030_BIN ||
        collation_type == CS_TYPE_GB18030_CHINESE_CS ||
        (collation_type >= CS_TYPE_GB18030_2022_BIN &&
         collation_type <= CS_TYPE_GB18030_2022_STROKE_CS)) {
        *is_gb = true;
    }
    return ret; 
}

int obcharset_is_single_byte_charset_of_collation(ObCollationType collation_type, bool *is_single_byte) {
    int ret = OB_SUCCESS;
    *is_single_byte = false;
    if (collation_type == CS_TYPE_LATIN1_GERMAN1_CI ||
        collation_type == CS_TYPE_LATIN1_SWEDISH_CI ||
        collation_type == CS_TYPE_LATIN1_DANISH_CI ||
        collation_type == CS_TYPE_LATIN1_GERMAN2_CI ||
        collation_type == CS_TYPE_LATIN1_BIN ||
        collation_type == CS_TYPE_LATIN1_GENERAL_CI ||
        collation_type == CS_TYPE_LATIN1_GENERAL_CS ||
        collation_type == CS_TYPE_LATIN1_SPANISH_CI ||
        collation_type == CS_TYPE_ASCII_GENERAL_CI ||
        collation_type == CS_TYPE_ASCII_BIN ||
        collation_type == CS_TYPE_TIS620_BIN ||
        collation_type == CS_TYPE_TIS620_THAI_CI ||
        collation_type == CS_TYPE_DEC8_BIN ||
        collation_type == CS_TYPE_DEC8_SWEDISH_CI ||
        collation_type == CS_TYPE_CP850_GENERAL_CI ||
        collation_type == CS_TYPE_CP850_BIN ||
        collation_type == CS_TYPE_HP8_ENGLISH_CI ||
        collation_type == CS_TYPE_HP8_BIN ||
        collation_type == CS_TYPE_MACROMAN_GENERAL_CI ||
        collation_type == CS_TYPE_MACROMAN_BIN ||
        collation_type == CS_TYPE_SWE7_SWEDISH_CI ||
        collation_type == CS_TYPE_SWE7_BIN) {
        *is_single_byte = true;
    }
    return ret; 
}

int obcharset_is_utf8_charset_of_collation(ObCollationType collation_type, bool *is_utf8) {
    int ret = OB_SUCCESS;
    *is_utf8 = false;
    if (collation_type == CS_TYPE_UTF8MB4_GENERAL_CI ||
        collation_type == CS_TYPE_UTF8MB4_BIN ||
        (collation_type >= CS_TYPE_UTF8MB4_UNICODE_CI &&
         collation_type <= CS_TYPE_UTF8MB4_VIETNAMESE_CI) ||
        collation_type == CS_TYPE_BINARY ||
        (collation_type >= CS_TYPE_UTF8MB4_0900_AI_CI &&
         collation_type <= CS_TYPE_UTF8MB4_MN_CYRL_0900_AS_CS)
    ) {
        *is_utf8 = true;
    }
    return ret;
}

}

#ifdef __cplusplus
}
#endif
