//
// scsi_cmd_00_test_unit_ready.cpp
//
// Handler for SCSI command TEST UNIT READY (0x00).
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
#include <usbcdgadget/scsi_cmd_00_test_unit_ready.h>
#include <usbcdgadget/usbcdgadget.h> // For CUSBCDGadget members and constants
#include <circle/logger.h> // For MLOGNOTE, MLOGERR (will need to be passed or accessible)

// Assuming MLOGNOTE and MLOGERR macros are available globally or via gadget
// For now, direct usage might cause issues if not properly included or defined.
// These typically rely on CLogger::Get(), which should be fine.

ScsiCmdTestUnitReady::ScsiCmdTestUnitReady() {
    // Constructor, if needed for initialization
}

ScsiCmdTestUnitReady::~ScsiCmdTestUnitReady() {
    // Destructor
}

void ScsiCmdTestUnitReady::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    // Original logic from CUSBCDGadget::HandleSCSICommand case 0x00:
    if (!gadget->IsCDReady()) { // Assuming a public getter IsCDReady() for m_CDReady
        MLOGNOTE("ScsiCmdTestUnitReady", "Test Unit Ready (returning CD_CSW_STATUS_FAIL)");
        // gadget->bmCSWStatus = CD_CSW_STATUS_FAIL; // This will be set by send_csw
        gadget->SetSenseParameters(0x02, 0x04, 0x00); // NOT READY, LOGICAL UNIT NOT READY, CAUSE NOT REPORTABLE
                                                       // Using a new public method SetSenseParameters
        send_csw(gadget, CD_CSW_STATUS_FAIL); // Use helper from base class
    } else {
        // MLOGNOTE ("CUSBCDGadget::HandleSCSICommand", "Test Unit Ready (returning CD_CSW_STATUS_FAIL)");
        // The original code had a commented out MLOGNOTE and then set m_CSW.bmCSWStatus = bmCSWStatus;
        // bmCSWStatus seems to be a member of CUSBCDGadget that holds the "default" status.
        // If the unit is ready, the default status is OK.
        // If there was a pending error (e.g. disc changed), bmCSWStatus would be FAIL.
        // So we should use gadget->GetDefaultCSWStatus() or similar.
        // For now, let's assume if it's ready and no prior error flag is explicitly checked here, it's OK.
        // However, the original code was:
        //    m_CSW.bmCSWStatus = bmCSWStatus;
        //    SendCSW();
        // This implies bmCSWStatus should reflect the current overall state.
        // If gadget->m_bmCSWStatus is CD_CSW_STATUS_OK (because previous command was ok) then this is fine.
        // If gadget->m_bmCSWStatus was FAIL (e.g. after a disc change sense key was set to UNIT_ATTENTION), then TUR should also fail.

        // Replicating original logic: use the gadget's current bmCSWStatus
        send_csw(gadget, gadget->GetCurrentCSWStatus());
    }
    // No change to gadget->m_nState is explicitly needed here beyond what SendCSW does (sets to SentCSW)
}
