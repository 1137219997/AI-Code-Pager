# The common ProMicro nRF52840 bootloader accepts UF2 files. West can still
# flash the merged HEX through a debug probe when one is attached.
board_runner_args(jlink "--device=nRF52840_xxAA" "--speed=4000")
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
