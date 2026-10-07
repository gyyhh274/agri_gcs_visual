#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILDER="cmake"
if [[ "${1:-}" == --builder=* ]]; then
    BUILDER="${1#--builder=}"
    shift
fi
# Absolute path works from any cwd and overrides a previously saved campus source.
exec "${ROOT_DIR}/scripts/run.sh" "--builder=${BUILDER}" "$@" \
    "--tiles=${ROOT_DIR}/maps/maxar-auckland" --tile-scheme=xyz
