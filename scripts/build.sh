#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILDER="${1:-cmake}"
JOBS="${JOBS:-4}"

case "${BUILDER}" in
    cmake)
        BUILD_DIR="${ROOT_DIR}/build-cmake"
        cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
        cmake --build "${BUILD_DIR}" --parallel "${JOBS}"
        ;;
    qmake)
        BUILD_DIR="${ROOT_DIR}/build-qmake"
        QMAKE="${QMAKE:-qmake}"
        mkdir -p "${BUILD_DIR}"
        cd "${BUILD_DIR}"
        "${QMAKE}" "${ROOT_DIR}/agri_gcs_visual.pro" CONFIG+=release CONFIG-=debug
        make -j "${JOBS}"
        ;;
    *)
        echo "Usage: $0 [cmake|qmake]" >&2
        exit 2
        ;;
esac

echo "Built in ${BUILD_DIR}"
echo "Run: ./scripts/run.sh --builder=${BUILDER}"
