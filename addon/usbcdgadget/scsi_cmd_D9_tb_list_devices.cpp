//
// scsi_cmd_D9_tb_list_devices.cpp
//
// Handler for SCSI ToolBox command LIST DEVICES (0xD9).
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
#include <usbcdgadget/scsi_cmd_D9_tb_list_devices.h>
#include <usbcdgadget/usbcdgadget.h>
#include <circle/logger.h>
#include <circle/util.h> // For memcpy

ScsiCmdTbListDevices::ScsiCmdTbListDevices() {}
ScsiCmdTbListDevices::~ScsiCmdTbListDevices() {}

void ScsiCmdTbListDevices::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    MLOGNOTE("ScsiCmdTbListDevices", "SCSITB LIST DEVICES (0xD9)");

    // First device is CDROM (0x02), others not implemented (0xFF)
    // This seems to be a custom response format for a specific tool.
    u8 devices_reply[] = {0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // Allocation length is typically in dCBWDataTransferLength for vendor commands if not in CDB
    u32 allocationLength = cbw.dCBWDataTransferLength;
    int length_to_send = sizeof(devices_reply);

    if (allocationLength < (u32)length_to_send) {
        length_to_send = allocationLength;
    }

    memcpy(gadget->GetInBuffer(), devices_reply, length_to_send);

    this->begin_data_in_transfer(gadget, gadget->GetInBuffer(), length_to_send);
    gadget->m_nnumber_blocks = 0; // Single data transfer
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;

    gadget->m_pCurrentCommandHandler = nullptr;
}
