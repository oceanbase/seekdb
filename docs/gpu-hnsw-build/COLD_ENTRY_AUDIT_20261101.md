# 2026-11-01 最小镜像冷入口修复与完整 TGZ 验收

本轮完整编译和打包源为 `4855acac7314df7d43f405b9c73c04ee4d40d562`。候选修复了缺少 RPM 工具时无法初始化依赖的问题；全新镜像上的完整 GPU 编译、真实 CPack、解包 SQL/DML 和隐藏 GPU 重启全部通过。后续发布提交仅补充本文及原始证据，不表示又完整重编译了另一个版本。

## 修复与环境

镜像为 Ubuntu 24.04、Linux x86-64、gfx1100、ROCm 7.2.4，hipVS 位于 /opt/hipvs，16核和55GiB内存配额。镜像有系统 libarchive，没有 rpm2cpio/cpio；独立 Rust/Cargo 为1.97.1，无rustup，而源码精确固定1.98.1。

`deps/init/dep_create.sh` 优先保留原 RPM 工具路径；两者不齐时调用新 `extract_rpm.py`，通过 Python 标准库 ctypes 使用系统 libarchive 私有解包。保持模式、时间和链接，拒绝路径穿越、特殊设备及链接越界写入，不安装系统包。原工具管道增加pipefail。依赖清单、Rust pin、C++、GPU 和 CMake 源码未改变。

29个真实RPM经网络下载并全部成功解包。清单MD5为 `6cbf427016d9c5328b3f45ad0bb925b5`；系统RPM工具始终不存在。28项Linux入口/工具链/部署/解包测试全部通过，没有跳过。

## 构建、冷安装和真实入口

全新TGZ配置只传课程的 `OB_BUILD_CUVS=ON` 启用GPU，随后完整 `make -j4 seekdb`、admin生成和 `make -j2` 全部成功。9,485个跟踪文件SHA256在构建和验收后均不变，主程序版本内嵌4855acac7。完整构建使用已校验官方Rust1.98.1 archive和123个逐文件校验的锁定crate vendor离线编译。

另外从默认镜像PATH及全空私有Cargo/Rustup目录运行原helper，完全通过官方网络下载工具链。网络很慢，验收包装器在3600秒超时，但原rustup子进程继续完成并正常退出0；共3682.31秒。没有重新启动安装器，也没有预置下载。随后核对新下载的rustc/cargo与完整构建工具链逐字节一致，原helper的仅复用版本检查返回0。原超时回执和后续完成回执分别保留，不能称为首次包装器在一小时内通过。

真实 `.github/script/seekdb/compile.sh` 在独立相同源码副本上，通过TGZ及默认Release两个配置的初始化、GPU宏和worker/bridge生成目标检查；刻意在主C++ make处以42停止，不重复完整编译。详细缓存条件、编译器哈希、完整日志和原始驱动见[证据目录](results/radeon-20261101-cold-entry/)。初始驱动错误要求CMakeCache的课程别名类型必须为BOOL，已保留失败并修正为检查真实宏和生成目标；产品源码未因此修改。

## 实际安装包和运行结果

实际CPack生成server/libs两个TGZ，解包检查主程序、obshell、worker、bridge、libaio、配置和systemd文件，ldd无缺库。启动解包程序，不设置LD_LIBRARY_PATH/LD_PRELOAD、不运行系统安装钩子；显式INFO日志保留GPU成功事件。

27项SQL检查全部通过。50,000×128 float32、L2、M=32、ef_construction=200、ef_search=1000，100条独立查询，CPU/GPU各3轮。计时从CREATE VECTOR INDEX到同一首次可用性查询完成，包含扫描、传输、worker、GPU同步、图转换、导入、持久化和必要的首次加载。

| 项目 | CPU/VSAG | GPU/hipVS |
|---|---:|---:|
| 构建中位数（秒） | 24.943233504 | 2.994012695 |
| 三轮Recall@10 | 0.996/0.996/0.996 | 1.000/0.999/1.000 |

中位数比为 **8.331037990倍**。更新、删除、重插、过滤均通过，过滤Recall@10=1.000。以 `HIP_VISIBLE_DEVICES=-1` 重启同一数据库后100条查询均通过，Recall@10=1.000，精确SQL交叉验证通过；最终服务已停止。

三轮均捕获 `gpu=true fallback=false`。重启轮转后的原始日志保留第3轮和后续DML重建；前两轮成功事件已在基准JSON中捕获，对应整份旧日志不再存在。该留存限制在gpu-log-proof.json记录，没有为补日志重复计时。

## 版本和正式成绩边界

主程序SHA256：`28793cd4fb6a17d90f77061c5bebc7e757aee0717f3aa24ee69f7dd760a9fad2`。两个包的哈希、大小、9,485文件清单、原始SQL结果及驱动均见证据目录。后续文档提交不改变编译的产品代码。

官方任务47237054、提交3c1920e09在34秒后失败，正式0/20，只有failMsg=8、空错误文字、空用例和空详情。公开记录页直接显示errorMessage，没有提供该数值的解释；不能宣称已证实官方根因。平台保存的地址在内存中完成实际Git对象拉取与build.sh读取，成功，明文凭据未打印或保存；不能由此推断官方机器网络状态。

本轮是本地真机验收，不能预报正式20分或所有规模均加速。完整构建依赖已校验缓存，独立冷安装约61分钟；官方机器的网络和时间上限仍可能不同。RPM/DEB安装钩子和Bazel GPU构建未重新验证。
