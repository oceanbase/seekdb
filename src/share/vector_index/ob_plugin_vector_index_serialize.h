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
#ifndef OCEANBASE_SHARE_PLUGIN_VECTOR_INDEX_SERIALIZE_H_
#define OCEANBASE_SHARE_PLUGIN_VECTOR_INDEX_SERIALIZE_H_
#include <iostream>
#include "lib/function/ob_function.h"
#include "lib/allocator/page_arena.h"
#include "common/row/ob_row_iterator.h"
#include "share/ob_lob_access_utils.h"
#include "share/vector_index/ob_plugin_vector_index_util.h"
#include "ob_vector_index_util.h"

namespace oceanbase
{
namespace share
{


class ObStreamBuf : public std::streambuf
{
public:
  explicit ObStreamBuf(char *data, const int64_t capacity)
    : std::streambuf(),
      capacity_(capacity),
      data_(data),
      last_error_code_(OB_SUCCESS)
  {}
  bool is_valid() const { return nullptr != data_; }
  bool is_success() const { return OB_SUCCESS == last_error_code_; }

  TO_STRING_KV(K_(data), K_(capacity));
protected:
  int64_t capacity_;
  char *data_;
  int last_error_code_;
};

class ObOStreamBuf : public ObStreamBuf
{
public:
  struct CbParam
  {
    virtual ~CbParam() = default;
  };
  using Callback = ObFunction<int(const char *, const int64_t, CbParam &)>;
  explicit ObOStreamBuf(char *data, const int64_t capacity, CbParam &cb_param, Callback &cb) 
    : ObStreamBuf(data, capacity),
      cb_param_(cb_param),
      cb_(cb)
  {
    setp(data_, data_ + capacity_ - 1);
  }

  void check_finish();
  int get_error_code() const { return last_error_code_; }
  TO_STRING_KV(K(this));

protected:
  virtual std::streamsize xsputn(const char* s, std::streamsize count) override;
  virtual int_type overflow(int_type c) override;

private:
  int do_callback();

private:
  CbParam &cb_param_;
  Callback cb_;
};

class ObIStreamBuf : public ObStreamBuf
{
public:
  struct CbParam
  {
    virtual ~CbParam() = default;
  };
  using Callback = ObFunction<int(char *&, const int64_t, int64_t &, CbParam &)>;
  explicit ObIStreamBuf(char *data, const int64_t capacity, CbParam &cb_param, Callback &cb) 
    : ObStreamBuf(data, capacity),
      cb_param_(cb_param),
      cb_(cb)
  {
    setg(data_, data_, data_);
  }
  int init();
  int get_error_code() const { return last_error_code_; }
  TO_STRING_KV(K_(data));

protected:
  virtual std::streamsize xsgetn(char* s, std::streamsize n) override;
  virtual int_type underflow() override;

  virtual pos_type seekoff(off_type off, std::ios_base::seekdir dir,
                           std::ios_base::openmode mode = std::ios_base::in | std::ios_base::out) override;

  virtual pos_type seekpos(pos_type pos, std::ios_base::openmode mode = std::ios_base::in | std::ios_base::out) override;

private:
  int do_callback();

private:
  CbParam &cb_param_;
  Callback cb_;
};

class ObHNSWDeserializeCallback {
public:
  struct CbParam : public ObIStreamBuf::CbParam {
    CbParam(ObNewRowIterator *iter, ObIAllocator *allocator)
      : iter_(iter), allocator_(allocator), str_iter_(nullptr)
    {}
    CbParam()
      : iter_(nullptr),
        allocator_(nullptr),
        str_iter_(nullptr)
    {}
    virtual ~CbParam() {
      if (str_iter_ != nullptr) {
        str_iter_->~ObTextStringIter();
        if (allocator_ != nullptr) {
          allocator_->free(str_iter_);
        }
        str_iter_ = nullptr;
      }
    }
    bool is_valid() const
    {
      return nullptr != iter_
             && nullptr != allocator_;
    }
    ObNewRowIterator *iter_;
    ObIAllocator *allocator_;
    ObTextStringIter *str_iter_;
  };
public:
  ObHNSWDeserializeCallback(void *adp) : index_type_(VIAT_MAX), adp_(adp)
  {}
  ObVectorIndexAlgorithmType get_serialize_index_type() { return index_type_; }
  int operator()(char *&data, const int64_t data_size, int64_t &read_size, share::ObIStreamBuf::CbParam &cb_param);
private:
  ObVectorIndexAlgorithmType index_type_;
  void *adp_;
};

class ObHNSWSerializeCallback {
public:
  struct CbParam : public ObOStreamBuf::CbParam {
    CbParam()
      : vctx_(nullptr), allocator_(nullptr), tmp_allocator_(nullptr), tx_desc_(nullptr), snapshot_(nullptr),
        timeout_(0), lob_inrow_threshold_(0)
    {}
    virtual ~CbParam() {}
    bool is_valid() const
    {
      return nullptr != vctx_
             && nullptr != allocator_
             && nullptr != tx_desc_
             && nullptr != snapshot_;
    }
    void *vctx_; // ObVecIdxSnapshotDataWriteCtx
    ObIAllocator *allocator_;
    ObIAllocator *tmp_allocator_;
    void *tx_desc_; // transaction::ObTxDesc
    void *snapshot_; // transaction::ObTxReadSnapshot
    int64_t timeout_;
    int64_t lob_inrow_threshold_;
  };
public:
  ObHNSWSerializeCallback()
  {}
  int operator()(const char *data, const int64_t data_size, share::ObOStreamBuf::CbParam &cb_param);
private:
};

class ObVectorIndexSerializer 
{
public:
  explicit ObVectorIndexSerializer(ObIAllocator &allocator)
    : allocator_(allocator)
  {}
  
  int serialize(void *index, ObOStreamBuf::CbParam &cb_param, ObOStreamBuf::Callback &cb, uint64_t tenant_id, const int64_t capacity = DEFAULT_OUTBUF_CAPACITY);
  int deserialize(void *&index, ObIStreamBuf::CbParam &cb_param, ObIStreamBuf::Callback &cb, uint64_t tenant_id);
private:
  static const int64_t DEFAULT_OUTBUF_CAPACITY = 2LL * 1024LL * 1024LL; // 2MB

private:
  bool is_inited_;
  ObIAllocator &allocator_;
};

};
};
#endif // OCEANBASE_SHARE_PLUGIN_VECTOR_INDEX_SERIALIZE_H_
