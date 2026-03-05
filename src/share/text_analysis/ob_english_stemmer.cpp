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

/*
 * Porter Stemmer (M.F. Porter, 1980) — C++ implementation for seekdb.
 *
 * Adapted from the original public-domain C implementation by M.F. Porter.
 * Input: lowercase ASCII string in buf[0..k-1] (length = k).
 * Returns: stem length (the stem is buf[0..ret-1]).
 *
 * Minimum word length to stem: OB_STEM_MIN_WORD_LEN (default 3).
 * Words shorter than this are returned unchanged.
 */

#include "share/text_analysis/ob_english_stemmer.h"
#include <string.h>

namespace oceanbase
{
namespace share
{

static const int OB_STEM_MIN_WORD_LEN = 3;

/* Working state for the stemmer */
struct StemState {
  char *b;   /* buffer */
  int   k;   /* offset to the end of the string (exclusive, i.e. length) */
  int   j;   /* a general offset into the string */
};

/* cons(i) is TRUE <=> b[i] is a consonant. */
static bool cons(const StemState &s, int i)
{
  switch (s.b[i]) {
  case 'a': case 'e': case 'i': case 'o': case 'u': return false;
  case 'y': return (i == 0) ? true : !cons(s, i - 1);
  default:  return true;
  }
}

/*
 * m() measures the number of consonant sequences between 0 and j.
 * if c is a consonant sequence and v a vowel sequence, and <..>
 * indicates arbitrary presence,
 *   <c><v>       gives 0
 *   <c>vc<v>     gives 1
 *   <c>vcvc<v>   gives 2
 *   <c>vcvcvc<v> gives 3
 *   ...
 */
static int m(const StemState &s)
{
  int n = 0;
  int i = 0;
  while (true) {
    if (i > s.j) return n;
    if (!cons(s, i)) break;
    i++;
  }
  i++;
  while (true) {
    while (true) {
      if (i > s.j) return n;
      if (cons(s, i)) break;
      i++;
    }
    i++;
    n++;
    while (true) {
      if (i > s.j) return n;
      if (!cons(s, i)) break;
      i++;
    }
    i++;
  }
}

/* vowelinstem() is TRUE <=> 0,...j contains a vowel */
static bool vowelinstem(const StemState &s)
{
  for (int i = 0; i <= s.j; i++)
    if (!cons(s, i)) return true;
  return false;
}

/* doublec(j) is TRUE <=> j,(j-1) contain a double consonant. */
static bool doublec(const StemState &s, int j)
{
  if (j < 1) return false;
  if (s.b[j] != s.b[j - 1]) return false;
  return cons(s, j);
}

/*
 * cvc(i) is TRUE <=> i-2,i-1,i has the form consonant - vowel - consonant
 * and also if the second c is not w,x or y. this is used when trying to
 * restore an e at the end of a short word. e.g.
 *   cav(e), lov(e), hop(e), crim(e), but
 *   snow, box, tray.
 */
static bool cvc(const StemState &s, int i)
{
  if (i < 2 || !cons(s, i) || cons(s, i - 1) || !cons(s, i - 2)) return false;
  int ch = s.b[i];
  if (ch == 'w' || ch == 'x' || ch == 'y') return false;
  return true;
}

/* ends(s) is TRUE <=> 0,...k ends with the string s. */
static bool ends(StemState &st, const char *suf, int suf_len)
{
  if (suf_len > st.k) return false;
  if (memcmp(st.b + (st.k - suf_len), suf, suf_len) != 0) return false;
  st.j = st.k - suf_len;
  return true;
}

/* setto(s) sets (j+1),...k to the characters in the string s, readjusting k. */
static void setto(StemState &st, const char *s, int s_len)
{
  memcpy(st.b + st.j + 1, s, s_len);
  st.k = st.j + s_len;
}

/* r(s) is used further down. */
static void r(StemState &st, const char *s, int s_len)
{
  if (m(st) > 0) setto(st, s, s_len);
}

#define ENDS(lit) ends(st, lit, (int)(sizeof(lit)-1))
#define SETTO(lit) setto(st, lit, (int)(sizeof(lit)-1))
#define R(lit) r(st, lit, (int)(sizeof(lit)-1))

/*
 * step1ab() gets rid of plurals and -ed or -ing. e.g.
 *   caresses  ->  caress
 *   ponies    ->  poni
 *   ties      ->  ti
 *   caress    ->  caress
 *   cats      ->  cat
 *   feed      ->  feed
 *   agreed    ->  agree
 *   disabled  ->  disable
 *   matting   ->  mat
 *   mating    ->  mate
 *   meeting   ->  meet
 *   milling   ->  mill
 *   messing   ->  mess
 *   meetings  ->  meet
 */
static void step1ab(StemState &st)
{
  if (st.b[st.k - 1] == 's') {
    if      (ENDS("sses")) { st.k -= 2; }
    else if (ENDS("ies"))  { SETTO("i"); }
    else if (st.b[st.k - 2] != 's') { st.k--; }
  }
  if (ENDS("eed")) {
    if (m(st) > 0) st.k--;
  } else if ((ENDS("ed") || ENDS("ing")) && vowelinstem(st)) {
    st.k = st.j;
    if      (ENDS("at")) { SETTO("ate"); }
    else if (ENDS("bl")) { SETTO("ble"); }
    else if (ENDS("iz")) { SETTO("ize"); }
    else if (doublec(st, st.k - 1)) {
      st.k--;
      int ch = st.b[st.k - 1];
      if (ch == 'l' || ch == 's' || ch == 'z') st.k++;
    } else if (m(st) == 1 && cvc(st, st.k - 1)) {
      SETTO("e");
    }
  }
}

/* step1c() turns terminal y to i when there is another vowel in the stem. */
static void step1c(StemState &st)
{
  if (ENDS("y") && vowelinstem(st))
    st.b[st.k - 1] = 'i';
}

/*
 * step2() maps double suffices to single ones. so -ization ( = -ize plus
 * -ation) maps to -ize etc. note that the string before the suffix must give
 * m() > 0.
 */
static void step2(StemState &st)
{
  switch (st.b[st.k - 2]) {
  case 'a':
    if      (ENDS("ational")) { R("ate"); }
    else if (ENDS("tional"))  { R("tion"); }
    break;
  case 'c':
    if      (ENDS("enci")) { R("ence"); }
    else if (ENDS("anci")) { R("ance"); }
    break;
  case 'e':
    if (ENDS("izer")) { R("ize"); }
    break;
  case 'l':
    if      (ENDS("bli"))   { R("ble"); }
    else if (ENDS("alli"))  { R("al"); }
    else if (ENDS("entli")) { R("ent"); }
    else if (ENDS("eli"))   { R("e"); }
    else if (ENDS("ousli")) { R("ous"); }
    break;
  case 'o':
    if      (ENDS("ization")) { R("ize"); }
    else if (ENDS("ation"))   { R("ate"); }
    else if (ENDS("ator"))    { R("ate"); }
    break;
  case 's':
    if      (ENDS("alism"))   { R("al"); }
    else if (ENDS("iveness")) { R("ive"); }
    else if (ENDS("fulness")) { R("ful"); }
    else if (ENDS("ousness")) { R("ous"); }
    break;
  case 't':
    if      (ENDS("aliti"))  { R("al"); }
    else if (ENDS("iviti"))  { R("ive"); }
    else if (ENDS("biliti")) { R("ble"); }
    break;
  case 'g':
    if (ENDS("logi")) { R("log"); }
    break;
  default:
    break;
  }
}

/* step3() deals with -ic-, -full, -ness etc. similar strategy to step2. */
static void step3(StemState &st)
{
  switch (st.b[st.k - 1]) {
  case 'e':
    if      (ENDS("icate")) { R("ic"); }
    else if (ENDS("ative")) { R(""); }
    else if (ENDS("alize")) { R("al"); }
    break;
  case 'i':
    if (ENDS("iciti")) { R("ic"); }
    break;
  case 'l':
    if      (ENDS("ical")) { R("ic"); }
    else if (ENDS("ful"))  { R(""); }
    break;
  case 's':
    if (ENDS("ness")) { R(""); }
    break;
  default:
    break;
  }
}

/* step4() takes off -ant, -ence etc., in context <c>vcvc<v>. */
static void step4(StemState &st)
{
  switch (st.b[st.k - 2]) {
  case 'a':
    if (ENDS("al")) break; else return;
  case 'c':
    if      (ENDS("ance")) break;
    else if (ENDS("ence")) break;
    else return;
  case 'e':
    if (ENDS("er")) break; else return;
  case 'i':
    if (ENDS("ic")) break; else return;
  case 'l':
    if      (ENDS("able")) break;
    else if (ENDS("ible")) break;
    else return;
  case 'n':
    if      (ENDS("ant"))  break;
    else if (ENDS("ement")) break;
    else if (ENDS("ment")) break;
    else if (ENDS("ent"))  break;
    else return;
  case 'o':
    if      (ENDS("ion") && st.j >= 0 && (st.b[st.j] == 's' || st.b[st.j] == 't')) break;
    else if (ENDS("ou"))  break;
    else return;
  case 's':
    if (ENDS("ism")) break; else return;
  case 't':
    if      (ENDS("ate")) break;
    else if (ENDS("iti")) break;
    else return;
  case 'u':
    if (ENDS("ous")) break; else return;
  case 'v':
    if (ENDS("ive")) break; else return;
  case 'z':
    if (ENDS("ize")) break; else return;
  default:
    return;
  }
  if (m(st) > 1) st.k = st.j;
}

/* step5() removes a final -e if m() > 1, and changes -ll to -l if m() > 1. */
static void step5(StemState &st)
{
  st.j = st.k;
  if (st.b[st.k - 1] == 'e') {
    int a = m(st);
    if (a > 1 || (a == 1 && !cvc(st, st.k - 2)))
      st.k--;
  }
  if (st.b[st.k - 1] == 'l' && doublec(st, st.k - 1) && m(st) > 1)
    st.k--;
}

/*
 * ob_porter_stem: stem the word in b[0..k-1] (k = length).
 * Returns the new length of the stem (the stem is b[0..ret-1]).
 * Words shorter than OB_STEM_MIN_WORD_LEN are returned unchanged.
 */
int ob_porter_stem(char *b, int k)
{
  if (k <= OB_STEM_MIN_WORD_LEN) return k;

  StemState st;
  st.b = b;
  st.k = k;
  st.j = 0;

  step1ab(st);
  if (st.k > 1) {
    step1c(st);
    step2(st);
    step3(st);
    step4(st);
    step5(st);
  }
  // Safety: never return 0 or negative length
  return (st.k > 0) ? st.k : k;
}

/*
 * Snowball English stemmer using libstemmer.
 * Thread-local stemmer instance for thread safety.
 */
} // namespace share
} // namespace oceanbase

#include <libstemmer.h>
#include <string.h>

namespace oceanbase
{
namespace share
{

static thread_local struct sb_stemmer *tl_stemmer = nullptr;
static thread_local bool tl_init_attempted = false;

static struct sb_stemmer *get_snowball_stemmer()
{
  if (!tl_init_attempted) {
    tl_init_attempted = true;
    tl_stemmer = sb_stemmer_new("english", "UTF_8");
  }
  return tl_stemmer;
}

int ob_snowball_stem(char *b, int k)
{
  if (k <= 0) return k;
  struct sb_stemmer *stemmer = get_snowball_stemmer();
  if (stemmer == nullptr) {
    // Fallback to Porter if libstemmer unavailable
    return ob_porter_stem(b, k);
  }
  const sb_symbol *result = sb_stemmer_stem(stemmer,
      reinterpret_cast<const sb_symbol *>(b), k);
  if (result == nullptr) {
    return ob_porter_stem(b, k);
  }
  int stem_len = sb_stemmer_length(stemmer);
  if (stem_len > 0 && stem_len <= k) {
    memcpy(b, result, stem_len);
    return stem_len;
  } else if (stem_len > k) {
    // Stem is longer than input buffer, keep original
    return k;
  }
  return k;
}

} // namespace share
} // namespace oceanbase
