//
// scsi_cmd_52_read_track_information.cpp
//
// Handler for SCSI command READ TRACK INFORMATION (0x52).
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
#include <usbcdgadget/scsi_cmd_52_read_track_information.h>
#include <usbcdgadget/usbcdgadget.h> // For CUSBCDGadget, TUSBCDTrackInformationBlock, etc.
#include <circle/logger.h>          // For MLOGNOTE
#include <circle/util.h>            // For memcpy, memset, htons, htonl
#include <cueparser/cueparser.h>    // For CUETrackInfo

ScsiCmdReadTrackInformation::ScsiCmdReadTrackInformation() {}
ScsiCmdReadTrackInformation::~ScsiCmdReadTrackInformation() {}

void ScsiCmdReadTrackInformation::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    // u8 open_flag = (cbw.CBWCB[1] >> 2) & 0x01; // OPEN bit, not used in original logic
    u8 address_type = cbw.CBWCB[1] & 0x03; // LBN or Track Number
    u32 address_field = (u32)(cbw.CBWCB[2] << 24) | (u32)(cbw.CBWCB[3] << 16) | (u32)(cbw.CBWCB[4] << 8) | cbw.CBWCB[5];
    u16 allocationLength = (cbw.CBWCB[7] << 8) | cbw.CBWCB[8];
    // u8 control_byte = cbw.CBWCB[9]; // Not used in original logic

    MLOGNOTE("ScsiCmdReadTrackInformation", "READ TRACK INFO (0x52), AddrType: %u, Addr: %u, AllocLen: %u",
             address_type, address_field, allocationLength);

    TUSBCDTrackInformationBlock response_block;
    memset(&response_block, 0, sizeof(TUSBCDTrackInformationBlock));

    // Data Length field (bytes 0-1) is the length of the data *following* this field.
    // So, sizeof(TUSBCDTrackInformationBlock) - 2.
    response_block.dataLength = htons(sizeof(TUSBCDTrackInformationBlock) - 2);

    bool track_found = false;

    if (address_type == 0x00) { // Logical Block Address
        // Find track containing this LBA
        CUETrackInfo trackInfo = gadget->GetTrackInfoForLBA(address_field);
        if (trackInfo.track_number != -1) {
            response_block.logicalTrackNumberLSB = trackInfo.track_number & 0xFF;
            // response_block.logicalTrackNumberMSB = (trackInfo.track_number >> 8) & 0xFF; // If track numbers > 255
            response_block.sessionNumberLSB = 0x01; // Assuming single session
            // response_block.sessionNumberMSB = 0x00;

            // Populate common fields based on trackInfo
            response_block.trackMode = (trackInfo.track_mode == CUETrack_AUDIO) ? 0x00 : 0x04; //MMC-6: 00b=Audio, 04b=Data track, blank, etc.
                                                                                             // Original used 0x02/0x06 - checking spec for these bits.
                                                                                             // Bit2 Blank, Bit1 Copy, Bit0 Damage
                                                                                             // Let's use the MMC-6 Table 309 - Track Mode field definitions.
                                                                                             // For data: Bit2=0(not blank), Bit1=0(copy prohibited by default), Bit0=0(not damaged) -> 0x00
                                                                                             // For audio: Bit2=0, Bit1=0(copy prohibited), Bit0=0 -> 0x00
                                                                                             // The original 0x02/0x06 might have been for specific flags.
                                                                                             // Let's use 0x04 for data track with data characteristics (implies not audio).
                                                                                             // And 0x00 for audio track.
            if (trackInfo.track_mode != CUETrack_AUDIO) response_block.trackMode |= (1<<2); // Set Data bit (0x04)

            response_block.dataMode = 0x01; // Assume Mode 1 for data tracks, or as per CUE if available.
                                            // This field is more relevant for DVD/BD. For CD, trackMode is key.
            response_block.logicalTrackStartAddress = htonl(trackInfo.track_start);
            // Other fields like nextWriteableAddress, freeBlocks, fixedPacketSize, logicalTrackSize, lastRecordedAddress
            // are mostly for writable media or more detailed disc layouts. Zeroed by memset.
            // logicalTrackSize could be (next_track_start_LBA - current_track_start_LBA)
            track_found = true;
        }
    } else if (address_type == 0x01) { // Track Number
        CUETrackInfo trackInfo = gadget->GetTrackInfoForTrack(static_cast<int>(address_field));
        if (trackInfo.track_number != -1) {
            response_block.logicalTrackNumberLSB = trackInfo.track_number & 0xFF;
            response_block.sessionNumberLSB = 0x01; // Assuming single session

            if (trackInfo.track_mode != CUETrack_AUDIO) response_block.trackMode |= (1<<2); // Set Data bit (0x04)
            response_block.dataMode = 0x01;
            response_block.logicalTrackStartAddress = htonl(trackInfo.track_start);
            track_found = true;
        }
    }
    // address_type 0x02 (Session Number) not implemented in original, leads to empty response.

    if (!track_found) {
        MLOGWARN("ScsiCmdReadTrackInformation", "Track/LBA not found for AddrType: %u, Addr: %u", address_type, address_field);
        // Behavior for not found: The spec says if LBA/Track is out of range, return CHECK CONDITION
        // with ILLEGAL REQUEST / LOGICAL BLOCK ADDRESS OUT OF RANGE or ILLEGAL REQUEST / INVALID FIELD IN CDB.
        // Original code would send an empty (zeroed) block. Let's send an error.
        gadget->SetSenseParameters(0x05, 0x21, (address_type == 0x00 ? 0x00 : 0x02)); // LBA out of range or Invalid field for track#
        send_csw(gadget, CD_CSW_STATUS_FAIL);
        gadget->m_pCurrentCommandHandler = nullptr;
        return;
    }

    int length_to_send = sizeof(TUSBCDTrackInformationBlock);
    if (allocationLength < length_to_send) {
        length_to_send = allocationLength;
    }

    memcpy(gadget->GetInBuffer(), &response_block, length_to_send);
    this->begin_data_in_transfer(gadget, gadget->GetInBuffer(), length_to_send);

    gadget->m_nnumber_blocks = 0;
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK; // Original was OK

    gadget->m_pCurrentCommandHandler = nullptr;
}
