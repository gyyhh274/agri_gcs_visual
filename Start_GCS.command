#!/usr/bin/env bash
# Finder 双击或终端运行；保留诊断日志，避免从自动化的受限进程继承启动环境。
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOG_DIR="${TMPDIR:-/tmp}/agri-gcs"
mkdir -p "$LOG_DIR"
cd "$ROOT_DIR"
exec ./scripts/run.sh --gimbal-diag --video-diag "$@" >> "$LOG_DIR/ground-station.log" 2>&1
