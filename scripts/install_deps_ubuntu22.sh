#!/usr/bin/env bash
set -euo pipefail

sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    qtbase5-dev \
    qtdeclarative5-dev \
    qtquickcontrols2-5-dev \
    qtpositioning5-dev \
    qml-module-qtpositioning \
    qml-module-qtquick2 \
    qml-module-qtquick-window2 \
    qml-module-qtquick-controls2 \
    qml-module-qtquick-layouts \
    qml-module-qt-labs-settings \
    fonts-noto-cjk \
    libgl1-mesa-dri \
    libglx-mesa0

qmake --version
