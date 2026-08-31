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

#include <algorithm>

#include "lib/charset/mb_wc.h"
#include "lib/charset/ob_byteorder.h"
#include "lib/charset/ob_ctype_uca_tab.h"
#define OB_UCA_PSHIFT 8
static constexpr uint16 nochar[] = {0, 0};
#ifdef _WIN32
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#define strncasecmp _strnicmp
#endif

static inline uint16_t *ob_char_weight_addr(ObUCAInfo *uca, ob_wc_t wc) {
  unsigned int page, ofst;
  return wc > uca->maxchar ? nullptr
                           : (uca->weights[page = (wc >> 8)]
                                  ? uca->weights[page] + (ofst = (wc & 0xFF)) *
                                                             uca->lengths[page]
                                  : nullptr);
}
class ob_uca_scanner {
protected:
  ob_uca_scanner(const ObCharsetInfo *cs_arg, const unsigned char *str,
                 size_t length)
      : wbeg(nochar), sbeg(str), send(str + length), uca(cs_arg->uca) {}

public:
  unsigned int get_weight_level() const { return weight_lv; }

protected:
  unsigned int weight_lv{0};
  const uint16_t *wbeg;
  const unsigned char *sbeg;
  const unsigned char *send;
  const ObUCAInfo *uca;
  uint16_t implicit[2];
};
template <class Mb_wc> struct uca_scanner_any : public ob_uca_scanner {
  uca_scanner_any(const Mb_wc mb_wc, const ObCharsetInfo *cs_arg,
                  const unsigned char *str, size_t length)
      : ob_uca_scanner(cs_arg, str, length), mb_wc(mb_wc) {}
  unsigned int get_char_index() const { return char_index; }
  inline int next();

private:
  unsigned int char_index{0};
  const Mb_wc mb_wc;
  inline int next_implicit(ob_wc_t ch);
};
template <class Mb_wc>
ALWAYS_INLINE int uca_scanner_any<Mb_wc>::next_implicit(ob_wc_t ch) {
  implicit[0] = (ch & 0x7FFF) | 0x8000;
  implicit[1] = 0;
  wbeg = implicit;
  unsigned int page = ch >> 15;
  if (ch >= 0x3400 && ch <= 0x4DB5) {
    page += 0xFB80;
  } else if (ch >= 0x4E00 && ch <= 0x9FA5) {
    page += 0xFB40;
  } else {
    page += 0xFBC0;
  }
  return page;
}
template <class Mb_wc> ALWAYS_INLINE int uca_scanner_any<Mb_wc>::next() {

  if (wbeg[0]) {
    return *wbeg++;
  }
  do {
    ob_wc_t wc = 0;
    int mblen = mb_wc(&wc, sbeg, send);
    if (mblen <= 0) {
      ++weight_lv;
      return -1;
    }
    sbeg += mblen;
    char_index++;
    if (wc > uca->maxchar) {

      wbeg = nochar;
      return 0xFFFD;
    }
    unsigned int page = wc >> 8;
    unsigned int code = wc & 0xFF;
    const uint16_t *wpage = uca->weights[page];
    if (!wpage) {
      return next_implicit(wc);
    }
    wbeg = wpage + code * uca->lengths[page];
  } while (!wbeg[0]);
  return *wbeg++;
}
template <class Scanner, int LEVELS_FOR_COMPARE, class Mb_wc>
static int ob_strnncoll_uca(const ObCharsetInfo *cs, const Mb_wc mb_wc,
                            const unsigned char *s, size_t slen,
                            const unsigned char *t, size_t tlen,
                            bool t_is_prefix) {
  Scanner sscanner(mb_wc, cs, s, slen);
  Scanner tscanner(mb_wc, cs, t, tlen);
  int s_res = 0;
  int t_res = 0;
  for (unsigned int current_lv = 0; current_lv < LEVELS_FOR_COMPARE;
       ++current_lv) {
    do {
      s_res = sscanner.next();
      t_res = tscanner.next();
    } while (s_res == t_res && s_res >= 0 &&
             sscanner.get_weight_level() == current_lv &&
             tscanner.get_weight_level() == current_lv);
    if (sscanner.get_weight_level() == tscanner.get_weight_level()) {
      if (s_res == t_res && s_res >= 0)
        continue;
      break; // Error or inequality found, end.
    }
    if (tscanner.get_weight_level() > current_lv) {
      // t ran out of weights on this level, and s didn't.
      if (t_is_prefix) {
        // Consume the rest of the weights from s.
        do {
          s_res = sscanner.next();
        } while (s_res >= 0 && sscanner.get_weight_level() == current_lv);
        if (s_res < 0)
          break; // Error found, end.
        // s is now also on the next level. Continue comparison.
        continue;
      } else {
        // s is longer than t (and t_prefix isn't set).
        return 1;
      }
    }
    if (sscanner.get_weight_level() > current_lv) {
      // s ran out of weights on this level, and t didn't.
      return -1;
    }
    break;
  }
  return (s_res - t_res);
}
static inline int ob_space_weight(const ObCharsetInfo *cs) {
  return cs->uca->weights[0][0x20 * cs->uca->lengths[0]];
}
template <class Mb_wc>
static int ob_strnncollsp_uca(const ObCharsetInfo *cs, Mb_wc mb_wc,
                              const unsigned char *s, size_t slen,
                              const unsigned char *t, size_t tlen) {
  int s_res, t_res;
  uca_scanner_any<Mb_wc> sscanner(mb_wc, cs, s, slen);
  uca_scanner_any<Mb_wc> tscanner(mb_wc, cs, t, tlen);
  do {
    s_res = sscanner.next();
    t_res = tscanner.next();
  } while (s_res == t_res && s_res > 0);
  if (s_res > 0 && t_res < 0) {
    t_res = ob_space_weight(cs);
    do {
      if (s_res != t_res)
        return (s_res - t_res);
      s_res = sscanner.next();
    } while (s_res > 0);
    return 0;
  }
  if (s_res < 0 && t_res > 0) {
    s_res = ob_space_weight(cs);
    do {
      if (s_res != t_res)
        return (s_res - t_res);
      t_res = tscanner.next();
    } while (t_res > 0);
    return 0;
  }
  return (s_res - t_res);
}
template <class Mb_wc>
static void ob_hash_sort_uca(const ObCharsetInfo *cs, Mb_wc mb_wc,
                             const unsigned char *s, size_t slen, ulong *n1,
                             ulong *n2,
                             const bool calc_end_space __attribute__((unused)),
                             hash_algo hash_algo) {
  int s_res;
  ulong tmp1;
  ulong tmp2;
  int space_weight = ob_space_weight(cs);
  slen = cs->cset->lengthsp(cs, pointer_cast<const char *>(s), slen);
  uca_scanner_any<Mb_wc> scanner(mb_wc, cs, s, slen);
  if (NULL == hash_algo) {
    tmp1 = *n1;
    tmp2 = *n2;
    while ((s_res = scanner.next()) > 0) {
      tmp1 ^= (((tmp1 & 63) + tmp2) * (s_res >> 8)) + (tmp1 << 8);
      tmp2 += 3;
      tmp1 ^= (((tmp1 & 63) + tmp2) * (s_res & 0xFF)) + (tmp1 << 8);
      tmp2 += 3;
      if (s_res != space_weight) {
        *n1 = tmp1;
        *n2 = tmp2;
      }
    }
  } else {
    unsigned char data[HASH_BUFFER_LENGTH];
    unsigned int length = 0;
    tmp1 = *n1;
    unsigned int last_non_space_len = 0;
    while ((s_res = scanner.next()) > 0) {
      if (length > HASH_BUFFER_LENGTH - 4) {
        tmp1 = hash_algo((void *)&data, length, tmp1);
        length = 0;
        if (last_non_space_len > 0) {
          *n1 = hash_algo((void *)&data, last_non_space_len, tmp1);
          last_non_space_len = 0;
        }
      }
      memcpy(data + length, &s_res, 4);
      length += 4;
      if (s_res != space_weight) {
        last_non_space_len = length;
      }
    }
    if (last_non_space_len > 0) {
      n1[0] = hash_algo((void *)&data, last_non_space_len, tmp1);
    }
  }
}
template <class Mb_wc>
static size_t
ob_strnxfrm_uca(const ObCharsetInfo *cs, Mb_wc mb_wc, unsigned char *dst,
                size_t dstlen, unsigned int num_codepoints,
                const unsigned char *src, size_t srclen, unsigned int flags) {
  unsigned char *d0 = dst;
  unsigned char *de = dst + dstlen;
  int s_res;
  uca_scanner_any<Mb_wc> scanner(mb_wc, cs, src, srclen);
  while (dst < de && (s_res = scanner.next()) > 0) {
    *dst++ = s_res >> 8;
    if (dst < de)
      *dst++ = s_res & 0xFF;
  }
  if (dst < de) {
    ob_charset_assert(num_codepoints >= scanner.get_char_index());
    num_codepoints -= scanner.get_char_index();
    if (num_codepoints) {
      unsigned int space_count =
          std::min<unsigned int>((de - dst) / 2, num_codepoints);
      s_res = ob_space_weight(cs);
      for (; space_count; space_count--) {
        dst = store16be(dst, s_res);
      }
    }
  }
  if ((flags & OB_STRXFRM_PAD_TO_MAXLEN) && dst < de) {
    s_res = ob_space_weight(cs);
    for (; dst < de;) {
      *dst++ = s_res >> 8;
      if (dst < de)
        *dst++ = s_res & 0xFF;
    }
  }
  return dst - d0;
}
static int ob_uca_charcmp(const ObCharsetInfo *cs, ob_wc_t wc1, ob_wc_t wc2) {
  if (wc1 == wc2)
    return 0;
  size_t length1, length2;
  uint16_t *weight1 = ob_char_weight_addr(cs->uca, wc1);
  uint16_t *weight2 = ob_char_weight_addr(cs->uca, wc2);
  if (!weight1 || !weight2) {
    return wc1 != wc2;
  } else if (weight1[0] != weight2[0]) {
    return 1;
  }
  length1 = cs->uca->lengths[wc1 >> OB_UCA_PSHIFT];
  length2 = cs->uca->lengths[wc2 >> OB_UCA_PSHIFT];
  if (length1 > length2) {
    return memcmp((const void *)weight1, (const void *)weight2, length2 * 2)
               ? 1
               : weight1[length2];
  } else if (length1 < length2) {
    return memcmp((const void *)weight1, (const void *)weight2, length1 * 2)
               ? 1
               : weight2[length1];
  }
  return memcmp((const void *)weight1, (const void *)weight2, length1 * 2);
}
static int ob_wildcmp_uca_impl(const ObCharsetInfo *cs, const char *str,
                               const char *str_end, const char *wildstr,
                               const char *wildend, int escape, int w_one,
                               int w_many, int recurse_level) {
  while (wildstr != wildend) {
    int result = -1;
    auto mb_wc = cs->cset->mb_wc;
    ob_wc_t w_wc;
    while (true) {
      int mb_len;
      if ((mb_len = mb_wc(cs, &w_wc, (const unsigned char *)wildstr,
                          (const unsigned char *)wildend)) <= 0) {
        return 1;
      }
      wildstr += mb_len;
      // If we found '%' (w_many), break out this loop.
      if (w_wc == (ob_wc_t)w_many) {
        result = 1;
        break;
      }
      bool escaped = false;
      if (w_wc == (ob_wc_t)escape && wildstr < wildend) {
        if ((mb_len = mb_wc(cs, &w_wc, (const unsigned char *)wildstr,
                            (const unsigned char *)wildend)) <= 0)
          return 1;
        wildstr += mb_len;
        escaped = true;
      }
      ob_wc_t s_wc;
      if ((mb_len = mb_wc(cs, &s_wc, (const unsigned char *)str,
                          (const unsigned char *)str_end)) <= 0) {
        return 1;
      }
      str += mb_len;
      // If we found '_' (w_one), skip one character in expression string.
      if (!escaped && w_wc == (ob_wc_t)w_one) {
        result = 1;
      } else {
        if (ob_uca_charcmp(cs, s_wc, w_wc))
          return 1;
      }
      if (wildstr == wildend) {
        return (str != str_end);
      }
    }
    if (w_wc == (ob_wc_t)w_many) {
      // Remove any '%' and '_' following w_many in the pattern string.
      for (;;) {
        if (wildstr == wildend) {
          return 0;
        }
        int mb_len_wild = mb_wc(cs, &w_wc, (const unsigned char *)wildstr,
                                (const unsigned char *)wildend);
        if (mb_len_wild <= 0)
          return 1;
        wildstr += mb_len_wild;
        if (w_wc == (ob_wc_t)w_many)
          continue;
        if (w_wc == (ob_wc_t)w_one) {
          ob_wc_t s_wc;
          int mb_len = mb_wc(cs, &s_wc, (const unsigned char *)str,
                             (const unsigned char *)str_end);
          if (mb_len <= 0)
            return 1;
          str += mb_len;
          continue;
        }
        break;
      }
      // No character in the expression string to match w_wc.
      if (str == str_end)
        return -1;
      // Skip the escape character ('\') in the pattern if needed.
      if (w_wc == (ob_wc_t)escape && wildstr < wildend) {
        int mb_len = mb_wc(cs, &w_wc, (const unsigned char *)wildstr,
                           (const unsigned char *)wildend);
        if (mb_len <= 0)
          return 1;
        wildstr += mb_len;
      }
      while (true) {
        int mb_len = 0;
        while (str != str_end) {
          ob_wc_t s_wc;
          if ((mb_len = mb_wc(cs, &s_wc, (const unsigned char *)str,
                              (const unsigned char *)str_end)) <= 0)
            return 1;
          if (!ob_uca_charcmp(cs, s_wc, w_wc))
            break;
          str += mb_len;
        }
        // No character in the expression string is equal to w_wc.
        if (str == str_end)
          return -1;
        str += mb_len;
        result = ob_wildcmp_uca_impl(cs, str, str_end, wildstr, wildend, escape,
                                     w_one, w_many, recurse_level + 1);
        if (result <= 0)
          return result;
      }
    }
  }
  return (str != str_end ? 1 : 0);
}
static int ob_strcasecmp_uca(const ObCharsetInfo *cs, const char *s,
                             const char *t) {
  const ObUnicaseInfo *uni_plane = cs->caseinfo;
  const ObUnicaseInfoChar *page;
  while (s[0] && t[0]) {
    ob_wc_t s_wc, t_wc;
    if (static_cast<unsigned char>(s[0]) < 128) {
      s_wc = uni_plane->page[0][static_cast<unsigned char>(s[0])].tolower;
      s++;
    } else {
      int res;
      res = cs->cset->mb_wc(cs, &s_wc, pointer_cast<const unsigned char *>(s),
                            pointer_cast<const unsigned char *>(s + 4));
      if (res <= 0)
        return strcmp(s, t);
      s += res;
      if (s_wc <= uni_plane->maxchar && (page = uni_plane->page[s_wc >> 8]))
        s_wc = page[s_wc & 0xFF].tolower;
    }
    if (static_cast<unsigned char>(t[0]) < 128) {
      t_wc = uni_plane->page[0][static_cast<unsigned char>(t[0])].tolower;
      t++;
    } else {
      int res =
          cs->cset->mb_wc(cs, &t_wc, pointer_cast<const unsigned char *>(t),
                          pointer_cast<const unsigned char *>(t + 4));
      if (res <= 0)
        return strcmp(s, t);
      t += res;
      if (t_wc <= uni_plane->maxchar && (page = uni_plane->page[t_wc >> 8]))
        t_wc = page[t_wc & 0xFF].tolower;
    }
    if (s_wc != t_wc)
      return static_cast<int>(s_wc) - static_cast<int>(t_wc);
  }
  return static_cast<int>(static_cast<unsigned char>(s[0])) -
         static_cast<int>(static_cast<unsigned char>(t[0]));
}
extern "C" {
static int ob_wildcmp_uca(const ObCharsetInfo *cs, const char *str,
                          const char *str_end, const char *wildstr,
                          const char *wildend, int escape, int w_one,
                          int w_many) {
  return ob_wildcmp_uca_impl(cs, str, str_end, wildstr, wildend, escape, w_one,
                             w_many, 1);
}
} // extern "C"
extern "C" {
static int ob_strnncoll_any_uca(const ObCharsetInfo *cs, const unsigned char *s,
                                size_t slen, const unsigned char *t,
                                size_t tlen, bool t_is_prefix) {
  if (cs->cset->mb_wc == ob_mb_wc_utf8mb4_thunk) {
    return ob_strnncoll_uca<uca_scanner_any<Mb_wc_utf8mb4>, 1>(
        cs, Mb_wc_utf8mb4(), s, slen, t, tlen, t_is_prefix);
  }
  Mb_wc_through_function_pointer mb_wc(cs);
  return ob_strnncoll_uca<uca_scanner_any<decltype(mb_wc)>, 1>(
      cs, mb_wc, s, slen, t, tlen, t_is_prefix);
}
static int ob_strnncollsp_any_uca(const ObCharsetInfo *cs,
                                  const unsigned char *s, size_t slen,
                                  const unsigned char *t, size_t tlen,
                                  bool diff_if_only_endspace_difference
                                  __attribute__((unused))) {
  if (cs->cset->mb_wc == ob_mb_wc_utf8mb4_thunk) {
    return ob_strnncollsp_uca(cs, Mb_wc_utf8mb4(), s, slen, t, tlen);
  }
  Mb_wc_through_function_pointer mb_wc(cs);
  return ob_strnncollsp_uca(cs, mb_wc, s, slen, t, tlen);
}
static void ob_hash_sort_any_uca(const ObCharsetInfo *cs,
                                 const unsigned char *s, size_t slen, ulong *n1,
                                 ulong *n2, const bool calc_end_space,
                                 hash_algo hash_algo) {
  if (cs->cset->mb_wc == ob_mb_wc_utf8mb4_thunk) {
    ob_hash_sort_uca(cs, Mb_wc_utf8mb4(), s, slen, n1, n2, calc_end_space,
                     hash_algo);
  } else {
    Mb_wc_through_function_pointer mb_wc(cs);
    ob_hash_sort_uca(cs, mb_wc, s, slen, n1, n2, calc_end_space, hash_algo);
  }
}
static size_t ob_strnxfrm_any_uca(const ObCharsetInfo *cs, unsigned char *dst,
                                  size_t dstlen, unsigned int num_codepoints,
                                  const unsigned char *src, size_t srclen,
                                  unsigned int flags, bool *is_valid_unicode) {
  *is_valid_unicode = true;
  if (cs->cset->mb_wc == ob_mb_wc_utf8mb4_thunk) {
    return ob_strnxfrm_uca(cs, Mb_wc_utf8mb4(), dst, dstlen, num_codepoints,
                           src, srclen, flags);
  }
  Mb_wc_through_function_pointer mb_wc(cs);
  return ob_strnxfrm_uca(cs, mb_wc, dst, dstlen, num_codepoints, src, srclen,
                         flags);
}
} // extern "C"

extern "C" {
static bool ob_coll_init_uca(ObCharsetInfo *cs, ObCharsetLoader *loader) {
  (void)loader;
  cs->pad_char = ' ';
  cs->ctype = ob_charset_utf8mb4_unicode_ci.ctype;
  if (!cs->caseinfo)
    cs->caseinfo = &ob_unicase_default;
  if (!cs->uca)
    cs->uca = &ob_uca_v400;
  return false;
}
static void ob_coll_uninit_uca(ObCharsetInfo *cs) { (void)cs; }
} // extern "C"
ObCollationHandler ob_collation_any_uca_handler = {ob_coll_init_uca,
                                                   ob_coll_uninit_uca,
                                                   ob_strnncoll_any_uca,
                                                   ob_strnncollsp_any_uca,
                                                   ob_strnxfrm_any_uca,
                                                   ob_strnxfrmlen_simple,
                                                   NULL,
                                                   ob_like_range_mb,
                                                   ob_wildcmp_uca,
                                                   ob_strcasecmp_uca,
                                                   ob_instr_mb,
                                                   ob_hash_sort_any_uca,
                                                   ob_propagate_complex};

#define OB_CS_UTF8MB4_UCA_FLAGS                                                \
  (OB_CS_COMPILED | OB_CS_STRNXFRM | OB_CS_UNICODE | OB_CS_UNICODE_SUPPLEMENT)
ObCharsetInfo ob_charset_utf8mb4_unicode_ci = {224,
                                               0,
                                               0,
                                               OB_CS_UTF8MB4_UCA_FLAGS |
                                                   OB_CS_CI,
                                               OB_UTF8MB4,
                                               OB_UTF8MB4_UNICODE_CI,
                                               "",
                                               "",
                                               NULL,
                                               ctype_utf8,
                                               NULL,
                                               NULL,
                                               NULL,
                                               &ob_uca_v400,
                                               NULL,
                                               NULL,
                                               &ob_unicase_default,
                                               NULL,
                                               NULL,
                                               8,
                                               1,
                                               1,
                                               1,
                                               4,
                                               1,
                                               9,
                                               0x10FFFF,
                                               ' ',
                                               0,
                                               1,
                                               1,
                                               &ob_charset_utf8mb4_handler,
                                               &ob_collation_any_uca_handler,
                                               PAD_SPACE};
