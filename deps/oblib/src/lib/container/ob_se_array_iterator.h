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

#ifndef _OB_SE_ARRAY_ITERATOR_H
#define _OB_SE_ARRAY_ITERATOR_H 1
#include "lib/container/ob_se_array.h"
namespace oceanbase
{
namespace common
{
template<typename T, int64_t LOCAL_ARRAY_SIZE, typename BlockAllocatorT, bool auto_free>
typename ObSEArrayImpl<T, LOCAL_ARRAY_SIZE, BlockAllocatorT, auto_free>::iterator ObSEArrayImpl<T, LOCAL_ARRAY_SIZE, BlockAllocatorT, auto_free>::begin()
{
  return iterator(this, 0);
}

template<typename T, int64_t LOCAL_ARRAY_SIZE, typename BlockAllocatorT, bool auto_free>
typename ObSEArrayImpl<T, LOCAL_ARRAY_SIZE, BlockAllocatorT, auto_free>::iterator ObSEArrayImpl<T, LOCAL_ARRAY_SIZE, BlockAllocatorT, auto_free>::end()
{
  return iterator(this, count_);
}

template<typename T, int64_t LOCAL_ARRAY_SIZE, typename BlockAllocatorT, bool auto_free>
typename ObSEArrayImpl<T, LOCAL_ARRAY_SIZE, BlockAllocatorT, auto_free>::const_iterator
ObSEArrayImpl<T, LOCAL_ARRAY_SIZE, BlockAllocatorT, auto_free>::begin() const
{
  return const_iterator(this, 0);
}

template<typename T, int64_t LOCAL_ARRAY_SIZE, typename BlockAllocatorT, bool auto_free>
typename ObSEArrayImpl<T, LOCAL_ARRAY_SIZE, BlockAllocatorT, auto_free>::const_iterator
ObSEArrayImpl<T, LOCAL_ARRAY_SIZE, BlockAllocatorT, auto_free>::end() const
{
  return const_iterator(this, count_);
}

namespace array
{
template <typename T, int64_t LOCAL_ARRAY_SIZE,
          typename BlockAllocatorT,
          bool auto_free>
class ObSEArrayIterator
{
  friend class ObSEArrayImpl<T, LOCAL_ARRAY_SIZE, BlockAllocatorT, auto_free>;
public:
  typedef T value_type;
  typedef int64_t difference_type;
  typedef T *pointer;
  typedef T &reference;
  typedef std::random_access_iterator_tag iterator_category;

public:
  ObSEArrayIterator() : arr_(NULL), index_(0) {}
  explicit ObSEArrayIterator(ObSEArrayImpl<T, LOCAL_ARRAY_SIZE, BlockAllocatorT, auto_free> *arr, int64_t index)
  {
    arr_ = arr;
    index_ = index;
  }
  ObSEArrayIterator(const ObSEArrayImpl<T, LOCAL_ARRAY_SIZE, BlockAllocatorT, auto_free> *arr, int64_t index)
  {
    arr_ = const_cast<ObSEArrayImpl<T, LOCAL_ARRAY_SIZE, BlockAllocatorT, auto_free>*>(arr);
    index_ = index;
  };
  inline T &operator*()
  {
    OB_ASSERT(arr_ != NULL);
    return arr_->at(index_);
  }
  inline T *operator->() const
  {
    OB_ASSERT(arr_ != NULL);
    return &arr_->at(index_);
  }

  operator pointer() const
  {
    return &arr_->at(index_);
  }

  inline ObSEArrayIterator operator++(int)// ObSEArrayIterator++
  {
    OB_ASSERT(arr_ != NULL);
    return ObSEArrayIterator(arr_, index_++);
  }
  inline ObSEArrayIterator operator++()
  {
    OB_ASSERT(arr_ != NULL);
    index_++;
    return *this;
  }
  inline ObSEArrayIterator operator--(int)
  {
    OB_ASSERT(arr_ != NULL);
    return ObSEArrayIterator(arr_, index_--);
  }
  inline ObSEArrayIterator operator--()
  {
    OB_ASSERT(arr_ != NULL);
    index_--;
    return *this;
  }
  inline ObSEArrayIterator operator+(int64_t off)
  {
    OB_ASSERT(arr_ != NULL);
    return ObSEArrayIterator(arr_, index_ + off);
  }
  inline ObSEArrayIterator &operator+=(int64_t off)
  {
    OB_ASSERT(arr_ != NULL);
    index_ += off;
    return *this;
  }
  inline difference_type operator-(const ObSEArrayIterator &rhs) const
  {
    OB_ASSERT(arr_ == rhs.arr_);
    return index_ - rhs.index_;
  }
  inline ObSEArrayIterator operator-(int64_t index)
  {
    OB_ASSERT(arr_ != NULL);
    return ObSEArrayIterator(arr_, this->index_ - index);
  }
  inline ObSEArrayIterator &operator-=(int64_t off)
  {
    OB_ASSERT(arr_ != NULL);
    index_ -= off;
    return *this;
  }
  inline bool operator==(const ObSEArrayIterator &rhs) const
  {
    OB_ASSERT(arr_ == rhs.arr_);
    return (this->index_ == rhs.index_);
  }
  inline bool operator==(const long &value) const
  {
    return (arr_ == NULL);
  }
  inline bool operator!=(const ObSEArrayIterator &rhs) const
  {
    OB_ASSERT(arr_ == rhs.arr_);
    return (this->index_ != rhs.index_);
  }
  inline bool operator<(const ObSEArrayIterator &rhs) const
  {
    OB_ASSERT(arr_ == rhs.arr_);
    return (index_ < rhs.index_);
  }

  inline bool operator<=(const ObSEArrayIterator &rhs) const
  {
    OB_ASSERT(arr_ == rhs.arr_);
    return (index_ <= rhs.index_);
  }
private:
  ObSEArrayImpl<T, LOCAL_ARRAY_SIZE, BlockAllocatorT, auto_free> *arr_;
  int64_t index_;
};

template <typename T, int64_t LOCAL_ARRAY_SIZE,
          typename BlockAllocatorT,
          bool auto_free>
ObSEArrayIterator<T, LOCAL_ARRAY_SIZE, BlockAllocatorT, auto_free> operator+(
  typename ObSEArrayIterator<T, LOCAL_ARRAY_SIZE, BlockAllocatorT, auto_free>::difference_type diff,
  const ObSEArrayIterator<T, LOCAL_ARRAY_SIZE, BlockAllocatorT, auto_free>& iter)
{
  ObSEArrayIterator<T, LOCAL_ARRAY_SIZE, BlockAllocatorT, auto_free> iter2 = iter;
  iter2 += diff;
  return iter2;
}

}
}
}

#endif /* _OB_SE_ARRAY_ITERATOR_H */



