# seekdb KVCache 现状剖析与 Rust 内存缓存替代方案评估

## 1. 背景与目标

seekdb 当前使用 `ObKVCache`（位于 `src/share/cache/`）作为核心通用缓存底座，支撑了微块缓存（`ObDataMicroBlockCache`、`ObIndexMicroBlockCache`）、存储元数据缓存（`ObStorageMetaCache`）、Schema 缓存（`ObSchemaCache`）、行缓存（`ObRowCache`）、布隆过滤器缓存（`ObBloomFilterCache`）、优化器统计信息缓存（`ObOptTableStatCache` 等）以及临时文件缓存（`ObTmpFileCache`）等 15+ 种内部缓存。

本评估文档梳理现有 `ObKVCache` 的架构缺陷与耦合约束，并结合以下新架构目标给出 Rust 替代选型与设计方案：

1. **纯内存缓存**（不引入磁盘二级缓存复杂性）；
2. **支持空闲自动过期释放（TTI, Time-To-Idle）**，一段时间不使用的缓存条目自动释放内存；
3. **支持控制整个缓存系统的总内存使用量（Global Memory Budget）**；
4. **各业务种类的 Cache 在实现与类型上完全解耦分开**，不再强绑在单一无类型 C++ 基类上。

---

## 2. 现有 `ObKVCache` 的核心机制与缺陷剖析

### 2.1 核心架构痛点

1. **以 2MB MemBlock 为粒度的粗粒度淘汰与写放大**
   - `ObKVCacheStore`（`src/share/cache/ob_kvcache_store.h`）将 KV 对追加写入约 2MB（`DEFAULT_BLOCK_SIZE = 2080768`）的 `ObKVStoreMemBlock` 中。
   - 淘汰（`wash`）只能按整块 `ObKVMemBlockHandle` 进行，无法淘汰单个冷 KV。
   - 当一个 `LRU` 块内某个 KV 被高频访问触发 `need_modify_cache` 时，`ObKVCacheMap::get`（`src/share/cache/ob_kvcache_map.cpp`）必须在读路径上加哈希桶写锁并执行 `internal_data_move`，将 KV 重新深拷贝到 `LFU` 块中，带来读路径锁竞争与内存拷贝放大。

2. **哈希索引与存储生命周期割裂，后台巡检开销大**
   - `ObKVCacheStore::wash()` 淘汰 MemBlock 时，仅递增 `mb_handle->seq_num_` 并将其状态置为 `FREE`，**不会同步清理 `ObKVCacheMap` 哈希桶中的 `Node`**。
   - 哈希表中残留大量指向已失效 `seq_num_` 的僵尸节点（garbage node），需要后台定时任务 `clean_garbage_node` 和 `replace_fragment_node` 持续扫表清理与搬迁碎片节点（`src/share/cache/ob_kv_storecache.cpp`），在桶数量达百万/千万级时产生持续的 CPU 与锁开销。

3. **三套并发延迟回收机制叠加，维护成本高且易产生长尾内存占用**
   - MemBlock 层使用自定义的 `HazardDomain` + `HazardPointer`（`src/share/cache/ob_kvcache_hazard_domain.h`）；
   - `ObKVMemBlockHandle` 句柄池复用使用 `QClock` + `RetireStation`（`src/share/cache/ob_kvcache_store.cpp`）；
   - `ObKVCacheMap::Node` 又使用第三套基于 Hazard Version 的 `ObKVCacheHazardStation`（`src/share/cache/ob_kvcache_hazard_version.h`）。
   - 任何一个慢读者持有 `ObKVCacheHandle` 都会 pin 住整个 2MB MemBlock 无法回收；同时 `ObIKVCacheValue` 在 MemBlock 淘汰时**不会逐个调用析构函数**，迫使所有缓存值必须通过 `deep_copy` 平铺在连续裸 buffer 上。

4. **多逻辑 Cache 混写，缺乏有效隔离与空闲自动释放能力**
   - 全局最多注册 32 个逻辑 Cache，不同 `cache_id` 的小 KV 混写在同一个 `active_mb_handles_[LRU/LFU]` 块中，无法做到精确的按 Cache 种类独立过期释放（当全局内存未达上限时，早已不再访问的冷缓存会一直占着已分配的 2MB 块）。

---

### 2.2 替换时需关注的存量调用契约

在将各业务 Cache 逐步迁移或替换到 Rust 实现时，需关注现有 C++ 调用方的 4 个核心交互契约：

1. **按字节权重控容（Byte Weight）**：缓存条目大小从几十字节到几十 KB（甚至 >2MB 的大微块）不等，必须按实际字节数而非条目数限制容量。
2. **句柄借用生命周期（`ObKVCacheHandle`）**：调用方通过 `get(key, pvalue, handle)` 获取指针 `const Value *pvalue`，只要 `handle` 未析构或未 `reset()`，即使该条目已从缓存中淘汰或超时驱逐，`pvalue` 指向的内存仍必须保持有效。
3. **预分配零拷贝写入（`alloc` + `put_kvpair`）**：`ObDataMicroBlockCache::put_cache_block`、`ObKVCachePreWarmer` 和 `ObTmpFileCache` 会先在缓存中预分配空间并原地解压/构造数据，再插入哈希表以避免二次 `memcpy`。
4. **Pointer Swizzling（`ObPointerSwizzleNode`）**：索引微块扫描器（`ObIndexBlockRowScanner`）在内存中保存指向数据微块的弱引用，跳过哈希查找直接验证有效性（`src/storage/blocksstable/ob_kvcache_pointer_swizzle.cpp`）。

---

## 3. Rust 生态主流内存 Cache 库评估对比

在不考虑磁盘缓存、仅考虑纯内存，且要求支持 **TTI 空闲释放 + 字节容量控制 + 各 Cache 类型独立** 的前提下，Rust 生态最核心的两个候选库是 **`moka`** 与 **`quick_cache`**：

| 评估维度 | **`moka` (`moka::sync::Cache`)** | **`quick_cache` (`quick_cache::sync::Cache`)** | **`foyer-memory`** |
| :--- | :--- | :--- | :--- |
| **核心淘汰算法** | **W-TinyLFU**（Window + Count-Min Sketch 准入 + SLRU，源自 Java Caffeine） | **S3-FIFO**（10% Small FIFO + 90% Main FIFO + Ghost 队列） | S3-FIFO / W-TinyLFU / SIEVE 可插拔 |
| **底层并发模型** | 无锁并发哈希表 + 读写事件通道异步批处理维护淘汰队列 | 分片 `RwLock`（Sharded Lock）+ 扁平数组索引链表（无节点堆分配） | 分片锁 + Intrusive 链表 |
| **空闲自动释放（TTI）** | **原生内置 (`.time_to_idle(Duration)`)** 及单条目自定义 `Expiry` trait（内置分层时间轮） | **不内置时间轮**，需在 Value 外包装 `last_access` 原子时间戳并配合周期性 `retain` 清理 | 不内置时间轮，需外层封装 |
| **单实例字节容量控制** | 原生支持 `.weigher(\|k, v\| -> u32)` + `.max_capacity(bytes)` | 原生支持 `Weighter<K, V>` + 字节容量上限 | 原生支持 `Weighter` |
| **运行时动态扩缩容** | `max_capacity` 在 `build()` 时固定，不支持运行期直接调小单实例上限 | **原生支持动态调整容量**，适合做跨实例动态配额挪用 | 部分支持，抽象偏重 |
| **淘汰实时性（内存边界）** | **最终一致（异步微批）**：高频写入时先入队后淘汰，可通过 `run_pending_tasks()` 同步收敛 | **强一致（同步淘汰）**：`insert` 时在分片锁内当场逐出旧条目，严格不超配额 | 同步淘汰 |
| **并发回源去重（Singleflight）** | 原生支持 `get_with` / `try_get_with` | 原生支持 `get_value_or_guard` / `Placeholder` | 原生支持请求去重 |

### 3.1 `moka` (`moka::sync::Cache`) 详细分析

- **核心优势**：
  - **开箱即用的 TTI 与自定义过期策略**：内置 Hierarchical Timer Wheel（分层时间轮），O(1) 复杂度维护条目的 `time_to_idle` 和 `time_to_live`。
  - **支持 Eviction Listener（淘汰回调）**：条目因超时（`Expired`）、容量满（`Size`）或主动删除（`Explicit`）被移除时均可触发回调，非常适合用来做全局内存账本扣减与监控统计。
- **使用注意点（空闲时主动释放内存）**：
  - `moka::sync::Cache` 为了避免后台线程开销，默认在日常读写操作时顺带推进时间轮。
  - 若某类 Cache 在一段时间内**完全没有任何读写请求**，只需由全局一个轻量定时任务（如每秒一次）调用 `cache.run_pending_tasks()`，即可主动推进时间轮并将所有超过 TTI 的条目 Drop 释放回内存分配器。

### 3.2 `quick_cache` (`quick_cache::sync::Cache`) 详细分析

- **核心优势**：
  - **S3-FIFO 算法极契合数据库负载**：对全表扫描（Scan Resistance）天然免疫，One-hit wonder 数据在 10% 的 Small 队列中即被快速淘汰；且读命中仅需一次 `AtomicU8` 置位，**读路径零搬迁、零写锁、无异步通道开销**。
  - **严格同步的内存上限**：写入时当场完成超限驱逐，不会在突发大吞吐写入时产生瞬时内存尖峰。
  - **支持运行时动态调整容量**：非常适合多个独立物理实例之间动态借还内存配额。

---

## 4. 各类型 Cache 独立后，如何控制全局总内存使用量？

当把原来的单一 `ObKVGlobalCache` 拆分为按业务类型独立的 `Cache<K1, V1>`、`Cache<K2, V2>` 后，要同时实现 **“各 Cache 独立 + 空闲自动过期释放 + 严格控制所有 Cache 的总内存使用量”**，有以下三种架构方案：

### 方案一：外层独立强类型 `TypedCache<K, V>` + 底层共享引擎（最推荐）

#### 设计思路
对外每个业务模块拥有完全独立的 `TypedCache<K, V>` 类型、独立的接口、独立的统计指标以及独立的 `time_to_idle` 配置；对内通过 Rust 的 `Arc<dyn Any + Send + Sync>` 类型擦除，将条目存储在同一个全局 `moka::sync::Cache` 实例中，由该实例的 `.max_capacity(global_memory_limit)` 统一控制总字节数。

#### 示例代码

```rust
use moka::{policy::Expiry, sync::Cache};
use std::{
    any::Any,
    hash::{Hash, Hasher},
     marker::PhantomData,
    sync::Arc,
    time::{Duration, Instant},
};

/// 内部统一存储的值载体
#[derive(Clone)]
struct ErasedEntry {
    value: Arc<dyn Any + Send + Sync>,
    weight_bytes: u32,
    tti: Duration,
}

/// 按缓存种类支持各自独立的空闲过期时间 (Per-Cache TTI)
struct PerCacheExpiry;

impl<K> Expiry<K, ErasedEntry> for PerCacheExpiry {
    fn expire_after_create(&self, _key: &K, value: &ErasedEntry, _now: Instant) -> Option<Duration> {
        Some(value.tti)
    }

    fn expire_after_read(
        &self,
        _key: &K,
        value: &ErasedEntry,
        _now: Instant,
        _duration_until_expiry: Option<Duration>,
        _last_modified_at: Instant,
    ) -> Option<Duration> {
        // 每次命中后重置空闲倒计时
        Some(value.tti)
    }
}

/// 全局共享引擎（仅负责底层总容量控制与时间轮推进）
pub struct GlobalCacheEngine {
    inner: Cache<(u16, u64), ErasedEntry>,
}

impl GlobalCacheEngine {
    pub fn new(total_memory_limit_bytes: u64) -> Self {
        let inner = Cache::builder()
            .weigher(|_k, v: &ErasedEntry| v.weight_bytes)
            .max_capacity(total_memory_limit_bytes)
            .expire_after(PerCacheExpiry)
            .build();
        Self { inner }
    }

    /// 由定时任务周期性调用（如每 1s），确保无请求时也能主动释放过期内存
    pub fn maintenance(&self) {
        self.inner.run_pending_tasks();
    }
}

/// 对外暴露给各业务模块的独立强类型 Cache
pub struct TypedCache<K, V> {
    namespace_id: u16,
    tti: Duration,
    engine: Arc<GlobalCacheEngine>,
    _marker: PhantomData<(K, V)>,
}
```

#### 方案一优缺点
- **优点**：
  1. **总内存控制 100% 精确**：直接由单一 `max_capacity(total_memory_limit_bytes)` 管住全局上限。
  2. **全局最优冷热淘汰**：当总内存触顶时，W-TinyLFU 自动在整个系统范围内淘汰最冷的数据，不会出现某类 Cache 配额闲置而另一类热点 Cache 频繁淘汰的问题。
  3. **各业务类型完全解耦**：不同模块拥有各自的 `TypedCache<K, V>` 和专属 TTI，无需继承任何 C++ 基类。
- **缺点**：
  - `get` 命中时有一次 `Arc::downcast::<V>()` 的开销（仅一次 `TypeId` 整数比较，纳秒级，几乎可忽略）。

---

### 方案二：物理分离的 `quick_cache` 实例 + 全局自由水池动态配额（Quota Rebalancer）

如果希望连底层的哈希表与锁分片也**完全物理隔离**（每个种类各自持有一个独立的 `quick_cache::sync::Cache<K, Arc<V>>` 实例），可以采用 **“全局 Free Pool + Chunk 动态配额借还”** 机制。

#### 设计思路
1. **全局水池（`GlobalQuotaManager`）**：
   - 设全局总上限为 `GlobalLimit`（例如 `4 GiB`），以 `Chunk = 8 MiB` 或 `16 MiB` 为单位划分配额。
   - 每个独立 `Cache<K, V>` 初始仅分配极小的保底容量（如 `1 个 Chunk`），其余额度全部留在全局 `free_pool_bytes: AtomicU64` 中。
2. **按需扩容（借额度）**：
   - 当某个独立 Cache 写入导致自身使用量逼近当前 `local_capacity` 时，向 `GlobalQuotaManager` 申请 1 个 `Chunk`，成功则扩大自身 `local_capacity`。
3. **TTI 空闲自动释放与归还额度**：
   - 每个独立 Cache 条目记录 `last_accessed_sec: AtomicU32`。
   - 周期性巡检任务（如每秒）清理各独立 Cache 中超过 TTI 未访问的条目；当某个 Cache 的 `local_capacity - current_weight > Chunk` 时，自动缩减其 `local_capacity` 并将空闲额度归还给全局 `free_pool_bytes`。
4. **全局满载时的跨实例抢占（Rebalance）**：
   - 当 `free_pool_bytes == 0` 且某个热点 Cache 仍在高频 Miss 时，协调器根据各实例近期的访问热度/Miss 率，从最冷的 Cache 实例缩减 `1 个 Chunk` 容量（触发其内部 S3-FIFO 同步淘汰），将该额度划拨给热点 Cache。

---

### 方案三：物理分离的 `moka` 实例 + 全局原子账本与淘汰监听

如果希望每个种类使用物理独立的 `moka::sync::Cache<K, Arc<V>>` 实例：

1. **全局原子计数器 `GLOBAL_USED_BYTES: AtomicU64`**：
   - 在每个独立 `moka` 实例插入时累加条目字节数；
   - 在每个实例的 `.eviction_listener(|_k, v, _cause| { GLOBAL_USED_BYTES.fetch_sub(v.weight()); })` 中自动扣减字节数（无论是 TTI 超时过期还是容量淘汰都会触发）。
2. **按压驱逐**：
   - 正常负载下，不活跃的 Cache 实例会因内置 `.time_to_idle(tti)` 自动过期释放并降低 `GLOBAL_USED_BYTES`。
   - 当 `GLOBAL_USED_BYTES > GLOBAL_LIMIT` 时，由全局控容逻辑按各实例占用比例或冷热程度主动摘除冷条目。

---

## 5. 与 C++ 存量特性的对接设计

无论上层业务是纯 Rust 调用还是通过 CXX Bridge 供 C++ 调用，都可以通过以下方式消除原 `ObKVCache` 的缺陷：

1. **用 `Arc<V>` 彻底替代三套 Hazard 延迟回收机制**
   - 淘汰粒度从 **2MB 整块** 细化到 **单条 KV**。
   - C++ 侧的 Handle 只需持有一个包装了 Rust `Arc<V>` 的句柄：Cache 内部因 TTI 超时或容量满驱逐条目时只需 Drop 自己持有的那份 `Arc`；若此时无读者持有 Handle，内存当场释放；若有慢查询正持有 Handle，仅延迟该单个条目的释放，绝不会像以前那样卡住整个 2MB 块。
2. **用 `Weak<V>` 升级替代 `ObPointerSwizzleNode`**
   - 索引扫描器中的 Pointer Swizzling 可以直接保存 `std::sync::Weak<V>`：读取时调用 `Weak::upgrade()`，若条目尚未被驱逐则 O(1) 直接拿到 `Arc<V>`，若已被驱逐则返回 `None` 并自动回退，彻底消除原 `seq_num_` 与 `mb_handle_` 的 ABA 风险。
3. **支持预分配零拷贝构建**
   - 对于需要原地解压的微块，可在 Rust 侧提供带内联数据区的 `ArcBuffer`（预分配指定字节长度的对齐 buffer，初始化完成后冻结为不可变引用插入 Cache），全程零额外拷贝，且若中途解压失败直接 Drop 即可回收，不会在 2MB 块中留下内存空洞。

---

## 6. 选型结论与落地建议

| 维度 | 推荐选择 | 说明 |
| :--- | :--- | :--- |
| **首选基础库** | **`moka` (`moka::sync::Cache`)** | 原生内置分层时间轮 `time_to_idle`（TTI）、字节权重 `weigher`、淘汰回调 `eviction_listener` 与 W-TinyLFU 算法，最直接满足“一段时间不用自动释放 + 容量控制”。 |
| **备选高性能库** | **`quick_cache` (`quick_cache::sync::Cache`)** | 若对微块缓存（`MicroBlockCache`）要求极致读写延迟、零后台通道开销和物理隔离下的动态容量调节，可用 `quick_cache` + 轻量级时间戳巡检。 |
| **全局内存控制方案** | **方案一（外层强类型独立 + 底层统一容量池）** 或 **方案二（物理独立实例 + 动态 Chunk 配额池）** | 方案一实现最简单（~150 行代码）且全局冷热淘汰最优；方案二实现完全物理隔离。 |
| **迁移策略** | **按业务 Cache 种类渐进式独立替换** | 由于新实现各类型完全分开，可优先挑选边界清晰的缓存（如 `ObOptTableStatCache`、`ObBloomFilterCache`、`ObStorageMetaCache`）试点独立的 Rust Cache，最后迁移体量最大的 `ObDataMicroBlockCache`，最终下线 `src/share/cache/`。 |
