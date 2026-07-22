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

#ifndef OCEANBASE_STORAGE_BLOCKSTORE_OB_OBJECT_READER_WRITER_H_
#define OCEANBASE_STORAGE_BLOCKSTORE_OB_OBJECT_READER_WRITER_H_

#include "storage/meta_mem/ob_meta_obj_struct.h"
#include "storage/blocksstable/ob_macro_block_handle.h"
#include "storage/blocksstable/ob_macro_block_id.h"
#include "storage/blocksstable/ob_data_buffer.h"
#include "storage/blocksstable/ob_storage_object_handle.h"
#include "storage/blocksstable/ob_object_manager.h"
#include "storage/blocksstable/ob_imacro_block_flush_callback.h"

namespace oceanbase
{
namespace storage
{
class ObObjectIOCallback;
struct ObObjectWriteInfo final
{
public:
  ObObjectWriteInfo();
  ~ObObjectWriteInfo() = default;
  ObObjectWriteInfo &operator=(const ObObjectWriteInfo &other);
  bool is_valid() const;
  void reset();
  TO_STRING_KV(KP_(buffer), K_(offset), K_(size), K_(io_desc), K_(io_callback), KP_(write_callback));
public:
  const char *buffer_;
  int64_t offset_;
  int64_t size_;
  common::ObIOFlag io_desc_;
  ObObjectIOCallback *io_callback_;
  blocksstable::ObIMacroBlockFlushCallback *write_callback_;
};

struct ObObjectReadInfo final
{
public:
  ObObjectReadInfo()
    : addr_(), io_desc_(), io_callback_(nullptr), io_timeout_ms_(DEFAULT_IO_WAIT_TIME_MS)
  {}
  ~ObObjectReadInfo() = default;
  bool is_valid() const;
  TO_STRING_KV(K_(addr), K_(io_desc), K_(io_callback), K_(io_timeout_ms));
public:
  ObMetaDiskAddr addr_;
  common::ObIOFlag io_desc_;
  ObObjectIOCallback *io_callback_;
  int64_t io_timeout_ms_;
  DISALLOW_COPY_AND_ASSIGN(ObObjectReadInfo);
};

struct ObObjectsWriteCtx final
{
public:
  ObObjectsWriteCtx()
    : addr_(), block_ids_(), next_opt_()
  {
    block_ids_.set_attr(ObMemAttr("ObjectWCtx"));
  }
  ~ObObjectsWriteCtx();
  bool is_valid() const;
  int set_addr(const ObMetaDiskAddr &addr); // overwrite
  int add_object_id(const blocksstable::MacroBlockId &object_id); // distinct
  void clear();
  int assign(const ObObjectsWriteCtx &other);
  TO_STRING_KV(K_(addr), K_(block_ids), K_(next_opt));
public:
  ObMetaDiskAddr addr_;
  ObArray<blocksstable::MacroBlockId> block_ids_;
  blocksstable::ObStorageObjectOpt next_opt_;
  DISALLOW_COPY_AND_ASSIGN(ObObjectsWriteCtx);
};

struct ObObjectHeader final
{
public:
  static const uint16_t OB_OBJECT_HEADER_MAGIC = 1386;
  static const uint16_t OB_OBJECT_HEADER_VERSION = 1;
  static const blocksstable::MacroBlockId DEFAULT_MACRO_ID; // -1
  ObObjectHeader()
    : magic_(OB_OBJECT_HEADER_MAGIC), version_(OB_OBJECT_HEADER_VERSION),
      cur_block_idx_(0), total_block_cnt_(0), header_size_(0), data_size_(0),
      checksum_(0), next_macro_id_(DEFAULT_MACRO_ID)
  {
    prev_addr_.set_none_addr();
  }
  ~ObObjectHeader() = default;
  bool is_valid() const;

  NEED_SERIALIZE_AND_DESERIALIZE;
  TO_STRING_KV(K_(magic), K_(version), K_(header_size), K_(data_size),
               K_(cur_block_idx), K_(total_block_cnt), K_(checksum),
               K_(next_macro_id), K_(prev_addr));
public:
  uint16_t magic_;
  uint16_t version_;
  uint16_t cur_block_idx_;
  uint16_t total_block_cnt_;
  int32_t header_size_;
  int32_t data_size_;
  int64_t checksum_;
  blocksstable::MacroBlockId next_macro_id_; // -1 indicates end
  ObMetaDiskAddr prev_addr_;
};

class ObObjectBaseHandle
{
  friend class ObObjectReaderWriter;
  friend class ObObjectLinkIter;
public:
  ObObjectBaseHandle()
    : object_handles_(), addrs_()
  {}
  virtual ~ObObjectBaseHandle() = default;
  void reset();
  TO_STRING_KV(K(addrs_.count()), K(object_handles_.count()), K_(addrs), K_(object_handles));
protected:
  int wait();
  int add_object_handle(const blocksstable::ObStorageObjectHandle &object_handle);
  int add_meta_addr(const ObMetaDiskAddr &addr);
protected:
  ObSEArray<blocksstable::ObStorageObjectHandle, 1> object_handles_;
  ObSEArray<ObMetaDiskAddr, 1> addrs_;
  DISALLOW_COPY_AND_ASSIGN(ObObjectBaseHandle);
};

class ObObjectIOCallback : public common::ObIOCallback
{
public:
  ObObjectIOCallback(common::ObIAllocator *io_allocator, const ObMetaDiskAddr addr,
                           const common::ObIOCallbackType type)
    : common::ObIOCallback(type), io_allocator_(io_allocator), addr_(addr), data_buf_(nullptr) {}
  virtual ~ObObjectIOCallback();
  virtual int alloc_data_buf(const char *io_data_buffer, const int64_t data_size) override;
  int inner_process(const char *data_buffer, const int64_t size) override;
  virtual int do_process(const char *buf, const int64_t buf_len) = 0;
  virtual int64_t size() const = 0;
  virtual const char *get_data() override;
  virtual ObIAllocator *get_allocator() override { return io_allocator_; }

  VIRTUAL_TO_STRING_KV(K_(addr), KP_(io_allocator), KP_(data_buf));
  bool is_valid() const
  {
    return addr_.is_block() && nullptr != io_allocator_;
  }

private:
  ObIAllocator *io_allocator_;
  ObMetaDiskAddr addr_;
  char *data_buf_;   // actual data buffer
};


class ObObjectReadHandle final
{
  friend class ObObjectReaderWriter;
  friend class ObObjectLinkIter;
  friend class ObObjectIOCallback;
public:
  ObObjectReadHandle();
  ObObjectReadHandle(ObIAllocator &allocator);
  ~ObObjectReadHandle();
  ObObjectReadHandle(const ObObjectReadHandle &other);
  ObObjectReadHandle &operator=(const ObObjectReadHandle &other);
  bool is_valid() const;
  bool is_empty() const;
  int wait();
  int get_data(ObIAllocator &allocator, char *&buf, int64_t &buf_len);

  static int get_data(const ObMetaDiskAddr &addr, const char *src_buf, const int64_t src_buf_len,
                      char *&buf, int64_t &buf_len);
  void reset();
  TO_STRING_KV(K_(addr), K_(object_handle));

private:
  static int verify_checksum(
      const char *data_buf,
      const int64_t data_size,
      int64_t &header_size,
      int64_t &buf_len);
  int alloc_io_buf(char *&buf, const int64_t &buf_size);
  int set_addr_and_object_handle(const ObMetaDiskAddr &addr, const blocksstable::ObStorageObjectHandle &object_handle);

private:
  ObIAllocator *allocator_;
  blocksstable::ObStorageObjectHandle object_handle_;
  ObMetaDiskAddr addr_;
};

class ObObjectWriteHandle final : public ObObjectBaseHandle
{
  friend class ObObjectReaderWriter;
public:
  ObObjectWriteHandle() = default;
  ~ObObjectWriteHandle() = default;
  bool is_valid() const;
  int get_write_ctx(ObObjectsWriteCtx &write_ctx);
  DISALLOW_COPY_AND_ASSIGN(ObObjectWriteHandle);
};

class ObObjectBatchHandle final : public ObObjectBaseHandle
{
  friend class ObObjectReaderWriter;
public:
  ObObjectBatchHandle() = default;
  ~ObObjectBatchHandle() = default;
  bool is_valid() const;
  int batch_get_write_ctx(ObIArray<ObObjectsWriteCtx> &write_ctxs);
  INHERIT_TO_STRING_KV("ObObjectBaseHandle", ObObjectBaseHandle, K_(write_ctxs));
protected:
  ObArray<ObObjectsWriteCtx> write_ctxs_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObObjectBatchHandle);
};
class ObObjectLinkHandle final : public ObObjectBaseHandle
{
  friend class ObObjectReaderWriter;
public:
  ObObjectLinkHandle() = default;
  ~ObObjectLinkHandle() = default;
  bool is_valid() const;
  int get_write_ctx(ObObjectsWriteCtx &write_ctx); // always get prev block
  INHERIT_TO_STRING_KV("ObObjectBaseHandle", ObObjectBaseHandle, K_(write_ctx));
protected:
  ObObjectsWriteCtx write_ctx_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObObjectLinkHandle);
};

class ObObjectLinkIter final
{
public:
  ObObjectLinkIter()
    : head_(), cur_(), is_inited_(false)
  {}
  ~ObObjectLinkIter() = default;
  int init(const ObMetaDiskAddr &head);
  int reuse();
  int get_next_block(ObIAllocator &allocator, char *&buf, int64_t &buf_len);
  int get_next_macro_id(blocksstable::MacroBlockId &macro_id);
  TO_STRING_KV(K_(head), K_(cur), K_(is_inited));
private:
  int read_next_block(ObObjectReadHandle &object_io_handle);
private:
  ObMetaDiskAddr head_;
  ObMetaDiskAddr cur_;
  bool is_inited_;
};

class ObObjectReaderWriter final
{
private:
  struct ObObjectWriteArgs;
  struct ObObjectWriteSession;
public:
  ObObjectReaderWriter();
  ~ObObjectReaderWriter();
  int init();
  void reset();
  void get_cur_block(blocksstable::MacroBlockId &macro_id);
  static int async_read(const ObObjectReadInfo &read_info, ObObjectReadHandle &object_io_handle);
  static int parse_data_from_object(
      blocksstable::ObStorageObjectHandle &object_handle,
      const ObMetaDiskAddr addr,
      char *&buf,
      int64_t &buf_len);
  int async_write(
      const ObObjectWriteInfo &write_info,
      const blocksstable::ObStorageObjectOpt &curr_opt,
      ObObjectWriteHandle &object_io_handle);
  int async_batch_write(
      const common::ObIArray<ObObjectWriteInfo> &write_infos,
      ObObjectBatchHandle &object_io_handle,
      blocksstable::ObStorageObjectOpt &curr_opt);
  int async_link_write(
      const ObObjectWriteInfo &write_infos,
      const blocksstable::ObStorageObjectOpt &curr_opt,
      ObObjectLinkHandle &object_io_handle);
private:
  int inner_async_write(
      ObObjectWriteSession &write_session,
      const ObObjectWriteInfo &write_info,
      const ObObjectWriteArgs &write_args,
      ObObjectBaseHandle &object_io_handle,
      ObObjectsWriteCtx &write_ctx);
  int write_block(
      ObObjectWriteSession &write_session,
      const ObObjectWriteInfo &write_info,
      const ObObjectWriteArgs &write_args,
      ObObjectBaseHandle &object_io_handle,
      ObObjectsWriteCtx &write_ctx);
  int calc_store_size(
      const int64_t total_size,
      const bool need_align,
      int64_t &store_size,
      int64_t &align_store_size);
  int inner_write_block(
      ObObjectWriteSession &write_session,
      const ObObjectHeader &header,
      const ObObjectWriteInfo &write_info,
      const ObObjectWriteArgs &write_args,
      ObObjectBaseHandle &object_io_handle,
      ObObjectsWriteCtx &write_ctx);
  int switch_object(ObObjectWriteSession &write_session,
                    blocksstable::ObStorageObjectHandle &object_handle,
                    const blocksstable::ObStorageObjectOpt &next_opt);
  int do_switch(ObObjectWriteSession &write_session,
                const blocksstable::ObStorageObjectOpt &opt);
  int reserve_header(ObObjectWriteSession &write_session);
  int ensure_data_buffer_for_write_(ObObjectWriteSession &write_session);
  int check_object_size_(const int64_t total_size, const bool need_align);
private:
struct ObObjectWriteArgs final
{
public:
  ObObjectWriteArgs()
    : need_flush_(true), need_align_(true), is_linked_(false), with_header_(true), object_opt_()
  {}
  ~ObObjectWriteArgs() = default;
  TO_STRING_KV(K_(need_flush), K_(need_align), K_(is_linked), K_(with_header), K_(object_opt));
  bool need_flush_;
  bool need_align_;
  bool is_linked_;
  bool with_header_;
  blocksstable::ObStorageObjectOpt object_opt_;
};
private:
  lib::ObMutex mutex_;
  blocksstable::ObStorageObjectHandle object_handle_;
  int64_t offset_;
  int64_t align_offset_;
  int64_t write_align_size_;
  bool hanging_;
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObObjectReaderWriter);
};

}  // end namespace storage
}  // end namespace oceanbase

#endif  // OCEANBASE_STORAGE_BLOCKSTORE_OB_OBJECT_READER_WRITER_H_
