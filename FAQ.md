# 决赛 FAQ

## 开发机连接方式

选手需要通过跳板机中转来连接开发机。连接命令如下：

```bash
ssh -J jumpuser@47.243.211.179 root@xxx.xxx.xxx.xxx
```

**命令说明：**

- `-J jumpuser@47.243.211.179`：通过跳板机中转，跳板机地址为 `47.243.211.179`，用户名为 `jumpuser`
- `root@xxx.xxx.xxx.xxx`：目标开发机的用户名和 IP 地址（请替换为实际 IP）

**注意事项：**

- 跳板机的 `jumpuser` 是一个低权限用户，无法直接登录，仅用于 SSH 跳转中转

## 比赛提供的 Api Key 对应的 BaseURL 是什么?

http://47.243.159.229:8081/v1 这个，.env 示例：

```bash
# LLM Config
OPENAI_API_KEY="xxx" # your api-key here
OPENAI_BASE_URL="http://47.243.159.229:8081/v1"
```

## 比赛提供的 Api Key 有限流吗，能否无限制使用?

代理平台 Api Key 未开启限流，仅有百炼测限流。但用量存在监控，请合理的使用，且用于大赛。若存在滥用行为，可能将取消 Api Key 使用权。

## 决赛一天能提交几次呢?

每个赛道各 10 次。
