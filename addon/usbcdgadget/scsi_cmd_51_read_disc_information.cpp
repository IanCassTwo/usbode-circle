//
// scsi_cmd_51_read_disc_information.cpp
//
// Handler for SCSI command READ DISC INFORMATION (0x51).
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
#include <usbcdgadget/scsi_cmd_51_read_disc_information.h>
#include <usbcdgadget/usbcdgadget.h> // For CUSBCDGadget, TUSBDiscInfoReply, etc.
#include <circle/logger.h>          // For MLOGDEBUG (optional)
#include <circle/util.h>            // For memcpy, htons, htonl

ScsiCmdReadDiscInformation::ScsiCmdReadDiscInformation() {}
ScsiCmdReadDiscInformation::~ScsiCmdReadDiscInformation() {}

void ScsiCmdReadDiscInformation::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    MLOGDEBUG("ScsiCmdReadDiscInformation", "READ DISC INFORMATION (0x51)");

    // gadget->m_DiscInfoReply is public.
    // Populate dynamic fields of the reply using the correct member names from TUSBDiscInfoReply in usbcdgadget.h
    gadget->m_DiscInfoReply.data_length = htons(sizeof(TUSBDiscInfoReply) - 2);
    gadget->m_DiscInfoReply.disc_status = 0x02; // Finalized disc
    gadget->m_DiscInfoReply.first_track_number = 1;
    gadget->m_DiscInfoReply.number_of_sessions = 1;
    gadget->m_DiscInfoReply.first_track_last_session = 1;
    gadget->m_DiscInfoReply.last_track_last_session = gadget->GetLastTrackNumber();
    // gadget->m_DiscInfoReply.disc_type = 0x00; // Already initialized

    u32 leadoutLBA = gadget->GetLeadoutLBA();
    // Correcting member names based on typical TUSBDiscInfoReply structure:
    gadget->m_DiscInfoReply.disc_id = htonl(0); // Optional, usually zero
    gadget->m_DiscInfoReply.last_session_lead_in_start_address = htonl(0); // Or start of first track
    gadget->m_DiscInfoReply.last_possible_lead_out_start_address = htonl(leadoutLBA);
    // disc_bar_code is usually all zeros and is part of the static init.

    u16 allocationLength = (cbw.CBWCB[7] << 8) | cbw.CBWCB[8];
    int length_to_send = sizeof(TUSBDiscInfoReply);
    if (allocationLength < length_to_send) {
        length_to_send = allocationLength;
    }

    memcpy(gadget->GetInBuffer(), &gadget->m_DiscInfoReply, length_to_send);

    this->begin_data_in_transfer(gadget, gadget->GetInBuffer(), length_to_send);

    gadget->m_nnumber_blocks = 0;
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = gadget->GetCurrentCSWStatus();

    gadget->m_pCurrentCommandHandler = nullptr;
}
