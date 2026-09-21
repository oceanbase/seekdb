# Server-dev：链接宿主绑定与加载准入

状态：宿主版本绑定准入、C++ CMake profile 和 Rust 原生 Server-dev
构建/打包入口已接入。独立分发的完整 Server-dev SDK、通用深层 adapter
以及完整 planner/index API 尚未完成。

## 为什么不只比较产品版本

深层插件依赖内核构建产生的类型与实现，不能仅凭相同产品版本号就假定
兼容。本轮为这种插件增加显式 manifest 契约：桥接协议版本与目标宿主的
GNU build ID。现有 CMake 与 Bazel 宿主链接配置均使用 `--build-id=sha1`；
实际加载时读取正在运行的进程，不接受插件指定另一个可执行文件作为宿主。

build ID 标识链接产物，不是完整文件校验和：链接后的文件变化不一定改变
该字段。它不能替代签名、完整性校验、管理员信任或正确的 SDK/header
来源校验。不要将相等的 build ID 当作任意 C++ 类布局兼容性的数学证明。
[GNU ld build-ID 说明](https://sourceware.org/binutils/docs/ld.html#Options)

## 交付与加载契约

公共插件继续使用原有 manifest，无需附加信息，也不触发宿主标识读取。
底层版本绑定 ABI 如下。C++ CMake profile 使用生成 wrapper；Rust
使用 SDK `server_dev::bind` 与构建生成的宿主 ID，均无需手填绑定字段：

1. 使用 `include/seekdb/plugin/server_dev.h` 的
   `seekdb_plugin_server_dev_manifest_v1_t`，保留不变的 v1 前缀。
2. 设置 manifest-only 的 `SEEKDB_PLUGIN_CAPABILITY_SERVER_DEV`。
   该位不用于 service、requirement 或 implementation reference。
3. 前缀 `struct_size` 写整个扩展结构的大小；填写 bridge version、
   host build ID 字节及长度，未使用字节和 reserved 均为零。
4. 导出原有 `seekdb_plugin_entry_v1` 并返回 v1 前缀地址，沿用相同
   loader、生命周期、registry、catalog 与资源协议。

旧宿主会拒绝未知 capability，而不是忽略后缀后直接调用新插件。
新宿主检查完整后缀、版本、长度、尾部字节和宿主标识，失败不进入
`init/start`，也不发布 SQL 对象或服务。加载拒绝仍通过现有 activation
abort 路径返回；这不等于新的事务安装机制。

注意时机：这些检查发生在 OS 装载动态库、调用 entry 之后。动态库
constructor 已可能执行，因此本机制不是不可信 native 代码的沙箱。

## Rust 实现与工具

`rust/plugin-runtime/src/build_contract.rs` 读取 ELF64 little-endian 的
program header 和 PT_NOTE，不依赖 section table、外部 readelf 进程
或全文件哈希。最多检查 1024 个 program header、累计 1 MiB note；
范围溢出、截断、缺少/重复 build ID、过长 ID 均拒绝。

加载路径读取 `/proc/self/exe`，成功结果缓存，I/O 失败不缓存，后续可
重试。首个 Server-dev 激活才读取宿主；公共插件不承担这次 I/O。
当前只支持 Linux ELF64 little-endian；其他平台或缺少有效 note 的宿主
拒绝 Server-dev，公共插件路径保持原契约。尚未测量首次读取延迟与 RSS。

开发工具根据指定的最终宿主二进制生成可检查的 C 头文件：

```sh
cargo run --offline --manifest-path rust/Cargo.toml \
  -p seekdb-plugin-runtime --example server_dev_contract -- \
  build_plugin_overlay_verify/src/observer/seekdb /tmp/server_dev_contract.h
```

输出路径必须不存在。工具拒绝覆盖已有文件，包含与输入相同的路径。
生成文件提供 `SEEKDB_SERVER_DEV_HOST_BUILD_ID_SIZE` 和
`SEEKDB_SERVER_DEV_HOST_BUILD_ID_BYTES`，供 manifest 初始化使用。
从另一次构建或另一个测试 executable 生成的契约不能当作当前宿主契约。
工具也接受 `-` 输出到 stdout，供 CMake 在读取成功后写入其管理的生成
目录。后续新增 `--rust` 输出 Rust 常量源码，供下文原生 Rust 流程使用。

## 同源码树 CMake profile

插件根目录 `plugin.toml` 显式设置 `api_profile = "server-dev"`，并声明
`server_headers` 与额外 `exports`。Public 插件继续使用默认规则。
当前支持 Linux、C++ sources/adapters，显式 TOML profile 需要 Python
3.11+。文件名、头文件路径和导出名按字面值校验，拒绝路径逃逸、缺失或
歧义头文件、重复声明、通配符与保留入口名。

生产工程先配置 plugins/，随后才定义最终宿主。因此 profile 在配置
结束时获取完成定义的 `ob_sql` 编译上下文，包含其传递 include、宏和
选项；没有把 ob_sql 加入插件 LINK_LIBRARIES。编译属性的传递求值使用
[CMake TARGET_PROPERTY 规则](https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html#target-properties)。
仍禁止复制核心静态库及核心反向依赖具体插件；最终配置检查继续拒绝
额外私有 source/include、未授权编译选项和核心链接。

构建先完成 seekdb 链接，再调用 Rust 工具生成绑定该 artifact 的头文件。
原始入口重命名为内部实现，生成 wrapper 复制普通 manifest 并加上后缀；
已经携带自定义后缀的 manifest 被拒绝，避免静默截断。生成的导出表隐藏
原始入口和其他未声明符号，二进制审计接受清单中的额外 C 导出。
仍保留 `-z defs`，并未开放任意宿主符号解析。非 inline 内核调用仍需
显式宿主接口或 adapter。

[server_dev_reference](../../../plugins/server_dev_reference/README.md) 已在
生产编译配置下使用真实 optimizer 头文件构建。它只是 inline 类型与
额外导出的接入示例，不是自定义 planner 实现，也不证明对象持久化、
查询事务或独立 SDK 兼容性。依赖策略不是不可信 native 插件沙箱。

## 证据与未完成部分

Rust 单测读取真实测试进程，检查相同/改变后的标识；受控 ELF 样例验证
无 section table 的读取、每个截断位置、边界溢出、错误 header 与重复
note。C++/Rust 跨语言测试针对实际 loader executable 生成契约，随后
编译七个 DSO：匹配，以及错误标识、错误桥接版本、缺失后缀、保留字段、
非零尾部、零长度。拒绝样例的 init 会 abort，保证测试不能把“进入 init
后失败”误认为准入成功。catalog/verifier 仍是明确的测试替身，不是签名
验证或实库事务证据。

本轮生产构建、完整 kernel 回归、118 项 Rust runtime 单测和最终
25 项 CTest 均通过。首轮脚手架测试曾遇到链接器 SIGSEGV，未更改其代码
或链接参数，单独与完整重跑通过；该记录不代表已修复链接器问题。
另实际安装 plugin-sdk，使用安装后的头文件编译生产宿主绑定 DSO，
确认另一个测试 executable 在 init 前拒绝它；生成的生产契约与 readelf
读取结果一致。12 项 build gate、源码边界、格式和 diff 检查通过。

后续 CMake profile 验证新增了 6 项声明/源码单测与一个实际编译、加载的
集成测试。后者使用受控的传递编译上下文与真实 loader：匹配宿主加载
成功，更换绑定 executable 后拒绝；未声明导出隐藏，额外私有编译/链接
修改拒绝。27 项完整 CTest 与 118 项 Rust runtime 单测通过。生产
Server-dev 示例编译及二进制审计通过，在另一个 loader 宿主下被拒绝。
完整 SQL kernel 回归、12 项 build gate、源码边界、Rust 格式与 diff
检查也通过。早期构建暴露的目标定义顺序与遗漏传递编译上下文已修正，
最终结果来自修正后的实际构建，不以配置成功代替编译证据。

本轮没有放开任意 server headers、导出所有进程符号，或提供裸 planner
指针。公共插件边界保持原规则。后续必须提供匹配构建的独立 SDK、
Rust adapter 与真实
candidate path/计划构造/执行/EXPLAIN，结合
[hook 模式与结果校验](plugin-hook-modes.md) 才能兑现深层扩展自由度。

GIS 无需改写为 Rust；它继续按当前公共 C ABI 使用 C++ 实现。完整目标
仍见[实施进度](plugin-implementation-status.md)。

后续已接入 [Rust 候选选择](plugin-candidate-selection.md)：Server-dev
代码可以在真实 planner 候选集合中选择结果，Public optimizer v1 仍是
around-only。进一步开放宿主 materialization 候选构造，任意自定义
算子、索引与匹配构建的完整 SDK 交付仍未完成。

## Rust 原生构建与打包入口

`seekdb_add_rust_plugin` 现在读取与 C++ 共用的 `api_profile/exports`
声明。Server-dev 与 Public 仍使用同一个 Rust SDK、loader 和包配方。
不引入插件对 host runtime crate 或核心静态库的依赖。

同源码树插件绑定最终 `seekdb` target；独立项目使用 `STANDALONE` 和
显式 `SERVER_DEV_HOST` 绝对路径。构建器在 Cargo 编译前调用相同 Rust
ELF 工具，生成 `contract.rs`；SDK `server_dev::bind` 添加原 manifest
的版本绑定后缀，原来的 entry 返回其 v1 地址。C/Rust 布局测试覆盖该
后缀的大小、对齐和全部字段偏移，并核对 manifest-only capability。

宿主契约每次构建都检查，内容未变不更新时间戳；因此既避免同一宿主
反复重编译，也不因切换到时间戳更早的 ELF 而保留旧绑定。非法宿主在
Cargo/复制前失败。cargo-seekdb 复用构建器生成的 package recipe，
不推测产物名、不另建安装器，也不覆盖已存在的包目录。

Rust 通过 C bridge 使用深层能力，本阶段 `server_headers` 必须为空，
需要 C++ class 的 adapter 仍单独受管。额外导出按字面清单审计；不放开
任意宿主符号解析。Public 中声明 Server-dev 宿主参数直接拒绝。

纯 Rust 示例见 [rust_candidate](../../../plugins/rust_candidate/README.md)。
它通过 SDK 注册构造 hook 并选择新增的 MATERIAL，明确是演示而非生产
代价策略。独立源码工程已验证构建/包复制/实际加载、候选回调、换宿主
重编译和拒绝、非法宿主失败、导出清单与无覆盖规则。其依赖仍是匹配的
源码 SDK，不宣称完整 Server-dev SDK 已作为独立产品发布。
