# ESP32-S3 健康监测手环 —— ESP-IDF 顶层工程
#
# 目标芯片：ESP32-S3（模组型号待串口日志确认，见 docs/BUILD_FLASH.md）
# ESP-IDF：v5.5.5
#
# 常用命令（PowerShell，环境变量见 docs/BUILD_FLASH.md）：
#   .\flash.ps1 set-target esp32s3
#   .\flash.ps1 build
#   .\flash.ps1 flash monitor

cmake_minimum_required(VERSION 3.16)

# 第三方组件通过 EXTRA_COMPONENT_DIRS 引入，而不是复制进 components/。
# 这样它们（尤其 LVGL 的 1200+ 文件）不会污染本仓库。
# 首次使用前跑一次 scripts/fetch_deps.ps1 把它们准备到 third_party/。
#
# ⚠️ 组件名 = 目录名。所以这里用 esp32_camera（下划线）而不是上游的
#    esp32-camera（横线），免得 REQUIRES 里要写带横线的名字。
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/third_party"
    "${CMAKE_CURRENT_LIST_DIR}/third_party/esp32_camera"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(health_monitor_band)
