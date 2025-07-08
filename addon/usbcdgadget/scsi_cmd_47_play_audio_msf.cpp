//
// scsi_cmd_47_play_audio_msf.cpp
//
// Handler for SCSI command PLAY AUDIO MSF (0x47).
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
#include <usbcdgadget/scsi_cmd_47_play_audio_msf.h>
#include <usbcdgadget/usbcdgadget.h>
#include <cdplayer/cdplayer.h>
#include <circle/sched/scheduler.h>
#include <circle/logger.h>
#include <cueparser/cueparser.h> // For CUETrackInfo

ScsiCmdPlayAudioMsf::ScsiCmdPlayAudioMsf() {}
ScsiCmdPlayAudioMsf::~ScsiCmdPlayAudioMsf() {}

void ScsiCmdPlayAudioMsf::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    u8 SM = cbw.CBWCB[3]; // Starting Minute
    u8 SS = cbw.CBWCB[4]; // Starting Second
    u8 SF = cbw.CBWCB[5]; // Starting Frame

    u8 EM = cbw.CBWCB[6]; // Ending Minute
    u8 ES = cbw.CBWCB[7]; // Ending Second
    u8 EF = cbw.CBWCB[8]; // Ending Frame

    u32 start_lba = gadget->msf_to_lba(SM, SS, SF);
    u32 end_lba = gadget->msf_to_lba(EM, ES, EF);

    MLOGNOTE("ScsiCmdPlayAudioMsf", "PLAY AUDIO MSF (0x47) Start: %u:%u:%u (LBA %u), End: %u:%u:%u (LBA %u)",
             SM, SS, SF, start_lba, EM, ES, EF, end_lba);

    // Default CSW status, can be overridden if there's an error
    u8 csw_status = gadget->GetCurrentCSWStatus(); // Usually OK unless a prior error set it.

    if (start_lba > end_lba && !(start_lba == 0xFFFFFFFF && end_lba == 0xFFFFFFFF)) { // 0xFFFFFFFF is often a special value (e.g. resume)
        MLOGERR("ScsiCmdPlayAudioMsf", "Start LBA > End LBA. Invalid parameters.");
        gadget->SetSenseParameters(0x05, 0x24, 0x00); // ILLEGAL REQUEST, INVALID FIELD IN CDB
        csw_status = CD_CSW_STATUS_FAIL;
    } else {
        CUETrackInfo trackInfo = gadget->GetTrackInfoForLBA(start_lba);
        if (trackInfo.track_number == -1 || trackInfo.track_mode != CUETrack_AUDIO) {
            MLOGERR("ScsiCmdPlayAudioMsf", "PLAY AUDIO MSF: LBA %u is not on an audio track.", start_lba);
            gadget->SetSenseParameters(0x05, 0x64, 0x00); // ILLEGAL REQUEST, ILLEGAL MODE FOR THIS TRACK
            csw_status = CD_CSW_STATUS_FAIL;
        } else {
            CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
            if (cdplayer) {
                if (start_lba == 0xFFFFFFFF && end_lba == 0xFFFFFFFF) { // Special case for resume?
                    MLOGNOTE("ScsiCmdPlayAudioMsf", "CD Player: Resume");
                    cdplayer->Resume();
                } else if (start_lba == end_lba) { // Pause? Or play single block? SCSI spec says play until end_lba is reached.
                                                 // Original code had this as a pause.
                    MLOGNOTE("ScsiCmdPlayAudioMsf", "CD Player: Pause (start_lba == end_lba)");
                    cdplayer->Pause();
                } else {
                    int num_blocks_to_play = end_lba - start_lba;
                    MLOGNOTE("ScsiCmdPlayAudioMsf", "CD Player: Play from LBA %u for %d blocks", start_lba, num_blocks_to_play);
                    cdplayer->Play(start_lba, num_blocks_to_play);
                }
            } else {
                MLOGWARN("ScsiCmdPlayAudioMsf", "CCDPlayer task not found.");
                // Consider if this is an error condition. Original didn't explicitly error here.
            }
        }
    }

    send_csw(gadget, csw_status);
    gadget->m_pCurrentCommandHandler = nullptr;
}
