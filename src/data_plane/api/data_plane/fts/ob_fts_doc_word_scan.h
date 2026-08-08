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

#ifndef OCEANBASE_DATA_PLANE_API_FTS_OB_FTS_DOC_WORD_SCAN_H_
#define OCEANBASE_DATA_PLANE_API_FTS_OB_FTS_DOC_WORD_SCAN_H_

#include <cstdint>

namespace oceanbase
{
namespace common
{
class ObDatum;
class ObIAllocator;
class ObTabletID;
}
namespace blocksstable
{
class ObDatumRow;
}
namespace transaction
{
class ObTxReadSnapshot;
}
namespace data_plane
{

struct ObFTDocWordIterator;

int create_ft_doc_word_iterator(common::ObIAllocator &allocator,
                                ObFTDocWordIterator *&iterator);
void destroy_ft_doc_word_iterator(ObFTDocWordIterator *&iterator);
void reset_ft_doc_word_iterator(ObFTDocWordIterator *iterator);
int init_ft_doc_word_iterator(ObFTDocWordIterator *iterator,
                              uint64_t table_id,
                              const common::ObTabletID &tablet_id,
                              const transaction::ObTxReadSnapshot *snapshot,
                              int64_t schema_version);
int scan_ft_doc_words(ObFTDocWordIterator *iterator,
                      uint64_t table_id,
                      const common::ObDatum &row_mapping_id);
int next_ft_doc_word(ObFTDocWordIterator *iterator,
                     blocksstable::ObDatumRow *&row);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_FTS_OB_FTS_DOC_WORD_SCAN_H_
