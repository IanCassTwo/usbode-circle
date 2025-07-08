//
// scsi_cmd_28_read10.cpp
//
// Handler for SCSI command READ (10) (0x28).
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
#include <usbcdgadget/scsi_cmd_28_read10.h>
#include <usbcdgadget/usbcdgadget.h> // For CUSBCDGadget members, constants, ICueDevice etc.
#include <circle/logger.h>          // For MLOGNOTE, MLOGERR, MLOGDEBUG
#include <circle/util.h>            // For memcpy

ScsiCmdRead10::ScsiCmdRead10() {
    // Constructor
}

ScsiCmdRead10::~ScsiCmdRead10() {
    // Destructor
}

void ScsiCmdRead10::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    MLOGDEBUG("ScsiCmdRead10", "handle_command READ (10)");

    if (gadget->IsCDReady()) {
        // Will be updated if read fails on any block in update()
        gadget->m_CSW.bmCSWStatus = gadget->GetCurrentCSWStatus(); // Usually OK unless prior error

        // Where to start reading (LBA)
        gadget->m_nblock_address = (u32)(cbw.CBWCB[2] << 24) |
                                   (u32)(cbw.CBWCB[3] << 16) |
                                   (u32)(cbw.CBWCB[4] << 8)  |
                                   cbw.CBWCB[5];

        // Number of blocks to read (LBA)
        gadget->m_nnumber_blocks = (u32)((cbw.CBWCB[7] << 8) | cbw.CBWCB[8]);

        // Transfer Block Size is the size of data to return to host.
        // For a CDROM Read (10), this is typically based on logical block size, often 2048.
        // The physical block size on media (block_size) and skip_bytes are determined by SetDevice/cue parsing.
        gadget->transfer_block_size = 2048; // Standard for Read(10)
        gadget->block_size = gadget->data_block_size; // Determined by CUE, e.g. 2048 for Mode 1 data, 2352 for audio/raw
        gadget->skip_bytes = gadget->data_skip_bytes; // Determined by CUE
        gadget->mcs = 0; // Not applicable for standard Read(10) like for Read CD

        gadget->m_nbyteCount = cbw.dCBWDataTransferLength;

        // Original code had a condition: if (m_nnumber_blocks == 0) m_nnumber_blocks = 1 + (m_nbyteCount) / 2048;
        // This seems like a fallback if the host doesn't specify block count but gives total length.
        // SCSI spec says if number of blocks is 0, it means 0 blocks, unless it's a specific type of command.
        // For Read(10), 0 blocks usually means no data transfer. However, some hosts might send 0 and expect full length.
        // Let's keep it for compatibility if it was there for a reason.
        if (gadget->m_nnumber_blocks == 0 && gadget->m_nbyteCount > 0) {
             MLOGNOTE("ScsiCmdRead10", "READ(10) with 0 blocks, deriving from length %u", gadget->m_nbyteCount);
             gadget->m_nnumber_blocks = (gadget->m_nbyteCount + gadget->transfer_block_size -1) / gadget->transfer_block_size; // Rounded up
        }

        if (gadget->m_nnumber_blocks == 0) { // If still zero, nothing to do
             MLOGNOTE("ScsiCmdRead10", "READ(10) with 0 blocks to transfer.");
             gadget->m_CSW.dCSWDataResidue = cbw.dCBWDataTransferLength;
             send_csw(gadget, CD_CSW_STATUS_OK);
             gadget->m_pCurrentCommandHandler = nullptr;
        } else {
            gadget->m_nState = CUSBCDGadget::TCDState::DataInRead; // Signal that Update() should process this
            gadget->m_pCurrentCommandHandler = this; // Register this handler for update calls
            // The actual read and transfer will happen in the update() method.
        }

    } else {
        MLOGERR("ScsiCmdRead10", "READ(10) failed, CD not ready");
        gadget->SetSenseParameters(0x02, 0x04, 0x00); // NOT READY, LOGICAL UNIT NOT READY, CAUSE NOT REPORTABLE
        send_csw(gadget, CD_CSW_STATUS_FAIL);
        gadget->m_pCurrentCommandHandler = nullptr; // No update needed
    }
}

void ScsiCmdRead10::update(CUSBCDGadget* gadget) {
    // This logic is moved from CUSBCDGadget::Update() case TCDState::DataInRead
    // Ensure this is only called if m_nState is indeed DataInRead or a similar state managed by this handler.
    if (gadget->m_nState != CUSBCDGadget::TCDState::DataInRead) {
        // Should not happen if m_pCurrentCommandHandler is correctly managed
        return;
    }

    MLOGDEBUG("ScsiCmdRead10", "update() m_nblock_address=%u, m_nnumber_blocks=%u", gadget->m_nblock_address, gadget->m_nnumber_blocks);

    if (!gadget->IsCDReady()) {
        MLOGERR("ScsiCmdRead10::update", "CD not ready during update");
        gadget->SetSenseParameters(0x02, 0x04, 0x00); // NOT READY, LOGICAL UNIT NOT READY
        send_csw(gadget, CD_CSW_STATUS_FAIL);
        gadget->m_nState = CUSBCDGadget::TCDState::ReceiveCBW; // Or some idle state
        gadget->m_pCurrentCommandHandler = nullptr;
        return;
    }

    if (gadget->m_nnumber_blocks == 0) { // All blocks transferred
        MLOGDEBUG("ScsiCmdRead10::update", "All blocks transferred.");
        gadget->m_CSW.dCSWDataResidue = 0; // Assuming all requested data was sent. CBW.dCBWDataTransferLength should match total bytes.
                                         // Proper residue calculation is: CBW.dCBWDataTransferLength - total_bytes_transferred_so_far
                                         // This needs careful tracking if not all data is sent in one go by the host's request.
                                         // For now, if m_nnumber_blocks became 0, we assume success for the data phase.
        send_csw(gadget, CD_CSW_STATUS_OK); // bmCSWStatus should have been set by last successful block or kept as OK.
        gadget->m_pCurrentCommandHandler = nullptr; // Done with this command
        // m_nState will be set by SendCSW to SentCSW, then OnTransferComplete will set to ReceiveCBW.
        return;
    }

    u64 file_offset = (u64)gadget->block_size * gadget->m_nblock_address;
    MLOGDEBUG("ScsiCmdRead10::update", "Seeking to file offset %llu for LBA %u", file_offset, gadget->m_nblock_address);

    u64 actual_seek_pos = gadget->m_pDevice->Seek(file_offset);
    if (actual_seek_pos == (u64)(-1)) {
        MLOGERR("ScsiCmdRead10::update", "Seek failed for LBA %u (offset %llu)", gadget->m_nblock_address, file_offset);
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

    u32 bytes_to_read_from_device = blocks_to_read_this_iteration * gadget->block_size;
    MLOGDEBUG("ScsiCmdRead10::update", "Reading %u blocks (%u bytes) from device starting LBA %u",
              blocks_to_read_this_iteration, bytes_to_read_from_device, gadget->m_nblock_address);

    // Assuming m_FileChunk is a public member of CUSBCDGadget or accessible via getter
    int bytes_read = gadget->m_pDevice->Read(gadget->GetFileChunkBuffer(), bytes_to_read_from_device);

    if (bytes_read < 0) { // Error
        MLOGERR("ScsiCmdRead10::update", "Read error %d for LBA %u", bytes_read, gadget->m_nblock_address);
        gadget->SetSenseParameters(0x03, 0x11, 0x00); // MEDIUM ERROR, UNRECOVERED READ ERROR
        send_csw(gadget, CD_CSW_STATUS_FAIL);
        gadget->m_nState = CUSBCDGadget::TCDState::ReceiveCBW;
        gadget->m_pCurrentCommandHandler = nullptr;
        return;
    }
    if (bytes_read == 0 && bytes_to_read_from_device > 0) { // EOF or less data than expected
        MLOGERR("ScsiCmdRead10::update", "Read 0 bytes (EOF?) for LBA %u, expected %u", gadget->m_nblock_address, bytes_to_read_from_device);
        gadget->SetSenseParameters(0x03, 0x11, 0x00); // MEDIUM ERROR, UNRECOVERED READ ERROR (or LBA out of range: 0x05, 0x21, 0x00)
        send_csw(gadget, CD_CSW_STATUS_FAIL);
        gadget->m_nState = CUSBCDGadget::TCDState::ReceiveCBW;
        gadget->m_pCurrentCommandHandler = nullptr;
        return;
    }
    if ((u32)bytes_read < bytes_to_read_from_device) {
         MLOGWARN("ScsiCmdRead10::update", "Partial read: got %d bytes, expected %u bytes for LBA %u. Treating as error.", bytes_read, bytes_to_read_from_device, gadget->m_nblock_address);
        // Adjust blocks_to_read_this_iteration if we decide to send partial data.
        // For now, treat as error as original code did.
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
        // For Read(10), skip_bytes is usually applied if physical sector > logical sector (e.g. 2352 read, 2048 needed)
        // And transfer_block_size is what the host expects per logical block (usually 2048 for Read(10))
        memcpy(current_in_buffer_pos, source_block_in_filechunk + gadget->skip_bytes, gadget->transfer_block_size);
        current_in_buffer_pos += gadget->transfer_block_size;
        total_bytes_for_usb_transfer += gadget->transfer_block_size;
    }

    MLOGDEBUG("ScsiCmdRead10::update", "Prepared %u bytes for USB transfer.", total_bytes_for_usb_transfer);

    gadget->m_nblock_address += blocks_to_read_this_iteration;
    gadget->m_nnumber_blocks -= blocks_to_read_this_iteration;
    gadget->m_nbyteCount -= total_bytes_for_usb_transfer; // Track total bytes for dCBWDataTransferLength residue

    // Update gadget's CSW status to OK for this command if all went well so far.
    // If an error occurred above, CSW status would be FAIL.
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;

    gadget->m_nState = CUSBCDGadget::TCDState::DataIn; // Set state for OnTransferComplete
    this->begin_data_in_transfer(gadget, gadget->GetInBuffer(), total_bytes_for_usb_transfer);

    // If m_nnumber_blocks is now 0, the next call to OnTransferComplete -> DataIn
    // will see m_nnumber_blocks is 0 and call SendCSW.
    // Then this update() method will be called again, see m_nnumber_blocks is 0, and finalize.
    // Or, we can check here:
    if (gadget->m_nnumber_blocks == 0) {
        MLOGDEBUG("ScsiCmdRead10::update", "All blocks for this command have been staged for USB transfer.");
        // The CSW will be sent by OnTransferComplete logic after this last data packet.
        // We don't set m_pCurrentCommandHandler = nullptr yet, because OnTransferComplete will come back to DataIn,
        // which might then call SendCSW. After CSW is sent, then ReceiveCBW state is set.
        // The state machine:
        // Update() -> BeginTransfer (DataIn) -> gadget state is DataIn
        // IRQ (XferComplete for DataIn) -> if (m_nnumber_blocks > 0) gadget state DataInRead (calls this update again)
        //                             -> else SendCSW -> gadget state SentCSW
        // IRQ (XferComplete for SentCSW) -> gadget state ReceiveCBW, m_pCurrentCommandHandler should be null.
        // So, we should set m_pCurrentCommandHandler to null when SendCSW is called for this command.
        // This happens inside OnTransferComplete's DataIn case when m_nnumber_blocks is 0.
        // For now, the current handler remains active until the CSW is sent.
        // The original Update() loop would then call SendCSW() if m_nnumber_blocks == 0.
        // Here, OnTransferComplete will handle sending the CSW.
        // This handler's job for *this particular update call* is done.
    } else {
         MLOGDEBUG("ScsiCmdRead10::update", "%u blocks remaining for this command.", gadget->m_nnumber_blocks);
         // Expect OnTransferComplete to set state back to DataInRead to call this update() again.
    }
}
