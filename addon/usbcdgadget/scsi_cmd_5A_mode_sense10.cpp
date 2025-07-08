//
// scsi_cmd_5A_mode_sense10.cpp
//
// Handler for SCSI command MODE SENSE (10) (0x5A).
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
#include <usbcdgadget/scsi_cmd_5A_mode_sense10.h>
#include <usbcdgadget/usbcdgadget.h> // For CUSBCDGadget, structs, etc.
#include <cdplayer/cdplayer.h>       // For CCDPlayer (for audio volume)
#include <circle/sched/scheduler.h>  // For CScheduler
#include <circle/logger.h>           // For MLOGNOTE, MLOGDEBUG
#include <circle/util.h>             // For memcpy, memset, htons

ScsiCmdModeSense10::ScsiCmdModeSense10() {}
ScsiCmdModeSense10::~ScsiCmdModeSense10() {}

void ScsiCmdModeSense10::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    // int llbaa = (cbw.CBWCB[1] >> 3) & 0x01; // LLBAA bit (Long LBA Accepted) - original had different parsing here.
                                            // Correct for MODE SENSE(10) is bit 4 of byte 1 (LLBAA)
    // int dbd = (cbw.CBWCB[1] >> 4) & 0x01; // DBD bit (Disable Block Descriptors) - Correct is bit 3 of byte 1
    int page_control = (cbw.CBWCB[2] >> 6) & 0x03; // PC (Page Control) field
    int page_code = cbw.CBWCB[2] & 0x3F;          // Page Code
    // int sub_page_code = cbw.CBWCB[3]; // Subpage Code
    u16 allocation_length = (cbw.CBWCB[7] << 8) | cbw.CBWCB[8];
    // int control_byte = cbw.CBWCB[9]; // Control byte

    MLOGDEBUG("ScsiCmdModeSense10", "MODE SENSE (10) (0x5A), PC: %d, Page: 0x%02X, AllocLen: %u",
             page_control, page_code, allocation_length);

    u8 csw_status = CD_CSW_STATUS_OK;
    u8* buffer = gadget->GetInBuffer();
    memset(buffer, 0, allocation_length);

    int current_data_idx = 0;

    // Mode Parameter Header (10-byte command)
    ModeSense10Header header_10byte;
    memset(&header_10byte, 0, sizeof(header_10byte));
    header_10byte.mediumType = gadget->GetMediumType();
    header_10byte.deviceSpecificParameter = 0x80; // WP=1 for CD-ROM
    header_10byte.blockDescriptorLength = htons(0); // We don't support block descriptors. Spec says this is 2 bytes.

    // Mode Data Length (bytes 0-1 of header) will be set after all pages are added.
    // It is (total_data_length - 2).
    current_data_idx += sizeof(ModeSense10Header); // Reserve space for header

    auto copy_mode_page = [&](const void* page_data, size_t page_size) {
        if ((current_data_idx + page_size) <= (size_t)allocation_length && (current_data_idx + page_size) <= gadget->MaxInMessageSize) {
            memcpy(buffer + current_data_idx, page_data, page_size);
            current_data_idx += page_size;
        } else {
            MLOGWARN("ScsiCmdModeSense10", "Mode page data truncated.");
        }
    };

    if (page_control == 0x03) { // Saved Values - Not Supported
        MLOGERR("ScsiCmdModeSense10", "Saving parameters not supported (PC=3).");
        gadget->SetSenseParameters(0x05, 0x39, 0x00); // ILLEGAL REQUEST, SAVING PARAMETERS NOT SUPPORTED
        csw_status = CD_CSW_STATUS_FAIL;
    } else {
        bool page_found = false;
        // Same pages as Mode Sense (6) are typically supported, using same structures.
        if (page_code == 0x01 || page_code == 0x3F) { // Read/Write Error Recovery
            MLOGDEBUG("ScsiCmdModeSense10", "Adding Page 0x01 (Error Recovery)");
            ModePage0x01Data page01_data; // From usbcdgadget.h
            memset(&page01_data, 0, sizeof(page01_data));
            page01_data.pageCodeAndPS = 0x01;
            page01_data.pageLength = sizeof(ModePage0x01Data) - 2;
            page01_data.errorRecoveryBehaviour = 0x80;
            page01_data.readRetryCount = 0x01;
            copy_mode_page(&page01_data, sizeof(page01_data));
            page_found = true;
            if (page_code != 0x3F) goto send_response_ms10;
        }
        if (page_code == 0x1A || page_code == 0x3F) { // Power Condition
            MLOGDEBUG("ScsiCmdModeSense10", "Adding Page 0x1A (Power Condition)");
            ModePage0x1AData page1A_data; // From usbcdgadget.h
            memset(&page1A_data, 0, sizeof(page1A_data));
            page1A_data.pageCodeAndPS = 0x1A;
            page1A_data.pageLength = sizeof(ModePage0x1AData) - 2;
            copy_mode_page(&page1A_data, sizeof(page1A_data));
            page_found = true;
            if (page_code != 0x3F) goto send_response_ms10;
        }
        if (page_code == 0x2A || page_code == 0x3F) { // MM Capabilities
            MLOGDEBUG("ScsiCmdModeSense10", "Adding Page 0x2A (MM Capabilities)");
            ModePage0x2AData page2A_data; // From usbcdgadget.h
            memset(&page2A_data, 0, sizeof(page2A_data));
            page2A_data.pageCodeAndPS = 0x2A;
            page2A_data.pageLength = sizeof(ModePage0x2AData) - 2;
            page2A_data.capabilityBits[0] = 0x01;
            page2A_data.capabilityBits[2] = 0x01;
            page2A_data.capabilityBits[3] = 0x03;
            page2A_data.capabilityBits[4] = 0x28;
            page2A_data.maxSpeed = htons(706 * 4);
            page2A_data.numVolumeLevels = htons(0x00FF);
            page2A_data.currentSpeed = htons(706 * 4);
            copy_mode_page(&page2A_data, sizeof(page2A_data));
            page_found = true;
            if (page_code != 0x3F) goto send_response_ms10;
        }
        if (page_code == 0x0E || page_code == 0x3F) { // CD Audio Control
            MLOGDEBUG("ScsiCmdModeSense10", "Adding Page 0x0E (CD Audio Control)");
            ModePage0x0EData page0E_data; // From usbcdgadget.h
            memset(&page0E_data, 0, sizeof(page0E_data));
            page0E_data.pageCodeAndPS = 0x0E;
            page0E_data.pageLength = sizeof(ModePage0x0EData) - 2;
            page0E_data.IMMEDAndSOTC = 0x04;
            page0E_data.CDDAOutput0Select = 0x01;
            page0E_data.CDDAOutput1Select = 0x02;
            u8 volume = 0xFF;
            CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
            if (cdplayer) { /* volume = cdplayer->GetVolume(); */ }
            page0E_data.Output0Volume = volume;
            page0E_data.Output1Volume = volume;
            copy_mode_page(&page0E_data, sizeof(page0E_data));
            page_found = true;
            if (page_code != 0x3F) goto send_response_ms10;
        }

        if (!page_found && page_code != 0x3F) {
            MLOGERR("ScsiCmdModeSense10", "Unsupported page code: 0x%02X", page_code);
            gadget->SetSenseParameters(0x05, 0x24, 0x00);
            csw_status = CD_CSW_STATUS_FAIL;
        }
    }

send_response_ms10:
    if (csw_status == CD_CSW_STATUS_OK) {
        // Mode Data Length (bytes 0-1) = total length of mode pages + block descriptors (if any)
        // current_data_idx includes header size. So, (current_data_idx - sizeof(header_10byte))
        header_10byte.modeDataLength = htons(current_data_idx - sizeof(ModeSense10Header));
        memcpy(buffer, &header_10byte, sizeof(ModeSense10Header));

        int final_length_to_send = current_data_idx;
        if (allocation_length < final_length_to_send) {
            final_length_to_send = allocation_length;
        }

        this->begin_data_in_transfer(gadget, buffer, final_length_to_send);
        gadget->m_nnumber_blocks = 0;
        gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    }

    send_csw(gadget, csw_status);
    gadget->m_pCurrentCommandHandler = nullptr;
}
