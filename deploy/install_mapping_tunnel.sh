#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
install -d -m 700 "$HOME/.ssh"
install -d -m 755 "$HOME/.local/lib/agri-gcs" "$HOME/.config/systemd/user"
if [[ ! -f "$HOME/.ssh/agri_mapping_ed25519" ]]; then
    ssh-keygen -q -t ed25519 -f "$HOME/.ssh/agri_mapping_ed25519" -N '' -C agri-gcs-mapping-tunnel
fi
install -m 644 "$SCRIPT_DIR/mapping_tunnel.py" "$HOME/.local/lib/agri-gcs/mapping_tunnel.py"
install -m 644 "$SCRIPT_DIR/agri-mapping-tunnel.service" "$HOME/.config/systemd/user/agri-mapping-tunnel.service"
systemctl --user daemon-reload
echo "Authorize this dedicated public key on the Jetson with permitopen=127.0.0.1:37262 and verify its SSH host key:"
cat "$HOME/.ssh/agri_mapping_ed25519.pub"
echo "Then run: systemctl --user enable --now agri-mapping-tunnel.service"
