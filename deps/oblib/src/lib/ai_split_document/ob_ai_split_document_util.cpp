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

#define USING_LOG_PREFIX LIB

#include "lib/ai_split_document/ob_ai_split_document_util.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
namespace common
{

template <typename IteratorType>
static int create_iterator(ObIAllocator &allocator, ObDocSplitIterator *&iterator)
{
  int ret = OB_SUCCESS;
  void *buf = allocator.alloc(sizeof(IteratorType));
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate document split iterator", K(ret));
  } else {
    iterator = new (buf) IteratorType();
  }
  return ret;
}

int ObAiSplitDocumentUtil::create_doc_split_iterator(const ObAiSplitDocParams &params,
                                                     ObIAllocator &allocator,
                                                     ObDocSplitIterator *&iterator)
{
  int ret = OB_SUCCESS;
  iterator = nullptr;
  if (params.type_ == ObAiSplitContentType::TEXT) {
    ret = create_iterator<ObTextSplitIterator>(allocator, iterator);
  } else if (params.type_ == ObAiSplitContentType::MARKDOWN) {
    ret = create_iterator<ObMarkdownSplitIterator>(allocator, iterator);
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid document content type", K(ret), K(params.type_));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "type must be 'text' or 'markdown'");
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
