#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
在 GitHub Actions 中用于「等待 SeekDB Farm2 结果」的脚本。
配合 seekdb_robot_for_ob_for_retry.py：robot 发现 workflow 里 seekdb3 步骤 in_progress 后会发 Farm2 任务，
任务结束后把结果写到 OSS；本脚本轮询该 OSS 结果文件，读到后根据 status 退出并写 GitHub output。

用法（在 workflow 的 job seekdb3、step 名以 "action by seekdb3" 开头）:
  export GITHUB_RUN_ID=123456789
  export GITHUB_RUN_ATTEMPT=1
  python3 seekdb_wait_result.py [--timeout 14400] [--poll-interval 15]

结果文件 URL: https://obfarm-ce.oss-cn-hongkong.aliyuncs.com/farm/seekdb_results/{run_id}-{attempt}.json
status: 4=success, 其他为失败。
"""

import os
import sys
import json
import time
import argparse

try:
    import requests
except ImportError:
    print("需要 requests: pip install requests", file=sys.stderr)
    sys.exit(2)

# 与 seekdb_robot 里 RESULT_FILE_KEY 及 bucket 一致
OSS_RESULT_BASE = "https://obfarm-ce.oss-cn-hongkong.aliyuncs.com/farm/seekdb_results"
# Farm2 TaskStatusEnum.success = 4
STATUS_SUCCESS = 4


def main():
    parser = argparse.ArgumentParser(description="轮询 OSS 上的 SeekDB Farm2 结果，用于 GitHub Actions 回调")
    parser.add_argument("--timeout", type=int, default=24 * 3600, help="轮询超时秒数，默认 24h")
    parser.add_argument("--poll-interval", type=int, default=15, help="轮询间隔秒数")
    parser.add_argument("--run-id", default=None, help="GitHub run id，默认用环境变量 GITHUB_RUN_ID")
    parser.add_argument("--attempt", default=None, help="run attempt，默认用环境变量 GITHUB_RUN_ATTEMPT 或 1")
    args = parser.parse_args()

    run_id = args.run_id or os.environ.get("GITHUB_RUN_ID")
    attempt = args.attempt or os.environ.get("GITHUB_RUN_ATTEMPT", "1")
    if not run_id:
        print("[ERROR] 需要 GITHUB_RUN_ID 或 --run-id", file=sys.stderr)
        sys.exit(2)

    result_key = "{}-{}.json".format(run_id, attempt)
    url = "{}/{}".format(OSS_RESULT_BASE.rstrip("/"), result_key)
    end_time = time.time() + args.timeout

    print("[INFO] 等待 Farm2 结果: {}".format(url))
    while time.time() <= end_time:
        try:
            r = requests.get(url, timeout=30)
            if r.status_code == 200:
                data = r.json()
                status = data.get("status")
                task_id = data.get("task_id", "")
                _write_github_output(task_id=task_id, status=status, success=(status == STATUS_SUCCESS))
                if status == STATUS_SUCCESS:
                    print("[INFO] SeekDB Farm2 任务成功, task_id={}".format(task_id))
                    sys.exit(0)
                print("[INFO] SeekDB Farm2 任务未通过, status={}, task_id={}".format(status, task_id))
                sys.exit(1)
        except requests.RequestException:
            pass
        except json.JSONDecodeError:
            pass
        time.sleep(args.poll_interval)

    _write_github_output(task_id="", status="", success=False)
    print("[ERROR] 等待 Farm2 结果超时", file=sys.stderr)
    sys.exit(1)


def _write_github_output(task_id="", status="", success=False):
    out = os.environ.get("GITHUB_OUTPUT")
    if not out:
        return
    with open(out, "a") as f:
        f.write("task_id={}\n".format(task_id))
        f.write("status={}\n".format(status))
        f.write("success={}\n".format("true" if success else "false"))


if __name__ == "__main__":
    main()
