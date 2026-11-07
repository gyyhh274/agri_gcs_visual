#!/usr/bin/env python3
"""点云桥看门狗：基于桥自身的统计日志判断死活，不占用任何网络带宽。

判定依据（读 /tmp/pointcloud_bridge.log 的 "bridge stats:" 行）：
  - 15 秒内没有新的统计行  → 采集线程停了，重启
  - 有新行但 tx_cloud=+0 连续 3 次 → 收到了点云却发不出去，重启

为什么不用 TCP 客户端探测：桥每秒产出几 MB，探测端读不过来会被当作慢客户端
踢掉，从而把健康进程误判为卡死。
"""
import os
import re
import signal
import subprocess
import sys
import time

BRIDGE = '/home/nv/.local/lib/agri-gcs/pointcloud_bridge.py'
LOG = '/home/nv/.local/state/agri-gcs/bridge-watchdog.log'
BRIDGE_LOG = '/tmp/pointcloud_bridge.log'
ARGS = ['--bind', '127.0.0.1', '--port', '37262', '--cloud-source', 'body']
STALL_SECONDS = 15.0
TICK = 2.0
STATS_RE = re.compile(r'bridge stats: rx_cloud=\+(\d+) rx_pose=\+(\d+) tx_cloud=\+(\d+)')


def log(message):
    line = '[%s] %s' % (time.strftime('%Y-%m-%d %H:%M:%S'), message)
    print(line, flush=True)
    try:
        os.makedirs(os.path.dirname(LOG), exist_ok=True)
        with open(LOG, 'a', encoding='utf-8') as handle:
            handle.write(line + '\n')
    except OSError:
        pass


def bridge_pid():
    try:
        result = subprocess.run(['pgrep', '-f', 'pointcloud_bridge.py'], stdout=subprocess.PIPE,
                                stderr=subprocess.DEVNULL, text=True, timeout=5.)
    except (OSError, subprocess.SubprocessError):
        return None
    pids = [int(item) for item in result.stdout.split() if item.isdigit()]
    return pids[0] if pids else None


def start_bridge():
    log_file = open(BRIDGE_LOG, 'ab', buffering=0)
    subprocess.Popen(['setsid', sys.executable, '-u', BRIDGE] + ARGS,
                     stdin=subprocess.DEVNULL, stdout=log_file, stderr=log_file, close_fds=True)
    log('桥已启动')


def last_stats_line():
    """返回日志里最后一条统计行的 (行文本, 该行出现后的文件偏移)。文件不存在返回 (None, 0)。"""
    try:
        with open(BRIDGE_LOG, 'rb') as handle:
            handle.seek(0, os.SEEK_END)
            size = handle.tell()
            handle.seek(max(0, size - 65536))
            tail = handle.read().decode('utf-8', 'replace')
    except OSError:
        return None, 0
    for line in reversed(tail.splitlines()):
        if 'bridge stats:' in line:
            return line, size
    return None, size


def main():
    log('看门狗启动（%d 秒无统计行判定卡死，不占用网络）' % STALL_SECONDS)
    seen_size = 0
    last_change = time.monotonic()
    zero_tx = 0
    while True:
        time.sleep(TICK)
        if bridge_pid() is None:
            log('桥进程不存在，重新拉起')
            start_bridge()
            time.sleep(10)
            seen_size = 0
            last_change = time.monotonic()
            zero_tx = 0
            continue
        line, size = last_stats_line()
        if line is None:
            # 还没有统计行（刚启动），给足启动时间
            if time.monotonic() - last_change > STALL_SECONDS:
                log('启动后 %d 秒仍无统计行，重启' % STALL_SECONDS)
                restart('无统计输出')
                last_change = time.monotonic()
            continue
        if size != seen_size:
            seen_size = size
            last_change = time.monotonic()
            match = STATS_RE.search(line)
            if match and int(match.group(3)) == 0 and int(match.group(1)) > 0:
                zero_tx += 1
            else:
                zero_tx = 0
            if zero_tx >= 3:
                log('连续 %d 次收到点云但没有发出，判定发送侧卡死' % zero_tx)
                restart('发送侧卡死')
                last_change = time.monotonic()
                zero_tx = 0
            continue
        if time.monotonic() - last_change > STALL_SECONDS:
            log('超过 %d 秒没有新的统计行，判定采集线程停了' % STALL_SECONDS)
            restart('无统计输出')
            last_change = time.monotonic()
            zero_tx = 0


def restart(reason):
    pid = bridge_pid()
    log('判定卡死（%s），pid=%s' % (reason, pid))
    if pid is not None:
        try:
            os.kill(pid, signal.SIGUSR1)          # 线程栈写入桥日志
            log('已发送 SIGUSR1 抓取线程栈')
        except OSError as error:
            log('SIGUSR1 失败：%s' % error)
        time.sleep(2)
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass
        time.sleep(2)
        remaining = bridge_pid()
        if remaining is not None:
            try:
                os.kill(remaining, signal.SIGKILL)
            except OSError:
                pass
    start_bridge()
    time.sleep(10)


if __name__ == '__main__':
    main()
