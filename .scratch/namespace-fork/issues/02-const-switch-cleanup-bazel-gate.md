# 02: Phase 0b：清理恒真开关残留 + bazel 门禁雏形

**What to build:** 机械删除 93 处恒真开关残留三元（审计 §8.2）；新建 `src/namespace/` 骨架目录与 BUILD.bazel，落地 visibility + layering_check 规则，编译期强制 namespace → observer → sql → storage 单向依赖。

**Blocked by:** None (can start immediately, parallel with 01)

**Status:** done

- [x] 恒真开关删除（开关定义/声明+全部调用点），行为不变（已确认删除前均 return true）
- [x] bazel 门禁：src/namespace visibility 白名单（仅 observer/sql）+ layering_check；visible() 查询验证 storage/rootserver 不可见
- [x] 四 prototype 套件 PASS（bootstrap / sql full / direct full / direct tls）
