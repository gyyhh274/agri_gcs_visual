#!/usr/bin/env bash
# 在机载电脑（Jetson）上安装/更新云台控制中转服务
#
# 用法： sudo bash install_relay.sh
#
# 注意：/usr/local/bin 属 root，必须用 sudo，否则会静默失败、
#       服务重启后跑的还是旧版本（这个坑踩过一次，见 GIMBAL_DEV_PROCESS.md §12.4）。
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
TARGET=/usr/local/bin/siyi_gimbal_relay.py
UNIT=/etc/systemd/system/siyi-gimbal-relay.service

if [ "$(id -u)" -ne 0 ]; then
  echo "请用 sudo 运行： sudo bash $0" >&2
  exit 1
fi

echo "=== 1) 安装脚本 ==="
install -m 755 "$HERE/siyi_gimbal_relay.py" "$TARGET"
echo "  $TARGET ($(stat -c%s "$TARGET") 字节)"

echo "=== 2) 安装 systemd 单元 ==="
install -m 644 "$HERE/siyi-gimbal-relay.service" "$UNIT"

echo "=== 3) 重启服务 ==="
systemctl daemon-reload
systemctl enable --now siyi-gimbal-relay
systemctl restart siyi-gimbal-relay
sleep 2

echo "=== 4) 自检（别只看 active，要确认跑的是新版本）==="
RC=0
systemctl is-active --quiet siyi-gimbal-relay \
  && echo "  ✔ 服务在跑" || { echo "  ✘ 服务未运行"; RC=1; }

N=$(grep -c '死手保护' "$TARGET" || true)
if [ "$N" -ge 1 ]; then
  echo "  ✔ 版本特征检查通过（死手保护代码存在，命中 $N 处）"
else
  echo "  ✘ 部署的仍是旧版本（未找到死手保护代码）"; RC=1
fi

if ss -ulnp 2>/dev/null | grep -q ':37260'; then
  echo "  ✔ 已监听 UDP 37260"
else
  echo "  ✘ 未监听 UDP 37260"; RC=1
fi

echo
if [ "$RC" -eq 0 ]; then
  echo "安装完成。日志： journalctl -u siyi-gimbal-relay -f"
else
  echo "安装存在问题，请检查上面的 ✘ 项。" >&2
fi
exit "$RC"
