/*
 * Copyright (c) 2026 OceanBase.
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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/graph/graph_expand_edge_page.h"
#include "share/ob_errno.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
GraphExpandEdgePage::GraphExpandEdgePage(ObIAllocator &allocator)
    : identity_allocator_(allocator), cursor_allocator_(allocator),
      edge_page_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand")),
      target_identities_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand")),
      existing_targets_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "GraphExpand"))
{}

void GraphExpandEdgePage::reuse()
{
  edge_page_.reuse();
  target_identities_.reuse();
  existing_targets_.reuse();
  identity_allocator_.reuse();
}

void GraphExpandEdgePage::reset()
{
  edge_page_.reset();
  target_identities_.reset();
  existing_targets_.reset();
  identity_allocator_.reuse();
  after_edge_.reset();
  has_after_edge_ = false;
  cursor_allocator_.reuse();
}

int GraphExpandEdgePage::save_continuation()
{
  int ret = OB_SUCCESS;
  if (edge_page_.empty()) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    // scan_edges has consumed the previous token. Copy the last edge from the
    // independent page arena before that arena can be reused for the next page.
    cursor_allocator_.reuse();
    has_after_edge_ = false;
    after_edge_ = edge_page_.at(edge_page_.count() - 1).edge_identity_;
    if (OB_FAIL(edge_page_.at(edge_page_.count() - 1).edge_identity_.rowkey_.deep_copy(
            after_edge_.rowkey_, cursor_allocator_))) {
    } else { has_after_edge_ = true; }
  }
  return ret;
}

int64_t GraphExpandEdgePage::used_memory() const
{
  const int64_t parts[] = {identity_allocator_.total(), cursor_allocator_.total(),
      edge_page_.get_data_size(), target_identities_.get_data_size(),
      existing_targets_.get_data_size()};
  int64_t total = 0;
  for (int64_t part : parts) {
    if (part < 0 || total > INT64_MAX - part) { return INT64_MAX; }
    total += part;
  }
  return total;
}

int GraphExpandEdgePage::deep_copy_identity(const GraphElementIdentity &source,
                                    GraphElementIdentity &destination)
{
  int ret = OB_SUCCESS;
  destination.reset();
  if (!source.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    destination.graph_id_ = source.graph_id_;
    destination.element_id_ = source.element_id_;
    if (OB_FAIL(source.rowkey_.deep_copy(destination.rowkey_,
                                         identity_allocator_))) {
    }
  }
  return ret;
}

int GraphExpandEdgePage::stabilize_identities(ObIArray<GraphElementIdentity> &identities)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < identities.count(); ++i) {
    GraphElementIdentity stable;
    if (OB_FAIL(deep_copy_identity(identities.at(i), stable))) {
    } else {
      identities.at(i) = stable;
    }
  }
  return ret;
}

int GraphExpandEdgePage::stabilize_edge_page()
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < edge_page_.count(); ++i) {
    GraphExpandEdge stable;
    if (OB_FAIL(deep_copy_identity(edge_page_.at(i).source_identity_,
                                   stable.source_identity_))) {
    } else if (OB_FAIL(deep_copy_identity(edge_page_.at(i).edge_identity_,
                                          stable.edge_identity_))) {
    } else if (OB_FAIL(deep_copy_identity(edge_page_.at(i).target_identity_,
                                          stable.target_identity_))) {
    } else if (OB_FAIL(stable.payload_.deep_copy(edge_page_.at(i).payload_,
                                                identity_allocator_))) {
    } else {
      edge_page_.at(i) = stable;
    }
  }
  return ret;
}


} // namespace sql
} // namespace oceanbase
