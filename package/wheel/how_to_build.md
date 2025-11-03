# 嵌入式Python介绍

## 简介
我们需要构建出Python的Wheel包，并上传到pypi，用户才可以使用pip install使用。

因为这个包底层是一个动态库，因此它依赖运行平台，包括CPU架构(x86\_64, aarch64)、操作系统(Windows, Linux, MacOS等)。另外由于Python的小版本之间不兼容，因为我们需要为Python 3.8 ~ 3.14 都需要进行构建并发布。特别的，对于python 3.13和3.14版本，还有无GIL版本，也需要发布。

> NOTE: Python文档说他们有稳健的API，如果只使用这些API，也可以不按照小版本来发包。

由于pypi限制了单个包最大100M，而我们的动态库有540M，压缩后160M，一个包无法容纳，因此按照二进制拆分成两个包，一个seekdb，一个是seekdb-lib。
在用户安装后，执行import seekdb时，会把这两个包的动态库分片，合并到一起放到$HOME/.seekdb/cache下。

## 如何构建

Python 提供了构建容器`quay.io/pypa/manylinux_2_28`，我们必须在这个容器中打包。

1. 复制 build\_python.sh 到项目目录
```bash
cp build_python.sh $PROJECT_HOME
```

2. 构建不同版本python版本的包
```bash
PYTHON_HOME=/opt/python/cp38-cp38 bash build_python.sh
```
构建结果在 `$PROJECT_HOME/build_python/wheelhouse`

> 构建时使用了ccache缓存编译结果，因此每次重新构建速度也还行。

构建所有python3版本的包
```bash
PACKAGE_VERSION=0.0.1.dev4
for python_home in /opt/python/cp3*/; do
    echo "start to build with python_home=$python_home"
    PACKAGE_VERSION=$PACKAGE_VERSION PYTHON_HOME=$python_home bash package/wheel/build_python.sh
    echo "$python_home build done"
done
```

## 发布到pypi
发布前测试一下
```bash
python3 -m twine check wheelhouse/seekdb*

# 本地安装测试
python3 -m pip install wheelhouse/seekdb-0.0.1.dev2-xxxx wheelhouse/seekdb_lib-0.0.1.dev2-xxxx
python3 $PROJECT_HOME/src/observer/embed/python/package/seekdb_test.py
```

> NOTE: 同样的包名，不管是测试环境还是正式环境，都只能上传一次。

```bash
# 先发布到测试环境
python3 -m twine upload --repository testpypi wheelhouse/seekdb*
# 检查测试没有问题，再发布到正式环境

python3 -m twine upload wheelhouse/seekdb*
```

发布到正式环境后，过一段时间才会同步到国内的一些源。


## TIPS
1. uv 虚拟环境安装包需要增加uv前缀，比如
```
uv pip install build wheel
```

2. manylinux镜像
这是官方提供的打包镜像。我们在本地打的包是带linux\_x86\_64标签的，这个标签不允许上传，需要使用auditwheel修复后上传，它会自动修改标签。
这个镜像中还提供了多个版本的Python。

镜像名称中的 `2_28` 是glibc的版本，这个版本比较老，打出来的包，会适用于更多的操作系统。

3. 安装 twine
```bash
pip install twine
```

4. 境外机器构建
因为构建会依赖一些东西，比如docker、Python包、YUM包等，使用外网构建会拉这些东西会快一点。
在境外机器构建需要把build\_python.sh中的INDEX_URL注释掉，否则会从境内的链接拉取数据。

如果要从gitlab上的代码构建的话，还需要把代码、deps/3rd提前下载后复制过去。manylinux_2_28_aarch64镜像使用的deps是 oceanbase.el8.aarch64.deps。
