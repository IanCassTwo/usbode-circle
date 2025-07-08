//
// scsi_cmd_2B_seek.cpp
//
// Handler for SCSI command SEEK (10) (0x2B).
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
#include <usbcdgadget/scsi_cmd_2B_seek.h>
#include <usbcdgadget/usbcdgadget.h>
#include <cdplayer/cdplayer.h>       // For CCDPlayer
#include <circle/sched/scheduler.h>  // For CScheduler
#include <circle/logger.h>           // For MLOGNOTE

ScsiCmdSeek::ScsiCmdSeek() {}
ScsiCmdSeek::~ScsiCmdSeek() {}

void ScsiCmdSeek::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    // Logical Block Address to seek to
    u32 lba_to_seek = (u32)(cbw.CBWCB[2] << 24) |
                      (u32)(cbw.CBWCB[3] << 16) |
                      (u32)(cbw.CBWCB[4] << 8)  |
                      cbw.CBWCB[5];

    MLOGNOTE("ScsiCmdSeek", "SEEK (10) (0x2B) to LBA %u", lba_to_seek);

    // The SEEK command primarily affects the logical position for subsequent READ commands.
    // For audio CDs, it might also set the starting point for PLAY AUDIO commands.
    // The original code sets m_nblock_address which is used by Read(10) and PlayAudio(10)/(12).
    // And calls cdplayer->Seek().

    gadget->m_nblock_address = lba_to_seek; // Update gadget's current LBA

    CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
    if (cdplayer) {
        cdplayer->Seek(lba_to_seek);
    } else {
        MLOGWARN("ScsiCmdSeek", "CCDPlayer task not found for SEEK.");
    }

    // SEEK typically returns OK if the LBA is within a valid range,
    // or an error if out of range or if the seek operation itself fails.
    // The original code always returned current bmCSWStatus.
    // For simplicity, returning OK unless a specific check fails.
    // A more robust implementation might check if lba_to_seek is valid.
    // e.g. if (lba_to_seek >= gadget->GetLeadoutLBA()) -> set sense ILLEGAL_REQUEST / LBA_OUT_OF_RANGE

    send_csw(gadget, gadget->GetCurrentCSWStatus());
    gadget->m_pCurrentCommandHandler = nullptr;
}
