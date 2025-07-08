//
// scsi_cmd_55_mode_select10.cpp
//
// Handler for SCSI command MODE SELECT (10) (0x55).
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
#include <usbcdgadget/scsi_cmd_55_mode_select10.h>
#include <usbcdgadget/usbcdgadget.h> // For CUSBCDGadget, ModePage0x0EData, etc.
#include <cdplayer/cdplayer.h>       // For CCDPlayer
#include <circle/sched/scheduler.h>  // For CScheduler
#include <circle/logger.h>           // For MLOGNOTE, MLOGDEBUG
#include <circle/util.h>             // For memcpy

ScsiCmdModeSelect10::ScsiCmdModeSelect10() {}
ScsiCmdModeSelect10::~ScsiCmdModeSelect10() {}

void ScsiCmdModeSelect10::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    u16 parameter_list_length = (cbw.CBWCB[7] << 8) | cbw.CBWCB[8];
    // u8 pf_bit = (cbw.CBWCB[1] >> 4) & 0x01; // Page Format bit
    // u8 sp_bit = cbw.CBWCB[1] & 0x01;       // Save Pages bit

    MLOGDEBUG("ScsiCmdModeSelect10", "MODE SELECT (10) (0x55), ParamListLen: %u", parameter_list_length);

    if (parameter_list_length == 0) {
        // No data to transfer, command is essentially a no-op.
        send_csw(gadget, CD_CSW_STATUS_OK);
        gadget->m_pCurrentCommandHandler = nullptr;
        return;
    }

    if (parameter_list_length > CUSBCDGadget::MaxOutMessageSize) {
         MLOGERR("ScsiCmdModeSelect10", "Parameter list length %u exceeds buffer size %u",
                parameter_list_length, CUSBCDGadget::MaxOutMessageSize);
        gadget->SetSenseParameters(0x05, 0x1A, 0x00); // ILLEGAL REQUEST, PARAMETER LIST LENGTH ERROR
        send_csw(gadget, CD_CSW_STATUS_FAIL);
        gadget->m_pCurrentCommandHandler = nullptr;
        return;
    }

    // Set up to receive data from the host.
    gadget->m_nState = CUSBCDGadget::TCDState::DataOut;
    gadget->m_pEP[CUSBCDGadget::EPOut]->BeginTransfer(
        CUSBCDGadgetEndpoint::TransferDataOut, // This enum member might need to be public or accessed via a gadget method
        gadget->GetOutBuffer(),
        parameter_list_length
    );

    // The gadget->m_CSW.bmCSWStatus will be set by process_received_data after data is processed.
    // It's important that m_pCurrentCommandHandler remains 'this' until process_received_data is called.
    gadget->m_pCurrentCommandHandler = this;
}

void ScsiCmdModeSelect10::process_received_data(CUSBCDGadget* gadget, size_t received_length) {
    MLOGDEBUG("ScsiCmdModeSelect10", "process_received_data, length: %u", received_length);
    u8* out_buffer = gadget->GetOutBuffer();

    // Basic validation: Mode Parameter Header (10) is 8 bytes. Block Descriptors not supported.
    // Mode Parameter Header (10) structure:
    // Byte 0-1: Mode Data Length (N-6) - not used by us
    // Byte 2: Medium Type - not used by us
    // Byte 3: Device-Specific Parameter - not used by us
    // Byte 4-5: Reserved
    // Byte 6-7: Block Descriptor Length (shall be 0000h if no block descriptors)

    if (received_length < 8) { // Minimum length for Mode Parameter Header (10)
        MLOGERR("ScsiCmdModeSelect10", "Received data length %u too short for Mode Parameter Header (10).", received_length);
        gadget->SetSenseParameters(0x05, 0x1A, 0x00); // PARAMETER LIST LENGTH ERROR
        send_csw(gadget, CD_CSW_STATUS_FAIL);
        gadget->m_pCurrentCommandHandler = nullptr;
        return;
    }

    u16 block_descriptor_length = (out_buffer[6] << 8) | out_buffer[7];
    if (block_descriptor_length != 0) {
        MLOGWARN("ScsiCmdModeSelect10", "Block descriptors received (len %u), but not supported.", block_descriptor_length);
        // Depending on strictness, this could be an error. For now, ignore them and process pages.
    }

    // Pointer to the first mode page, after the header and any block descriptors
    u8* mode_page_ptr = out_buffer + 8 + block_descriptor_length;
    size_t remaining_length = received_length - (8 + block_descriptor_length);

    // Loop through mode pages if multiple are present (though typically one per command)
    // For now, assuming only one page is sent, or we only care about the first one.
    // A full implementation would parse page_code and page_length to step through multiple pages.

    if (remaining_length > 0) { // Check if there's any mode page data
        u8 page_code = mode_page_ptr[0] & 0x3F; // Mask out PS and SPF bits
        // u8 page_length = mode_page_ptr[1]; // Length of this mode page's parameters

        MLOGDEBUG("ScsiCmdModeSelect10", "Processing Page Code: 0x%02X", page_code);

        switch (page_code) {
            case 0x0E: { // CD Audio Control Mode Page
                if (remaining_length >= sizeof(ModePage0x0EData)) { // Ensure enough data for this page
                    ModePage0x0EData* audio_page = reinterpret_cast<ModePage0x0EData*>(mode_page_ptr);
                    MLOGNOTE("ScsiCmdModeSelect10", "Mode Select (10) for Audio Control Page (0x0E), Channel0 Vol: %u, Channel1 Vol: %u",
                             audio_page->Output0Volume, audio_page->Output1Volume);

                    CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
                    if (cdplayer) {
                        // Original logic to pick minimum of the two volumes due to game behavior (Descent 2)
                        u8 volume_to_set = audio_page->Output0Volume < audio_page->Output1Volume ?
                                           audio_page->Output0Volume : audio_page->Output1Volume;
                        cdplayer->SetVolume(volume_to_set);
                        MLOGDEBUG("ScsiCmdModeSelect10", "CDPlayer volume set to %u", volume_to_set);
                    } else {
                        MLOGWARN("ScsiCmdModeSelect10", "CCDPlayer task not found for setting volume.");
                    }
                } else {
                     MLOGERR("ScsiCmdModeSelect10", "Data for Page 0x0E too short: %u bytes.", remaining_length);
                     // Error: Not enough data for the page.
                }
                break;
            }
            // Add other mode pages here if supported (e.g., 0x01 Error Recovery, 0x1A Power Condition)
            default:
                MLOGWARN("ScsiCmdModeSelect10", "Unsupported Mode Page Code: 0x%02X", page_code);
                // Behavior for unsupported pages: could be an error or ignored.
                // SCSI spec often says to return CHECK CONDITION with INVALID FIELD IN PARAMETER LIST.
                // For now, let's assume OK if we don't recognize it, to match original simple handling.
                break;
        }
    } else {
        MLOGNOTE("ScsiCmdModeSelect10", "No mode page data found after header/block descriptors.");
    }

    // If no specific error occurred during processing of known pages:
    send_csw(gadget, CD_CSW_STATUS_OK); // bmCSWStatus was likely OK from handle_command
    gadget->m_pCurrentCommandHandler = nullptr; // Done with this command
}
