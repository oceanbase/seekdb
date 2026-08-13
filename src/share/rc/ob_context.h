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

#ifndef OB_CONTEXT_H_
#define OB_CONTEXT_H_

#include "lib/worker.h"
#include "lib/lock/ob_latch.h"
#include "lib/rc/context.h"

namespace oceanbase
{
namespace share
{
class ObResourceOwner;
}

#define CREATE_ENTITY(entity, ...) share::create_entity(entity, __VA_ARGS__)
#define DESTROY_ENTITY(entity) destroy_entity(entity)
#define WITH_ENTITY_P(condition, entity)                                                              \
  ENTITY_P(condition, share::Entity2Enum<typename std::remove_pointer<decltype(entity)>::type>::type, \
           share::EntitySource::WITH, entity)
#define CREATE_WITH_TEMP_ENTITY_P(condition, entity_type, ...)                                        \
  ENTITY_P(condition, share::ObEntityType::entity_type, share::EntitySource::CREATE, __VA_ARGS__)
#define WITH_ENTITY(entity) WITH_ENTITY_P(true, entity)
#define CREATE_WITH_TEMP_ENTITY(entity_type, ...) CREATE_WITH_TEMP_ENTITY_P(true, entity_type, __VA_ARGS__)
// The following are auxiliary macros
#define ENTITY_P(condition, entity_type, entity_source, ...)                                                   \
  for (share::_S<entity_type, entity_source> _s{condition, __VA_ARGS__}; OB_SUCC(ret) && _s.i_-- > 0; _s.i_--) \
    if (OB_SUCC(_s.get_ret()))

#define CURRENT_ENTITY(TYPE) share::Guard<share::ObEntityType::TYPE>::current_guard()

#define BIND_ENTITY(ENTITY_TYPE, CLS)             \
  template<>                                      \
  class Enum2Entity<ENTITY_TYPE>                  \
  {                                               \
  public:                                         \
    using type = CLS;                             \
  };                                              \
  template<>                                      \
  class Entity2Enum<CLS>                          \
  {                                               \
  public:                                         \
    const static ObEntityType type = ENTITY_TYPE; \
  };

namespace share
{

enum class ObEntityType
{
  RESOURCE_OWNER
};

template<ObEntityType et>
class Enum2Entity;
template<typename T_Entity>
class Entity2Enum;

class EntityBase
{
  template<typename T_Entity, typename ... Args>
  friend int create_entity(T_Entity *&entity,
                           Args && ... args);
  template<typename T_Entity, typename ... Args>
  friend int create_entity(T_Entity &entity,
                           Args && ... args);
  template<typename T_Entity>
  friend void destroy_entity(T_Entity *entity);
public:
  EntityBase()
    : need_free_(true) {}
private:
  bool need_free_;
};

class ObResourceOwner : public EntityBase
{
public:
  explicit ObResourceOwner(const uint64_t owner_id)
    : owner_id_(owner_id)
  {}
  int init() { return common::OB_SUCCESS; }
  void deinit() {}
  uint64_t get_owner_id() const { return owner_id_; }
  static int guard_init_cb(const ObResourceOwner &, char *, bool &)
  { return common::OB_SUCCESS; }
  static void guard_deinit_cb(const ObResourceOwner &, char *) {}
  static ObResourceOwner &root();
private:
  uint64_t owner_id_;
};

template<ObEntityType et>
class Guard;
enum class EntitySource
{
  WITH,     // The parameter is already the target Entity, switch directly
  CREATE    // Created by parameters
};

BIND_ENTITY(ObEntityType::RESOURCE_OWNER, share::ObResourceOwner);

template<ObEntityType et>
class Guard final
{
  using T_Entity = typename Enum2Entity<et>::type;
public:
  Guard(T_Entity &ref_entity)
    : ref_entity_(ref_entity),
      prev_(nullptr),
      next_(nullptr),
      is_inited_(false),
      is_inited_of_cb_(false)
  {}
  T_Entity *operator -> () { return &ref_entity_; }
  T_Entity &entity() { return ref_entity_; }
  int init()
  {
    int ret = common::OB_SUCCESS;
    Guard *&cur = g_guard();
    if (nullptr == cur) {
      cur = this;
    } else {
      abort_unless(cur != this);
      cur->next_ = this;
      this->prev_ = cur;
      cur = this;
    }
    is_inited_ = true;
    ret = T_Entity::guard_init_cb(ref_entity_, buf_, is_inited_of_cb_);
    return ret;
  }
  void deinit()
  {
    if (is_inited_of_cb_) {
      T_Entity::guard_deinit_cb(ref_entity_, buf_);
    }
    if (is_inited_) {
      Guard *&cur = g_guard();
      abort_unless(cur == this);
      Guard *parent = cur->prev_;
      if (nullptr == parent) {
        cur = nullptr;
      } else {
        parent->next_ = nullptr;
        cur->prev_ = nullptr;
        cur = parent;
      }
    }
  }
  static Guard &current_guard()
  {
    Guard *&cur = g_guard();
    if (OB_UNLIKELY(nullptr == cur)) {
      struct GuardBuf {
        char v_[sizeof(Guard)];
      };
      RLOCAL(GuardBuf, buf);
      Guard *guard = new ((&buf)->v_) Guard(T_Entity::root());
      abort_unless(guard != nullptr);
      int ret = guard->init();
      abort_unless(common::OB_SUCCESS == ret);
      cur = guard;
    }
    return *cur;
  }
  Guard &parent()
  {
    abort_unless(prev_ != nullptr);
    return *prev_;
  }
private:
  static Guard *&g_guard()
  {
    RLOCAL(Guard*, g_guard);
    return g_guard;
  }
private:
  T_Entity &ref_entity_;
  Guard *prev_;
  Guard *next_;
  bool is_inited_;
  bool is_inited_of_cb_;
  // Buf for forced conversion, temporary code
  char buf_[32];
};

template<typename T_Entity, typename ... Args>
inline int create_entity(T_Entity *&entity,
                         Args && ... args)
{
  int ret = common::OB_SUCCESS;

  lib::ObMemAttr attr;
  attr.label_ = "CreateEntity";
  void *ptr = ROOT_CONTEXT->allocf(sizeof(T_Entity), attr);
  if (OB_ISNULL(ptr)) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
  } else {
    entity = new (ptr) T_Entity(args...);
    entity->need_free_ = true;
    if (OB_FAIL(entity->init())) {
    }
  }
  if (OB_FAIL(ret)) {
    destroy_entity(entity);
  }
  return ret;
}

template<typename T_Entity, typename ... Args>
inline int create_entity(T_Entity &entity,
                         Args && ... args)
{
  int ret = common::OB_SUCCESS;

  new (&entity) T_Entity(args...);
  entity.need_free_ = false;
  ret = entity.init();
  if (OB_FAIL(ret)) {
    destroy_entity(&entity);
  }
  return ret;
}

template<typename T_Entity>
inline void destroy_entity(T_Entity *entity)
{
  if (OB_LIKELY(entity != nullptr)) {
    const bool need_free = entity->need_free_;
    entity->deinit();
    entity->~T_Entity();
    if (need_free) {
      ROOT_CONTEXT->free(entity);
    }
  }
}

class _SBase
{
public:
  int get_ret() const
  {
    return ret_;
  }
  _SBase()
    : i_(1),
      ret_(common::OB_SUCCESS)
  {}
  ~_SBase()
  {
    if (OB_UNLIKELY(0 == i_)) {
      OB_LOG_RET(WARN, common::OB_ERR_UNEXPECTED, "has break statement!!!");
    }
  }
  int i_;
  int ret_;
};

template<ObEntityType ct, EntitySource es> class _S {};

template<ObEntityType ct>
class _S<ct, EntitySource::WITH> : public _SBase
{
  using T_Guard =  Guard<ct>;
  using T_Entity = typename Enum2Entity<ct>::type;
public:
  _S(const bool condition, T_Entity *entity)
    : _SBase(), guard_(nullptr)
  {
    int ret = common::OB_SUCCESS;
    if (OB_ISNULL(entity)) {
      ret = common::OB_INVALID_ARGUMENT;
    } else if (condition) {
      T_Guard *tmp_guard = new (buf_) T_Guard(*entity);
      if (OB_FAIL(tmp_guard->init())) {
      } else {
        guard_ = tmp_guard;
      }
    }
    ret_ = ret;
  }
  ~_S()
  {
    if (guard_ != nullptr) {
      guard_->deinit();
      guard_->~T_Guard();
    }
  }
  char buf_[sizeof(T_Guard)] __attribute__ ((aligned (16)));
  T_Guard *guard_;
};

template<ObEntityType ct>
class _S<ct, EntitySource::CREATE> : public _SBase
{
  using T_Guard =  Guard<ct>;
  using T_Entity = typename Enum2Entity<ct>::type;
public:
  template<typename ... Args>
  _S(const bool condition, Args && ... args)
    : _SBase(), entity_(nullptr), guard_(nullptr)
  {
    int ret = common::OB_SUCCESS;
    if (OB_LIKELY(condition)) {
      T_Entity *tmp_entity = reinterpret_cast<T_Entity*>(buf0_);
      if (OB_FAIL(create_entity(*tmp_entity, args...))) {
      } else {
        entity_ = tmp_entity;
        T_Guard *tmp_guard = new (buf1_) T_Guard(*entity_);
        if (OB_FAIL(tmp_guard->init())) {
        } else {
          guard_ = tmp_guard;
        }
      }
    }
    ret_ = ret;
  }
  ~_S()
  {
    if (guard_ != nullptr) {
      guard_->deinit();
      guard_->~T_Guard();
    }
    if (entity_ != nullptr) {
      destroy_entity(entity_);
    }
  }
  char buf0_[sizeof(T_Entity)] __attribute__ ((aligned (16)));
  char buf1_[sizeof(T_Guard)] __attribute__ ((aligned (16)));
  T_Entity *entity_;
  T_Guard *guard_;
};

} // end of namespace share
} // end of namespace oceanbase

#endif // OB_CONTEXT_H_
