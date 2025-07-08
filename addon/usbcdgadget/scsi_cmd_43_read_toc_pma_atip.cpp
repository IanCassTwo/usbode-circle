//
// scsi_cmd_43_read_toc_pma_atip.cpp
//
// Handler for SCSI command READ TOC/PMA/ATIP (0x43).
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
#include <usbcdgadget/scsi_cmd_43_read_toc_pma_atip.h>
#include <usbcdgadget/usbcdgadget.h> // For CUSBCDGadget, TUSBTOCData, TUSBTOCEntry, etc.
#include <circle/logger.h>          // For MLOGNOTE, MLOGDEBUG
#include <circle/util.h>            // For memcpy, htons
#include <cueparser/cueparser.h>    // For CUETrackInfo

ScsiCmdReadTocPmaAtip::ScsiCmdReadTocPmaAtip() {}
ScsiCmdReadTocPmaAtip::~ScsiCmdReadTocPmaAtip() {}

void ScsiCmdReadTocPmaAtip::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    if (!gadget->IsCDReady()) {
        MLOGERR("ScsiCmdReadTocPmaAtip", "READ TOC failed, CD not ready");
        gadget->SetSenseParameters(0x02, 0x04, 0x00); // NOT READY, LOGICAL UNIT NOT READY
        send_csw(gadget, CD_CSW_STATUS_FAIL);
        gadget->m_pCurrentCommandHandler = nullptr;
        return;
    }

    int msf = (cbw.CBWCB[1] >> 1) & 0x01;
    int format_field = cbw.CBWCB[2] & 0x0F; // MMC-6: bits 3-0 for format. Others are reserved or for ATIP.
                                         // Original used 0x07. Let's use 0x0F for full format field.
    int startingTrack = cbw.CBWCB[6];
    int allocationLength = (cbw.CBWCB[7] << 8) | cbw.CBWCB[8];

    MLOGNOTE("ScsiCmdReadTocPmaAtip", "Read TOC format: %d, MSF: %d, StartTrack: %d, AllocLen: %d",
             format_field, msf, startingTrack, allocationLength);

    // TUSBTOCData m_TOCData; // Local instance, not the member from gadget as it's small
    // The original m_TOCReply was a full structure including one track, which is not right for a header.
    TUSBTOCData toc_header_reply;
    TUSBTOCEntry* toc_entries_array = nullptr; // Using raw pointer, manage with new/delete

    int num_toc_entries_for_reply = 0; // Number of TUSBTOCEntry structs we will actually populate
    int total_data_bytes_to_send = 0;    // Total size of (TOC Header + all TOC Entries)

    // Format 0x00: Normal TOC
    // Format 0x01: Session Info (returns first track of first session, last track of last session, leadout of last session)
    // Format 0x02: Full TOC (includes lead-in, all tracks, lead-out)
    // Format 0x03: PMA (Program Memory Area) - Not typically emulated for CD-ROM
    // Format 0x04: ATIP (Absolute Time In Pre-Groove) - Not typically emulated for CD-ROM
    // Others are reserved or obsolete.
    // The original code mostly implements format 0x00 and a variant for format 0x01 (Session Info).

    if (format_field == 0x00 || format_field == 0x01) { // Handle format 00 (standard TOC) and 01 (Session Info - simplified)
        const CUETrackInfo* trackInfo = nullptr;
        int lastTrackNumberOnDisc = gadget->GetLastTrackNumber();
        int firstTrackNumberOnDisc = 1; // Assuming tracks start at 1

        toc_header_reply.FirstTrack = firstTrackNumberOnDisc; // For format 00, this is first track #
                                                              // For format 01, this is first session # (always 1 for us)
        toc_header_reply.LastTrack = lastTrackNumberOnDisc;   // For format 00, this is last track #
                                                              // For format 01, this is last session # (always 1 for us)

        total_data_bytes_to_send = SIZE_TOC_DATA; // Start with header size

        // Determine how many entries we might need.
        // For format 00: (lastTrack - startingTrack + 1) for user tracks + 1 for Lead-out.
        // For format 01: 1 entry for the first track of the session. (Simplified from spec which is more complex)
        int max_possible_entries = 0;
        if (format_field == 0x00) {
             max_possible_entries = (lastTrackNumberOnDisc - (startingTrack == 0xAA ? lastTrackNumberOnDisc : startingTrack) + 1) + 1; // Tracks + Leadout
             if (startingTrack == 0) startingTrack = 1; // SCSI spec: track 0 means start from first track
        } else { // format_field == 0x01
             max_possible_entries = 1; // Only first track of session
        }
        if (max_possible_entries <=0) max_possible_entries = 1; // At least for leadout or single session track

        toc_entries_array = new (std::nothrow) TUSBTOCEntry[max_possible_entries];
        if (!toc_entries_array) {
            MLOGERR("ScsiCmdReadTocPmaAtip", "Failed to allocate memory for TOC entries");
            gadget->SetSenseParameters(0x04, 0x00, 0x00); // HARDWARE ERROR (generic)
            send_csw(gadget, CD_CSW_STATUS_FAIL);
            gadget->m_pCurrentCommandHandler = nullptr;
            return;
        }

        int current_entry_idx = 0;

        if (format_field == 0x00) {
            if (startingTrack != 0xAA) { // If not asking only for lead-out
                gadget->cueParser.restart();
                while ((trackInfo = gadget->cueParser.next_track()) != nullptr) {
                    if (trackInfo->track_number < startingTrack) continue;
                    if (current_entry_idx >= max_possible_entries -1) break; // -1 to save space for leadout

                    toc_entries_array[current_entry_idx].ADR_Control = (trackInfo->track_mode == CUETrack_AUDIO) ? 0x10 : 0x14;
                    toc_entries_array[current_entry_idx].TrackNumber = trackInfo->track_number;
                    toc_entries_array[current_entry_idx].address = gadget->GetAddress(trackInfo->track_start, msf);
                    total_data_bytes_to_send += SIZE_TOC_ENTRY;
                    num_toc_entries_for_reply++;
                    current_entry_idx++;
                }
            }
            // Add Lead-Out
            if (current_entry_idx < max_possible_entries) {
                 u32 leadOutLBA = gadget->GetLeadoutLBA();
                 toc_entries_array[current_entry_idx].ADR_Control = 0x10; // Typically same as last audio track type or data
                 toc_entries_array[current_entry_idx].TrackNumber = 0xAA; // Lead-out identifier
                 toc_entries_array[current_entry_idx].address = gadget->GetAddress(leadOutLBA, msf);
                 total_data_bytes_to_send += SIZE_TOC_ENTRY;
                 num_toc_entries_for_reply++;
                 // current_entry_idx++; // Not strictly needed after this
            }
        } else { // format_field == 0x01 (Session Info - simplified to first track of first session)
            CUETrackInfo firstSessionTrackInfo = gadget->GetTrackInfoForTrack(1); // Assuming session 1 starts with track 1
            if (firstSessionTrackInfo.track_number != -1 && current_entry_idx < max_possible_entries) {
                 toc_entries_array[current_entry_idx].ADR_Control = (firstSessionTrackInfo.track_mode == CUETrack_AUDIO) ? 0x10 : 0x14;
                 toc_entries_array[current_entry_idx].TrackNumber = firstSessionTrackInfo.track_number; // Report actual track number
                 toc_entries_array[current_entry_idx].address = gadget->GetAddress(firstSessionTrackInfo.track_start, msf);
                 total_data_bytes_to_send += SIZE_TOC_ENTRY;
                 num_toc_entries_for_reply++;
                 // current_entry_idx++;
            }
        }
    } else { // Unsupported format
        MLOGERR("ScsiCmdReadTocPmaAtip", "Read TOC unsupported format %d", format_field);
        gadget->SetSenseParameters(0x05, 0x24, 0x00); // INVALID FIELD IN CDB
        send_csw(gadget, CD_CSW_STATUS_FAIL);
        gadget->m_pCurrentCommandHandler = nullptr;
        return;
    }

    toc_header_reply.DataLength = htons(total_data_bytes_to_send - 2); // Length of data following this field itself

    // Copy header to buffer
    memcpy(gadget->GetInBuffer(), &toc_header_reply, SIZE_TOC_DATA);
    // Copy entries to buffer
    if (num_toc_entries_for_reply > 0 && toc_entries_array != nullptr) {
        memcpy(gadget->GetInBuffer() + SIZE_TOC_DATA, toc_entries_array, num_toc_entries_for_reply * SIZE_TOC_ENTRY);
    }

    delete[] toc_entries_array; // Clean up allocated memory

    // Trim to allocationLength
    if (allocationLength < total_data_bytes_to_send) {
        total_data_bytes_to_send = allocationLength;
    }

    gadget->m_nnumber_blocks = 0; // Single data transfer
    this->begin_data_in_transfer(gadget, gadget->GetInBuffer(), total_data_bytes_to_send);
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = gadget->GetCurrentCSWStatus(); // Usually OK

    gadget->m_pCurrentCommandHandler = nullptr;
}
