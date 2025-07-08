//
// scsi_cmd_4E_stop_play_scan.cpp
//
// Handler for SCSI command STOP PLAY/SCAN (0x4E).
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
#include <usbcdgadget/scsi_cmd_4E_stop_play_scan.h>
#include <usbcdgadget/usbcdgadget.h>
#include <cdplayer/cdplayer.h>
#include <circle/sched/scheduler.h>
#include <circle/logger.h>

ScsiCmdStopPlayScan::ScsiCmdStopPlayScan() {}
ScsiCmdStopPlayScan::~ScsiCmdStopPlayScan() {}

void ScsiCmdStopPlayScan::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    MLOGNOTE("ScsiCmdStopPlayScan", "STOP PLAY/SCAN (0x4E)");

    // The command can be used to stop audio play, or stop a scan operation.
    // For this emulation, it's primarily used to stop audio playback, which means Pause.
    CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
    if (cdplayer) {
        cdplayer->Pause(); // Or Stop() if a more complete stop is intended. Original used Pause.
    } else {
        MLOGWARN("ScsiCmdStopPlayScan", "CCDPlayer task not found.");
    }

    send_csw(gadget, gadget->GetCurrentCSWStatus()); // Original used bmCSWStatus
    gadget->m_pCurrentCommandHandler = nullptr;
}
