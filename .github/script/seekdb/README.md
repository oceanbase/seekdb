# SeekDB CI 脚本（执行下沉版）

本目录被 `.github/workflows/seekdb.yml` 使用，在 self-hosted Runner 上直接执行 Prepare → Compile → Mysqltest(slices) → Collect result，不经过 Farm2。

## 文件说明

| 文件 | 说明 |
|------|------|
| `prepare.sh` | 创建 `SEEKDB_TASK_DIR`，生成 `jobargs.output`、`run_jobs.output` |
| `compile.sh` | 调用 `scripts/farm_compile.sh` 与 `scripts/farm_post_compile.sh`，产出 observer.zst、obproxy.zst 到任务目录 |
| `mysqltest_slice.sh` | 运行单个 mysqltest 分片（由环境变量 `SLICE_IDX`、`SLICES` 指定） |
| `collect_result.sh` | 根据 `fail_cases.output` 生成 `seekdb_result.json` |
| `scripts/frame.sh` | 公共环境（WORKSPACE、SEEKDB_TASK_DIR、SCRIPTS_DIR） |
| `scripts/farm_compile.sh` | 调用仓库 `build.sh` 执行编译 |
| `scripts/farm_post_compile.sh` | 打包 observer/obproxy 为 zst 并拷贝到任务目录 |
| `scripts/mysqltest_for_farm.sh` | 单分片 mysqltest 执行逻辑（可按需替换为 farm-jenkins 同款） |
| `scripts/dep_cache.sh` | 依赖缓存（可选，默认 no-op） |

## 环境变量

- **必选**：`GITHUB_WORKSPACE`、`GITHUB_RUN_ID`、`SEEKDB_TASK_DIR`
- **Prepare**：`MYSQLTEST_SLICES`（默认 4）
- **Compile**：`RELEASE_MODE`、`FORWARDING_HOST`（可选）
- **Mysqltest**：`SLICE_IDX`、`SLICES`、`BRANCH`、`FORWARDING_HOST`（可选）

## 可选变量（仓库 / Actions 配置）

- `FARM2_WORKER_IMAGE`：若设置，Compile 与 Mysqltest 步骤在容器内执行，使用该镜像。
- `RELEASE_MODE`：非空时按 release 模式编译。
- `FORWARDING_HOST`：写 `/etc/hosts` 将 mirrors.oceanbase.com 解析到该主机（代理场景）。

## 与 farm-jenkins 的对应关系

本目录下 `scripts/` 中的脚本为本地占位实现；若需与 Jenkins/Farm2 行为完全一致，可从 farm-jenkins 仓库复制同名脚本（如 `farm_compile.sh`、`farm_post_compile.sh`、`mysqltest_for_farm.sh`）到 `scripts/` 覆盖即可，无需 clone。
