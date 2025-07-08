//
// scsi_cmd_25_read_capacity10.cpp
//
// Handler for SCSI command READ CAPACITY (10) (0x25).
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
#include <usbcdgadget/scsi_cmd_25_read_capacity10.h>
#include <usbcdgadget/usbcdgadget.h> // For CUSBCDGadget, TUSBCDReadCapacityReply, htonl, etc.
#include <circle/logger.h>          // For MLOGDEBUG (optional)
#include <circle/util.h>            // For memcpy

ScsiCmdReadCapacity10::ScsiCmdReadCapacity10() {}
ScsiCmdReadCapacity10::~ScsiCmdReadCapacity10() {}

void ScsiCmdReadCapacity10::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    MLOGDEBUG("ScsiCmdReadCapacity10", "READ CAPACITY (10)");

    // Ensure m_ReadCapReply is correctly populated. nSectorSize is usually static (2048).
    // nLastBlockAddr needs to be dynamic based on the loaded media.
    gadget->m_ReadCapReply.nLastBlockAddr = htonl(gadget->GetLeadoutLBA() - 1);
    // m_ReadCapReply.nSectorSize is already set to htonl(2048) in CUSBCDGadget's m_ReadCapReply member initializer.

    memcpy(gadget->GetInBuffer(), &gadget->m_ReadCapReply, SIZE_READCAPREP);

    gadget->m_nnumber_blocks = 0;  // Signifies that this data transfer is the only one for this command.
                                 // OnTransferComplete will see this and send CSW.

    this->begin_data_in_transfer(gadget, gadget->GetInBuffer(), SIZE_READCAPREP);

    gadget->m_nState = CUSBCDGadget::TCDState::DataIn; // Set state for OnTransferComplete
    gadget->m_CSW.bmCSWStatus = gadget->GetCurrentCSWStatus(); // Use current default status (usually OK)
                                                              // Or explicitly CD_CSW_STATUS_OK if no errors are possible here.
                                                              // Original code used bmCSWStatus.

    gadget->m_pCurrentCommandHandler = nullptr; // This command completes with this handler call.
}
