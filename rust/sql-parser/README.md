# Rust SQL 解析器

使用 `peg 0.8.6`，输入为 UTF-8。支持 ANSI_QUOTES、NO_BACKSLASH_ESCAPES、
PIPES_AS_CONCAT、HIGH_NOT_PRECEDENCE。

`src/grammar.rs` 定义 SQL、PL、表达式和统计选项语法。Rust 拥有语法树、源码范围、
占位符编号、语句拆分、参数化规则、批量 SQL 改写和全文检索语法。
`src/lib.rs` 导出 Rust 接口，`src/bridge.rs` 提供 CXX 接口；不依赖 C++ 引擎或回调。

C++ 直接读取 Rust 持有的语法树，负责会话信息、名称绑定、引擎值构造和计划缓存。
PL 名称解析后的绑定作为数据传回 Rust 完成 SQL 改写。
缓存命中只运行快速扫描，不完整解析；完整解析失败直接报错。

```sh
./build.py build -j32 //src/observer:seekdb
```

TODO：对照历史 Bison/Flex 规则审计语法覆盖，并补充组合差分验证。
