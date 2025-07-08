#
# Makefile
#
# Circle - A C++ bare metal environment for Raspberry Pi
# Copyright (C) 2023-2024  R. Stange <rsta2@o2online.de>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

USBODEHOME = ../..
STDLIBHOME = $(USBODEHOME)/circle-stdlib
CIRCLEHOME = $(STDLIBHOME)/libs/circle

all: libusbcdgadget.a

include $(CIRCLEHOME)/Rules.mk

CPPFLAGS += -DUSB_GADGET_VENDOR_ID=0x04da
CPPFLAGS += -DUSB_GADGET_DEVICE_ID_CD=0x0d01
CFLAGS += -I $(USBODEHOME)/addon

ifneq ($(strip $(RASPPI)),5)
OBJS    = usbcdgadget.o usbcdgadgetendpoint.o \
	scsi_cmd_00_test_unit_ready.o \
	scsi_cmd_03_request_sense.o \
	scsi_cmd_12_inquiry.o \
	scsi_cmd_1A_mode_sense6.o \
	scsi_cmd_1B_start_stop_unit.o \
	scsi_cmd_1E_prevent_allow_medium_removal.o \
	scsi_cmd_25_read_capacity10.o \
	scsi_cmd_28_read10.o \
	scsi_cmd_2B_seek.o \
	scsi_cmd_2F_verify.o \
	scsi_cmd_42_read_sub_channel.o \
	scsi_cmd_43_read_toc_pma_atip.o \
	scsi_cmd_45_play_audio10.o \
	scsi_cmd_46_get_configuration.o \
	scsi_cmd_47_play_audio_msf.o \
	scsi_cmd_4A_get_event_status_notification.o \
	scsi_cmd_4B_pause_resume.o \
	scsi_cmd_4E_stop_play_scan.o \
	scsi_cmd_51_read_disc_information.o \
	scsi_cmd_52_read_track_information.o \
	scsi_cmd_55_mode_select10.o \
	scsi_cmd_5A_mode_sense10.o \
	scsi_cmd_A4_win2k_specific.o \
	scsi_cmd_A5_play_audio12.o \
	scsi_cmd_AC_get_performance.o \
	scsi_cmd_AD_read_disc_structure.o \
	scsi_cmd_BE_read_cd.o \
	scsi_cmd_D0_D7_tb_list_items.o \
	scsi_cmd_D2_DA_tb_get_count.o \
	scsi_cmd_D8_tb_set_next_cd.o \
	scsi_cmd_D9_tb_list_devices.o

endif

libusbcdgadget.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

-include $(DEPS)
