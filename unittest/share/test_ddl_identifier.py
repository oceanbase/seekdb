#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Compile the production DDL formatter with small, fault-injectable string adapters.

This focused test does not link or emulate the database. The accompanying DDL
mysqltest exercises index builds on a real server. Run from any directory with:

    python3 unittest/share/test_ddl_identifier.py
    python3 unittest/share/test_ddl_identifier.py --sanitize
    python3 unittest/share/test_ddl_identifier.py --revision <unfixed-commit>

The last command is expected to fail on an unfixed revision. Generated C++ and
the executable are kept in a temporary directory and removed after the run.
"""

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


ADAPTERS = r"""
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#define OB_SUCCESS 0
#define OB_SUCC(value) ((value) == OB_SUCCESS)
#define OB_FAIL(value) ((ret = (value)) != OB_SUCCESS)

struct ObString {
  const char *data;
  int size;
  int length() const { return size; }
  const char *ptr() const { return data; }
};
struct ObColumnNameInfo {
  ObString column_name_;
  bool is_enum_set_need_cast_;
  bool is_shadow_column_;
};
struct ObSqlString {
  static constexpr int WRITE_FAILURE = -777;
  std::string value = "existing ";
  int calls = 0;
  int fail_at = 0;

  int append(const char *text) { return append(text, std::strlen(text)); }
  int append(const char *text, const int64_t length) {
    if (++calls == fail_at) { return WRITE_FAILURE; }
    value.append(text, static_cast<size_t>(length));
    return OB_SUCCESS;
  }
  int append_fmt(const char *format, ...) {
    if (++calls == fail_at) { return WRITE_FAILURE; }
    va_list args;
    va_start(args, format);
    va_list size_args;
    va_copy(size_args, args);
    const int size = std::vsnprintf(nullptr, 0, format, size_args);
    va_end(size_args);
    if (size < 0) { va_end(args); return WRITE_FAILURE; }
    std::vector<char> text(static_cast<size_t>(size) + 1);
    std::vsnprintf(text.data(), text.size(), format, args);
    va_end(args);
    value.append(text.data(), static_cast<size_t>(size));
    return OB_SUCCESS;
  }
};
"""

MATRIX = r"""
static std::string quote(const ObString &name, bool shadow)
{
  std::string result = shadow ? "`__SHADOW_" : "`";
  for (int i = 0; i < name.length(); ++i) {
    result += name.ptr()[i];
    if (name.ptr()[i] == '`') { result += '`'; }
  }
  return result + '`';
}

int main()
{
  int cases = 0;
  int checks = 0;
  int failures = 0;
  int fault_cases = 0;
  auto check = [&](bool ok, const char *label) {
    ++checks;
    if (!ok && failures++ < 10) { std::cerr << "FAIL: " << label << '\n'; }
  };
  std::vector<std::string> names = {
    "plain", "select", "with space", "apostrophe'", "double\"quote", "back\\slash",
    "中文", "cost`usd", "`first", "last`", "two``ticks", "`", "``", "列`名", "",
    std::string(256, '`')
  };
  std::vector<ObString> views;
  for (const auto &name : names) { views.push_back({name.data(), static_cast<int>(name.size())}); }
  const char unterminated[] = {'a', '`', 'b'};
  views.push_back({unterminated, 3});
  const char bounded[] = "short`unused";
  views.push_back({bounded, 5});
  views.push_back({nullptr, 0});

  for (const auto &name : views) {
    for (unsigned flags = 0; flags < 32; ++flags) {
      const bool origin = flags & 1;
      const bool alias = flags & 2;
      const bool comma = flags & 4;
      const bool cast = flags & 8;
      const bool shadow = flags & 16;
      ObColumnNameInfo info{name, cast, shadow};
      std::string expected = "existing ";
      if (comma) { expected += ", "; }
      if (origin) {
        if (cast) { expected += "cast("; }
        expected += quote(name, false);
        if (cast) { expected += " as unsigned)"; }
      }
      if (origin && alias) { expected += " AS "; }
      if (alias) { expected += quote(name, shadow); }
      ObSqlString actual;
      const int ret = generate_column_name_str(info, origin, alias, comma, actual);
      ++cases;
      check(ret == OB_SUCCESS, "successful appends must succeed");
      check(actual.value == expected, "quoted identifier output");
      for (int fail_at = 1; fail_at <= actual.calls; ++fail_at) {
        ObSqlString failing;
        failing.fail_at = fail_at;
        const int failure = generate_column_name_str(info, origin, alias, comma, failing);
        ++fault_cases;
        check(failure == ObSqlString::WRITE_FAILURE, "preserve append failure");
        check(failing.calls == fail_at, "stop appending after failure");
        // A failed append must not erase the caller's existing SQL prefix.
        check(failing.value.compare(0, 9, "existing ") == 0, "preserve caller prefix");
      }
    }
  }
  std::cout << cases << " formatting cases, " << fault_cases << " injected failures, "
            << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
"""


def production_formatter(source):
    """Retain the exact production bodies, replacing only the enclosing class name."""
    helpers = ""
    helper = "static int append_ddl_identifier("
    if helper in source:
        begin = source.index(helper)
        helpers = source[begin:source.index("int ObDDLUtil::generate_column_name_str(", begin)]
    begin = source.index("int ObDDLUtil::generate_column_name_str(\n    const ObColumnNameInfo")
    end = source.index("\nint ObDDLUtil::generate_ddl_schema_hint_str(", begin)
    body = source[begin:end].replace("ObDDLUtil::generate_column_name_str", "generate_column_name_str", 1)
    return helpers + body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", help="read the production source from this local Git revision")
    parser.add_argument("--sanitize", action="store_true", help="enable AddressSanitizer and UBSan")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    relative_source = "src/share/ob_ddl_common.cpp"
    if args.revision:
        source = subprocess.check_output(
            ["git", "show", f"{args.revision}:{relative_source}"], cwd=root, text=True
        )
    else:
        source = (root / relative_source).read_text(encoding="utf-8")
    command = shlex.split(os.environ.get("CXX", "c++"))
    command += ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-O1", "-g"]
    if args.sanitize:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    with tempfile.TemporaryDirectory(prefix="seekdb-ddl-identifier-") as directory:
        executable = Path(directory) / "test_ddl_identifier"
        subprocess.run(
            command + ["-x", "c++", "-", "-o", str(executable)],
            input=ADAPTERS + production_formatter(source) + MATRIX,
            text=True,
            check=True,
        )
        return subprocess.run([str(executable)], check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
