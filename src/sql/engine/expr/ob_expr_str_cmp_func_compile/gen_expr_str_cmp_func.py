#!/usr/bin/env python3
# -*- coding: UTF-8 -*-

import argparse
import sys
import tempfile
from pathlib import Path

# Scalar evaluators are runtime-collation-aware, so preserve the full set of
# table entries.  Datum comparator instantiation is independently limited by
# SupportedCollection in ob_expr_cmp_func.ipp.
DEFINED_COLLS = [
    "CS_TYPE_GBK_CHINESE_CI",
    "CS_TYPE_UTF8MB4_GENERAL_CI",
    "CS_TYPE_UTF8MB4_BIN",
    "CS_TYPE_UTF16_GENERAL_CI",
    "CS_TYPE_UTF16_BIN",
    "CS_TYPE_BINARY",
    "CS_TYPE_GBK_BIN",
    "CS_TYPE_UTF16_UNICODE_CI",
    "CS_TYPE_UTF8MB4_UNICODE_CI",
    "CS_TYPE_GB18030_CHINESE_CI",
    "CS_TYPE_GB18030_BIN",
    "CS_TYPE_UJIS_JAPANESE_CI",
    "CS_TYPE_UJIS_BIN",
    "CS_TYPE_EUCKR_KOREAN_CI",
    "CS_TYPE_EUCKR_BIN",
    "CS_TYPE_CP932_JAPANESE_CI",
    "CS_TYPE_CP932_BIN",
    "CS_TYPE_EUCJPMS_JAPANESE_CI",
    "CS_TYPE_EUCJPMS_BIN",
    "CS_TYPE_LATIN1_GERMAN1_CI",
    "CS_TYPE_LATIN1_SWEDISH_CI",
    "CS_TYPE_LATIN1_DANISH_CI",
    "CS_TYPE_LATIN1_GERMAN2_CI",
    "CS_TYPE_LATIN1_BIN",
    "CS_TYPE_LATIN1_GENERAL_CI",
    "CS_TYPE_LATIN1_GENERAL_CS",
    "CS_TYPE_LATIN1_SPANISH_CI",
    "CS_TYPE_GB2312_CHINESE_CI",
    "CS_TYPE_GB2312_BIN",
    "CS_TYPE_GB18030_2022_BIN",
    "CS_TYPE_GB18030_2022_PINYIN_CI",
    "CS_TYPE_GB18030_2022_PINYIN_CS",
    "CS_TYPE_GB18030_2022_RADICAL_CI",
    "CS_TYPE_GB18030_2022_RADICAL_CS",
    "CS_TYPE_GB18030_2022_STROKE_CI",
    "CS_TYPE_GB18030_2022_STROKE_CS",
    "CS_TYPE_ASCII_GENERAL_CI",
    "CS_TYPE_ASCII_BIN",
    "CS_TYPE_TIS620_THAI_CI",
    "CS_TYPE_TIS620_BIN",
    "CS_TYPE_UTF16LE_GENERAL_CI",
    "CS_TYPE_UTF16LE_BIN",
    "CS_TYPE_SJIS_JAPANESE_CI",
    "CS_TYPE_SJIS_BIN",
    "CS_TYPE_BIG5_CHINESE_CI",
    "CS_TYPE_BIG5_BIN",
    "CS_TYPE_HKSCS_BIN",
    "CS_TYPE_HKSCS31_BIN",
    "CS_TYPE_UTF8MB4_ICELANDIC_UCA_CI",
    "CS_TYPE_UTF8MB4_LATVIAN_UCA_CI",
    "CS_TYPE_UTF8MB4_ROMANIAN_UCA_CI",
    "CS_TYPE_UTF8MB4_SLOVENIAN_UCA_CI",
    "CS_TYPE_UTF8MB4_POLISH_UCA_CI",
    "CS_TYPE_UTF8MB4_ESTONIAN_UCA_CI",
    "CS_TYPE_UTF8MB4_SPANISH_UCA_CI",
    "CS_TYPE_UTF8MB4_SWEDISH_UCA_CI",
    "CS_TYPE_UTF8MB4_TURKISH_UCA_CI",
    "CS_TYPE_UTF8MB4_CZECH_UCA_CI",
    "CS_TYPE_UTF8MB4_DANISH_UCA_CI",
    "CS_TYPE_UTF8MB4_LITHUANIAN_UCA_CI",
    "CS_TYPE_UTF8MB4_SLOVAK_UCA_CI",
    "CS_TYPE_UTF8MB4_SPANISH2_UCA_CI",
    "CS_TYPE_UTF8MB4_ROMAN_UCA_CI",
    "CS_TYPE_UTF8MB4_PERSIAN_UCA_CI",
    "CS_TYPE_UTF8MB4_ESPERANTO_UCA_CI",
    "CS_TYPE_UTF8MB4_HUNGARIAN_UCA_CI",
    "CS_TYPE_UTF8MB4_SINHALA_UCA_CI",
    "CS_TYPE_UTF8MB4_GERMAN2_UCA_CI",
    "CS_TYPE_UTF8MB4_CROATIAN_UCA_CI",
    "CS_TYPE_UTF8MB4_UNICODE_520_CI",
    "CS_TYPE_UTF8MB4_VIETNAMESE_CI",
    "CS_TYPE_UTF16_ICELANDIC_UCA_CI",
    "CS_TYPE_UTF16_LATVIAN_UCA_CI",
    "CS_TYPE_UTF16_ROMANIAN_UCA_CI",
    "CS_TYPE_UTF16_SLOVENIAN_UCA_CI",
    "CS_TYPE_UTF16_POLISH_UCA_CI",
    "CS_TYPE_UTF16_ESTONIAN_UCA_CI",
    "CS_TYPE_UTF16_SPANISH_UCA_CI",
    "CS_TYPE_UTF16_SWEDISH_UCA_CI",
    "CS_TYPE_UTF16_TURKISH_UCA_CI",
    "CS_TYPE_UTF16_CZECH_UCA_CI",
    "CS_TYPE_UTF16_DANISH_UCA_CI",
    "CS_TYPE_UTF16_LITHUANIAN_UCA_CI",
    "CS_TYPE_UTF16_SLOVAK_UCA_CI",
    "CS_TYPE_UTF16_SPANISH2_UCA_CI",
    "CS_TYPE_UTF16_ROMAN_UCA_CI",
    "CS_TYPE_UTF16_PERSIAN_UCA_CI",
    "CS_TYPE_UTF16_ESPERANTO_UCA_CI",
    "CS_TYPE_UTF16_HUNGARIAN_UCA_CI",
    "CS_TYPE_UTF16_SINHALA_UCA_CI",
    "CS_TYPE_UTF16_GERMAN2_UCA_CI",
    "CS_TYPE_UTF16_CROATIAN_UCA_CI",
    "CS_TYPE_UTF16_UNICODE_520_CI",
    "CS_TYPE_UTF16_VIETNAMESE_CI",
    "CS_TYPE_UTF8MB4_0900_AI_CI",
    "CS_TYPE_UTF8MB4_DE_PB_0900_AI_CI",
    "CS_TYPE_UTF8MB4_IS_0900_AI_CI",
    "CS_TYPE_UTF8MB4_LV_0900_AI_CI",
    "CS_TYPE_UTF8MB4_RO_0900_AI_CI",
    "CS_TYPE_UTF8MB4_SL_0900_AI_CI",
    "CS_TYPE_UTF8MB4_PL_0900_AI_CI",
    "CS_TYPE_UTF8MB4_ET_0900_AI_CI",
    "CS_TYPE_UTF8MB4_ES_0900_AI_CI",
    "CS_TYPE_UTF8MB4_SV_0900_AI_CI",
    "CS_TYPE_UTF8MB4_TR_0900_AI_CI",
    "CS_TYPE_UTF8MB4_CS_0900_AI_CI",
    "CS_TYPE_UTF8MB4_DA_0900_AI_CI",
    "CS_TYPE_UTF8MB4_LT_0900_AI_CI",
    "CS_TYPE_UTF8MB4_SK_0900_AI_CI",
    "CS_TYPE_UTF8MB4_ES_TRAD_0900_AI_CI",
    "CS_TYPE_UTF8MB4_LA_0900_AI_CI",
    "CS_TYPE_UTF8MB4_EO_0900_AI_CI",
    "CS_TYPE_UTF8MB4_HU_0900_AI_CI",
    "CS_TYPE_UTF8MB4_HR_0900_AI_CI",
    "CS_TYPE_UTF8MB4_VI_0900_AI_CI",
    "CS_TYPE_UTF8MB4_0900_AS_CS",
    "CS_TYPE_UTF8MB4_DE_PB_0900_AS_CS",
    "CS_TYPE_UTF8MB4_IS_0900_AS_CS",
    "CS_TYPE_UTF8MB4_LV_0900_AS_CS",
    "CS_TYPE_UTF8MB4_RO_0900_AS_CS",
    "CS_TYPE_UTF8MB4_SL_0900_AS_CS",
    "CS_TYPE_UTF8MB4_PL_0900_AS_CS",
    "CS_TYPE_UTF8MB4_ET_0900_AS_CS",
    "CS_TYPE_UTF8MB4_ES_0900_AS_CS",
    "CS_TYPE_UTF8MB4_SV_0900_AS_CS",
    "CS_TYPE_UTF8MB4_TR_0900_AS_CS",
    "CS_TYPE_UTF8MB4_CS_0900_AS_CS",
    "CS_TYPE_UTF8MB4_DA_0900_AS_CS",
    "CS_TYPE_UTF8MB4_LT_0900_AS_CS",
    "CS_TYPE_UTF8MB4_SK_0900_AS_CS",
    "CS_TYPE_UTF8MB4_ES_TRAD_0900_AS_CS",
    "CS_TYPE_UTF8MB4_LA_0900_AS_CS",
    "CS_TYPE_UTF8MB4_EO_0900_AS_CS",
    "CS_TYPE_UTF8MB4_HU_0900_AS_CS",
    "CS_TYPE_UTF8MB4_HR_0900_AS_CS",
    "CS_TYPE_UTF8MB4_VI_0900_AS_CS",
    "CS_TYPE_UTF8MB4_JA_0900_AS_CS",
    "CS_TYPE_UTF8MB4_JA_0900_AS_CS_KS",
    "CS_TYPE_UTF8MB4_0900_AS_CI",
    "CS_TYPE_UTF8MB4_RU_0900_AI_CI",
    "CS_TYPE_UTF8MB4_RU_0900_AS_CS",
    "CS_TYPE_UTF8MB4_ZH_0900_AS_CS",
    "CS_TYPE_UTF8MB4_0900_BIN",
    "CS_TYPE_UTF8MB4_NB_0900_AI_CI",
    "CS_TYPE_UTF8MB4_NB_0900_AS_CS",
    "CS_TYPE_UTF8MB4_NN_0900_AI_CI",
    "CS_TYPE_UTF8MB4_NN_0900_AS_CS",
    "CS_TYPE_UTF8MB4_SR_LATN_0900_AI_CI",
    "CS_TYPE_UTF8MB4_SR_LATN_0900_AS_CS",
    "CS_TYPE_UTF8MB4_BS_0900_AI_CI",
    "CS_TYPE_UTF8MB4_BS_0900_AS_CS",
    "CS_TYPE_UTF8MB4_BG_0900_AI_CI",
    "CS_TYPE_UTF8MB4_BG_0900_AS_CS",
    "CS_TYPE_UTF8MB4_GL_0900_AI_CI",
    "CS_TYPE_UTF8MB4_GL_0900_AS_CS",
    "CS_TYPE_UTF8MB4_MN_CYRL_0900_AI_CI",
    "CS_TYPE_UTF8MB4_MN_CYRL_0900_AS_CS",
    "CS_TYPE_DEC8_SWEDISH_CI",
    "CS_TYPE_DEC8_BIN",
    "CS_TYPE_CP850_GENERAL_CI",
    "CS_TYPE_CP850_BIN",
    "CS_TYPE_HP8_ENGLISH_CI",
    "CS_TYPE_HP8_BIN",
    "CS_TYPE_MACROMAN_GENERAL_CI",
    "CS_TYPE_MACROMAN_BIN",
    "CS_TYPE_SWE7_SWEDISH_CI",
    "CS_TYPE_SWE7_BIN",
  ]

LICENSE_HEADER = '''/*
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
'''

COMPILE_UNIT_CNT = 8
SCRIPT_DIR = Path(__file__).resolve().parent
SOURCE_MODE = 0o644
PART_GLOB = "ob_expr_str_cmp_func_part_*.cpp"


def render_compile_part(part_idx, collations):
  init_lines = "\n".join(
      "  INIT_COMPILE_STR_FUNC(%s);" % collation for collation in collations)
  return '''{license}

#include "ob_expr_str_cmp_func_common.ipp"

namespace oceanbase
{{
namespace sql
{{
void __init_str_expr_cmp_func_part_{part_idx}()
{{
{init_lines}
}}
}} // end sql
}} // end oceanbase
'''.format(license=LICENSE_HEADER.rstrip(), part_idx=part_idx, init_lines=init_lines)


def render_ctrl_part():
  declarations = "\n".join(
      "extern void __init_str_expr_cmp_func_part_%d();" % i
      for i in range(COMPILE_UNIT_CNT))
  calls = "\n".join(
      "  __init_str_expr_cmp_func_part_%d();" % i
      for i in range(COMPILE_UNIT_CNT))
  return '''{license}

namespace oceanbase
{{
namespace sql
{{
{declarations}

void __init_all_str_expr_cmp_func()
{{
{calls}
}}
}} // end sql
}} // end oceanbase
'''.format(license=LICENSE_HEADER.rstrip(), declarations=declarations, calls=calls)


def generate_sources():
  sources = {}
  unit_size = (len(DEFINED_COLLS) + COMPILE_UNIT_CNT - 1) // COMPILE_UNIT_CNT
  for part_idx in range(COMPILE_UNIT_CNT):
    start = part_idx * unit_size
    collations = DEFINED_COLLS[start:start + unit_size]
    name = "ob_expr_str_cmp_func_part_%d.cpp" % part_idx
    sources[name] = render_compile_part(part_idx, collations)
  sources["ob_expr_str_cmp_func_all.cpp"] = render_ctrl_part()
  return sources


def write_atomic(path, content):
  if path.exists() and path.read_text(encoding="utf-8") == content:
    path.chmod(SOURCE_MODE)
    return
  temp_path = None
  try:
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", dir=str(path.parent),
        prefix=".gen_expr_str_cmp_func.", suffix=".tmp", delete=False) as output:
      temp_path = Path(output.name)
      output.write(content)
    temp_path.chmod(SOURCE_MODE)
    temp_path.replace(path)
  except BaseException:
    if temp_path is not None and temp_path.exists():
      temp_path.unlink()
    raise


def unexpected_part_paths(output_dir, sources):
  expected_names = set(sources)
  return sorted(
      path for path in output_dir.glob(PART_GLOB)
      if path.name not in expected_names)


def check_sources(output_dir, sources):
  stale = []
  for name, content in sources.items():
    path = output_dir / name
    if (not path.exists()
        or path.read_text(encoding="utf-8") != content
        or path.stat().st_mode & 0o777 != SOURCE_MODE):
      stale.append(name)
  stale.extend(path.name for path in unexpected_part_paths(output_dir, sources))
  if stale:
    print("generated string comparison sources are stale: %s" % ", ".join(stale),
          file=sys.stderr)
    return 1
  return 0


def main():
  parser = argparse.ArgumentParser(
      description="Generate sharded string comparison initializers.")
  parser.add_argument("--output-dir", type=Path, default=SCRIPT_DIR)
  parser.add_argument("--check", action="store_true",
                      help="check generated files without changing them")
  args = parser.parse_args()

  output_dir = args.output_dir.resolve()
  sources = generate_sources()
  if args.check:
    return check_sources(output_dir, sources)

  output_dir.mkdir(parents=True, exist_ok=True)
  for path in unexpected_part_paths(output_dir, sources):
    path.unlink()
  for name, content in sources.items():
    write_atomic(output_dir / name, content)
  return 0


if __name__ == "__main__":
  sys.exit(main())
