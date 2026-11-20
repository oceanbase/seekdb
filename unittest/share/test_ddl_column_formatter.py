# Copyright (c) 2026 OceanBase.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Supplementary formatter check using the production function and API adapters.

This does not link OBLib or replace patched-server mysqltest coverage. Run from
any directory; --source can point at an old source file to demonstrate failure.
Requires Python 3 and a C++17 compiler. Use --sanitize for ASan/UBSan.
"""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2] / 'src/share/ob_ddl_common.cpp')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
source = args.source.read_text()
start=source.index('int ObDDLUtil::generate_column_name_str(\n    const ObColumnNameInfo')
end=source.index('int ObDDLUtil::generate_ddl_schema_hint_str(',start)
pre=r'''
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <iostream>
#include <cstring>
constexpr int OB_SUCCESS=0, OB_ALLOCATE_MEMORY_FAILED=-4013;
#define OB_SUCC(x) ((x)==0)
#define OB_FAIL(x) ((ret=(x))!=0)
struct ObString { std::string value; int length() const {return value.size();} const char *ptr() const {return value.data();} };
struct ObColumnNameInfo { ObString column_name_; bool is_enum_set_need_cast_; bool is_shadow_column_; };
struct ObSqlString {
 std::string value="prefix:"; int calls=0; int fail_at=-1;
 int append(const char *s,int64_t n) {if(++calls==fail_at)return OB_ALLOCATE_MEMORY_FAILED;value.append(s,n);return 0;}
 int append(const char *s){return append(s,strlen(s));}
 int append_fmt(const char *fmt,...) {char b[16384];va_list args;va_start(args,fmt);int n=vsnprintf(b,sizeof(b),fmt,args);va_end(args);if(n<0 || n>=int(sizeof(b)))std::abort();return append(b,n);}
};
struct ObDDLUtil {static int generate_column_name_str(const ObColumnNameInfo &,bool,bool,bool,ObSqlString &);};
'''
post=r'''
std::string quote(const std::string &s) {std::string r="`";for(char c:s){r+=c;if(c=='`')r+=c;}return r+"`";}
int main(){
 std::vector<std::string> names={"id","select","","a`b","`x","x`","``","x``y","列`名",std::string("x\0`z",4)};
 for(int n=1;n<=32;++n)names.push_back(std::string(n,'`'));
 int checks=0, failures=0, faults=0;
 for(const auto &name:names)for(int bits=0;bits<32;++bits){
  bool origin=bits&1,alias=bits&2,comma=bits&4,cast=bits&8,shadow=bits&16;
  ObColumnNameInfo col{{name},cast,shadow};ObSqlString out;
  int rc=ObDDLUtil::generate_column_name_str(col,origin,alias,comma,out);
  std::string expected="prefix:";if(comma)expected+=", ";
  if(origin){if(cast)expected+="cast(";expected+=quote(name);if(cast)expected+=" as unsigned)";}
  if(origin&&alias)expected+=" AS ";
  if(alias)expected+=quote((shadow?"__SHADOW_":"")+name);
  ++checks;if(rc!=0||out.value!=expected)++failures;
  int appends=out.calls;
  for(int n=1;n<=appends;++n){ObSqlString failing;failing.fail_at=n;rc=ObDDLUtil::generate_column_name_str(col,origin,alias,comma,failing);++faults;if(rc!=OB_ALLOCATE_MEMORY_FAILED||failing.calls!=n)++failures;}
 }
 std::cout<<"formatting="<<checks<<" append_failure_injections="<<faults<<" failures="<<failures<<"\n";
 return failures?1:0;
}
'''
with tempfile.TemporaryDirectory(prefix='seekdb-ddl-format-') as directory:
    source_file = Path(directory) / 'check.cpp'
    binary = Path(directory) / 'check'
    source_file.write_text(pre + source[start:end] + post)
    command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-O1', '-g',
               '-Wall', '-Wextra', '-Werror']
    if args.sanitize:
        command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command + [str(source_file), '-o', str(binary)], check=True)
    raise SystemExit(subprocess.run([str(binary)]).returncode)
