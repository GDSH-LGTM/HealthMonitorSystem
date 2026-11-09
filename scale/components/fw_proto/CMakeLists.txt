# fw_proto：协议 v1.2 纯 C 层（体重秤用）
#
# 与手环工程 components/fw_core 里的 fw_proto.c **同一份源文件**——
# 不复制第二份实现，避免两端协议漂移。
#
# 只编译 fw_proto.c：秤不需要 fw_band_app（手环应用状态机）、fw_log（离线补传
# 日志）、fw_image_* / fw_host_router（图像与下行路由）。
#
# ---------------------------------------------------------------------------
# 手环工程在哪？
#
# 秤与手环是**两个独立仓库**，所以手环目录的名字由 clone 时的仓库名决定，
# 未必要叫 lower/。这里按顺序试几个候选路径，覆盖常见的几种放法：
#
#   -D FWP_ROOT=...                               显式指定（优先级最高）
#   <父目录>/lower/components/fw_core              本地开发时的目录名
#   <父目录>/HealthMonitorSystem-lower/...         按 GitHub 仓库名 clone
#   <父目录>/health-monitor-lower/...              小写 kebab 命名习惯
#
# 都不命中就报错退出，并打印已尝试过的路径。
#
# ⚠️ message() 的文本必须是纯 ASCII。
#    CMake 的输出会经 Python 解码后打到控制台，而中文 Windows 控制台是 GBK：
#    消息里出现非 ASCII 字符会直接抛
#        UnicodeEncodeError: 'gbk' codec can't encode character ...
#    让 configure 莫名其妙失败。注释里可以写中文（不参与输出），消息只能英文。
#    这个坑和 .ps1 脚本必须 ASCII 是同一类问题。
# ---------------------------------------------------------------------------

set(_fwp_probe "")
if(DEFINED FWP_ROOT AND NOT FWP_ROOT STREQUAL "" AND
   EXISTS "${FWP_ROOT}/fw_proto.c")
    set(_fwp_probe "${FWP_ROOT}")
    set(_fwp_how "given by -DFWP_ROOT")
else()
    get_filename_component(_scale_dir "${CMAKE_CURRENT_LIST_DIR}/../.." REALPATH)
    get_filename_component(_parent_dir "${_scale_dir}/.." REALPATH)

    set(_fwp_candidates
        "${_parent_dir}/lower/components/fw_core"
        "${_parent_dir}/HealthMonitorSystem-lower/components/fw_core"
        "${_parent_dir}/health-monitor-lower/components/fw_core"
    )

    foreach(_cand IN LISTS _fwp_candidates)
        if(EXISTS "${_cand}/fw_proto.c")
            set(_fwp_probe "${_cand}")
            set(_fwp_how "auto-detected")
            break()
        endif()
    endforeach()

    if(NOT _fwp_probe)
        string(REPLACE ";" "\n    " _tried "${_fwp_candidates}")
        message(FATAL_ERROR
            "Cannot find the band protocol layer fw_proto.c.\n"
            "Tried:\n    ${_tried}\n"
            "\n"
            "Both repos must sit in the same parent directory, with the band\n"
            "directory named as one of the above. Renaming the clone to lower/\n"
            "is the easiest fix. Layout expected:\n"
            "    <some-dir>/\n"
            "      HealthMonitorSystem-lower/   <- band (rename to lower/ is fine)\n"
            "      HealthMonitorSystem-scale/   <- scale\n"
            "\n"
            "Or point at it explicitly, which works with any layout:\n"
            "    idf.py -DFWP_ROOT=<band-dir>/components/fw_core build")
    endif()
endif()

set(FWP_ROOT "${_fwp_probe}" CACHE PATH
    "fw_core directory (contains fw_proto.c and include/fw_proto.h)")

message(STATUS "fw_proto: band protocol layer ${FWP_ROOT} (${_fwp_how})")

idf_component_register(
    SRCS
        "${FWP_ROOT}/fw_proto.c"
    INCLUDE_DIRS
        "${FWP_ROOT}/include"
)
