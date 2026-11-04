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
// Standalone: c++ -std=c++17 -Wall -Wextra -Werror -Isrc unittest/share/test_identifier_quote.cpp -o /tmp/identifier-quote && /tmp/identifier-quote
#include "share/ddl/identifier_quote.h"
#include <cassert>
#include <string>
#include <iostream>
using oceanbase::share::append_quoted_identifier;
int main()
{
  const std::pair<std::string,std::string> cases[] = {
    {"plain", "`plain`"}, {"select", "`select`"}, {"a b", "`a b`"},
    {"a`b", "`a``b`"}, {"`", "````"}, {"a``b", "`a````b`"},
    {"列`名", "`列``名`"}, {"a\\b", "`a\\b`"}, {"", "``"},
    {std::string("a\0b",3), std::string("`a\0b`",5)}
  };
  for (const auto &item : cases) {
    for (bool shadow : {false,true}) {
      std::string output;
      int ret=append_quoted_identifier(item.first, shadow ? "__SHADOW_" : "",
          [&](const char *p,size_t n){output.append(p,n);return 0;});
      assert(ret==0);
      std::string expected=item.second;
      if(shadow) expected.insert(1,"__SHADOW_");
      assert(output==expected);
    }
  }
  // Failure at each append boundary is returned and stops subsequent writes.
  for(int fail=1; fail<=7; ++fail) {
    int calls=0;
    int ret=append_quoted_identifier("a`b", "__SHADOW_", [&](const char *,size_t){
      ++calls; return calls==fail ? -4013 : 0;
    });
    if(fail<=6){assert(ret==-4013);assert(calls==fail);}else{assert(ret==0);assert(calls==6);}
  }
  std::cout << "PASS identifier quoting and append failure propagation\n";
}
