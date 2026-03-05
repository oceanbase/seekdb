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
 * English Stemmer for full-text indexing.
 *
 * Provides both Porter stemmer (built-in) and Snowball English stemmer
 * (via libstemmer). Snowball is preferred as it produces better results.
 *
 * Usage:
 *   char buf[256];
 *   strncpy(buf, word, len);
 *   int stem_len = ob_porter_stem(buf, len);   // Porter
 *   int stem_len = ob_snowball_stem(buf, len);  // Snowball (preferred)
 *   // buf[0..stem_len-1] is the stem
 */

#ifndef OCEANBASE_SHARE_OB_ENGLISH_STEMMER_H_
#define OCEANBASE_SHARE_OB_ENGLISH_STEMMER_H_

#include <stdint.h>

namespace oceanbase
{
namespace share
{

/*
 * Porter stemmer: Stem the word stored in b[0..k-1].
 * Returns the new length of the stem.
 * The buffer b must be writable and at least k bytes long.
 * Input must be lowercase ASCII letters only.
 */
int ob_porter_stem(char *b, int k);

/*
 * Snowball English stemmer: Stem the word stored in b[0..k-1].
 * Returns the new length of the stem (stem is written in-place).
 * Uses thread-local libstemmer instance for thread safety.
 * Input must be lowercase ASCII/UTF-8.
 * Falls back to Porter if libstemmer is unavailable.
 */
int ob_snowball_stem(char *b, int k);

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_ENGLISH_STEMMER_H_
