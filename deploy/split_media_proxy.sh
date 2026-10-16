#!/usr/bin/env bash
# 部署（修正版）：把媒体代理拆成独立进程/服务
# ⚠️ 教训：不要写成 `S tee xxx <<'EOF'` —— heredoc 会被函数吞掉，
#    落地的文件第 1 行会变成密码。必须先写 /tmp（无需 sudo），再 sudo cp。
set -uo pipefail
S() { echo 1 | sudo -S -p '' "$@"; }
SRC=/home/ly/siyi_gimbal_relay.py
DST=/usr/local/bin/siyi_gimbal_relay.py

echo "=== 1) 语法检查 ==="
python3 -m py_compile "$SRC" && echo "  ✔ 语法正确" || { echo "  ✘ 语法错误"; exit 1; }

echo
echo "=== 2) 安装新版本 ==="
S cp "$SRC" "$DST"
S chmod 755 "$DST"
echo "  已安装 $DST（$(stat -c %s "$DST") 字节）"
echo "  参数自检:"
python3 "$DST" --help 2>&1 | grep -E '^\s+--' | sed 's/^/    /'

echo
echo "=== 3) 媒体代理服务（独立进程，先写 /tmp 再 cp）==="
cat > /tmp/siyi-media-proxy.service <<'EOF'
[Unit]
Description=SIYI camera media proxy (photos/videos over HTTP)
Documentation=file:///usr/local/bin/siyi_gimbal_relay.py
After=network.target

[Service]
Type=simple
ExecStart=/usr/bin/python3 /usr/local/bin/siyi_gimbal_relay.py --http-only
Restart=always
RestartSec=2
User=root

[Install]
WantedBy=multi-user.target
EOF
S cp /tmp/siyi-media-proxy.service /etc/systemd/system/siyi-media-proxy.service
S chmod 644 /etc/systemd/system/siyi-media-proxy.service
echo "  已安装，首行: $(head -1 /etc/systemd/system/siyi-media-proxy.service)"

echo
echo "=== 4) 云台中转加 drop-in：--no-http ==="
cat > /tmp/no-http.conf <<'EOF'
[Service]
ExecStart=
ExecStart=/usr/bin/python3 /usr/local/bin/siyi_gimbal_relay.py --no-http
EOF
S mkdir -p /etc/systemd/system/siyi-gimbal-relay.service.d
S cp /tmp/no-http.conf /etc/systemd/system/siyi-gimbal-relay.service.d/no-http.conf
echo "  已加 drop-in:"
S cat /etc/systemd/system/siyi-gimbal-relay.service.d/no-http.conf | sed 's/^/    /'

echo
echo "=== 5) 重启服务 ==="
S systemctl daemon-reload
S systemctl enable siyi-media-proxy.service >/dev/null 2>&1
S systemctl restart siyi-media-proxy.service
sleep 3
S systemctl restart siyi-gimbal-relay.service
sleep 4

echo "  media-proxy: $(systemctl is-active siyi-media-proxy.service) / $(systemctl is-enabled siyi-media-proxy.service 2>/dev/null)"
echo "  relay:       $(systemctl is-active siyi-gimbal-relay.service)"

echo
echo "=== 6) 进程与端口核对（关键：两个独立进程）==="
pgrep -af 'siyi_gimbal_relay' | sed 's/^/  /'
echo "  37260 UDP: $(ss -uln 2>/dev/null | grep -c 37260)   37261 TCP: $(ss -tln 2>/dev/null | grep -c 37261)"

echo
echo "=== 7) 功能验证 ==="
sleep 2
echo -n "  媒体接口: "
timeout 10 curl -s "http://127.0.0.1:37261/cgi-bin/media.cgi/api/v1/getmediacount?media_type=0&path=101SIYI_IMG" | python3 -c "import json,sys; d=json.load(sys.stdin); print('照片数 =', d['data']['count'])" 2>/dev/null || echo "读取失败"

echo
echo "=== 8) 两端日志 ==="
S journalctl -u siyi-gimbal-relay -n 5 --no-pager 2>/dev/null | tail -5 | sed 's/^/  [relay] /'
S journalctl -u siyi-media-proxy -n 5 --no-pager 2>/dev/null | tail -5 | sed 's/^/  [proxy] /'
