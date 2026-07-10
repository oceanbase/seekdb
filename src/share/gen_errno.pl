#!/usr/bin/env perl
# Copyright 2016 Alibaba Inc. All Rights Reserved.
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# version 2 as published by the Free Software Foundation.
# create date: 06 Nov 2013
# description: script to generate ob_errno.h from ob_errno.def

use strict;
use warnings;
open my $fh, '<', "ob_errno.def";
my %map_share;
my %other_map_share;
my %map_deps;
my %other_map_deps;
my %map;
my %other_map;
my $last_errno = 0;
my $error_count=0;
my $print_error_cause="\"Internal Error\"";
my $print_error_solution="\"Contact OceanBase Support\"";

sub store_error
{
  my ($target_map, $name, $ob_errno, $mysql_errno, $sqlstate, $str_error, $str_user_error, $cause, $solution) = @_;
  $cause = $print_error_cause if (!defined $cause);
  $solution = $print_error_solution if (!defined $solution);
  my $entry = [$ob_errno, $mysql_errno, $sqlstate, $str_error, $str_user_error, "$name", $cause, $solution];
  $target_map->{$name} = $entry;
  $map{$name} = $entry;
  $last_errno = $ob_errno if ($ob_errno < $last_errno);
  return ($ob_errno, $sqlstate, $str_error);
}

while(<$fh>) {
  my $error_msg;
  my $sqlstate;
  my $error_code;

  if (/^DEFINE_ERROR\(([^,]+),\s*([^,]*),\s*([^,]*),\s*([^,]*),\s*("[^"]*"),\s*("[^"]*"),\s*("[^"]*")/) {
    ++$error_count;
    ($error_code, $sqlstate, $error_msg) = store_error(\%map_share, $1, $2, $3, $4, $5, $5, $6, $7);
  } elsif (/^DEFINE_ERROR\(([^,]+),\s*([^,]*),\s*([^,]*),\s*([^,]*),\s*("[^"]*")/) {
    ++$error_count;
    ($error_code, $sqlstate, $error_msg) = store_error(\%map_share, $1, $2, $3, $4, $5, $5);
  } elsif (/^DEFINE_ERROR_EXT\(([^,]+),\s*([^,]*),\s*([^,]*),\s*([^,]*),\s*("[^"]*"),\s*("[^"]*"),\s*("[^"]*"),\s*("[^"]*")/) {
    ++$error_count;
    ($error_code, $sqlstate, $error_msg) = store_error(\%map_share, $1, $2, $3, $4, $5, $6, $7, $8);
  } elsif (/^DEFINE_ERROR_EXT\(([^,]+),\s*([^,]*),\s*([^,]*),\s*([^,]*),\s*("[^"]*"),\s*("[^"]*")/) {
    ++$error_count;
    ($error_code, $sqlstate, $error_msg) = store_error(\%map_share, $1, $2, $3, $4, $5, $6);
  } elsif (/^DEFINE_OTHER_MSG_FMT\(([^,]+),\s*([^,]*),\s*("[^"]*")\s*,\s*("[^"]*")/) {
    $other_map_share{$1} = [$2, $3, $4];
    $other_map{$1} = [$2, $3, $4];
  } elsif (/^DEFINE_ERROR_DEP\(([^,]+),\s*([^,]*),\s*([^,]*),\s*([^,]*),\s*("[^"]*"),\s*("[^"]*"),\s*("[^"]*")/) {
    ++$error_count;
    ($error_code, $sqlstate, $error_msg) = store_error(\%map_deps, $1, $2, $3, $4, $5, $5, $6, $7);
  } elsif (/^DEFINE_ERROR_DEP\(([^,]+),\s*([^,]*),\s*([^,]*),\s*([^,]*),\s*("[^"]*")/) {
    ++$error_count;
    ($error_code, $sqlstate, $error_msg) = store_error(\%map_deps, $1, $2, $3, $4, $5, $5);
  } elsif (/^DEFINE_ERROR_EXT_DEP\(([^,]+),\s*([^,]*),\s*([^,]*),\s*([^,]*),\s*("[^"]*"),\s*("[^"]*"),\s*("[^"]*"),\s*("[^"]*")/) {
    ++$error_count;
    ($error_code, $sqlstate, $error_msg) = store_error(\%map_deps, $1, $2, $3, $4, $5, $6, $7, $8);
  } elsif (/^DEFINE_ERROR_EXT_DEP\(([^,]+),\s*([^,]*),\s*([^,]*),\s*([^,]*),\s*("[^"]*"),\s*("[^"]*")/) {
    ++$error_count;
    ($error_code, $sqlstate, $error_msg) = store_error(\%map_deps, $1, $2, $3, $4, $5, $6);
  } elsif (/^DEFINE_OTHER_MSG_FMT_DEP\(([^,]+),\s*([^,]*),\s*("[^"]*")\s*,\s*("[^"]*")/) {
    $other_map_deps{$1} = [$2, $3, $4];
    $other_map{$1} = [$2, $3, $4];
  }
  if (defined $error_code) {
    print "WARN: undefined SQLSTATE for $1\n" if ($sqlstate eq "\"\"");
    print "WARN: undefined error message for $1\n" if ($error_msg eq "\"\"");
    print "WARN: error code out of range: $1\n" if ($error_code <= -1 && $error_code > -3000);
  }
}

print "total error code: $error_count\n";
print "please wait for writing files ...\n";
# check duplicate error number
my %dedup;
for my $oberr (keys % map) {
  my $errno = $map{$oberr}->[0];
  if (defined $dedup{$errno})
  {
    print "Error: error code($errno) is duplicated for $oberr and $dedup{$errno}\n";
    exit 1;
  } else {
    $dedup{$errno} = $oberr;
  }
}

# sort for share
my @pairs_share = map {[$_, $map_share{$_}->[0] ]} keys %map_share;
my @sorted_share = sort {$b->[1] <=> $a->[1]} @pairs_share;
my @errors_share = map {$_->[0]} @sorted_share;

# sort for deps
my @pairs_deps = map {[$_, $map_deps{$_}->[0] ]} keys %map_deps;
my @sorted_deps = sort {$b->[1] <=> $a->[1]} @pairs_deps;
my @errors_deps = map {$_->[0]} @sorted_deps;

# sort for all
my @pairs = map {[$_, $map{$_}->[0] ]} keys %map;
my @sorted = sort {$b->[1] <=> $a->[1]} @pairs;
my @errors = map {$_->[0]} @sorted;
my @errnos = reverse sort { $a <=> $b } map {$map{$_}->[0]} keys %map;

# generate share/ob_errno.h
open my $fh_header, '>', "ob_errno.h";
print $fh_header '
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

// DO NOT EDIT. This file is automatically generated from `ob_errno.def\'.
// ob_errno.h
//   Author:
//   Normalizer:

#ifndef OCEANBASE_LIB_OB_ERRNO_H_
#define OCEANBASE_LIB_OB_ERRNO_H_
#include <stdint.h>
#include "share/mysql_errno.h"
#include "lib/ob_errno.h"

namespace oceanbase
{
namespace common
{
';
  print $fh_header "
constexpr int OB_LAST_ERROR_CODE = $last_errno;
constexpr int OB_ERR_SQL_START = -5000;
constexpr int OB_ERR_SQL_END = -5999;
";
  for my $oberr (@errors_share) {
    if (system "grep $oberr ../../deps/oblib/src/lib/ob_errno.h >/dev/null") {
      print $fh_header "constexpr int $oberr = $map_share{$oberr}->[0];\n";
    }
  }
  foreach my $oberr (sort keys %other_map){
    if (system "grep $oberr ../../deps/oblib/src/lib/ob_errno.h >/dev/null") {
      my $errno;
      if (exists($map{$other_map{$oberr}->[0]})){
        $errno = $map{$other_map{$oberr}->[0]}->[0];
      } else {
        print "Error: error code($other_map{$oberr}->[0]) is not exists\n";
        exit 1;
      }
      print $fh_header "constexpr int $oberr = $errno;\n";
    }
  }
  print $fh_header "\n\n";
  for my $oberr (@errors) {
    print $fh_header "#define ${oberr}__USER_ERROR_MSG $map{$oberr}->[4]\n";
  }
  foreach my $oberr (sort keys %other_map){
    print $fh_header "#define ${oberr}__USER_ERROR_MSG $other_map{$oberr}->[1]\n";
  }
  print $fh_header "\nextern int g_all_ob_errnos[${\(scalar @errnos)}];";

  print $fh_header '

  const char *ob_error_name(const int oberr);
  const char* ob_error_cause(const int oberr);
  const char* ob_error_solution(const int oberr);

  int ob_mysql_errno(const int oberr);
  int ob_mysql_errno_with_check(const int oberr);
  const char *ob_sqlstate(const int oberr);
  const char *ob_strerror(const int oberr);
  const char *ob_str_user_error(const int oberr);

  int ob_errpkt_errno(const int oberr);
  const char *ob_errpkt_strerror(const int oberr);
  const char *ob_errpkt_str_user_error(const int oberr);


} // end namespace common
} // end namespace oceanbase

#endif //OCEANBASE_LIB_OB_ERRNO_H_
';

#generate dep/ob_errno.h
open my $fh_header_dep, '>', "../../deps/oblib/src/lib/ob_errno.h";
print $fh_header_dep '/*
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

// DO NOT EDIT. This file is automatically generated from ob_errno.def.
// DO NOT EDIT. This file is automatically generated from ob_errno.def.
// DO NOT EDIT. This file is automatically generated from ob_errno.def.
// To add errno in this header file, you should use DEFINE_***_DEP to define errno in ob_errno.def
// For any question, call fyy280124
#ifndef OB_ERRNO_H
#define OB_ERRNO_H

namespace oceanbase {
namespace common {

constexpr int OB_MAX_ERROR_CODE                      = 65535;
';

for my $oberr (@errors_deps) {
  print $fh_header_dep "\nconstexpr int $oberr = $map_deps{$oberr}->[0];";
}

print $fh_header_dep '
constexpr int OB_MAX_RAISE_APPLICATION_ERROR         = -20000;
constexpr int OB_MIN_RAISE_APPLICATION_ERROR         = -20999;

} // common
using namespace common; // maybe someone can fix
} // oceanbase

#endif /* OB_ERRNO_H */
';


# generate ob_errno.cpp
open my $fh_cpp, '>', "ob_errno.cpp";
print $fh_cpp '
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

// DO NOT EDIT. This file is automatically generated from `ob_errno.def\'.
// ob_errno.h
//   Author:
//   Normalizer:

#define USING_LOG_PREFIX LIB_MYSQLC

// DO NOT DELETE `#include <iostream>` !!!
// fix: ob_error.cpp file requires at least 20g memory for release(-O2) compilation
// and will jam when asan turned on
// it can be solved by introducing <iostream> header file currently
// TODO: it is clang bug and the specific reason to be further located
// issue: 
#include <iostream>

#include "ob_errno.h"
#ifndef __ERROR_CODE_PARSER_
#include "ob_define.h"
#include "lib/utility/ob_edit_distance.h"
#else
#define OB_LIKELY
#define OB_UNLIKELY
#include <string.h>
#endif
using namespace oceanbase::common;

struct _error {
  public:
    const char *error_name;
    const char *error_cause;
    const char *error_solution;
    int         mysql_errno;
    const char *sqlstate;
    const char *str_error;
    const char *str_user_error;
};
static _error _error_default;
static _error const *_errors[OB_MAX_ERROR_CODE] = {NULL};
';

for my $oberr (@errors) {
  if (0 > $map{$oberr}->[0]) {
    my $err = "static const _error _error_$oberr = {
      .error_name            = \"$map{$oberr}->[5]\",
      .error_cause           = $map{$oberr}->[6],
      .error_solution        = $map{$oberr}->[7],
      .mysql_errno           = $map{$oberr}->[1],
      .sqlstate              = $map{$oberr}->[2],
      .str_error             = $map{$oberr}->[3],
      .str_user_error        = $map{$oberr}->[4]
};\n";
  print $fh_cpp $err;
  }
}

print $fh_cpp '
struct ObStrErrorInit
{
  ObStrErrorInit()
  {
    memset(&_error_default, 0, sizeof  _error_default);
    for (int i = 0; i < OB_MAX_ERROR_CODE; ++i) {
      _errors[i] = &_error_default;
    }
';
    for my $oberr (@errors) {
      if (0 > $map{$oberr}->[0]) {
        print $fh_cpp "    _errors[-$oberr] = &_error_$oberr;\n";
      }
    }
  print $fh_cpp '
  }
};

inline const _error *get_error(int index)
{
  static ObStrErrorInit error_init;
  return _errors[index];
}

int get_mysql_errno(int index)
{
  return get_error(index)->mysql_errno;
}

const char* get_mysql_str_error(int index)
{
  return get_error(index)->str_error;
}

namespace oceanbase
{
namespace common
{
';
print $fh_cpp "int g_all_ob_errnos[${\(scalar @errnos)}] = {" . join(", ", @errnos) . "};";

print $fh_cpp '
  const char *ob_error_name(const int err)
  {
    const char *ret = "Unknown error";
    if (OB_UNLIKELY(0 == err)) {
      ret = "OB_SUCCESS";
    } else if (OB_LIKELY(0 > err && err > -OB_MAX_ERROR_CODE)) {
      ret = get_error(-err)->error_name;
      if (OB_UNLIKELY(NULL == ret || \'\0\' == ret[0]))
      {
        ret = "Unknown Error";
      }
    }
    return ret;
  }
  const char *ob_error_cause(const int err)
  {
    const char *ret = "Internal Error";
    if (OB_UNLIKELY(0 == err)) {
      ret = "Not an Error";
    } else if (OB_LIKELY(0 > err && err > -OB_MAX_ERROR_CODE)) {
      ret = get_error(-err)->error_cause;
      if (OB_UNLIKELY(NULL == ret || \'\0\' == ret[0]))
      {
        ret = "Internal Error";
      }
    }
    return ret;
  }
  const char *ob_error_solution(const int err)
  {
    const char *ret = "Contact OceanBase Support";
    if (OB_UNLIKELY(0 == err)) {
      ret = "Contact OceanBase Support";
    } else if (OB_LIKELY(0 > err && err > -OB_MAX_ERROR_CODE)) {
      ret = get_error(-err)->error_solution;
      if (OB_UNLIKELY(NULL == ret || \'\0\' == ret[0]))
      {
        ret = "Contact OceanBase Support";
      }
    }
    return ret;
  }
  const char *ob_strerror(const int err)
  {
    const char *ret = "Unknown error";
    if (OB_LIKELY(0 >= err && err > -OB_MAX_ERROR_CODE)) {
      ret = get_error(-err)->str_error;
      if (OB_UNLIKELY(NULL == ret || \'\0\' == ret[0]))
      {
        ret = "Unknown Error";
      }
    }
    return ret;
  }
  const char *ob_str_user_error(const int err)
  {
    const char *ret = NULL;
    if (OB_LIKELY(0 >= err && err > -OB_MAX_ERROR_CODE)) {
      ret = get_error(-err)->str_user_error;
      if (OB_UNLIKELY(NULL == ret || \'\0\' == ret[0])) {
        ret = NULL;
      }
    }
    return ret;
  }
  const char *ob_sqlstate(const int err)
  {
    const char *ret = "HY000";
    if (OB_LIKELY(0 >= err && err > -OB_MAX_ERROR_CODE)) {
      ret = get_error(-err)->sqlstate;
      if (OB_UNLIKELY(NULL == ret || \'\0\' == ret[0])) {
        ret = "HY000";
      }
    }
    return ret;
  }
  int ob_mysql_errno(const int err)
  {
    int ret = -1;
    if (OB_LIKELY(0 >= err && err > -OB_MAX_ERROR_CODE)) {
      ret = get_error(-err)->mysql_errno;
    }
    return ret;
  }
  int ob_mysql_errno_with_check(const int err)
  {
    int ret = (err > 0 ? err : ob_mysql_errno(err));
    if (ret < 0) {
      ret = -err;
    }
    return ret;
  }
  int ob_errpkt_errno(const int err)
  {
    return ob_mysql_errno_with_check(err);
  }
  const char *ob_errpkt_strerror(const int err)
  {
    return ob_strerror(err);
  }
  const char *ob_errpkt_str_user_error(const int err)
  {
    return ob_str_user_error(err);
  }

} // end namespace common
} // end namespace oceanbase
';
