"""Compile the production share-local quoting helper with a small string adapter.

This focused test does not replace the full database build or SQL regression suite.
Run with python3 unittest/test_ddl_identifier.py (requires a C++ compiler).
"""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
source = (root / "src/share/ob_ddl_common.cpp").read_text()
start = source.index("int append_ddl_column_identifier(")
end = source.index("} // namespace", start)
helper = source[start:end]
harness = r'''
#include <string>
#include <cstring>
#include <cassert>
#include <iostream>
#include <cstdint>
constexpr int OB_SUCCESS = 0;
#define OB_FAIL(expr) ((ret = (expr)) != 0)
#define OB_SUCC(expr) ((expr) == 0)
struct ObString {
  const char *data; int64_t size;
  int64_t length() const { return size; }
  const char *ptr() const { return data; }
};
struct ObSqlString {
  std::string value;
  int fail_after = -1, calls = 0;
  int append(const char *p) { return append(p, std::strlen(p)); }
  int append(const char *p, int64_t n) {
    if (calls++ == fail_after) return -99;
    value.append(p, n); return 0;
  }
};
'''
cases = r'''
int main() {
  const char *names[] = {"plain", "select", "a`b", "`", "``", "`edge`", "two words", ""};
  for (const char *name : names) {
    for (bool shadow : {false, true}) {
      std::string expected = shadow ? "`__SHADOW_" : "`";
      for (const char *p = name; *p; ++p) {
        expected += *p;
        if (*p == '`') expected += '`';
      }
      expected += '`';
      ObSqlString sql;
      assert(append_ddl_column_identifier({name, (int64_t)std::strlen(name)}, shadow, sql) == 0);
      assert(sql.value == expected);
      for (int call = 0; call < sql.calls; ++call) {
        ObSqlString failing;
        failing.fail_after = call;
        assert(append_ddl_column_identifier({name, (int64_t)std::strlen(name)}, shadow, failing) == -99);
        assert(failing.calls == call + 1);
      }
    }
  }
  const char bounded[] = {'a', '`', 'b', 'X'};
  ObSqlString sql;
  assert(append_ddl_column_identifier({bounded, 3}, false, sql) == 0);
  assert(sql.value == "`a``b`");
  std::cout << "PASS: quoting, shadow aliases, bounded strings, and every append failure\n";
}
'''
assert '#include "sql/ob_sql_utils.h"' not in source
with tempfile.TemporaryDirectory() as directory:
    cpp = pathlib.Path(directory) / "test.cpp"
    exe = pathlib.Path(directory) / "test"
    cpp.write_text(harness + helper + cases)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

