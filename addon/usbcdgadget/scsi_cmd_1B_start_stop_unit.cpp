//
// scsi_cmd_1B_start_stop_unit.cpp
//
// Handler for SCSI command START STOP UNIT (0x1B).
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
#include <usbcdgadget/scsi_cmd_1B_start_stop_unit.h>
#include <usbcdgadget/usbcdgadget.h>
#include <circle/logger.h>

ScsiCmdStartStopUnit::ScsiCmdStartStopUnit() {}
ScsiCmdStartStopUnit::~ScsiCmdStartStopUnit() {}

void ScsiCmdStartStopUnit::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    int start = cbw.CBWCB[4] & 1;
    int loej = (cbw.CBWCB[4] >> 1) & 1;

    // The original code notes:
    // TODO: Emulate a disk eject/load
    // loej Start Action
    // 0    0     Stop the disc - no action for us
    // 0    1     Start the disc - no action for us
    // 1    0     Eject the disc - perhaps we need to throw a check condition?
    // 1    1     Load the disc - perhaps we need to throw a check condition?
    // For now, it always returns OK.

    MLOGNOTE("ScsiCmdStartStopUnit", "START STOP UNIT, Start: %d, LOEJ: %d", start, loej);

    // If LOEJ is 1 (eject or load), and we want to signal a change:
    if (loej) {
        // Example: Simulate eject, make unit not ready until next TUR or Inquiry
        // gadget->m_CDReady = false; // This would be one way, but might be too aggressive.
        // For now, just acknowledge. If actual eject/load simulation is needed,
        // this is where sense keys for MEDIUM NOT PRESENT or NOT READY TO READY CHANGE would be set.
        // The original code simply returned OK.
        if (start == 0) { // Eject
            MLOGNOTE("ScsiCmdStartStopUnit", "Simulating Eject (currently no-op beyond acknowledge)");
            // Potentially set:
            // gadget->SetSenseParameters(0x02, 0x3A, 0x00); // NOT READY, MEDIUM NOT PRESENT
            // gadget->bmCSWStatus = CD_CSW_STATUS_FAIL; // For the *next* command
            // gadget->discChanged = true; // If this flag is used for GET EVENT STATUS
        } else { // Load
            MLOGNOTE("ScsiCmdStartStopUnit", "Simulating Load (currently no-op beyond acknowledge)");
            // Potentially set:
            // gadget->SetSenseParameters(0x06, 0x28, 0x00); // UNIT ATTENTION, NOT READY TO READY CHANGE
            // gadget->bmCSWStatus = CD_CSW_STATUS_FAIL; // For the *next* command
            // gadget->discChanged = true;
        }
    }


    // Original code always returned OK using the current bmCSWStatus (which is usually OK unless an error is pending)
    // send_csw(gadget, gadget->GetCurrentCSWStatus());
    // For this command, it's safer to explicitly return OK unless an action here causes a specific failure.
    send_csw(gadget, CD_CSW_STATUS_OK);
    gadget->m_pCurrentCommandHandler = nullptr;
}
