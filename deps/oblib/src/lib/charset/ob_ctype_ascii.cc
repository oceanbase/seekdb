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

#include "lib/charset/ob_ctype.h"
#include "lib/charset/ob_ctype_ascii_tab.h"

static ObCharsetHandler ob_charset_ascii_handler = {
    ob_cset_init_8bit,
    ob_ismbchar_8bit,
    ob_mbcharlen_8bit,
    ob_numchars_8bit,
    ob_charpos_8bit,
    ob_max_bytes_charpos_8bit,
    ob_well_formed_len_ascii,
    ob_lengthsp_8bit,
    //ob_numcells_8bit,
    ob_mb_wc_8bit,
    ob_wc_mb_8bit,
    ob_mb_ctype_8bit,
    //ob_caseup_str_8bit,
    //ob_casedn_str_8bit,
    ob_caseup_8bit,
    ob_casedn_8bit,
    //ob_snprintf_8bit,
    //ob_long10_to_str_8bit,
    //ob_longlong10_to_str_8bit,
    ob_fill_8bit,
    ob_strntol_8bit,
    ob_strntoul_8bit,
    ob_strntoll_8bit,
    ob_strntoull_8bit,
    ob_strntod_8bit,
    //ob_strtoll10_8bit,
    ob_strntoull10rnd_8bit,
    ob_scan_8bit,
    skip_trailing_space
  };

ObCharsetInfo ob_charset_ascii = {
  11,0,0,
  OB_CS_COMPILED | OB_CS_PRIMARY | OB_CS_PUREASCII,
  "ascii",
  "ascii_general_ci",
  "US ASCII",
  NULL,
  NULL,
  ctype_ascii_general_ci,
  to_lower_ascii_general_ci,
  to_upper_ascii_general_ci,
  sort_order_ascii_general_ci,
  NULL,
  to_uni_ascii_general_ci,
  NULL,
  &ob_unicase_default,
  NULL,
  NULL,
  1,
  1,
  1,
  1,
  1,
  1,
  0,
  255,
  ' ',
  false,
  1,
  1,
  &ob_charset_ascii_handler,
  &ob_collation_8bit_simple_ci_handler,
  PAD_SPACE};

ObCharsetInfo ob_charset_ascii_bin = {
  65,0,0,
  OB_CS_COMPILED | OB_CS_BINSORT | OB_CS_PUREASCII,
  "ascii",
  "ascii_bin",
  "US ASCII",
  NULL,
  NULL,
  ctype_ascii_bin,
  to_lower_ascii_bin,
  to_upper_ascii_bin,
  NULL,
  NULL,
  to_uni_ascii_bin,
  nullptr,
  &ob_unicase_default,
  NULL,
  NULL,
  1,
  1,
  1,
  1,
  1,
  1,
  0,
  255,
  ' ',
  false,
  1,
  1,
  &ob_charset_ascii_handler,
  &ob_collation_8bit_bin_handler,
  PAD_SPACE};
