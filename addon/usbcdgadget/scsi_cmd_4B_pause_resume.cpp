//
// scsi_cmd_4B_pause_resume.cpp
//
// Handler for SCSI command PAUSE/RESUME (0x4B).
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
#include <usbcdgadget/scsi_cmd_4B_pause_resume.h>
#include <usbcdgadget/usbcdgadget.h>
#include <cdplayer/cdplayer.h>       // For CCDPlayer
#include <circle/sched/scheduler.h>  // For CScheduler
#include <circle/logger.h>           // For MLOGNOTE

ScsiCmdPauseResume::ScsiCmdPauseResume() {}
ScsiCmdPauseResume::~ScsiCmdPauseResume() {}

void ScsiCmdPauseResume::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    int resume_action = cbw.CBWCB[8] & 0x01; // RESUME bit: 1 = Resume, 0 = Pause

    MLOGNOTE("ScsiCmdPauseResume", "PAUSE/RESUME (0x4B), Action: %s", resume_action ? "Resume" : "Pause");

    CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
    if (cdplayer) {
        if (resume_action) {
            cdplayer->Resume();
        } else {
            cdplayer->Pause();
        }
        // If cdplayer interaction could fail and require a different SCSI status:
        // gadget->SetSenseParameters(...);
        // send_csw(gadget, CD_CSW_STATUS_FAIL);
        // gadget->m_pCurrentCommandHandler = nullptr;
        // return;
    } else {
        MLOGWARN("ScsiCmdPauseResume", "CCDPlayer task not found.");
        // Depending on requirements, could set sense data for "Logical Unit Communication Failure"
        // or similar if CCDPlayer is essential for this command's success.
        // For now, mirroring original behavior which didn't error out if cdplayer was null.
    }

    // Original code used gadget->bmCSWStatus, which is usually OK unless a prior error set it.
    // For a control command like this, explicitly sending OK is generally safe if no internal error occurred.
    send_csw(gadget, gadget->GetCurrentCSWStatus());
    gadget->m_pCurrentCommandHandler = nullptr;
}
