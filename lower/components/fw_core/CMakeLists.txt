cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS
        fw_proto.c
        fw_image_rx.c
        fw_image_tx.c
        fw_log.c
        fw_band_app.c
        fw_host_router.c
    INCLUDE_DIRS
        include
)
