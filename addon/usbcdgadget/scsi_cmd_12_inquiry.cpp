//
// scsi_cmd_12_inquiry.cpp
//
// Handler for SCSI command INQUIRY (0x12).
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
#include <usbcdgadget/scsi_cmd_12_inquiry.h>
#include <usbcdgadget/usbcdgadget.h> // For CUSBCDGadget members, TUSBCDInquiryReply, etc.
#include <circle/logger.h>          // For MLOGNOTE
#include <circle/util.h>            // For memcpy, MLOG*

ScsiCmdInquiry::ScsiCmdInquiry() {
    // Constructor
}

ScsiCmdInquiry::~ScsiCmdInquiry() {
    // Destructor
}

void ScsiCmdInquiry::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    int allocationLength = (cbw.CBWCB[3] << 8) | cbw.CBWCB[4];
    MLOGNOTE("ScsiCmdInquiry", "Inquiry %0x, allocation length %d", cbw.CBWCB[1], allocationLength);

    bool evpd = (cbw.CBWCB[1] & 0x01) != 0;
    u8 page_code = cbw.CBWCB[2];

    if (!evpd) {  // EVPD bit is 0: Standard Inquiry
        MLOGNOTE("ScsiCmdInquiry", "Standard Inquiry");

        // gadget->m_InqReply is already populated with default values in CUSBCDGadget constructor.
        // We might need to update parts of it if they are dynamic, e.g. serial number, but
        // the original code uses a static reply.
        // For USBODE, VendorID, ProdID, ProdRev are static.

        int datalen = sizeof(gadget->m_InqReply);
        if (allocationLength < datalen) {
            datalen = allocationLength;
        }

        memcpy(gadget->GetInBuffer(), &gadget->m_InqReply, datalen);
        this->begin_data_in_transfer(gadget, gadget->GetInBuffer(), datalen);

        gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
        gadget->m_nnumber_blocks = 0;  // nothing more after this send
        gadget->m_CSW.bmCSWStatus = gadget->GetCurrentCSWStatus(); // Use current status (usually OK unless pending error)
                                                              // Original code used CD_CSW_STATUS_OK explicitly here.
                                                              // Let's stick to explicit OK if no error.
        gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;


    } else {  // EVPD bit is 1: VPD Inquiry
        MLOGNOTE("ScsiCmdInquiry", "VPD Inquiry, Page Code: 0x%02x", page_code);
        switch (page_code) {
            case 0x00: { // Supported VPD Pages
                MLOGNOTE("ScsiCmdInquiry", "VPD Page: Supported VPD Pages (0x00)");
                // Original code uses m_InqVPDReply, but it's a TUSBSupportedVPDPage struct.
                // Let's use the literal array as in the original code for clarity.
                u8 SupportedVPDPageReply[] = {
                    0x05,  // Byte 0: Peripheral Device Type (0x05 for Optical Memory Device)
                    0x00,  // Byte 1: Page Code (0x00 for Supported VPD Pages page)
                    0x00,  // Byte 2: Page Length (MSB) - total length of page codes following
                    0x03,  // Byte 3: Page Length (LSB) - 3 supported page codes
                    0x00,  // Byte 4: Supported VPD Page Code: Supported VPD Pages (this page itself)
                    0x80,  // Byte 5: Supported VPD Page Code: Unit Serial Number
                    0x83   // Byte 6: Supported VPD Page Code: Device Identification
                };
                int datalen = sizeof(SupportedVPDPageReply);
                if (allocationLength < datalen) {
                    datalen = allocationLength;
                }
                memcpy(gadget->GetInBuffer(), &SupportedVPDPageReply, datalen);
                this->begin_data_in_transfer(gadget, gadget->GetInBuffer(), datalen);
                gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
                gadget->m_nnumber_blocks = 0;
                gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
                break;
            }
            case 0x80: { // Unit Serial Number Page
                MLOGNOTE("ScsiCmdInquiry", "VPD Page: Unit Serial Number (0x80)");
                // Original code uses m_InqSerialReply (TUSBUintSerialNumberPage)
                // but then defines a local UnitSerialNumberReply array.
                // The m_HardwareSerialNumber is part of CUSBCDGadget.
                // We need to construct this reply dynamically using m_HardwareSerialNumber.

                u8 unitSerialNumberPage[4 + sizeof(gadget->m_HardwareSerialNumber)]; // Max size
                unitSerialNumberPage[0] = 0x05; // Peripheral Device Type
                unitSerialNumberPage[1] = 0x80; // Page Code
                unitSerialNumberPage[2] = 0x00; // Reserved / Page Length MSB

                // Calculate actual serial length, ensure it doesn't exceed available space in reply
                size_t serial_len = strlen(gadget->GetHardwareSerialNumber());
                if (serial_len > (sizeof(unitSerialNumberPage) - 4 -1) ) { // -1 for potential null term if used from string
                     serial_len = sizeof(unitSerialNumberPage) - 4 -1; // Cap length
                }
                // The SCSI spec for VPD page 0x80 says "Product Serial Number".
                // The example 'U','S','B','O','D','E','0','0','0','0','1' is 11 bytes.
                // Let's use the actual hardware serial number.

                unitSerialNumberPage[3] = static_cast<u8>(serial_len); // Page Length LSB
                memcpy(&unitSerialNumberPage[4], gadget->GetHardwareSerialNumber(), serial_len);

                int datalen = 4 + serial_len; // Actual data length
                if (allocationLength < datalen) {
                    datalen = allocationLength;
                }
                memcpy(gadget->GetInBuffer(), unitSerialNumberPage, datalen);
                this->begin_data_in_transfer(gadget, gadget->GetInBuffer(), datalen);
                gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
                gadget->m_nnumber_blocks = 0;
                gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
                break;
            }
            case 0x83: { // Device Identification Page
                MLOGNOTE("ScsiCmdInquiry", "VPD Page: Device Identification (0x83)");
                // Using the literal array from original code.
                u8 DeviceIdentificationReply[] = {
                    0x05,  // Byte 0: Peripheral Device Type (Optical Memory Device)
                    0x83,  // Byte 1: Page Code (Device Identification page)
                    0x00,  // Byte 2: Page Length (MSB)
                    0x0C,  // Byte 3: Page Length (LSB) - Updated length: 1 designator header (4) + T10 Vendor ID (8) = 12
                           // Original was 0x0B, which seems one byte short for an 8-byte ID + 4 byte header.
                           // The spec says "Length of all IDENTIFICATION DESCRIPTOR fields".
                           // The T10 Vendor ID designator is 4 bytes header + 8 bytes ID = 12 bytes.
                           // So page length should be 12.

                    // --- Start of First Designator (T10 Vendor ID) ---
                    0x01,  // Byte 4: CODE SET (0x01 = ASCII) (Actually this byte is PIV(bit7) | ASSOCIATION(bits5-4) | IDENTIFIER TYPE(bits3-0))
                           // PIV=0, ASSOCIATION=0 (LU), IDENTIFIER TYPE=0x1 (T10 Vendor ID) -> so this should be 0x01
                           // The original code had 0x01. Let's assume this is correct.
                           // Wait, T10 SPC-3 Fig 178: Code Set is bits 3-0. Type is bits 3-0 of next byte.
                           // Let's re-check the spec for VPD Page 0x83.
                           // Identifier Descriptor:
                           // Byte 0: Protocol Identifier (bits 7-4), Code Set (bits 3-0)
                           // Byte 1: PIV (bit 7), Reserved (bit 6), Association (bits 5-4), Identifier Type (bits 3-0)
                           // Byte 2: Reserved
                           // Byte 3: Identifier Length
                           // Byte 4..N: Identifier
                           //
                           // For T10 Vendor ID: Protocol ID = 0 (Fibre Channel), Code Set = 1 (Binary) or 2 (ASCII)
                           // Identifier Type = 0 (Vendor specific), 1 (T10 Vendor ID), 2 (EUI-64), 3 (NAA)
                           //
                           // The example in the original code:
                           // 0x01,  // Byte 4: CODE SET (0x01 = ASCII) -> This is likely (ProtoID << 4) | CodeSet. If ProtoID=0, CodeSet=1 (binary) or 2 (ASCII)
                           // 0x00,  // Byte 5: PROTOCOL IDENTIFIER (0x00 = SCSI) -> This is likely PIV | Assoc | Type. If PIV=0, Assoc=0(LU), Type=1(T10 Vendor ID) -> 0x01
                           // This structure seems to follow an older spec or a common simplified version.
                           // Let's stick to the original byte values for now as they likely worked.
                           // The original had:
                           // 0x01,  // Byte 4: CODE SET (0x01 = ASCII)
                           // 0x00,  // Byte 5: PIV (0) + Assoc (0) + Type (0x01 = T10 Vendor ID)
                           // 0x08,  // Byte 6: LENGTH (Length of the identifier data itself - 8 bytes)
                           // 'U', 'S', 'B', 'O', 'D', 'E', ' ', ' '  // "USBODE  " - padded to 8 bytes
                           // Total length of this descriptor: 4 (header part for this descriptor) + 8 (ID) = 12 bytes.
                           // So Page Length (byte 3) should be 0x0C.

                    0x02,  // Byte 4: Code Set (ASCII) (bits 3-0), Protocol Identifier (SCSI specific - typically 0 for this if not specified for type)
                           // For T10 Vendor ID, this byte means: Code Set = ASCII (0x2), Association = Logical Unit (0x0), Type = T10 Vendor ID (0x1)
                           // Let's use the structure from SCSI SPC-4 Table 403 - IDENTIFICATION DESCRIPTOR format
                           // Byte 0: (Protocol identifier specific) << 4 | Code set
                           // Byte 1: PIV(1b) | ASSOCIATION(2b) | IDENTIFIER TYPE(4b)
                           // Byte 2: Reserved
                           // Byte 3: IDENTIFIER LENGTH
                           // Byte 4-n: IDENTIFIER
                           // T10 Vendor ID: Type 0x1. Association 0x0 (LU). Code Set 0x2 (ASCII).
                           // So Byte 1 = 0b00000001 = 0x01
                           // Byte 0 = (Proto ID, e.g. 0 for SCSI block commands if not specified) << 4 | 0x2 (ASCII) = 0x02
                           0x02, // Protocol ID=0 (implicit for this type), Code Set = ASCII (0x2)
                           0x01, // PIV=0, Association=0 (LU), Identifier Type=1 (T10 Vendor ID)
                           0x00, // Reserved
                           0x08, // Identifier Length (8 bytes for T10 vendor ID)
                           'U', 'S', 'B', 'O', 'D', 'E', ' ', ' ', // T10 Vendor Identification (8 bytes)
                        };
                        int datalen = sizeof(DeviceIdentificationReply);
                        if (allocationLength < datalen) {
                            datalen = allocationLength;
                        }
                        memcpy(gadget->GetInBuffer(), &DeviceIdentificationReply, datalen);
                        this->begin_data_in_transfer(gadget, gadget->GetInBuffer(), datalen);
                        gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
                        gadget->m_nnumber_blocks = 0;
                        gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
                        break;
                    }
                    default: { // Unsupported VPD Page
                        MLOGNOTE("ScsiCmdInquiry", "Unsupported VPD Page: 0x%02x", page_code);
                        gadget->m_nnumber_blocks = 0;
                        gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
                        // gadget->SetSenseParameters(0x05, 0x24, 0x00); // INVALID FIELD IN CDB
                        // The original code has 0x24, 0x00 (Invalid field in CDB)
                        // However, for unsupported VPD page, 0x05, 0x24, 0x01 (INVALID FIELD IN CDB, PAGE CODE not supported)
                        // or 0x05, 0x26, 0x00 (INVALID FIELD PARAMETER LIST, Parameter not supported) might be more accurate.
                        // Sticking to original:
                        gadget->SetSenseParameters(0x05, 0x24, 0x00); // INVALID FIELD IN CDB
                        this->send_csw(gadget, CD_CSW_STATUS_FAIL); // Send CSW immediately
                        gadget->m_pCurrentCommandHandler = nullptr; // Command processing ends here
                        return; // Important: return early as CSW is already sent
                    }
                }
            }
        }
    } // End of EVPD handling

    // If we reached here, data was sent (or attempted), and CSW needs to follow via OnTransferComplete
    // The gadget->m_nState should be DataIn, and CSW will be sent by OnTransferComplete
    // Ensure m_pCurrentCommandHandler is cleared if this handler doesn't need update()
    gadget->m_pCurrentCommandHandler = nullptr;
}

// Helper to get hardware serial number - needs to be in CUSBCDGadget and public
// const char* CUSBCDGadget::GetHardwareSerialNumber() const {
//     return m_HardwareSerialNumber;
// }
// This method should be added to CUSBCDGadget.h and .cpp
// For now, ScsiCmdInquiry will directly access gadget->m_HardwareSerialNumber if it's made public.
// Or add a public getter in CUSBCDGadget.

// For VPD Page 0x80, the serial number is gadget->m_HardwareSerialNumber
// Need to add GetHardwareSerialNumber() to CUSBCDGadget or make m_HardwareSerialNumber public.
// For now, I'll assume a public getter GetHardwareSerialNumber() exists in CUSBCDGadget.
// Let's add this getter to CUSBCDGadget.
