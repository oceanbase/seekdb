# Windows 上快速启动 seekdb

整理自官方 Docker 镜像说明与本地实践，供部署类实践参考。

## 环境要求

- Docker Desktop（Windows 需启用 WSL2 或 Hyper-V）
- 至少 1 核 CPU、2 GB 内存
- Windows 上必须使用 Docker volume 挂载数据目录，不能使用主机目录挂载

## 启动实例

```bash
docker volume create seekdb
docker run -d -p 2881:2881 -p 2886:2886 -v seekdb:/var/lib/oceanbase --name seekdb oceanbase/seekdb
```

启动后可用 MySQL 兼容客户端连接：

```bash
mysql -h 127.0.0.1 -P 2881 -u root -p
```

管理页面为 `http://127.0.0.1:2886`。

## 查看部署 UUID

实例运行目录下的 `run/telemetry.json` 中 `content.id` 是 36 位 UUID。容器内可先定位该文件：

```bash
docker exec -it seekdb bash -c "find / -name telemetry.json 2>/dev/null"
```

遥测默认开启，无需额外配置；上报数据存在同步延迟，查询不到时可稍后重试。

## 停止与清理

```bash
docker stop seekdb && docker rm seekdb
```

数据仍保留在 `seekdb` volume 中。
