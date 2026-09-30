#
# Copyright (c) 2023 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

# Wifi boards
target_sources_ifdef(CONFIG_BOARD_NRF54LM20DK_NRF54LM20B_CPUAPP
		     app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/network_wifi.c)
