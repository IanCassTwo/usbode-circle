//
// scsi_command_handler.h
//
// Base class for SCSI command handlers in the USB CD Gadget.
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2023-2024  R. Stange <rsta2@o2online.de>
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
#ifndef _CIRCLE_USB_GADGET_SCSI_COMMAND_HANDLER_H
#define _CIRCLE_USB_GADGET_SCSI_COMMAND_HANDLER_H

#include <circle/types.h>
#include <usbcdgadget/usbcdgadget.h> // For TUSBCDCBW, CUSBCDGadget, etc.

// Forward declaration
class CUSBCDGadget;
struct TUSBCDCBW;

class ScsiCommandHandler {
public:
    ScsiCommandHandler() {}
    virtual ~ScsiCommandHandler() {}

    // Pure virtual method to handle a SCSI command.
    // cbw: The Command Block Wrapper received from the host.
    // gadget: Pointer to the main CUSBCDGadget object to access its members and methods.
    virtual void handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) = 0;

    // Virtual method for deferred processing, typically I/O.
    // Called by CUSBCDGadget::Update() if this handler is active.
    // gadget: Pointer to the main CUSBCDGadget object.
    virtual void update(CUSBCDGadget* gadget) {
        // Default implementation does nothing, as not all commands need deferred processing.
    }

protected:
    // Helper methods to interact with the CUSBCDGadget
    // These are convenience functions that derived classes can use.

    void send_csw(CUSBCDGadget* gadget, u8 csw_status) {
        gadget->m_CSW.bmCSWStatus = csw_status;
        gadget->SendCSW(); // Assuming SendCSW becomes public or this class is a friend
    }

    void send_csw_ok(CUSBCDGadget* gadget) {
        gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;
        gadget->SendCSW();
    }

    void send_csw_fail(CUSBCDGadget* gadget) {
        gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
        gadget->SendCSW();
    }

    void begin_data_in_transfer(CUSBCDGadget* gadget, void* buffer, size_t length) {
        // Ensure m_pEP, TransferDataIn are accessible
        // gadget->m_pEP[CUSBCDGadget::EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, (u8*)buffer, length);
        // The above line requires CUSBCDGadget::EPIn and CUSBCDGadgetEndpoint::TransferDataIn to be accessible
        // For now, directly call a method on gadget if it encapsulates this
        gadget->StartDataInTransfer(buffer, length);
    }

    void set_sense_data(CUSBCDGadget* gadget, u8 sense_key, u8 asc, u8 ascq) {
        gadget->m_SenseParams.bSenseKey = sense_key;
        gadget->m_SenseParams.bAddlSenseCode = asc;
        gadget->m_SenseParams.bAddlSenseCodeQual = ascq;
    }

    // It seems CUSBCDGadget::SendCSW is private.
    // And m_CSW, m_SenseParams, m_pEP are private or protected.
    // For this to work, ScsiCommandHandler might need to be a friend class of CUSBCDGadget,
    // or CUSBCDGadget needs to expose more public methods for these operations.
    // For now, I'll assume CUSBCDGadget will be modified to allow these interactions.
    // Specifically, SendCSW() needs to be callable, and m_CSW, m_SenseParams need to be modifiable.
    // Access to m_pEP for BeginTransfer is also needed.
    // A new public method `StartDataInTransfer` will be added to `CUSBCDGadget`.
};

#endif // _CIRCLE_USB_GADGET_SCSI_COMMAND_HANDLER_H
