//
// scsi_cmd_D0_D7_tb_list_items.cpp
//
// Handler for SCSI ToolBox commands LIST FILES (0xD0) / LIST CDS (0xD7).
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
#include <usbcdgadget/scsi_cmd_D0_D7_tb_list_items.h>
#include <usbcdgadget/usbcdgadget.h>     // For CUSBCDGadget, TUSBCDToolboxFileEntry
#include <scsitbservice/scsitbservice.h> // For SCSITBService
#include <circle/sched/scheduler.h>      // For CScheduler
#include <circle/logger.h>
#include <circle/util.h>                 // For memcpy, strncpy
#include <algorithm>                     // For std::min

ScsiCmdTbListItems::ScsiCmdTbListItems() {}
ScsiCmdTbListItems::~ScsiCmdTbListItems() {}

void ScsiCmdTbListItems::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    u8 opcode = cbw.CBWCB[0];
    MLOGNOTE("ScsiCmdTbListItems", "SCSITB List Items (0x%02X)", opcode);

    SCSITBService* scsitbservice = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));

    if (!scsitbservice) {
        MLOGERR("ScsiCmdTbListItems", "SCSITBService not found!");
        gadget->SetSenseParameters(0x02, 0x04, 0x01); // NOT READY, LOGICAL UNIT COMMUNICATION FAILURE
        send_csw(gadget, CD_CSW_STATUS_FAIL);
        gadget->m_pCurrentCommandHandler = nullptr;
        return;
    }

    const size_t MAX_ENTRIES_SCSITB = 100; // As defined by SCSITB protocol notes
    size_t actual_count = scsitbservice->GetCount();
    size_t count_to_report = std::min(actual_count, MAX_ENTRIES_SCSITB);

    // Calculate total data size to be sent
    size_t total_data_size = count_to_report * sizeof(TUSBCDToolboxFileEntry);

    // Check against buffer size (though GetInBuffer should be large enough for MaxInMessageSize)
    if (total_data_size > gadget->MaxInMessageSize) {
        MLOGERR("ScsiCmdTbListItems", "Calculated data size %u exceeds MaxInMessageSize %u", total_data_size, gadget->MaxInMessageSize);
        // This case should ideally not happen if MAX_ENTRIES_SCSITB * sizeof(TUSBCDToolboxFileEntry) is within limits.
        // Truncate count_to_report if necessary.
        count_to_report = gadget->MaxInMessageSize / sizeof(TUSBCDToolboxFileEntry);
        total_data_size = count_to_report * sizeof(TUSBCDToolboxFileEntry);
        MLOGWARN("ScsiCmdTbListItems", "Truncated items to %u due to buffer limits.", count_to_report);
    }

    u8* buffer = gadget->GetInBuffer();
    TUSBCDToolboxFileEntry* entries_ptr = reinterpret_cast<TUSBCDToolboxFileEntry*>(buffer);

    for (size_t i = 0; i < count_to_report; ++i) {
        TUSBCDToolboxFileEntry* entry = &entries_ptr[i];
        entry->index = static_cast<u8>(i);
        entry->type = 0; // 0 = file type (as per original code for both files/CDs)

        const char* name = scsitbservice->GetName(i);
        strncpy(reinterpret_cast<char*>(entry->name), name, 32);
        entry->name[32] = '\0'; // Ensure null termination

        u32 size = scsitbservice->GetSize(i); // DWORD in original, u32 here
        entry->size[0] = 0; // Highest byte of 40-bit size
        entry->size[1] = (size >> 24) & 0xFF;
        entry->size[2] = (size >> 16) & 0xFF;
        entry->size[3] = (size >> 8) & 0xFF;
        entry->size[4] = size & 0xFF;
    }

    u32 allocationLength = cbw.dCBWDataTransferLength;
    size_t length_to_send = total_data_size;

    if (allocationLength < length_to_send) {
        length_to_send = allocationLength;
    }

    this->begin_data_in_transfer(gadget, buffer, length_to_send);
    gadget->m_nnumber_blocks = 0;
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;

    gadget->m_pCurrentCommandHandler = nullptr;
}
