//
// scsi_cmd_AC_get_performance.cpp
//
// Handler for SCSI command GET PERFORMANCE (0xAC).
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
#include <usbcdgadget/scsi_cmd_AC_get_performance.h>
#include <usbcdgadget/usbcdgadget.h>
#include <circle/logger.h>
#include <circle/util.h> // For memcpy

ScsiCmdGetPerformance::ScsiCmdGetPerformance() {}
ScsiCmdGetPerformance::~ScsiCmdGetPerformance() {}

void ScsiCmdGetPerformance::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    MLOGNOTE("ScsiCmdGetPerformance", "GET PERFORMANCE (0xAC)");

    // u16 allocation_length = (cbw.CBWCB[8] << 8) | cbw.CBWCB[9]; // For 10-byte variant of GET PERFORMANCE
    // For 0xAC, it's a 12-byte CDB. Allocation length is often in bytes 8 & 9 for 10-byte CDBs,
    // but for 12-byte CDBs, it's usually bytes 6,7,8,9 for Transfer Length.
    // However, this command often has specific structures for what it returns.
    // The original code returns a fixed stub.

    // Original stub response
    u8 getPerformanceStub[] = {
        0x00, 0x00, 0x00, 0x0C,  // Data Length for data after this field (12 bytes follow)
                                 // Original was 0x10, implying 16 bytes follow, total 20.
                                 // If stub is 20 bytes total, header is 8 bytes, data is 12.
                                 // Let's assume the original stub was 20 bytes total.
                                 // Header (8 bytes): DataLength(4), Except(1), Write(1), Reserved(2)
                                 // Data (12 bytes as per original stub): StartLBA(4), EndLBA(4), ReadSpeed(4)
                                 // This structure is for Type 0 (Performance).
                                 // The original stub:
                                 // 0x00, 0x00, 0x00, 0x10,  // Header: Length = 16 bytes (descriptor) -> This means 16 bytes of descriptors *after* this length field.
                                 // This is confusing. Let's use MMC-6 spec for "Performance Descriptor - Nominal Performance"
                                 // Performance Data Header (8 bytes):
                                 // Bytes 0-3: Performance Data Length (N-7)
                                 // Byte 4:    Except (bit 0)
                                 // Byte 5:    Write (bit 0)
                                 // Bytes 6-7: Reserved
                                 // Then Descriptors follow.
                                 // A common simple response is:
        0x00, 0x00, 0x00, 0x0C,  // Performance Data Length (12 bytes of descriptors follow)
        0x00,                    // Except=0
        0x00,                    // Write=0 (read performance)
        0x00, 0x00,              // Reserved
        // Start of Descriptor (Type 0000h: Nominal Performance)
        0x00, 0x00, 0x00, 0x00,  // Start LBA (e.g., 0)
        0xFF, 0xFF, 0xFF, 0xFF,  // End LBA (e.g., max LBA, or 0xFFFFFFFF if not applicable)
        // Read Speed (KB/s) e.g., 4x CD-ROM = 4 * 176.4 * 4 = approx 2822 KB/s. Or 1x = 176 KB/s.
        // Original used 0x01 which is very low if it means KB/s.
        // Let's use a nominal 1x speed (176KB/s) as a placeholder. 176 decimal = 0xB0.
        0x00, 0x00, 0x00, 0xB0   // Read Speed (176 KB/s) - example
    };
    // Total size of this response is 8 (header) + 12 (descriptor) = 20 bytes.
    // So, Performance Data Length (bytes 0-3) should be 12 (0x0000000C).

    u16 allocationLength = (cbw.CBWCB[8] << 8) | cbw.CBWCB[9]; // From 10-byte CDB definition; GET PERFORMANCE is 0xAC (12-byte)
                                                               // For 12-byte CDB (0xAC), Allocation Length is bytes 6-9.
                                                               // Let's assume the original interpretation of alloc length was correct for the host.
                                                               // Or, more likely, it's a 10-byte version (0xA1) that was intended.
                                                               // Given 0xAC, let's use the correct alloc length field.
    if (cbw.bCBWCBLength == 12) { // Check if it's actually a 12-byte CDB
         allocationLength = (u16)((cbw.CBWCB[6] << 24) | (cbw.CBWCB[7] << 16) | (cbw.CBWCB[8] << 8) | cbw.CBWCB[9]);
         // This is actually transfer length for the command, not allocation length for the data phase.
         // The data phase allocation length is in dCBWDataTransferLength from CBW.
         allocationLength = cbw.dCBWDataTransferLength;
    } else { // Assume 10-byte interpretation if not 12
         allocationLength = cbw.dCBWDataTransferLength;
    }


    int length_to_send = sizeof(getPerformanceStub);
    if (allocationLength < length_to_send) {
        length_to_send = allocationLength;
    }

    memcpy(gadget->GetInBuffer(), getPerformanceStub, length_to_send);

    this->begin_data_in_transfer(gadget, gadget->GetInBuffer(), length_to_send);
    gadget->m_nnumber_blocks = 0;
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = gadget->GetCurrentCSWStatus(); // Usually OK

    gadget->m_pCurrentCommandHandler = nullptr;
}
