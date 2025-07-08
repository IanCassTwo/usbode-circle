//
// scsi_cmd_A4_win2k_specific.cpp
//
// Handler for SCSI command 0xA4 (Windows 2000 specific).
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
#include <usbcdgadget/scsi_cmd_A4_win2k_specific.h>
#include <usbcdgadget/usbcdgadget.h>
#include <circle/logger.h>
#include <circle/util.h> // For memcpy

ScsiCmdWin2kSpecific::ScsiCmdWin2kSpecific() {}
ScsiCmdWin2kSpecific::~ScsiCmdWin2kSpecific() {}

void ScsiCmdWin2kSpecific::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    MLOGNOTE("ScsiCmdWin2kSpecific", "Windows 2000 specific command (0xA4)");

    // Response copied from an ASUS CDROM drive, as per original code.
    u8 response[] = {0x00, 0x06, 0x00, 0x00, 0x25, 0xFF, 0x01, 0x00};
    // Check allocation length from CBW, though this command is fixed size.
    // u16 allocationLength = cbw.dCBWDataTransferLength;
    // int length_to_send = sizeof(response);
    // if (allocationLength < length_to_send) {
    //     length_to_send = allocationLength;
    // }
    // The original code did not check allocation length for this specific command.

    memcpy(gadget->GetInBuffer(), response, sizeof(response));

    this->begin_data_in_transfer(gadget, gadget->GetInBuffer(), sizeof(response));
    gadget->m_nnumber_blocks = 0;
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK; // Original code set this explicitly.

    gadget->m_pCurrentCommandHandler = nullptr;
}
