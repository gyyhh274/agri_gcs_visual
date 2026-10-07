#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILDER="cmake"
if [[ "${1:-}" == --builder=* ]]; then
    BUILDER="${1#--builder=}"
    shift
fi
if [[ "${BUILDER}" != "cmake" && "${BUILDER}" != "qmake" ]]; then
    echo "Usage: $0 [--builder=cmake|qmake] [application options]" >&2
    exit 2
fi

BINARY=""
for BUILD_DIR in "${ROOT_DIR}/build-${BUILDER}" "${ROOT_DIR}/build"; do
    for CANDIDATE in "${BUILD_DIR}/agri_gcs_visual" \
                     "${BUILD_DIR}/agri_gcs_visual.app/Contents/MacOS/agri_gcs_visual"; do
        if [[ -x "${CANDIDATE}" && -f "${CANDIDATE}" ]]; then
            BINARY="${CANDIDATE}"
            break 2
        fi
    done
done

if [[ ! -x "${BINARY}" ]]; then
    echo "Binary not found. Run ./scripts/build.sh ${BUILDER} first." >&2
    exit 1
fi

exec "${BINARY}" "$@"
