//
// scsi_cmd_46_get_configuration.cpp
//
// Handler for SCSI command GET CONFIGURATION (0x46).
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
#include <usbcdgadget/scsi_cmd_46_get_configuration.h>
#include <usbcdgadget/usbcdgadget.h> // For CUSBCDGadget, feature structs, etc.
#include <circle/logger.h>          // For MLOGDEBUG
#include <circle/util.h>            // For memcpy, memset, htonl

ScsiCmdGetConfiguration::ScsiCmdGetConfiguration() {}
ScsiCmdGetConfiguration::~ScsiCmdGetConfiguration() {}

void ScsiCmdGetConfiguration::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    int rt = cbw.CBWCB[1] & 0x03; // Request Type
    int starting_feature = (cbw.CBWCB[2] << 8) | cbw.CBWCB[3];
    u16 allocationLength = (cbw.CBWCB[7] << 8) | cbw.CBWCB[8];

    MLOGDEBUG("ScsiCmdGetConfiguration", "GET CONFIGURATION (0x46), RT: %d, Feature: 0x%04X, AllocLen: %u",
             rt, starting_feature, allocationLength);

    u8* buffer = gadget->GetInBuffer();
    memset(buffer, 0, allocationLength); // Zero out the portion of the buffer we might use

    int current_data_len = 0;

    // Space for the header first. Its dataLength field will be set at the end.
    current_data_len += sizeof(TUSBCDFeatureHeaderReply);

    // Lambda to simplify copying feature descriptors
    auto copy_feature = [&](const void* feature_data, size_t feature_size) {
        if ((current_data_len + feature_size) <= gadget->MaxInMessageSize) { // Check against overall buffer size
            memcpy(buffer + current_data_len, feature_data, feature_size);
            current_data_len += feature_size;
        } else {
            MLOGERR("ScsiCmdGetConfiguration", "Buffer overflow prevented when copying feature.");
        }
    };

    // RT field:
    // 00b: Return all feature descriptors, current and persistent, starting with specified feature.
    // 01b: Return current feature descriptor specified.
    // 02b: Return all feature descriptors, current and persistent, starting with feature 0000h. (Original code used this for RT 00/01)
    // The original code's rt=00 and rt=01 cases were identical to rt=02 (all features from 0000h).
    // Let's try to follow the spec more closely.

    if (rt == 0x00 || rt == 0x02) { // All features (current & persistent), starting from 'starting_feature' or 0x0000
        if (rt == 0x02) starting_feature = 0x0000;

        // Profile List Feature (0x0000)
        if (starting_feature <= 0x0000) {
            copy_feature(&gadget->profile_list, sizeof(gadget->profile_list));
            // And its associated profile(s) - CD-ROM Profile (0x0008)
            copy_feature(&gadget->cdrom_profile, sizeof(gadget->cdrom_profile));
        }
        // Core Feature (0x0001)
        if (starting_feature <= 0x0001) copy_feature(&gadget->core, sizeof(gadget->core));
        // Morphing Feature (0x0002)
        if (starting_feature <= 0x0002) copy_feature(&gadget->morphing, sizeof(gadget->morphing));
        // Removable Medium Feature (0x0003)
        if (starting_feature <= 0x0003) copy_feature(&gadget->mechanism, sizeof(gadget->mechanism));
        // Multi-Read Feature (0x001D)
        if (starting_feature <= 0x001D) copy_feature(&gadget->multiread, sizeof(gadget->multiread));
        // CD Read Feature (0x001E)
        if (starting_feature <= 0x001E) copy_feature(&gadget->cdread, sizeof(gadget->cdread));
        // Power Management Feature (0x0100)
        if (starting_feature <= 0x0100) copy_feature(&gadget->powermanagement, sizeof(gadget->powermanagement));
        // CD Audio Analog Play Feature (0x0103) - Original code had 'audioplay'
        if (starting_feature <= 0x0103) copy_feature(&gadget->audioplay, sizeof(gadget->audioplay));
        // Add other features here if supported and starting_feature condition met

    } else if (rt == 0x01) { // Only the specified current feature
        switch (starting_feature) {
            case 0x0000: // Profile List
                copy_feature(&gadget->profile_list, sizeof(gadget->profile_list));
                copy_feature(&gadget->cdrom_profile, sizeof(gadget->cdrom_profile)); // Also send current profile
                break;
            case 0x0001: copy_feature(&gadget->core, sizeof(gadget->core)); break;
            case 0x0002: copy_feature(&gadget->morphing, sizeof(gadget->morphing)); break;
            case 0x0003: copy_feature(&gadget->mechanism, sizeof(gadget->mechanism)); break;
            case 0x001D: copy_feature(&gadget->multiread, sizeof(gadget->multiread)); break;
            case 0x001E: copy_feature(&gadget->cdread, sizeof(gadget->cdread)); break;
            case 0x0100: copy_feature(&gadget->powermanagement, sizeof(gadget->powermanagement)); break;
            case 0x0103: copy_feature(&gadget->audioplay, sizeof(gadget->audioplay)); break;
            default:
                MLOGWARN("ScsiCmdGetConfiguration", "Requested specific feature 0x%04X not supported.", starting_feature);
                // Send error or empty valid response? Spec says CHECK CONDITION if feature not current/supported.
                // For now, let's send an empty valid response (header only).
                current_data_len = sizeof(TUSBCDFeatureHeaderReply); // Reset to only header
                break;
        }
    } else { // Invalid RT value
         MLOGERR("ScsiCmdGetConfiguration", "Invalid RT value: %d", rt);
         gadget->SetSenseParameters(0x05, 0x24, 0x00); // INVALID FIELD IN CDB
         send_csw(gadget, CD_CSW_STATUS_FAIL);
         gadget->m_pCurrentCommandHandler = nullptr;
         return;
    }

    // Populate the header
    TUSBCDFeatureHeaderReply header_reply;
    // Data Length field is (total length of all feature descriptors that follow)
    // current_data_len currently includes the header size. So, (current_data_len - sizeof(header))
    header_reply.dataLength = htonl(current_data_len - sizeof(TUSBCDFeatureHeaderReply));
    header_reply.reserved = 0;
    header_reply.currentProfile = htons(PROFILE_CDROM); // Assuming PROFILE_CDROM is defined (0x0008)
    memcpy(buffer, &header_reply, sizeof(TUSBCDFeatureHeaderReply));

    // Final length to send, capped by allocationLength
    if (allocationLength < current_data_len) {
        current_data_len = allocationLength;
    }

    this->begin_data_in_transfer(gadget, buffer, current_data_len);
    gadget->m_nnumber_blocks = 0;
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;

    gadget->m_pCurrentCommandHandler = nullptr;
}
