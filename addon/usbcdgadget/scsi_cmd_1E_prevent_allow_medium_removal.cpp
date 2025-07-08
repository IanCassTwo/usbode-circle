//
// scsi_cmd_1E_prevent_allow_medium_removal.cpp
//
// Handler for SCSI command PREVENT ALLOW MEDIUM REMOVAL (0x1E).
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
#include <usbcdgadget/scsi_cmd_1E_prevent_allow_medium_removal.h>
#include <usbcdgadget/usbcdgadget.h>
#include <circle/logger.h> // For MLOGNOTE (optional, but good for consistency)

ScsiCmdPreventAllowMediumRemoval::ScsiCmdPreventAllowMediumRemoval() {}
ScsiCmdPreventAllowMediumRemoval::~ScsiCmdPreventAllowMediumRemoval() {}

void ScsiCmdPreventAllowMediumRemoval::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    // u8 prevent = cbw.CBWCB[4] & 0x01; // Not used in original logic, just noted
    MLOGDEBUG("ScsiCmdPreventAllowMediumRemoval", "PREVENT ALLOW MEDIUM REMOVAL, Prevent: %d", cbw.CBWCB[4] & 0x01);

    // Original code: Lie to the host, always return OK.
    // gadget->m_CSW.bmCSWStatus = gadget->GetCurrentCSWStatus(); // Original used bmCSWStatus
    // send_csw(gadget, gadget->GetCurrentCSWStatus());
    // For this command, it's safer to explicitly return OK as it's a simple acknowledgement.
    send_csw(gadget, CD_CSW_STATUS_OK);
    gadget->m_pCurrentCommandHandler = nullptr;
}
