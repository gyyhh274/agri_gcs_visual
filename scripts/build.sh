#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILDER="${1:-cmake}"
JOBS="${JOBS:-4}"

# 自动检测本地工具链（适配 macOS 本机已有的独立开发环境）
if ! command -v cmake >/dev/null 2>&1; then
    if [[ -d "$HOME/Documents/A_Workspace/MiniGUI_A8mini/.tools/bin" ]]; then
        export PATH="$HOME/Documents/A_Workspace/MiniGUI_A8mini/.tools/bin:$PATH"
    fi
fi

QT_DIR="${QT_DIR:-}"
if [[ -z "$QT_DIR" && -d "$HOME/Documents/A_Workspace/MiniGUI_A8mini/.deps/qt/5.15.2/clang_64" ]]; then
    QT_DIR="$HOME/Documents/A_Workspace/MiniGUI_A8mini/.deps/qt/5.15.2/clang_64"
    export PATH="$QT_DIR/bin:$PATH"
fi

case "${BUILDER}" in
    cmake)
        BUILD_DIR="${ROOT_DIR}/build-cmake"
        CMAKE_FLAGS=(-DCMAKE_BUILD_TYPE=Release)
        if [[ -n "$QT_DIR" ]]; then
            CMAKE_FLAGS+=("-DCMAKE_PREFIX_PATH=$QT_DIR" "-DCMAKE_OSX_ARCHITECTURES=x86_64")
        fi
        cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja "${CMAKE_FLAGS[@]}"
        cmake --build "${BUILD_DIR}" --parallel "${JOBS}"

        # macOS bundle 需要配置 qt.conf 自动找到插件与 QML
        if [[ -d "${BUILD_DIR}/agri_gcs_visual.app" && -n "$QT_DIR" ]]; then
            mkdir -p "${BUILD_DIR}/agri_gcs_visual.app/Contents/Resources"
            cat << EOF > "${BUILD_DIR}/agri_gcs_visual.app/Contents/Resources/qt.conf"
[Paths]
Prefix = ${QT_DIR}
Plugins = plugins
Qml2Imports = qml
EOF
        fi
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
