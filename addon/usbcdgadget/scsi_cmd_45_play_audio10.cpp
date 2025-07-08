//
// scsi_cmd_45_play_audio10.cpp
//
// Handler for SCSI command PLAY AUDIO (10) (0x45).
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
#include <usbcdgadget/scsi_cmd_45_play_audio10.h>
#include <usbcdgadget/usbcdgadget.h>
#include <cdplayer/cdplayer.h>
#include <circle/sched/scheduler.h>
#include <circle/logger.h>
#include <cueparser/cueparser.h> // For CUETrackInfo

ScsiCmdPlayAudio10::ScsiCmdPlayAudio10() {}
ScsiCmdPlayAudio10::~ScsiCmdPlayAudio10() {}

void ScsiCmdPlayAudio10::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    // Starting LBA
    u32 starting_lba = (u32)(cbw.CBWCB[2] << 24) |
                       (u32)(cbw.CBWCB[3] << 16) |
                       (u32)(cbw.CBWCB[4] << 8)  |
                       cbw.CBWCB[5];

    // Transfer Length (number of logical blocks to play)
    u16 number_of_blocks = (u16)((cbw.CBWCB[7] << 8) | cbw.CBWCB[8]);

    MLOGNOTE("ScsiCmdPlayAudio10", "PLAY AUDIO (10) (0x45) LBA: %u, Length: %u blocks",
             starting_lba, number_of_blocks);

    u8 csw_status = gadget->GetCurrentCSWStatus(); // Default, usually OK

    if (number_of_blocks > 0) {
        CUETrackInfo trackInfo = gadget->GetTrackInfoForLBA(starting_lba);
        if (trackInfo.track_number != -1 && trackInfo.track_mode == CUETrack_AUDIO) {
            CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
            if (cdplayer) {
                MLOGDEBUG("ScsiCmdPlayAudio10", "CDPlayer: Play from LBA %u for %u blocks", starting_lba, number_of_blocks);
                if (starting_lba == 0xFFFFFFFF) { // Special case used by some for resume
                     cdplayer->Resume();
                } else {
                     cdplayer->Play(starting_lba, number_of_blocks);
                }
            } else {
                MLOGWARN("ScsiCmdPlayAudio10", "CCDPlayer task not found.");
            }
        } else {
            MLOGERR("ScsiCmdPlayAudio10", "LBA %u is not on an audio track.", starting_lba);
            gadget->SetSenseParameters(0x05, 0x64, 0x00); // ILLEGAL REQUEST, ILLEGAL MODE FOR THIS TRACK
            csw_status = CD_CSW_STATUS_FAIL;
        }
    } else {
        // Number of blocks is 0. Some interpretations might be to stop or pause.
        // Original code did nothing if m_nnumber_blocks (derived from CBWCB[7,8]) was 0.
        // For PLAY AUDIO (10), a length of 0 should mean no audio is played.
        MLOGNOTE("ScsiCmdPlayAudio10", "PLAY AUDIO (10) with 0 blocks. No action taken.");
    }

    send_csw(gadget, csw_status);
    gadget->m_pCurrentCommandHandler = nullptr;
}
