//
// scsi_cmd_BE_read_cd.cpp
//
// Handler for SCSI command READ CD (0xBE).
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
#include <usbcdgadget/scsi_cmd_BE_read_cd.h>
#include <usbcdgadget/usbcdgadget.h> // For CUSBCDGadget members, constants, ICueDevice etc.
#include <circle/logger.h>          // For MLOGNOTE, MLOGERR, MLOGDEBUG
#include <circle/util.h>            // For memcpy
#include <cueparser/cueparser.h>    // For CUETrackInfo

ScsiCmdReadCD::ScsiCmdReadCD() {}
ScsiCmdReadCD::~ScsiCmdReadCD() {}

void ScsiCmdReadCD::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    MLOGDEBUG("ScsiCmdReadCD", "handle_command READ CD (0xBE)");

    if (!gadget->IsCDReady()) {
        MLOGERR("ScsiCmdReadCD", "READ CD failed, CD not ready");
        gadget->SetSenseParameters(0x02, 0x04, 0x00); // NOT READY, LOGICAL UNIT NOT READY
        send_csw(gadget, CD_CSW_STATUS_FAIL);
        gadget->m_pCurrentCommandHandler = nullptr;
        return;
    }

    gadget->m_CSW.bmCSWStatus = gadget->GetCurrentCSWStatus(); // Usually OK

    int expectedSectorType = (cbw.CBWCB[1] >> 2) & 0x07;
    gadget->m_nblock_address = (u32)(cbw.CBWCB[2] << 24) | (u32)(cbw.CBWCB[3] << 16) | (u32)(cbw.CBWCB[4] << 8) | cbw.CBWCB[5];
    gadget->m_nnumber_blocks = (u32)(cbw.CBWCB[6] << 16) | (u32)((cbw.CBWCB[7] << 8) | cbw.CBWCB[8]);
    gadget->mcs = (cbw.CBWCB[9] >> 3) & 0x1F; // Main Channel Selection bits

    switch (expectedSectorType) {
        case 0x01: // CD-DA
            gadget->block_size = 2352;
            gadget->transfer_block_size = 2352;
            gadget->skip_bytes = 0;
            break;
        case 0x02: // Mode 1
        {
            CUETrackInfo trackInfo = gadget->GetTrackInfoForLBA(gadget->m_nblock_address);
            gadget->skip_bytes = gadget->GetSkipbytesForTrack(trackInfo);
            gadget->block_size = gadget->GetBlocksizeForTrack(trackInfo);
            gadget->transfer_block_size = 2048;
            break;
        }
        case 0x03: // Mode 2 formless
            gadget->skip_bytes = 16; // Typically, header (4) + subheader (8) + user data (2324) + EDC (4) = 2352. Skip first 16 (Sync+Header) to get 2336.
                                     // Or if it means Mode2/XA raw P/Q channels (2352), skip_bytes might be 0 and transfer_block_size 2352.
                                     // Original code: skip_bytes = 16, block_size = 2352, transfer_block_size = 2336.
            gadget->block_size = 2352;
            gadget->transfer_block_size = 2336;
            break;
        case 0x04: // Mode 2 form 1
        {
            CUETrackInfo trackInfo = gadget->GetTrackInfoForLBA(gadget->m_nblock_address);
            gadget->skip_bytes = gadget->GetSkipbytesForTrack(trackInfo); // Should be 24 for Mode2/2352 (sync+header+subheader)
            gadget->block_size = gadget->GetBlocksizeForTrack(trackInfo); // Should be 2352
            gadget->transfer_block_size = 2048; // User data part
            break;
        }
        case 0x05: // Mode 2 form 2
        {
            // CUETrackInfo trackInfo = gadget->GetTrackInfoForLBA(gadget->m_nblock_address);
            // gadget->skip_bytes = gadget->GetSkipbytesForTrack(trackInfo); // Should be 24
            // gadget->block_size = gadget->GetBlocksizeForTrack(trackInfo); // Should be 2352
            // gadget->transfer_block_size = 2328; // User data of M2F2 (no EDC/ECC)
            // Original: block_size = 2352, skip_bytes = 24, transfer_block_size = 2048 (this seems wrong for M2F2, 2048 is M1 or M2F1)
            // Let's assume the original logic for transfer_block_size was based on what hosts expect for this type.
            // However, typical M2F2 user data is 2324 or 2328 (if subheader included).
            // Sticking to original for now:
            gadget->block_size = 2352;
            gadget->skip_bytes = 24;
            gadget->transfer_block_size = 2048; // This implies it's treated like Mode 2 Form 1 for data retrieval.
            break;
        }
        case 0x00: // Any type (based on MCS)
        default:
        {
            CUETrackInfo trackInfo = gadget->GetTrackInfoForLBA(gadget->m_nblock_address);
            if (trackInfo.track_number != -1 && trackInfo.track_mode == CUETrack_AUDIO) {
                gadget->block_size = 2352;
                gadget->transfer_block_size = 2352;
                gadget->skip_bytes = 0;
            } else {
                gadget->block_size = gadget->GetBlocksizeForTrack(trackInfo); // Underlying image block size
                gadget->transfer_block_size = gadget->GetSectorLengthFromMCS(gadget->mcs);
                gadget->skip_bytes = gadget->GetSkipBytesFromMCS(gadget->mcs);
            }
            break;
        }
    }

    MLOGDEBUG("ScsiCmdReadCD", "READ CD for %lu blocks at LBA %lu, EST: %02x, MCS: %02x => dev_bs: %d, dev_skip: %d, usb_xfer_bs: %d",
              gadget->m_nnumber_blocks, gadget->m_nblock_address, expectedSectorType, gadget->mcs,
              gadget->block_size, gadget->skip_bytes, gadget->transfer_block_size);

    gadget->m_nbyteCount = cbw.dCBWDataTransferLength;
    if (gadget->m_nnumber_blocks == 0 && gadget->m_nbyteCount > 0) {
        MLOGNOTE("ScsiCmdReadCD", "READ CD with 0 blocks, deriving from length %u", gadget->m_nbyteCount);
        if (gadget->transfer_block_size > 0) {
            gadget->m_nnumber_blocks = (gadget->m_nbyteCount + gadget->transfer_block_size -1) / gadget->transfer_block_size;
        } else { // Avoid division by zero if transfer_block_size ended up as 0
             MLOGERR("ScsiCmdReadCD", "Error: transfer_block_size is 0, cannot derive block count.");
             gadget->SetSenseParameters(0x05, 0x24, 0x00); // ILLEGAL REQUEST, INVALID FIELD IN CDB
             send_csw(gadget, CD_CSW_STATUS_FAIL);
             gadget->m_pCurrentCommandHandler = nullptr;
             return;
        }
    }

    if (gadget->m_nnumber_blocks == 0) {
        MLOGNOTE("ScsiCmdReadCD", "READ CD with 0 blocks to transfer.");
        gadget->m_CSW.dCSWDataResidue = cbw.dCBWDataTransferLength;
        send_csw(gadget, CD_CSW_STATUS_OK);
        gadget->m_pCurrentCommandHandler = nullptr;
    } else {
        gadget->m_nState = CUSBCDGadget::TCDState::DataInRead;
        gadget->m_pCurrentCommandHandler = this;
    }
}

void ScsiCmdReadCD::update(CUSBCDGadget* gadget) {
    if (gadget->m_nState != CUSBCDGadget::TCDState::DataInRead) return;
    MLOGDEBUG("ScsiCmdReadCD", "update() LBA %u, num_blocks %u", gadget->m_nblock_address, gadget->m_nnumber_blocks);

    if (!gadget->IsCDReady()) {
        MLOGERR("ScsiCmdReadCD::update", "CD not ready");
        gadget->SetSenseParameters(0x02, 0x04, 0x00);
        send_csw(gadget, CD_CSW_STATUS_FAIL);
        gadget->m_nState = CUSBCDGadget::TCDState::ReceiveCBW;
        gadget->m_pCurrentCommandHandler = nullptr;
        return;
    }

    if (gadget->m_nnumber_blocks == 0) {
        MLOGDEBUG("ScsiCmdReadCD::update", "All blocks transferred.");
        gadget->m_CSW.dCSWDataResidue = gadget->m_nbyteCount; // Remaining bytes from original request
        send_csw(gadget, CD_CSW_STATUS_OK);
        gadget->m_pCurrentCommandHandler = nullptr;
        return;
    }

    u64 file_offset = (u64)gadget->block_size * gadget->m_nblock_address; // Use device's actual block size for seek
    u64 actual_seek_pos = gadget->m_pDevice->Seek(file_offset);

    if (actual_seek_pos == (u64)(-1)) {
        MLOGERR("ScsiCmdReadCD::update", "Seek failed for LBA %u (offset %llu)", gadget->m_nblock_address, file_offset);
        gadget->SetSenseParameters(0x03, 0x11, 0x00); // MEDIUM ERROR, UNRECOVERED READ ERROR
        send_csw(gadget, CD_CSW_STATUS_FAIL);
        gadget->m_nState = CUSBCDGadget::TCDState::ReceiveCBW;
        gadget->m_pCurrentCommandHandler = nullptr;
        return;
    }

    u32 blocks_to_read_this_iteration = gadget->m_nnumber_blocks;
    if (blocks_to_read_this_iteration > CUSBCDGadget::MaxBlocksToRead) {
        blocks_to_read_this_iteration = CUSBCDGadget::MaxBlocksToRead;
    }

    // Bytes to read from physical device based on its block_size
    u32 bytes_to_read_from_device = blocks_to_read_this_iteration * gadget->block_size;
    int bytes_read = gadget->m_pDevice->Read(gadget->GetFileChunkBuffer(), bytes_to_read_from_device);

    if (bytes_read <= 0 || (u32)bytes_read < bytes_to_read_from_device) {
        MLOGERR("ScsiCmdReadCD::update", "Read error/short read (%d bytes) for LBA %u, expected %u", bytes_read, gadget->m_nblock_address, bytes_to_read_from_device);
        gadget->SetSenseParameters(0x03, 0x11, 0x00); // MEDIUM ERROR, UNRECOVERED READ ERROR
        send_csw(gadget, CD_CSW_STATUS_FAIL);
        gadget->m_nState = CUSBCDGadget::TCDState::ReceiveCBW;
        gadget->m_pCurrentCommandHandler = nullptr;
        return;
    }

    u8* current_in_buffer_pos = gadget->GetInBuffer();
    u32 total_bytes_for_usb_transfer = 0;

    for (u32 i = 0; i < blocks_to_read_this_iteration; ++i) {
        u8* source_block_in_filechunk = gadget->GetFileChunkBuffer() + (i * gadget->block_size);

        if (gadget->transfer_block_size > gadget->block_size) { // Need to synthesize (e.g. raw 2048 to full 2352)
            u8 synthesized_sector[2352]; // Max possible sector size
            memset(synthesized_sector, 0, sizeof(synthesized_sector)); // Zero out for safety

            int synth_offset = 0;
            if (gadget->mcs & 0x10) { // SYNC
                synthesized_sector[synth_offset] = 0x00;
                memset(synthesized_sector + synth_offset + 1, 0xFF, 10);
                synthesized_sector[synth_offset + 11] = 0x00;
                synth_offset += 12;
            }
            if (gadget->mcs & 0x08) { // HEADER (MSF + Mode)
                u32 current_lba_for_header = gadget->m_nblock_address + i + 150; // MSF offset
                synthesized_sector[synth_offset + 0] = current_lba_for_header / (75 * 60); // M
                synthesized_sector[synth_offset + 1] = (current_lba_for_header / 75) % 60; // S
                synthesized_sector[synth_offset + 2] = current_lba_for_header % 75;        // F
                CUETrackInfo trackInfo = gadget->GetTrackInfoForLBA(gadget->m_nblock_address + i);
                // Mode byte needs to reflect actual data mode (e.g. 0x01 for Mode1, 0x02 for Mode2)
                // This simplified version uses 0x01, may need refinement based on trackInfo.track_mode
                synthesized_sector[synth_offset + 3] = (trackInfo.track_mode == CUETrack_MODE1_2048 || trackInfo.track_mode == CUETrack_MODE1_2352) ? 0x01 : 0x02;
                synth_offset += 4;
            }
            // Subheader would go here if MCS requested it and it's Mode 2
            // For now, assume user data is next if requested.

            if (gadget->mcs & 0x04) { // USER DATA
                // Copy from source_block_in_filechunk, assuming it's user data (e.g. 2048 bytes)
                // The skip_bytes on the source_block_in_filechunk should have already positioned it correctly
                // if the underlying file format itself has headers (e.g. Mode1/2352 bin files).
                // Here, we are constructing the *output* sector.
                memcpy(synthesized_sector + synth_offset, source_block_in_filechunk + gadget->skip_bytes, gadget->block_size - gadget->skip_bytes); // Copy actual user data portion
                // This assumes gadget->block_size - gadget->skip_bytes is the user data size (e.g. 2048)
                synth_offset += (gadget->block_size - gadget->skip_bytes);
            }
            // EDC/ECC would go here if (mcs & 0x02) and not already part of user data. Typically zeroed if synthesized.
            // The total length copied to USB will be gadget->transfer_block_size.
            // We need to ensure `synthesized_sector` is correctly filled up to `gadget->transfer_block_size`.
            // The `gadget->skip_bytes` for GetSectorLengthFromMCS is for *output* skipping.

            memcpy(current_in_buffer_pos, synthesized_sector + gadget->GetSkipBytesFromMCS(gadget->mcs), gadget->transfer_block_size);

        } else { // Standard case: transfer_block_size <= block_size
            memcpy(current_in_buffer_pos, source_block_in_filechunk + gadget->skip_bytes, gadget->transfer_block_size);
        }
        current_in_buffer_pos += gadget->transfer_block_size;
        total_bytes_for_usb_transfer += gadget->transfer_block_size;
    }

    MLOGDEBUG("ScsiCmdReadCD::update", "Prepared %u bytes for USB transfer.", total_bytes_for_usb_transfer);

    gadget->m_nblock_address += blocks_to_read_this_iteration;
    gadget->m_nnumber_blocks -= blocks_to_read_this_iteration;
    gadget->m_nbyteCount -= total_bytes_for_usb_transfer;

    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    this->begin_data_in_transfer(gadget, gadget->GetInBuffer(), total_bytes_for_usb_transfer);

    // Logic for m_pCurrentCommandHandler = nullptr will be handled by the next update call if m_nnumber_blocks is 0.
}
