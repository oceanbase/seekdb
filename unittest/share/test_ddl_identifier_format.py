#!/usr/bin/env python3
"""Compile the production DDL formatter body against explicit dependency doubles.

This focused regression does not compile ob_ddl_common.cpp in its full dependency
context and does not run a database. It exercises identifier output and append
failure propagation. For integration coverage, run mysqltest on a built server.
Run with --revision <unpatched-commit> to demonstrate the baseline failure, or
without --revision to test the working tree. --sanitize enables ASan and UBSan.
"""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile

PRELUDE = r"""// Copyright (c) 2025 OceanBase.
// Licensed under the Apache License, Version 2.0.
// See https://www.apache.org/licenses/LICENSE-2.0 for license terms.
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "lib/ob_errno.h"
using namespace oceanbase::common;
#define OB_SUCC(value) ((value) == OB_SUCCESS)
#define OB_FAIL(value) ((ret = (value)) != OB_SUCCESS)
// Dependency doubles implement only the production formatter's string API.
// The formatter body (and patched helper) below is extracted verbatim.
struct ObString {
 std::string value;
 int32_t length() const { return static_cast<int32_t>(value.size()); }
 const char *ptr() const { return value.data(); }
};
struct ObColumnNameInfo { ObString column_name_; bool is_enum_set_need_cast_; bool is_shadow_column_; };
struct ObSqlString {
 std::string value; int fail_after=-1; int calls=0;
 int append(const char *p, const int64_t n) {
  if (calls++==fail_after) return OB_ALLOCATE_MEMORY_FAILED;
  value.append(p,static_cast<size_t>(n)); return OB_SUCCESS;
 }
 int append(const char *p) { return append(p,static_cast<int64_t>(std::strlen(p))); }
 int append_fmt(const char *fmt, ...) {
  va_list args,copy;va_start(args,fmt);va_copy(copy,args);
  int n=std::vsnprintf(nullptr,0,fmt,copy);va_end(copy);
  if(n<0){va_end(args);return OB_ERROR;}
  std::vector<char> data(static_cast<size_t>(n)+1);std::vsnprintf(data.data(),data.size(),fmt,args);va_end(args);
  return append(data.data(),n);
 }
};
struct ObDDLUtil {static int generate_column_name_str(const ObColumnNameInfo &,bool,bool,bool,ObSqlString &);};
"""
CASES = r"""
std::string quoted(const std::string &s, bool shadow) {
 std::string result="`";if(shadow)result+="__SHADOW_";
 for(char c:s){result+=c;if(c=='`')result+='`';}return result+"`";
}
int main(){
 std::vector<std::string> names={"plain","a`b","`leading","trailing`","a``b","列`名","",std::string(64,'`')};
 uint32_t state=1469;
 const std::string alphabet="ab`\"'\\_";
 for(int sample=0;sample<128;++sample){
  std::string value;
  for(int j=0;j<sample%81;++j){
   state=state*1664525U+1013904223U;
   value+=alphabet[state%alphabet.size()];
  }
  names.push_back(value);
 }
 int total=0,fail=0;
 for(const auto &name:names)for(int flags=0;flags<32;++flags){
  bool origin=flags&1,alias=flags&2,comma=flags&4,cast=flags&8,shadow=flags&16;
  ObColumnNameInfo info{{name},cast,shadow};ObSqlString output;
  int ret=ObDDLUtil::generate_column_name_str(info,origin,alias,comma,output);
  std::string expected=comma?", ":"";
  if(origin){if(cast)expected+="cast(";expected+=quoted(name,false);if(cast)expected+=" as unsigned)";}
  if(origin&&alias){expected+=" AS ";}
  if(alias){expected+=quoted(name,shadow);}
  ++total;if(ret!=OB_SUCCESS||output.value!=expected){if(fail++<6)std::cout<<"FAIL flags="<<flags<<" name="<<name<<" actual="<<output.value<<" expected="<<expected<<"\n";}
 }
 ObColumnNameInfo info{{"a`b"},true,true};ObSqlString normal;
 ObDDLUtil::generate_column_name_str(info,true,true,true,normal);
 for(int n=0;n<normal.calls;++n){ObSqlString output;output.fail_after=n;int ret=ObDDLUtil::generate_column_name_str(info,true,true,true,output);++total;if(ret!=OB_ALLOCATE_MEMORY_FAILED || output.calls!=n+1){++fail;std::cout<<"FAIL swallowed append error at "<<n<<"\n";}}
 std::cout<<"formatter cases="<<total<<" failed="<<fail<<"\n";return fail?1:0;
}
"""

def extract_function(source, signature):
    begin = source.index(signature)
    brace = source.index("{", begin)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[begin:end]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", help="read source from a Git revision")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    relative = "src/share/ob_ddl_common.cpp"
    if args.revision:
        source = subprocess.check_output(
            ["git", "show", f"{args.revision}:{relative}"], cwd=repo, text=True)
    else:
        source = (repo / relative).read_text()
    body = extract_function(source,
        "int ObDDLUtil::generate_column_name_str(\n    const ObColumnNameInfo &")
    helper = ""
    signature = "static int append_quoted_ddl_column("
    if signature in source:
        helper = extract_function(source, signature)
    with tempfile.TemporaryDirectory(prefix="ddl_identifier_test_") as temp:
        test = Path(temp) / "formatter.cpp"
        binary = Path(temp) / "formatter"
        test.write_text(PRELUDE + "\n" + helper + "\n" + body + "\n" + CASES)
        command = ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                   "-O1", "-g", "-I" + str(repo / "src/oblib"), str(test),
                   "-o", str(binary)]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        print("COMMAND:", shlex.join(command), flush=True)
        compiled = subprocess.run(command)
        print("compile_exit=", compiled.returncode, flush=True)
        if compiled.returncode:
            return compiled.returncode
        result = subprocess.run([str(binary)])
        print("run_exit=", result.returncode, flush=True)
        return result.returncode

if __name__ == "__main__":
    raise SystemExit(main())
