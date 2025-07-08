//
// scsi_cmd_03_request_sense.cpp
//
// Handler for SCSI command REQUEST SENSE (0x03).
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
#include <usbcdgadget/scsi_cmd_03_request_sense.h>
#include <usbcdgadget/usbcdgadget.h> // For CUSBCDGadget members, TUSBCDRequestSenseReply, etc.
#include <circle/logger.h>          // For MLOGNOTE
#include <circle/util.h>            // For memcpy
#include <cstddef>                  // For offsetof

ScsiCmdRequestSense::ScsiCmdRequestSense() {
    // Constructor
}

ScsiCmdRequestSense::~ScsiCmdRequestSense() {
    // Destructor
}

void ScsiCmdRequestSense::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    // Original logic from CUSBCDGadget::HandleSCSICommand case 0x03:
    // bool desc = cbw.CBWCB[1] & 0x01; // desc parameter was unused in original code
    u8 allocation_length = (u8)(cbw.CBWCB[4]);

    MLOGNOTE("ScsiCmdRequestSense", "Request Sense CMD: bSenseKey 0x%02x, bAddlSenseCode 0x%02x, bAddlSenseCodeQual 0x%02x ",
             gadget->m_SenseParams.bSenseKey, gadget->m_SenseParams.bAddlSenseCode, gadget->m_SenseParams.bAddlSenseCodeQual);

    u8 length_to_send = sizeof(TUSBCDRequestSenseReply);
    if (allocation_length < length_to_send) {
        length_to_send = allocation_length;
    }

    // Populate the reply structure.
    // Assuming m_ReqSenseReply is a member of CUSBCDGadget that holds the template for this reply.
    // Or, we construct it here. The original code used a member m_ReqSenseReply.
    // For now, let's assume gadget has a public member or a method to get/fill this.
    // Let's make m_ReqSenseReply public in CUSBCDGadget for now.

    gadget->m_ReqSenseReply.bErrCode = 0x70; // Current error
    gadget->m_ReqSenseReply.bSegNum = 0x00;
    gadget->m_ReqSenseReply.bSenseKey = gadget->m_SenseParams.bSenseKey;
    // gadget->m_ReqSenseReply.bInformation = {0}; // Typically zero
    memset(gadget->m_ReqSenseReply.bInformation, 0, sizeof(gadget->m_ReqSenseReply.bInformation));
    gadget->m_ReqSenseReply.bAddlSenseLen = sizeof(TUSBCDRequestSenseReply) - offsetof(TUSBCDRequestSenseReply, bCmdSpecificInfo); // Correct length calculation
    // gadget->m_ReqSenseReply.bCmdSpecificInfo = {0}; // Typically zero
    memset(gadget->m_ReqSenseReply.bCmdSpecificInfo, 0, sizeof(gadget->m_ReqSenseReply.bCmdSpecificInfo));
    gadget->m_ReqSenseReply.bAddlSenseCode = gadget->m_SenseParams.bAddlSenseCode;
    gadget->m_ReqSenseReply.bAddlSenseCodeQual = gadget->m_SenseParams.bAddlSenseCodeQual;
    gadget->m_ReqSenseReply.bFieldReplaceUnitCode = 0x00;
    gadget->m_ReqSenseReply.bSKSVetc = 0x00;
    // gadget->m_ReqSenseReply.sKeySpecific = {0}; // Typically zero
    memset(gadget->m_ReqSenseReply.sKeySpecific, 0, sizeof(gadget->m_ReqSenseReply.sKeySpecific));


    memcpy(gadget->GetInBuffer(), &gadget->m_ReqSenseReply, length_to_send);

    // gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, gadget->GetInBuffer(), length_to_send);
    // Use helper:
    this->begin_data_in_transfer(gadget, gadget->GetInBuffer(), length_to_send);


    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK; // Status for this command's CSW
    gadget->m_nState = CUSBCDGadget::TCDState::SendReqSenseReply; // Next state for OnTransferComplete

    // If we were "Not Ready", switch to Unit Attention for subsequent commands
    if (gadget->m_SenseParams.bSenseKey == 0x02) {
        MLOGNOTE("ScsiCmdRequestSense", "Moving sense state to Unit Attention, Medium may have changed");
        gadget->bmCSWStatus = CD_CSW_STATUS_FAIL; // Default status for NEXT command if it's a check condition
        gadget->SetSenseParameters(0x06, 0x28, 0x00); // UNIT ATTENTION, NOT READY TO READY CHANGE, MEDIUM MAY HAVE CHANGED
    } else {
        // Reset response params after send for subsequent commands
        MLOGNOTE("ScsiCmdRequestSense", "Moving sense state to OK");
        gadget->bmCSWStatus = CD_CSW_STATUS_OK; // Default status for NEXT command
        gadget->SetSenseParameters(0, 0, 0);    // NO SENSE, NO ADDITIONAL SENSE INFORMATION
    }

    // No direct SendCSW() here; it's sent after the data transfer in OnTransferComplete via SendReqSenseReply state.
    gadget->m_pCurrentCommandHandler = nullptr; // This command is completed by this handler.
}
