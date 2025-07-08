//
// scsi_cmd_2F_verify.cpp
//
// Handler for SCSI command VERIFY (10) (0x2F).
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
#include <usbcdgadget/scsi_cmd_2F_verify.h>
#include <usbcdgadget/usbcdgadget.h>
#include <circle/logger.h> // For MLOGDEBUG

ScsiCmdVerify::ScsiCmdVerify() {}
ScsiCmdVerify::~ScsiCmdVerify() {}

void ScsiCmdVerify::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    MLOGDEBUG("ScsiCmdVerify", "VERIFY (10) (0x2F)");
    // The VERIFY command can be complex, involving checking data on the medium.
    // However, for many emulated devices, especially read-only ones,
    // it's common to just return OK if the LBA is valid, or an error if not.
    // The original code simply returns CSW_STATUS_OK.
    send_csw(gadget, CD_CSW_STATUS_OK);
    gadget->m_pCurrentCommandHandler = nullptr;
}
