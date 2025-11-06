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

#ifndef CHARSET_TIS620_H_
#define CHARSET_TIS620_H_

#include "lib/charset/ob_ctype.h"
#define LAST_LEVEL 4 /* TOT_LEVELS - 1 */
/* This define will conflict heavily with others */
#define _is(c) (t_ctype[(c)][LAST_LEVEL])
#define _level 8
#define _consnt 16
#define _ldvowel 32
#define _fllwvowel 64
#define _uprvowel 128
#define _lwrvowel 256
#define _tone 512
#define _diacrt1 1024
#define _diacrt2 2048
#define _combine 4096
#define _stone 8192
#define _tdig 16384
#define _rearvowel (_fllwvowel | _uprvowel | _lwrvowel)
#define _diacrt (_diacrt1 | _diacrt2)
#define levelof(c) (_is(c) & _level)
#define isthai(c) ((c) >= 128)
#define istalpha(c) \
  (_is(c) & (_consnt | _ldvowel | _rearvowel | _tone | _diacrt1 | _diacrt2))
#define isconsnt(c) (_is(c) & _consnt)
#define isldvowel(c) (_is(c) & _ldvowel)
#define isfllwvowel(c) (_is(c) & _fllwvowel)
#define ismidvowel(c) (_is(c) & (_ldvowel | _fllwvowel))
#define isuprvowel(c) (_is(c) & _uprvowel)
#define islwrvowel(c) (_is(c) & _lwrvowel)
#define isuprlwrvowel(c) (_is(c) & (_lwrvowel | _uprvowel))
#define isrearvowel(c) (_is(c) & _rearvowel)
#define isvowel(c) (_is(c) & (_ldvowel | _rearvowel))
#define istone(c) (_is(c) & _tone)
#define isunldable(c) (_is(c) & (_rearvowel | _tone | _diacrt1 | _diacrt2))
#define iscombinable(c) (_is(c) & _combine)
#define istdigit(c) (_is(c) & _tdig)
#define isstone(c) (_is(c) & _stone)
#define isdiacrt1(c) (_is(c) & _diacrt1)
#define isdiacrt2(c) (_is(c) & _diacrt2)
#define isdiacrt(c) (_is(c) & _diacrt)


#define M L_MIDDLE
//#define U L_UPPER
#define L L_LOWER
#define UU L_UPRUPR
#define X L_MIDDLE

#endif  // CHARSET_TIS620_H_
