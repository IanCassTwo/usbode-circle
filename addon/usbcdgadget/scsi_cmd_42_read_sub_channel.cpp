//
// scsi_cmd_42_read_sub_channel.cpp
//
// Handler for SCSI command READ SUB-CHANNEL (0x42).
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
#include <usbcdgadget/scsi_cmd_42_read_sub_channel.h>
#include <usbcdgadget/usbcdgadget.h> // For CUSBCDGadget, reply structs, etc.
#include <cdplayer/cdplayer.h>       // For CCDPlayer
#include <circle/sched/scheduler.h>  // For CScheduler
#include <circle/logger.h>           // For MLOGNOTE, MLOGDEBUG
#include <circle/util.h>             // For memcpy, memset

ScsiCmdReadSubChannel::ScsiCmdReadSubChannel() {}
ScsiCmdReadSubChannel::~ScsiCmdReadSubChannel() {}

void ScsiCmdReadSubChannel::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    unsigned int msf = (cbw.CBWCB[1] >> 1) & 0x01;
    // unsigned int subq = (cbw.CBWCB[2] >> 6) & 0x01; // SUBQ bit, if needed
    unsigned int parameter_list_code = cbw.CBWCB[3]; // "Parameter List" in original, but it's "Sub-Channel Data Format Code"
    // unsigned int track_number = cbw.CBWCB[6]; // Track Number, used for ISRC/MCN
    int allocationLength = (cbw.CBWCB[7] << 8) | cbw.CBWCB[8];
    int length_to_send = 0;

    MLOGDEBUG("ScsiCmdReadSubChannel", "READ SUB-CHANNEL (0x42), MSF: %u, FormatCode: 0x%02x, AllocLen: %d",
             msf, parameter_list_code, allocationLength);

    CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));

    // Original code defaulted to 0x01 if parameter_list_code was 0x00 (reserved)
    if (parameter_list_code == 0x00) {
        parameter_list_code = 0x01; // Q Sub-channel Current Position Data
    }

    switch (parameter_list_code) {
        case 0x01: { // Current Position Data (Q Sub-channel)
            TUSBCDSubChannelHeaderReply header_reply;
            memset(&header_reply, 0, SIZE_SUBCHANNEL_HEADER_REPLY);
            // header_reply.reserved = 0; // Already done by memset
            header_reply.audioStatus = 0x00; // Audio status not supported (default)
                                           // Will be overridden by player status if available
            header_reply.dataLength = htons(SIZE_SUBCHANNEL_01_DATA_REPLY); // Length of the TUSBCDSubChannel01CurrentPositionReply data

            if (cdplayer) {
                unsigned int player_state = cdplayer->GetState();
                switch (player_state) {
                    case CCDPlayer::PLAYING:       header_reply.audioStatus = 0x11; break;
                    case CCDPlayer::PAUSED:        header_reply.audioStatus = 0x12; break;
                    case CCDPlayer::STOPPED_OK:    header_reply.audioStatus = 0x13; break;
                    case CCDPlayer::STOPPED_ERROR: header_reply.audioStatus = 0x14; break;
                    default:                       header_reply.audioStatus = 0x15; break; // No status
                }
            }

            TUSBCDSubChannel01CurrentPositionReply position_data_reply;
            memset(&position_data_reply, 0, SIZE_SUBCHANNEL_01_DATA_REPLY);
            position_data_reply.dataFormatCode = 0x01; // This is Q Sub-channel current position data format

            u32 current_lba = 0;
            if (cdplayer) {
                current_lba = cdplayer->GetCurrentAddress();
                CUETrackInfo trackInfo = gadget->GetTrackInfoForLBA(current_lba);

                position_data_reply.adrControl = (trackInfo.track_mode == CUETrack_AUDIO) ? 0x10 : 0x14; // ADR=1, Control based on track type
                position_data_reply.trackNumber = trackInfo.track_number != -1 ? trackInfo.track_number : 0;
                position_data_reply.indexNumber = 0x01; // Assume main index 01. Pregap (index 00) not explicitly handled here.
                position_data_reply.absoluteAddress = gadget->GetAddress(current_lba, msf, false);
                position_data_reply.relativeAddress = (trackInfo.track_number != -1) ?
                                                     gadget->GetAddress(current_lba - trackInfo.track_start, msf, true) :
                                                     gadget->GetAddress(0, msf, true); // Or some default if no track info
            } else {
                 // Default values if no cdplayer
                 position_data_reply.adrControl = 0x10; // Default to audio-like
                 position_data_reply.trackNumber = 0;
                 position_data_reply.indexNumber = 0;
                 position_data_reply.absoluteAddress = gadget->GetAddress(0, msf, false);
                 position_data_reply.relativeAddress = gadget->GetAddress(0, msf, true);
            }

            MLOGDEBUG("ScsiCmdReadSubChannel", "Pos Reply: AudioStatus:0x%02X, Track:%d, Idx:%d, AbsAddr:0x%08X, RelAddr:0x%08X",
                header_reply.audioStatus, position_data_reply.trackNumber, position_data_reply.indexNumber,
                position_data_reply.absoluteAddress, position_data_reply.relativeAddress);

            length_to_send = SIZE_SUBCHANNEL_HEADER_REPLY + SIZE_SUBCHANNEL_01_DATA_REPLY;
            memcpy(gadget->GetInBuffer(), &header_reply, SIZE_SUBCHANNEL_HEADER_REPLY);
            memcpy(gadget->GetInBuffer() + SIZE_SUBCHANNEL_HEADER_REPLY, &position_data_reply, SIZE_SUBCHANNEL_01_DATA_REPLY);
            break;
        }
        case 0x02: // Media Catalog Number (UPC/Bar Code) - ISRC EAN
            MLOGNOTE("ScsiCmdReadSubChannel", "Media Catalog Number (0x02) not implemented.");
            // Fall through to error or send empty data if appropriate
        case 0x03: // Track International Standard Recording Code (ISRC)
            MLOGNOTE("ScsiCmdReadSubChannel", "Track ISRC (0x03) not implemented.");
            // Fall through to error. Need to set sense key.
            // For now, let's follow original behavior which would result in an empty payload if not case 0x01.
            // This will likely result in the host seeing less data than expected.
            // A proper implementation would set an error sense key.
            // For now, let's send CHECK_CONDITION for unsupported format codes.
            gadget->SetSenseParameters(0x05, 0x24, 0x00); // INVALID FIELD IN CDB (for parameter list code)
            send_csw(gadget, CD_CSW_STATUS_FAIL);
            gadget->m_pCurrentCommandHandler = nullptr;
            return;

        default:
            MLOGERR("ScsiCmdReadSubChannel", "Unsupported Sub-Channel Data Format Code: 0x%02x", parameter_list_code);
            gadget->SetSenseParameters(0x05, 0x24, 0x00); // INVALID FIELD IN CDB
            send_csw(gadget, CD_CSW_STATUS_FAIL);
            gadget->m_pCurrentCommandHandler = nullptr;
            return;
    }

    if (allocationLength < length_to_send) {
        length_to_send = allocationLength;
    }

    this->begin_data_in_transfer(gadget, gadget->GetInBuffer(), length_to_send);
    gadget->m_nnumber_blocks = 0;
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = gadget->GetCurrentCSWStatus(); // Usually OK

    gadget->m_pCurrentCommandHandler = nullptr;
}
