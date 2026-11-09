# 体重秤顶层工程（ESP-IDF）
#
# 目标芯片：ESP32（经典款）或 ESP32-S3 —— 见 docs/HARDWARE.md 的选择说明
# ESP-IDF：v5.5.5
#
# ⚠️ 本工程与手环工程共享协议层：components/fw_proto 直接编译
#    ../lower/components/fw_core/fw_proto.c，**不**编译 fw_band_app.c ——
#    那是手环专用的应用状态机（采样节拍 / 补传），秤用不到、还会拖进 fw_log。
#
# 用法（环境变量由 flash.ps1 铺好）：
#   .\flash.ps1 set-target esp32s3
#   .\flash.ps1 build

cmake_minimum_required(VERSION 3.16)

# 秤没有屏幕，不需要 LVGL 等第三方组件，因此不设 EXTRA_COMPONENT_DIRS。
# （若将来要加托管组件，再往这里加已存在的目录 —— IDF 要求该目录必须存在，
#   指向空目录会直接报错。）

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(health_monitor_scale)
