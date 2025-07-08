//
// scsi_cmd_55_mode_select10.h
//
// Handler for SCSI command MODE SELECT (10) (0x55).
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2024 Jules the AI Agent
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#ifndef _CIRCLE_USB_GADGET_SCSI_CMD_55_MODE_SELECT10_H
#define _CIRCLE_USB_GADGET_SCSI_CMD_55_MODE_SELECT10_H

#include <usbcdgadget/scsi_command_handler.h>

class ScsiCmdModeSelect10 : public ScsiCommandHandler {
public:
    ScsiCmdModeSelect10();
    ~ScsiCmdModeSelect10() override;

    void handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) override;

    // This method will be called by CUSBCDGadget::OnTransferComplete
    // when data for Mode Select has been received.
    void process_received_data(CUSBCDGadget* gadget, size_t received_length);
};

#endif // _CIRCLE_USB_GADGET_SCSI_CMD_55_MODE_SELECT10_H
