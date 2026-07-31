# 使用 Bazel 编译和运行单元测试

seekdb 的 C++ 单元测试由 Bazel 统一管理。`unittest` 下的每个测试都归属
一个生产模块；每个模块生成一个测试二进制，并由 Bazel 对其中的 GTest
用例分片并行执行。

运行全部单元测试：

```bash
./bazel.py test //unittest/...
```

运行一个模块，例如 OBLib：

```bash
./bazel.py test //unittest/oblib:oblib_tests
```

只运行匹配的 GTest 用例：

```bash
./bazel.py test //unittest/sql:sql_tests \
  --test_arg='--gtest_filter=TestName.CaseName'
```

远程编译可照常传递 Bazel 参数：

```bash
./bazel.py test //unittest/... \
  --jobs=80 \
  --remote-executor="${REMOTE_EXECUTOR}"
```

`unittest/run_tests.sh` 和 `unittest/oblib/run_tests.sh` 分别是全部测试和
OBLib 测试的便捷入口；它们最终仍调用根目录的 `bazel.py`。

## 测试构建模型

- 每个测试源码只能归属一个模块，归属关系集中定义在
  `bazel/architecture/module_policy.bzl`。
- 每个模块只有一个测试入口 `unittest/all_tests_main.cpp`。测试源码不得
  定义自己的 `main()`。
- 模块测试只通过该模块的 `*_unit_test_interface` 访问生产头文件；
  `liboceanbase.so` 只提供生产实现符号，不扩大源码可见范围。
- 测试源码按 `unity_size` 合并编译。确实无法参加 Unity 的源码写入
  `unity_exceptions`，并必须说明原因；它仍进入同一个模块测试二进制，
  不会生成独立测试程序。
- `shard_count` 控制同一模块测试二进制的 GTest 分片数，分片可并行执行。

## 新增或修改测试

1. 将测试放入对应的 `unittest/<module>` 目录，不要新增 `main()`。
2. 测试只应验证本模块。直接访问其他业务模块属于模块/集成测试，应改用
   本模块的接口、fixture 或 fake，或者移出 C++ 单元测试套件。
3. 将运行时数据显式加入模块 `BUILD.bazel` 的 `data`。
4. 优先让源码参加普通 Unity 分组；只有已证实的宏污染或翻译单元符号冲突
   才能加入带原因的 `unity_exceptions`。
5. 性能、压力和 benchmark 程序不属于默认单元测试套件。

中央门禁会检查测试归属、跨模块依赖、遗留 `main()`、未纳入构建的测试
源码以及性能/压力测试命名。修改架构关系时，应审查中央策略文件，而不是
在单个 `BUILD.bazel` 中绕过约束。
