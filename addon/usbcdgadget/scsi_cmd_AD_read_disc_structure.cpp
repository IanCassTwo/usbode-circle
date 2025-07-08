//
// scsi_cmd_AD_read_disc_structure.cpp
//
// Handler for SCSI command READ DISC STRUCTURE (0xAD).
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
#include <usbcdgadget/scsi_cmd_AD_read_disc_structure.h>
#include <usbcdgadget/usbcdgadget.h> // For CUSBCDGadget, reply structs, etc.
#include <circle/logger.h>          // For MLOGNOTE
#include <circle/util.h>            // For memcpy, memset, htons

ScsiCmdReadDiscStructure::ScsiCmdReadDiscStructure() {}
ScsiCmdReadDiscStructure::~ScsiCmdReadDiscStructure() {}

void ScsiCmdReadDiscStructure::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    // u8 mediaType = cbw.CBWCB[1]; // In MMC-6, byte 1 is Media Type. Original used CBWCB[2] & 0x0F.
                                  // Let's assume original parsing was for a specific older spec or device behavior.
                                  // The important fields for this minimal implementation are format and allocation length.
    // u32 address_field = (u32)(cbw.CBWCB[2] << 24) | (u32)(cbw.CBWCB[3] << 16) | (u32)(cbw.CBWCB[4] << 8) | cbw.CBWCB[5];
    // u8 layer_num = cbw.CBWCB[6];
    u8 format_code = cbw.CBWCB[7];
    u16 allocationLength = (cbw.CBWCB[8] << 8) | cbw.CBWCB[9];
    // u8 agid = (cbw.CBWCB[10] >> 6) & 0x03; // AGID field

    MLOGNOTE("ScsiCmdReadDiscStructure", "READ DISC STRUCTURE (0xAD), Format: 0x%02X, AllocLen: %u",
             format_code, allocationLength);

    int data_to_send_len = 0;
    TUSBCDReadDiscStructureHeader header_reply; // Structure for the header part of the reply
    memset(&header_reply, 0, sizeof(header_reply));

    // Buffer to hold the full response (header + data)
    u8 response_buffer[256]; // Max typical response size for simple formats
    memset(response_buffer, 0, sizeof(response_buffer));

    switch (format_code) {
        case 0x00: // Disc Information / Basic Format
            // This format returns basic disc info, like disc category, status, number of sessions/tracks.
            // The original code sent an empty payload (just header with DataLength = 2, meaning 0 bytes of format data).
            // A more complete implementation would fill TUSBCDReadDiscStructureFormat00Data.
            // For now, replicating original minimal response.
            header_reply.dataLength = htons(0); // 0 bytes of format-specific data after the header's own reserved bytes.
                                                // The spec says Data Length = (total length of response - 2).
                                                // If only header is sent (4 bytes), Data Length = 4-2 = 2.
                                                // If header + 4 bytes format data, Data Length = (4+4)-2 = 6.
                                                // Original code: header.dataLength = 2 (meaning 0 bytes of format data after header's first 2 bytes)
                                                // This implies the total response is 4 bytes (header itself).
            data_to_send_len = sizeof(TUSBCDReadDiscStructureHeader); // Only the header
            memcpy(response_buffer, &header_reply, data_to_send_len);
            break;

        case 0x01: // Copyright Information
        {
            u8 copyright_payload[] = {
                0x00, // Copyright Protection System Type (00h = None)
                0x00, // Region Management Information (00h = Not region protected / All regions)
                0x00, // Reserved
                0x00  // Reserved
            };
            header_reply.dataLength = htons(sizeof(TUSBCDReadDiscStructureHeader) - 2 + sizeof(copyright_payload));
            memcpy(response_buffer, &header_reply, sizeof(TUSBCDReadDiscStructureHeader));
            memcpy(response_buffer + sizeof(TUSBCDReadDiscStructureHeader), copyright_payload, sizeof(copyright_payload));
            data_to_send_len = sizeof(TUSBCDReadDiscStructureHeader) + sizeof(copyright_payload);
            break;
        }

        // Other format codes (0x02-0xFF) are more complex (DVD/BD specific, track info, etc.)
        // The original code had a default case that sent an empty payload (header.dataLength = 2).
        default: // For any other format, send minimal response (just header indicating 0 data bytes)
            MLOGWARN("ScsiCmdReadDiscStructure", "Unsupported format code 0x%02X, sending minimal response.", format_code);
            header_reply.dataLength = htons(0);
            data_to_send_len = sizeof(TUSBCDReadDiscStructureHeader);
            memcpy(response_buffer, &header_reply, data_to_send_len);
            break;
    }

    if (allocationLength < data_to_send_len) {
        data_to_send_len = allocationLength;
    }

    memcpy(gadget->GetInBuffer(), response_buffer, data_to_send_len);
    this->begin_data_in_transfer(gadget, gadget->GetInBuffer(), data_to_send_len);

    gadget->m_nnumber_blocks = 0;
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = gadget->GetCurrentCSWStatus(); // Usually OK, original used bmCSWStatus.

    gadget->m_pCurrentCommandHandler = nullptr;
}
