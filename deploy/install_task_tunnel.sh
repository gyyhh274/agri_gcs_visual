#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
install -d -m 700 "$HOME/.ssh"
install -d -m 755 "$HOME/.local/lib/agri-gcs" "$HOME/.config/systemd/user"
if [[ ! -f "$HOME/.ssh/agri_task_ed25519" ]]; then
    ssh-keygen -q -t ed25519 -f "$HOME/.ssh/agri_task_ed25519" -N '' -C agri-gcs-task-tunnel
fi
install -m 644 "$SCRIPT_DIR/mapping_tunnel.py" "$HOME/.local/lib/agri-gcs/mapping_tunnel.py"
install -m 644 "$SCRIPT_DIR/agri-task-tunnel.service" "$HOME/.config/systemd/user/agri-task-tunnel.service"
systemctl --user daemon-reload
echo 'Authorize this separate public key with restrict,port-forwarding,permitopen="127.0.0.1:8765",command="/bin/false"; reuse the verified agri-onboard-pointcloud host key:'
cat "$HOME/.ssh/agri_task_ed25519.pub"
echo 'Then: systemctl --user enable --now agri-task-tunnel.service'
