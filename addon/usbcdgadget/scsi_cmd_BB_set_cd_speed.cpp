//
// scsi_cmd_BB_set_cd_speed.cpp
//
// Handler for SCSI command SET CD SPEED (0xBB).
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
#include <usbcdgadget/scsi_cmd_BB_set_cd_speed.h>
#include <usbcdgadget/usbcdgadget.h>
#include <circle/logger.h> // For MLOGDEBUG

ScsiCmdSetCdSpeed::ScsiCmdSetCdSpeed() {}
ScsiCmdSetCdSpeed::~ScsiCmdSetCdSpeed() {}

void ScsiCmdSetCdSpeed::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    MLOGDEBUG("ScsiCmdSetCdSpeed", "SET CD SPEED (0xBB)");
    // This command is typically a no-op in emulators or if speed control is not supported.
    // Original code just sends CSW_STATUS_OK.
    send_csw(gadget, CD_CSW_STATUS_OK);
    gadget->m_pCurrentCommandHandler = nullptr;
}
