//
// scsi_cmd_D2_DA_tb_get_count.cpp
//
// Handler for SCSI ToolBox commands NUMBER OF FILES (0xD2) / NUMBER OF CDS (0xDA).
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
#include <usbcdgadget/scsi_cmd_D2_DA_tb_get_count.h>
#include <usbcdgadget/usbcdgadget.h>
#include <scsitbservice/scsitbservice.h> // For SCSITBService
#include <circle/sched/scheduler.h>     // For CScheduler
#include <circle/logger.h>
#include <circle/util.h>                // For memcpy

ScsiCmdTbGetCount::ScsiCmdTbGetCount() {}
ScsiCmdTbGetCount::~ScsiCmdTbGetCount() {}

void ScsiCmdTbGetCount::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    u8 opcode = cbw.CBWCB[0];
    MLOGNOTE("ScsiCmdTbGetCount", "SCSITB Get Count (0x%02X)", opcode);

    SCSITBService* scsitbservice = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));
    u8 num_items = 0;

    if (scsitbservice) {
        size_t count = scsitbservice->GetCount();
        if (count > 100) { // SCSITB defines max entries as 100
            count = 100;
        }
        num_items = static_cast<u8>(count);
        MLOGDEBUG("ScsiCmdTbGetCount", "SCSITB Discovered %u items", num_items);
    } else {
        MLOGERR("ScsiCmdTbGetCount", "SCSITBService not found!");
        // Return 0 items if service is not available. Or could be an error.
    }

    u32 allocationLength = cbw.dCBWDataTransferLength;
    int length_to_send = sizeof(num_items);

    if (allocationLength < (u32)length_to_send) {
        length_to_send = allocationLength;
    }

    memcpy(gadget->GetInBuffer(), &num_items, length_to_send);

    this->begin_data_in_transfer(gadget, gadget->GetInBuffer(), length_to_send);
    gadget->m_nnumber_blocks = 0;
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;

    gadget->m_pCurrentCommandHandler = nullptr;
}
