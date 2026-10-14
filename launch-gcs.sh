#!/usr/bin/env bash
# 启动地面站（桌面图标双击时调用）
#
# 特性：
#   * 单实例：重复启动会先关掉旧实例。相机与中转都是「单客户端」模型，
#     同时开两个地面站会互相抢应答，表现为云台频繁断连。
#   * 日志写到 /tmp/agri_gcs_visual.log，出问题时看这个文件。
set -uo pipefail

PROJ="$HOME/projects/agri_gcs_visual"
LOG="/tmp/agri_gcs_visual.log"
APP="$PROJ/build/agri_gcs_visual"

notify() {
    command -v notify-send >/dev/null 2>&1 && notify-send "无人机地面站" "$1" 2>/dev/null || true
}

if [ ! -x "$APP" ]; then
    notify "可执行文件不存在，请先编译：$PROJ"
    exit 1
fi

# ── 单实例：先关掉已在运行的实例 ──────────────────────────────
if pgrep -f '[b]uild/agri_gcs_visual' >/dev/null 2>&1; then
    notify "检测到已在运行，正在重启…"
    pkill -f '[b]uild/agri_gcs_visual' 2>/dev/null
    sleep 2
fi

cd "$PROJ" || exit 1
: > "$LOG"
nohup ./build/agri_gcs_visual >>"$LOG" 2>&1 &
sleep 3

if pgrep -f '[b]uild/agri_gcs_visual' >/dev/null 2>&1; then
    notify "地面站已启动（地址会自动发现）"
else
    notify "启动失败，详见 $LOG"
    exit 1
fi
