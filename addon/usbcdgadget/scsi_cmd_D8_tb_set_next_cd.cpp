//
// scsi_cmd_D8_tb_set_next_cd.cpp
//
// Handler for SCSI ToolBox command SET NEXT CD (0xD8).
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
#include <usbcdgadget/scsi_cmd_D8_tb_set_next_cd.h>
#include <usbcdgadget/usbcdgadget.h>
#include <scsitbservice/scsitbservice.h> // For SCSITBService
#include <circle/sched/scheduler.h>     // For CScheduler
#include <circle/logger.h>

ScsiCmdTbSetNextCd::ScsiCmdTbSetNextCd() {}
ScsiCmdTbSetNextCd::~ScsiCmdTbSetNextCd() {}

void ScsiCmdTbSetNextCd::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    int index = cbw.CBWCB[1]; // Index of the CD to set as next
    MLOGNOTE("ScsiCmdTbSetNextCd", "SCSITB SET NEXT CD (0xD8), Index: %d", index);

    SCSITBService* scsitbservice = static_cast<SCSITBService*>(CScheduler::Get()->GetTask("scsitbservice"));

    if (scsitbservice) {
        // The original code doesn't check bounds or return error if index is invalid.
        // It relies on SCSITBService to handle invalid indexes (e.g. silently ignore or log).
        // A more robust implementation might query count from service and check index validity here.
        // For now, maintaining original behavior.
        scsitbservice->SetNextCD(index);
    } else {
        MLOGERR("ScsiCmdTbSetNextCd", "SCSITBService not found!");
        // Original code would still send OK status.
        // Consider if this should be an error.
    }

    send_csw(gadget, CD_CSW_STATUS_OK);
    gadget->m_pCurrentCommandHandler = nullptr;
}
