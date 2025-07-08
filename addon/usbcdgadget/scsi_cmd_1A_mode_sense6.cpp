//
// scsi_cmd_1A_mode_sense6.cpp
//
// Handler for SCSI command MODE SENSE (6) (0x1A).
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
#include <usbcdgadget/scsi_cmd_1A_mode_sense6.h>
#include <usbcdgadget/usbcdgadget.h> // For CUSBCDGadget, structs, etc.
#include <cdplayer/cdplayer.h>       // For CCDPlayer (for audio volume)
#include <circle/sched/scheduler.h>  // For CScheduler
#include <circle/logger.h>           // For MLOGNOTE, MLOGDEBUG
#include <circle/util.h>             // For memcpy, memset, htons

ScsiCmdModeSense6::ScsiCmdModeSense6() {}
ScsiCmdModeSense6::~ScsiCmdModeSense6() {}

void ScsiCmdModeSense6::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    // int dbd = (cbw.CBWCB[1] >> 3) & 0x01; // DBD (Disable Block Descriptors) bit
    int page_control = (cbw.CBWCB[2] >> 6) & 0x03; // PC (Page Control) field
    int page_code = cbw.CBWCB[2] & 0x3F;          // Page Code
    // int sub_page_code = cbw.CBWCB[3]; // Subpage Code (usually 0x00 or 0xFF for all subpages)
    int allocation_length = cbw.CBWCB[4];
    // int control_byte = cbw.CBWCB[5]; // Control byte

    MLOGDEBUG("ScsiCmdModeSense6", "MODE SENSE (6) (0x1A), PC: %d, Page: 0x%02X, AllocLen: %d",
             page_control, page_code, allocation_length);

    u8 csw_status = CD_CSW_STATUS_OK;
    u8* buffer = gadget->GetInBuffer();
    memset(buffer, 0, allocation_length); // Zero out what we might use

    int current_data_idx = 0; // Current index in the buffer for writing data

    // Mode Parameter Header (6-byte command)
    ModeSense6Header header_6byte;
    memset(&header_6byte, 0, sizeof(header_6byte));
    header_6byte.mediumType = gadget->GetMediumType(); // Get from CUSBCDGadget
    // header_6byte.deviceSpecificParameter: set bit WP (Write Protect) if applicable. For CD-ROM, usually 0x00 or 0x80 if WP.
    // For CD-ROM (read-only), WP is true. Bit 7.
    header_6byte.deviceSpecificParameter = 0x80; // WP=1, DPOFUA=0
    header_6byte.blockDescriptorLength = 0; // We don't support block descriptors for Mode Sense (6)

    // Mode Data Length (byte 0 of header) will be set after all pages are added.
    // It is (total_data_length - 1).
    current_data_idx += sizeof(ModeSense6Header); // Reserve space for header

    // Lambda to copy mode page data
    auto copy_mode_page = [&](const void* page_data, size_t page_size) {
        if ((current_data_idx + page_size) <= (size_t)allocation_length && (current_data_idx + page_size) <= gadget->MaxInMessageSize) {
            memcpy(buffer + current_data_idx, page_data, page_size);
            current_data_idx += page_size;
        } else {
            // Not enough space in allocation_length or buffer, data will be truncated by host.
            // Or we could set an error if it's critical.
            MLOGWARN("ScsiCmdModeSense6", "Mode page data truncated due to allocation length or buffer size.");
        }
    };

    if (page_control == 0x03) { // Saved Values - Not Supported
        MLOGERR("ScsiCmdModeSense6", "Saving parameters not supported (PC=3).");
        gadget->SetSenseParameters(0x05, 0x39, 0x00); // ILLEGAL REQUEST, SAVING PARAMETERS NOT SUPPORTED
        csw_status = CD_CSW_STATUS_FAIL;
    } else { // Current, Changeable, or Default values (we return Current for all)
        bool page_found = false;
        if (page_code == 0x01 || page_code == 0x3F) { // Read/Write Error Recovery Parameters
            MLOGDEBUG("ScsiCmdModeSense6", "Adding Page 0x01 (Error Recovery)");
            ModePage0x01Data page01_data;
            memset(&page01_data, 0, sizeof(page01_data));
            page01_data.pageCodeAndPS = 0x01; // PS=0, Page Code=0x01
            page01_data.pageLength = sizeof(ModePage0x01Data) - 2; // Length of fields after pageLength
            // Populate with default/sensible values for a read-only CD-ROM
            page01_data.errorRecoveryBehaviour = 0x80; // AWRE=1 (Auto Write Realloc), ARRE=0 (Auto Read Realloc)
            page01_data.readRetryCount = 0x01;
            copy_mode_page(&page01_data, sizeof(page01_data));
            page_found = true;
            if (page_code != 0x3F) goto send_response; // Only this page was requested
        }
        if (page_code == 0x1A || page_code == 0x3F) { // Power Condition Mode Page
            MLOGDEBUG("ScsiCmdModeSense6", "Adding Page 0x1A (Power Condition)");
            ModePage0x1AData page1A_data;
            memset(&page1A_data, 0, sizeof(page1A_data));
            page1A_data.pageCodeAndPS = 0x1A;
            page1A_data.pageLength = sizeof(ModePage0x1AData) - 2;
            // Values from original code: idleStandby=0, timers=0
            copy_mode_page(&page1A_data, sizeof(page1A_data));
            page_found = true;
            if (page_code != 0x3F) goto send_response;
        }
        if (page_code == 0x2A || page_code == 0x3F) { // MM Capabilities and Mechanical Status
            MLOGDEBUG("ScsiCmdModeSense6", "Adding Page 0x2A (MM Capabilities)");
            ModePage0x2AData page2A_data; // This struct is in CUSBCDGadget.h
            memset(&page2A_data, 0, sizeof(page2A_data));
            page2A_data.pageCodeAndPS = 0x2A;
            page2A_data.pageLength = sizeof(ModePage0x2AData) - 2; // Correct length
            page2A_data.capabilityBits[0] = 0x01; // Can read CD-R
            page2A_data.capabilityBits[1] = 0x00; // Can't write
            page2A_data.capabilityBits[2] = 0x01; // AudioPlay supported
            page2A_data.capabilityBits[3] = 0x03; // CD-DA Commands Supported, CD-DA Stream is accurate
            page2A_data.capabilityBits[4] = 0x28; // Tray loading mechanism, with eject
            page2A_data.maxSpeed = htons(706 * 4); // 4x CD speed (706 KB/s * 4) ~2824. Original was 706. Max speed in KB/s
            page2A_data.numVolumeLevels = htons(0x00FF);
            page2A_data.bufferSize = htons(0); // No buffer specified
            page2A_data.currentSpeed = htons(706 * 4); // Current speed (same as max for simplicity)
            copy_mode_page(&page2A_data, sizeof(page2A_data));
            page_found = true;
            if (page_code != 0x3F) goto send_response;
        }
        if (page_code == 0x0E || page_code == 0x3F) { // CD Audio Control Page
            MLOGDEBUG("ScsiCmdModeSense6", "Adding Page 0x0E (CD Audio Control)");
            ModePage0x0EData page0E_data;
            memset(&page0E_data, 0, sizeof(page0E_data));
            page0E_data.pageCodeAndPS = 0x0E;
            page0E_data.pageLength = sizeof(ModePage0x0EData) - 2;
            page0E_data.IMMEDAndSOTC = 0x04; // IMMED=0, SOTC=1 (Stop On Track Crossing)
            page0E_data.CDDAOutput0Select = 0x01; // Output Port 0 connected to Channel 0
            page0E_data.CDDAOutput1Select = 0x02; // Output Port 1 connected to Channel 1

            CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
            u8 volume = 0xFF; // Default max volume
            if (cdplayer) {
                // volume = cdplayer->GetVolume(); // Original had this commented out, always returning 0xFF
            }
            page0E_data.Output0Volume = volume;
            page0E_data.Output1Volume = volume;
            copy_mode_page(&page0E_data, sizeof(page0E_data));
            page_found = true;
            if (page_code != 0x3F) goto send_response;
        }

        if (!page_found && page_code != 0x3F) { // Specific page requested but not found/supported
            MLOGERR("ScsiCmdModeSense6", "Unsupported page code: 0x%02X", page_code);
            gadget->SetSenseParameters(0x05, 0x24, 0x00); // ILLEGAL REQUEST, INVALID FIELD IN CDB (for page code)
            csw_status = CD_CSW_STATUS_FAIL;
        }
    }

send_response:
    if (csw_status == CD_CSW_STATUS_OK) {
        // Finalize Mode Data Length (total bytes of mode pages + block descriptors, if any)
        // current_data_idx currently holds total length of (header + pages)
        // Mode Data Length = (total length of data after this byte) = current_data_idx - 1
        header_6byte.modeDataLength = current_data_idx - 1;
        memcpy(buffer, &header_6byte, sizeof(ModeSense6Header)); // Copy finalized header

        int final_length_to_send = current_data_idx;
        if (allocation_length < final_length_to_send) {
            final_length_to_send = allocation_length;
        }
        // If final_length_to_send is less than sizeof(ModeSense6Header), adjust ModeDataLength again
        // This is tricky. SCSI spec says Mode Data Length should reflect actual data available,
        // host truncates if allocation_length is smaller.
        // If allocation_length is very small (e.g. < 4), we might need to adjust header.modeDataLength to 0.
        if (final_length_to_send < (int)sizeof(ModeSense6Header)) {
             // Not enough space for even the header. This is an issue.
             // The host should provide at least 4 bytes for Mode Sense (6).
             // For simplicity, if final_length_to_send is 0, send 0 bytes.
             // If it's 1, 2, or 3, send that many bytes of the header.
             // The original code sends 'length' which could be less than header size.
             // The behavior of `m_pEP[EPIn]->BeginTransfer` with length < header size is undefined here.
             // Let's assume allocation_length is always >= 4 for this command if it's valid.
        }


        this->begin_data_in_transfer(gadget, buffer, final_length_to_send);
        gadget->m_nnumber_blocks = 0;
        gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    }

    send_csw(gadget, csw_status); // Send CSW after data transfer is initiated or if error occurred
    gadget->m_pCurrentCommandHandler = nullptr;
}
