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

#include <gtest/gtest.h>
#include "data_plane/ddl/ob_direct_insert.h"
#include "lib/allocator/page_arena.h"

namespace oceanbase
{
namespace data_plane
{
namespace
{

class FakeWorkerContext final : public ObIDirectInsertWorkerContext
{
public:
  FakeWorkerContext() : bind_count_(0) {}
  void bind_current_thread() override { ++bind_count_; }
  int64_t bind_count_;
};

class FakeWriter final : public ObIDirectInsertWriter
{
public:
  FakeWriter(common::ObIAllocator &allocator, int64_t &destroy_count)
    : allocator_(&allocator), destroy_count_(&destroy_count), tablet_id_(1)
  {}

  int append_row(const ObDirectInsertRowView &) override
  {
    return common::OB_SUCCESS;
  }
  int append_batch(const ObDirectInsertBatchView &) override
  {
    return common::OB_SUCCESS;
  }
  int close() override { return common::OB_SUCCESS; }
  int64_t get_row_count() const override { return 0; }
  const common::ObTabletID &get_tablet_id() const override
  {
    return tablet_id_;
  }
  int64_t get_slice_index() const override { return 0; }

private:
  void destroy_self() override
  {
    common::ObIAllocator *allocator = allocator_;
    int64_t *destroy_count = destroy_count_;
    this->~FakeWriter();
    ++*destroy_count;
    allocator->free(this);
  }

private:
  common::ObIAllocator *allocator_;
  int64_t *destroy_count_;
  common::ObTabletID tablet_id_;
};

TEST(DirectInsertInterface, InvalidStartDoesNotPublishSession)
{
  common::ObArenaAllocator allocator("DirectInsertUt");
  FakeWorkerContext worker_context;
  ObDirectInsertStartParam invalid_param;
  ObIDirectInsertSession *session = nullptr;

  ASSERT_EQ(common::OB_INVALID_ARGUMENT,
      ObDirectInsertOrchestrator::start(
          allocator, invalid_param, worker_context, session));
  ASSERT_EQ(nullptr, session);
  ASSERT_EQ(0, worker_context.bind_count_);
  ASSERT_EQ(common::OB_SUCCESS, ObDirectInsertOrchestrator::finish(session));
}

TEST(DirectInsertInterface, BatchViewCarriesOneBorrowedSelection)
{
  common::ObIVector *vectors[] = {nullptr, nullptr};
  const uint16_t selected_rows[] = {1, 3, 5};

  const ObDirectInsertBatchView contiguous =
      ObDirectInsertBatchView::contiguous(vectors, 2, 2, 4);
  ASSERT_TRUE(contiguous.is_valid());
  ASSERT_EQ(ObDirectInsertBatchView::CONTIGUOUS_SELECTION,
      contiguous.selection_type_);
  ASSERT_EQ(4, contiguous.row_count_);

  const ObDirectInsertBatchView indexed =
      ObDirectInsertBatchView::indexed(vectors, 2, selected_rows, 3);
  ASSERT_TRUE(indexed.is_valid());
  ASSERT_EQ(ObDirectInsertBatchView::INDEX_SELECTION,
      indexed.selection_type_);
  ASSERT_EQ(selected_rows, indexed.indices_);
}

TEST(DirectInsertInterface, FactoryDestroyUsesWriterOwnedAllocator)
{
  common::ObArenaAllocator allocator("DirectInsertUt");
  int64_t destroy_count = 0;
  FakeWriter *fake = OB_NEWx(FakeWriter, &allocator, allocator, destroy_count);
  ASSERT_NE(nullptr, fake);
  ObIDirectInsertWriter *writer = fake;

  ObIDirectInsertWriterFactory::destroy(writer);
  ASSERT_EQ(nullptr, writer);
  ASSERT_EQ(1, destroy_count);
  ObIDirectInsertWriterFactory::destroy(writer);
  ASSERT_EQ(1, destroy_count);
}

} // namespace
} // namespace data_plane
} // namespace oceanbase
