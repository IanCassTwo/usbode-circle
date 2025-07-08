//
// scsi_cmd_4A_get_event_status_notification.cpp
//
// Handler for SCSI command GET EVENT STATUS NOTIFICATION (0x4A).
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
#include <usbcdgadget/scsi_cmd_4A_get_event_status_notification.h>
#include <usbcdgadget/usbcdgadget.h> // For CUSBCDGadget, reply structs, etc.
#include <circle/logger.h>          // For MLOGNOTE
#include <circle/util.h>            // For memcpy, memset, htons

ScsiCmdGetEventStatusNotification::ScsiCmdGetEventStatusNotification() {}
ScsiCmdGetEventStatusNotification::~ScsiCmdGetEventStatusNotification() {}

void ScsiCmdGetEventStatusNotification::handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) {
    u8 polled = cbw.CBWCB[1] & 0x01;
    u8 notification_class_request = cbw.CBWCB[4]; // Bitmask of classes host is interested in
    u16 allocationLength = (cbw.CBWCB[7] << 8) | cbw.CBWCB[8];

    MLOGNOTE("ScsiCmdGetEventStatusNotification", "GET EVENT STATUS NOTIFICATION (0x4A), Polled: %u, ClassReq: 0x%02X, AllocLen: %u",
             polled, notification_class_request, allocationLength);

    if (polled == 0) { // Asynchronous mode
        MLOGWARN("ScsiCmdGetEventStatusNotification", "Async GET EVENT STATUS not supported.");
        gadget->SetSenseParameters(0x05, 0x24, 0x00); // ILLEGAL REQUEST, INVALID FIELD IN CDB
        send_csw(gadget, CD_CSW_STATUS_FAIL);
        gadget->m_pCurrentCommandHandler = nullptr;
        return;
    }

    // Polled mode
    TUSBCDEventStatusReplyHeader event_header;
    memset(&event_header, 0, sizeof(event_header));
    event_header.supportedEventClass = 0x10; // We only support Media event class (bit 4)

    int current_data_len = sizeof(TUSBCDEventStatusReplyHeader);
    u8* buffer_ptr = gadget->GetInBuffer();

    // Check for Media Status Event (Class 4)
    if (notification_class_request & (1 << 4)) {
        event_header.notificationClass |= (1 << 4); // Indicate Media event class is being reported

        TUSBCDEventStatusReplyEvent media_event;
        memset(&media_event, 0, sizeof(media_event));
        media_event.eventCode = 0x00; // No event by default
        media_event.data[0] = 0x02;   // Media Present

        if (gadget->discChanged) {
            MLOGNOTE("ScsiCmdGetEventStatusNotification", "Reporting Media Event: New Media");
            media_event.eventCode = 0x02; // New Media
            // Only clear discChanged if we can actually report it (i.e., allocationLength is sufficient)
            if (allocationLength >= (current_data_len + sizeof(TUSBCDEventStatusReplyEvent))) {
                gadget->discChanged = false;
            }
        } else {
            MLOGNOTE("ScsiCmdGetEventStatusNotification", "Reporting Media Event: No Change or Eject (not fully distinct)");
            // Could also be Eject Request (0x01), but no specific flag for it in original code.
            // Media Present (0x02) is a reasonable status.
            // If no specific event, eventCode 0x00 (No event) but media_status_byte indicates current state.
            // The original code implies if discChanged is false, it's "No Change" for the event code.
            // data[0] (Media Status)
            // 00h NO_MEDIA
            // 01h MEDIA_PRESENT_CHANGED (tray just closed)
            // 02h MEDIA_PRESENT_NO_CHANGE (tray already closed)
            // 03h MEDIA_PRESENT_MEDIA_REMOVED (tray just opened)
            // For now, keep it simple as per original:
            media_event.data[0] = gadget->IsCDReady() ? 0x02 : 0x00; // Media Present (no change) or No Media
        }

        // Only add this event if there's space in the allocationLength
        if (allocationLength >= (current_data_len + sizeof(TUSBCDEventStatusReplyEvent))) {
             memcpy(buffer_ptr + current_data_len, &media_event, sizeof(TUSBCDEventStatusReplyEvent));
             current_data_len += sizeof(TUSBCDEventStatusReplyEvent);
        }
    }
    // Other event classes (Operational Change, Power Management, etc.) could be checked here if supported.

    event_header.eventDataLength = htons(current_data_len - sizeof(TUSBCDEventStatusReplyHeader));
    memcpy(buffer_ptr, &event_header, sizeof(TUSBCDEventStatusReplyHeader));

    // Final length is current_data_len, capped by allocationLength
    if (allocationLength < current_data_len) {
        current_data_len = allocationLength;
    }

    this->begin_data_in_transfer(gadget, gadget->GetInBuffer(), current_data_len);
    gadget->m_nnumber_blocks = 0;
    gadget->m_nState = CUSBCDGadget::TCDState::DataIn;
    gadget->m_CSW.bmCSWStatus = CD_CSW_STATUS_OK;

    gadget->m_pCurrentCommandHandler = nullptr;
}
