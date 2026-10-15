#!/usr/bin/env bash
# Install the read-only bridge; never launches ROS master, sensors or controllers.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$(id -un)" != "ly" || ! -d /opt/ros/noetic/lib/python3/dist-packages ]]; then
    echo "This deployment template targets user ly with ROS Noetic; adapt the service for other installations." >&2
    exit 1
fi
install -d -m 755 "$HOME/.local/lib/agri-gcs"
install -m 644 "$SCRIPT_DIR/pointcloud_bridge.py" "$HOME/.local/lib/agri-gcs/pointcloud_bridge.py"
/usr/bin/python3 -m py_compile "$HOME/.local/lib/agri-gcs/pointcloud_bridge.py"
sudo install -m 644 "$SCRIPT_DIR/agri-pointcloud-bridge.service" /etc/systemd/system/agri-pointcloud-bridge.service
sudo systemctl daemon-reload
sudo systemctl enable agri-pointcloud-bridge.service
sudo systemctl restart agri-pointcloud-bridge.service
systemctl --no-pager --full status agri-pointcloud-bridge.service
