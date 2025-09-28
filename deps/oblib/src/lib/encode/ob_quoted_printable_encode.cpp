/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#include "lib/encode/ob_quoted_printable_encode.h"

#include "lib/oblog/ob_log.h"

namespace oceanbase
{
namespace common
{
char ObQuotedPrintableEncoder::hex[] = "0123456789ABCDEF";
//todo for mime '?' is not printable

//this func only for encode raw to one line, do not do soft break
//为text encode 设计， 换行可以没有CR


} //namespace common
} //namespace oceanbase