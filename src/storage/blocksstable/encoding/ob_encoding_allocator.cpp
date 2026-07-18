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

#include "ob_encoding_allocator.h"

namespace oceanbase
{
namespace blocksstable
{

#define DEF_SIZE_ARRAY(Item, size_array) \
int64_t size_array [] = {                \
  sizeof(ObRaw##Item),                   \
  sizeof(ObDict##Item),                  \
  sizeof(ObRLE##Item),                   \
  sizeof(ObConst##Item),                 \
  sizeof(ObIntegerBaseDiff##Item),       \
  sizeof(ObStringDiff##Item),            \
  sizeof(ObHexString##Item),             \
  sizeof(ObStringPrefix##Item),          \
  sizeof(ObColumnEqual##Item),           \
  sizeof(ObInterColSubStr##Item),        \
}                                        \

DEF_SIZE_ARRAY(Encoder, encoder_sizes);
DEF_SIZE_ARRAY(Decoder, decoder_sizes);

int64_t MAX_DECODER_SIZE =  *(std::max_element(decoder_sizes,
    decoder_sizes + sizeof(decoder_sizes) / sizeof(decoder_sizes[0])));

}//end namespace blocksstable
}//end namespace oceanbase

