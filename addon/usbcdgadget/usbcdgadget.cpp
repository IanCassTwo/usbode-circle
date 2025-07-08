// This is an intentional overwrite of a large section of the file.
// The previous content of CUSBCDGadget::HandleSCSICommand (the large switch statement)
// is being replaced by the new dispatcher logic.
// Other methods in CUSBCDGadget.cpp remain unchanged by this specific operation.

// <Existing content of usbcdgadget.cpp up to HandleSCSICommand()>
// ... (Includes, class definitions, other member functions) ...

//
// usbcdgadget.cpp
//
// CDROM Gadget by Ian Cass, heavily based on
// USB Mass Storage Gadget by Mike Messinides
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2023-2024  R. Stange <rsta2@o2online.de>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FORF A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include <assert.h>
#include <scsitbservice/scsitbservice.h>
#include <cdplayer/cdplayer.h>
#include <circle/koptions.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/sysconfig.h>
#include <usbcdgadget/usbcdgadget.h>
#include <usbcdgadget/usbcdgadgetendpoint.h>
#include <circle/util.h>
#include <math.h>
#include <memory> // For std::make_unique

// Include new handler files as they are created
#include <usbcdgadget/scsi_cmd_00_test_unit_ready.h>
#include <usbcdgadget/scsi_cmd_03_request_sense.h>
#include <usbcdgadget/scsi_cmd_12_inquiry.h>
#include <usbcdgadget/scsi_cmd_28_read10.h>
#include <usbcdgadget/scsi_cmd_1B_start_stop_unit.h>
#include <usbcdgadget/scsi_cmd_1E_prevent_allow_medium_removal.h>
#include <usbcdgadget/scsi_cmd_25_read_capacity10.h>
#include <usbcdgadget/scsi_cmd_BE_read_cd.h>
#include <usbcdgadget/scsi_cmd_BB_set_cd_speed.h>
#include <usbcdgadget/scsi_cmd_2F_verify.h>
#include <usbcdgadget/scsi_cmd_43_read_toc_pma_atip.h>
#include <usbcdgadget/scsi_cmd_42_read_sub_channel.h>
#include <usbcdgadget/scsi_cmd_52_read_track_information.h>
#include <usbcdgadget/scsi_cmd_4A_get_event_status_notification.h>
#include <usbcdgadget/scsi_cmd_AD_read_disc_structure.h>
#include <usbcdgadget/scsi_cmd_51_read_disc_information.h>
#include <usbcdgadget/scsi_cmd_46_get_configuration.h>
#include <usbcdgadget/scsi_cmd_4B_pause_resume.h>
#include <usbcdgadget/scsi_cmd_2B_seek.h>
#include <usbcdgadget/scsi_cmd_47_play_audio_msf.h>
#include <usbcdgadget/scsi_cmd_4E_stop_play_scan.h>
#include <usbcdgadget/scsi_cmd_45_play_audio10.h>
#include <usbcdgadget/scsi_cmd_A5_play_audio12.h>
#include <usbcdgadget/scsi_cmd_55_mode_select10.h>
#include <usbcdgadget/scsi_cmd_1A_mode_sense6.h>
#include <usbcdgadget/scsi_cmd_5A_mode_sense10.h>
#include <usbcdgadget/scsi_cmd_AC_get_performance.h>
#include <usbcdgadget/scsi_cmd_A4_win2k_specific.h>
#include <usbcdgadget/scsi_cmd_D9_tb_list_devices.h>
#include <usbcdgadget/scsi_cmd_D2_DA_tb_get_count.h>
#include <usbcdgadget/scsi_cmd_D0_D7_tb_list_items.h>
#include <usbcdgadget/scsi_cmd_D8_tb_set_next_cd.h>
// ... other handlers
#include <stddef.h>
#include <filesystem>
#include <circle/bcmpropertytags.h>


#define MLOGNOTE(From, ...) CLogger::Get()->Write(From, LogNotice, __VA_ARGS__)
#define MLOGDEBUG(From, ...)  // CLogger::Get ()->Write (From, LogDebug, __VA_ARGS__)
#define MLOGERR(From, ...) CLogger::Get()->Write(From, LogError, __VA_ARGS__)
#define DEFAULT_BLOCKS 16000

const TUSBDeviceDescriptor CUSBCDGadget::s_DeviceDescriptor =
    {
        sizeof(TUSBDeviceDescriptor),
        DESCRIPTOR_DEVICE,
        0x200,  // bcdUSB
        0,      // bDeviceClass
        0,      // bDeviceSubClass
        0,      // bDeviceProtocol
        64,     // bMaxPacketSize0
        USB_GADGET_VENDOR_ID,
        USB_GADGET_DEVICE_ID_CD,
        0x000,    // bcdDevice
        1, 2, 3,  // strings
        1         // num configurations
};

const CUSBCDGadget::TUSBMSTGadgetConfigurationDescriptor CUSBCDGadget::s_ConfigurationDescriptorFullSpeed =
    {
        {
            sizeof(TUSBConfigurationDescriptor),
            DESCRIPTOR_CONFIGURATION,
            sizeof(TUSBMSTGadgetConfigurationDescriptor),
            1,  // bNumInterfaces
            1,
            0,
            0x80,    // bmAttributes (bus-powered)
            500 / 2  // bMaxPower (500mA)
        },
        {
            sizeof(TUSBInterfaceDescriptor),
            DESCRIPTOR_INTERFACE,
            0,                 // bInterfaceNumber
            0,                 // bAlternateSetting
            2,                 // bNumEndpoints
            0x08, 0x02, 0x50,  // bInterfaceClass, SubClass, Protocol
            0                  // iInterface
        },
        {
            sizeof(TUSBEndpointDescriptor),
            DESCRIPTOR_ENDPOINT,
            0x81,                                                                        // IN number 1
            2,                                                                           // bmAttributes (Bulk)
            64,  // wMaxPacketSize
            0                                                                            // bInterval
        },
        {
            sizeof(TUSBEndpointDescriptor),
            DESCRIPTOR_ENDPOINT,
            0x02,                                                                        // OUT number 2
            2,                                                                           // bmAttributes (Bulk)
            64,  // wMaxPacketSize
            0                                                                            // bInterval
        }};

const CUSBCDGadget::TUSBMSTGadgetConfigurationDescriptor CUSBCDGadget::s_ConfigurationDescriptorHighSpeed =
    {
        {
            sizeof(TUSBConfigurationDescriptor),
            DESCRIPTOR_CONFIGURATION,
            sizeof(TUSBMSTGadgetConfigurationDescriptor),
            1,  // bNumInterfaces
            1,
            0,
            0x80,    // bmAttributes (bus-powered)
            500 / 2  // bMaxPower (500mA)
        },
        {
            sizeof(TUSBInterfaceDescriptor),
            DESCRIPTOR_INTERFACE,
            0,                 // bInterfaceNumber
            0,                 // bAlternateSetting
            2,                 // bNumEndpoints
            0x08, 0x02, 0x50,  // bInterfaceClass, SubClass, Protocol
            0                  // iInterface
        },
        {
            sizeof(TUSBEndpointDescriptor),
            DESCRIPTOR_ENDPOINT,
            0x81,                                                                        // IN number 1
            2,                                                                           // bmAttributes (Bulk)
            512,  // wMaxPacketSize
            0                                                                            // bInterval
        },
        {
            sizeof(TUSBEndpointDescriptor),
            DESCRIPTOR_ENDPOINT,
            0x02,                                                                        // OUT number 2
            2,                                                                           // bmAttributes (Bulk)
            512,  // wMaxPacketSize
            0                                                                            // bInterval
        }};

const char* const CUSBCDGadget::s_StringDescriptorTemplate[] =
    {
        "\x04\x03\x09\x04",  // Language ID
        "USBODE",
        "USB Optical Disk Emulator",  // Product (index 2)
        "USBODE00001"         // Template Serial Number (index 3) - will be replaced with hardware serial
    };

CUSBCDGadget::CUSBCDGadget(CInterruptSystem* pInterruptSystem, boolean isFullSpeed, ICueDevice* pDevice)
    : CDWUSBGadget(pInterruptSystem, isFullSpeed ? FullSpeed : HighSpeed),
      m_pDevice(pDevice),
      m_pEP{nullptr, nullptr, nullptr}
{
    MLOGNOTE("CUSBCDGadget::CUSBCDGadget", "entered %d", isFullSpeed);
    m_IsFullSpeed = isFullSpeed;
    CBcmPropertyTags Tags;
    TPropertyTagSerial Serial;
    if (Tags.GetTag(PROPTAG_GET_BOARD_SERIAL, &Serial, sizeof(Serial)))
    {
        snprintf(m_HardwareSerialNumber, sizeof(m_HardwareSerialNumber), "USBODE-%08X", Serial.Serial[0]);
        MLOGNOTE("CUSBCDGadget::CUSBCDGadget", "Using hardware serial: %s (from %08X%08X)", 
                 m_HardwareSerialNumber, Serial.Serial[1], Serial.Serial[0]);
    }
    else
    {
        strcpy(m_HardwareSerialNumber, "USBODE-00000001");
        MLOGERR("CUSBCDGadget::CUSBCDGadget", "Failed to get hardware serial, using fallback: %s", m_HardwareSerialNumber);
    }
    
    m_StringDescriptor[0] = s_StringDescriptorTemplate[0];
    m_StringDescriptor[1] = s_StringDescriptorTemplate[1];
    m_StringDescriptor[2] = s_StringDescriptorTemplate[2];
    m_StringDescriptor[3] = m_HardwareSerialNumber;
    if (pDevice)
        SetDevice(pDevice);

    m_scsi_handlers[0x00] = std::make_unique<ScsiCmdTestUnitReady>();
    m_scsi_handlers[0x03] = std::make_unique<ScsiCmdRequestSense>();
    m_scsi_handlers[0x12] = std::make_unique<ScsiCmdInquiry>();
    m_scsi_handlers[0x28] = std::make_unique<ScsiCmdRead10>();
    m_scsi_handlers[0x1B] = std::make_unique<ScsiCmdStartStopUnit>();
    m_scsi_handlers[0x1E] = std::make_unique<ScsiCmdPreventAllowMediumRemoval>();
    m_scsi_handlers[0x25] = std::make_unique<ScsiCmdReadCapacity10>();
    m_scsi_handlers[0xBE] = std::make_unique<ScsiCmdReadCD>();
    m_scsi_handlers[0xBB] = std::make_unique<ScsiCmdSetCdSpeed>();
    m_scsi_handlers[0x2F] = std::make_unique<ScsiCmdVerify>();
    m_scsi_handlers[0x43] = std::make_unique<ScsiCmdReadTocPmaAtip>();
    m_scsi_handlers[0x42] = std::make_unique<ScsiCmdReadSubChannel>();
    m_scsi_handlers[0x52] = std::make_unique<ScsiCmdReadTrackInformation>();
    m_scsi_handlers[0x4A] = std::make_unique<ScsiCmdGetEventStatusNotification>();
    m_scsi_handlers[0xAD] = std::make_unique<ScsiCmdReadDiscStructure>();
    m_scsi_handlers[0x51] = std::make_unique<ScsiCmdReadDiscInformation>();
    m_scsi_handlers[0x46] = std::make_unique<ScsiCmdGetConfiguration>();
    m_scsi_handlers[0x4B] = std::make_unique<ScsiCmdPauseResume>();
    m_scsi_handlers[0x2B] = std::make_unique<ScsiCmdSeek>();
    m_scsi_handlers[0x47] = std::make_unique<ScsiCmdPlayAudioMsf>();
    m_scsi_handlers[0x4E] = std::make_unique<ScsiCmdStopPlayScan>();
    m_scsi_handlers[0x45] = std::make_unique<ScsiCmdPlayAudio10>();
    m_scsi_handlers[0xA5] = std::make_unique<ScsiCmdPlayAudio12>();
    m_scsi_handlers[0x55] = std::make_unique<ScsiCmdModeSelect10>();
    m_scsi_handlers[0x1A] = std::make_unique<ScsiCmdModeSense6>();
    m_scsi_handlers[0x5A] = std::make_unique<ScsiCmdModeSense10>();
    m_scsi_handlers[0xAC] = std::make_unique<ScsiCmdGetPerformance>();
    m_scsi_handlers[0xA4] = std::make_unique<ScsiCmdWin2kSpecific>();
    m_scsi_handlers[0xD9] = std::make_unique<ScsiCmdTbListDevices>();
    m_scsi_handlers[0xD2] = std::make_unique<ScsiCmdTbGetCount>();
    m_scsi_handlers[0xDA] = std::make_unique<ScsiCmdTbGetCount>();
    m_scsi_handlers[0xD0] = std::make_unique<ScsiCmdTbListItems>();
    m_scsi_handlers[0xD7] = std::make_unique<ScsiCmdTbListItems>();
    m_scsi_handlers[0xD8] = std::make_unique<ScsiCmdTbSetNextCd>();
}

CUSBCDGadget::~CUSBCDGadget(void) {
    assert(0);
}

const void* CUSBCDGadget::GetDescriptor(u16 wValue, u16 wIndex, size_t* pLength) {
    MLOGNOTE("CUSBCDGadget::GetDescriptor", "entered");
    assert(pLength);
    u8 uchDescIndex = wValue & 0xFF;
    switch (wValue >> 8) {
        case DESCRIPTOR_DEVICE:
            MLOGNOTE("CUSBCDGadget::GetDescriptor", "DESCRIPTOR_DEVICE %02x", uchDescIndex);
            if (!uchDescIndex) {
                *pLength = sizeof s_DeviceDescriptor;
                return &s_DeviceDescriptor;
            }
            break;
        case DESCRIPTOR_CONFIGURATION:
            MLOGNOTE("CUSBCDGadget::GetDescriptor", "DESCRIPTOR_CONFIGURATION %02x", uchDescIndex);
            if (!uchDescIndex) {
                *pLength = sizeof(TUSBMSTGadgetConfigurationDescriptor);
		return m_IsFullSpeed?&s_ConfigurationDescriptorFullSpeed : &s_ConfigurationDescriptorHighSpeed;
            }
            break;
        case DESCRIPTOR_STRING:
            if (!uchDescIndex) {
                *pLength = (u8)m_StringDescriptor[0][0];
                return m_StringDescriptor[0];
            } else if (uchDescIndex < 4) {
                return ToStringDescriptor(m_StringDescriptor[uchDescIndex], pLength);
            }
            break;
        default:
            break;
    }
    return nullptr;
}

void CUSBCDGadget::AddEndpoints(void) {
    MLOGNOTE("CUSBCDGadget::AddEndpoints", "entered");
    assert(!m_pEP[EPOut]);
    if (m_IsFullSpeed)
        m_pEP[EPOut] = new CUSBCDGadgetEndpoint(
            reinterpret_cast<const TUSBEndpointDescriptor*>(
                &s_ConfigurationDescriptorFullSpeed.EndpointOut),
            this);
    else
        m_pEP[EPOut] = new CUSBCDGadgetEndpoint(
            reinterpret_cast<const TUSBEndpointDescriptor*>(
                &s_ConfigurationDescriptorHighSpeed.EndpointOut),
            this);
    assert(m_pEP[EPOut]);
    assert(!m_pEP[EPIn]);
    if (m_IsFullSpeed)
        m_pEP[EPIn] = new CUSBCDGadgetEndpoint(
            reinterpret_cast<const TUSBEndpointDescriptor*>(
                &s_ConfigurationDescriptorFullSpeed.EndpointIn),
            this);
    else
        m_pEP[EPIn] = new CUSBCDGadgetEndpoint(
            reinterpret_cast<const TUSBEndpointDescriptor*>(
                &s_ConfigurationDescriptorHighSpeed.EndpointIn),
            this);
    assert(m_pEP[EPIn]);
    m_nState = TCDState::Init;
}

void CUSBCDGadget::SetDevice(ICueDevice* dev) {
    MLOGNOTE("CUSBCDGadget::SetDevice", "entered");
    if (m_pDevice && m_pDevice != dev) {
        MLOGNOTE("CUSBCDGadget::SetDevice", "Changing device");
        delete m_pDevice;
        m_pDevice = nullptr;
        bmCSWStatus = CD_CSW_STATUS_FAIL;
        m_SenseParams.bSenseKey = 0x02;
        m_SenseParams.bAddlSenseCode = 0x3a;
        m_SenseParams.bAddlSenseCodeQual = 0x00;  
	    discChanged = true;
    }
    m_pDevice = dev;
    cueParser = CUEParser(m_pDevice->GetCueSheet());
    MLOGNOTE("CUSBCDGadget::SetDevice", "cue sheet parsed"); // Changed log
    data_skip_bytes = GetSkipbytes();
    data_block_size = GetBlocksize();
    m_CDReady = true;
    MLOGNOTE("CUSBCDGadget::SetDevice", "Block size is %d, m_CDReady = %d", block_size, m_CDReady); // block_size here is a member, not local
    CCDPlayer* cdplayer = static_cast<CCDPlayer*>(CScheduler::Get()->GetTask("cdplayer"));
    if (cdplayer) {
        cdplayer->SetDevice(dev);
        MLOGNOTE("CUSBCDGadget::SetDevice", "Passed device to cd player"); // Changed log
    }
}

int CUSBCDGadget::GetBlocksize() {
    cueParser.restart();
    const CUETrackInfo* trackInfo = cueParser.next_track();
    if (!trackInfo) return 2048; // Default if no tracks
    return GetBlocksizeForTrack(*trackInfo);
}

int CUSBCDGadget::GetBlocksizeForTrack(CUETrackInfo trackInfo) {
    switch (trackInfo.track_mode) {
        case CUETrack_MODE1_2048: return 2048;
        case CUETrack_MODE1_2352: return 2352;
        case CUETrack_MODE2_2352: return 2352;
        case CUETrack_AUDIO:      return 2352;
        default: MLOGERR("CUSBCDGadget::GetBlocksizeForTrack", "Track mode %d not handled", trackInfo.track_mode); return 0;
    }
}

int CUSBCDGadget::GetSkipbytes() {
    cueParser.restart();
    const CUETrackInfo* trackInfo = cueParser.next_track();
    if (!trackInfo) return 0; // Default if no tracks
    return GetSkipbytesForTrack(*trackInfo);
}

int CUSBCDGadget::GetSkipbytesForTrack(CUETrackInfo trackInfo) {
    switch (trackInfo.track_mode) {
        case CUETrack_MODE1_2048: return 0;
        case CUETrack_MODE1_2352: return 16;
        case CUETrack_MODE2_2352: return 24; // Assuming this is Mode 2 Form 1 or Form 2 data that needs header skip
        case CUETrack_AUDIO:      return 0;
        default: MLOGERR("CUSBCDGadget::GetSkipbytesForTrack", "Track mode %d not handled", trackInfo.track_mode); return 0;
    }
}

int CUSBCDGadget::GetMediumType() {
    cueParser.restart();
    const CUETrackInfo* trackInfo = nullptr;
    bool has_audio = false;
    bool has_data = false;
    while ((trackInfo = cueParser.next_track()) != nullptr) {
        if (trackInfo->track_mode == CUETrack_AUDIO) has_audio = true;
        else has_data = true;
    }
    if (has_audio && has_data) return 0x03; // Mixed mode
    if (has_audio) return 0x02; // Audio CD
    if (has_data) return 0x01; // Data CD
    return 0x01; // Default to Data CD
}

CUETrackInfo CUSBCDGadget::GetTrackInfoForTrack(int track_number_to_find) { // Renamed param for clarity
    const CUETrackInfo* trackInfo = nullptr;
    cueParser.restart();
    while ((trackInfo = cueParser.next_track()) != nullptr) {
        if (trackInfo->track_number == track_number_to_find) {
            return *trackInfo;
        }
    }
    CUETrackInfo invalid = {}; invalid.track_number = -1; return invalid;
}

CUETrackInfo CUSBCDGadget::GetTrackInfoForLBA(u32 lba) {
    const CUETrackInfo* trackInfo;
    cueParser.restart();
    CUETrackInfo last_valid_track = {}; last_valid_track.track_number = -1;
    const CUETrackInfo* first_track = cueParser.next_track();

    if (!first_track) { CUETrackInfo invalid = {}; invalid.track_number = -1; return invalid; }
    if (lba == 0) return *first_track;

    // Restart and iterate properly
    cueParser.restart();
    while ((trackInfo = cueParser.next_track()) != nullptr) {
        if (lba >= trackInfo->track_start) {
            last_valid_track = *trackInfo;
        } else { // LBA is less than current track's start, so it must have been in the previous track (last_valid_track)
            return last_valid_track;
        }
    }
    return last_valid_track; // Return the last track if LBA is beyond or within it
}

u32 CUSBCDGadget::GetLeadoutLBA() {
    const CUETrackInfo* trackInfo = nullptr;
    u32 file_offset_last_track = 0;
    u32 sector_length_last_track = 2352; // Default
    u32 data_start_lba_last_track = 0;

    cueParser.restart();
    const CUETrackInfo* last_track_ptr = nullptr;
    while((trackInfo = cueParser.next_track()) != nullptr) {
        last_track_ptr = trackInfo;
    }

    if (last_track_ptr) {
        file_offset_last_track = last_track_ptr->file_offset;
        sector_length_last_track = GetBlocksizeForTrack(*last_track_ptr); // Use actual block size
        if (sector_length_last_track == 0) sector_length_last_track = 2352; // Safety
        data_start_lba_last_track = last_track_ptr->data_start;
    } else {
        return 150; // Default leadout for an empty disc (75 frames/sec * 2 sec)
    }

    u32 deviceSize = (u32)m_pDevice->GetSize();
    if (deviceSize <= file_offset_last_track) return data_start_lba_last_track; // Corrupted or empty last track

    u32 lastTrackBlocks = (deviceSize - file_offset_last_track) / sector_length_last_track;
    return data_start_lba_last_track + lastTrackBlocks;
}

int CUSBCDGadget::GetLastTrackNumber() {
    const CUETrackInfo* trackInfo = nullptr;
    int lastTrack = 0; // Start at 0, if any track found, it will be >= 1
    cueParser.restart();
    while ((trackInfo = cueParser.next_track()) != nullptr) {
        if (trackInfo->track_number > lastTrack)
            lastTrack = trackInfo->track_number;
    }
    return lastTrack == 0 ? 1 : lastTrack; // return 1 if no tracks found, mimics original
}

void CUSBCDGadget::CreateDevice(void) {
    assert(m_pDevice);
}

void CUSBCDGadget::OnSuspend(void) {
    MLOGNOTE("CUSBCDGadget::OnSuspend", "entered");
    delete m_pEP[EPOut]; m_pEP[EPOut] = nullptr;
    delete m_pEP[EPIn];  m_pEP[EPIn] = nullptr;
    m_nState = TCDState::Init;
}

const void* CUSBCDGadget::ToStringDescriptor(const char* pString, size_t* pLength) {
    assert(pString && pLength);
    size_t nLength = 2;
    for (u8* p = m_StringDescriptorBuffer + 2; *pString; pString++) {
        assert(nLength < sizeof m_StringDescriptorBuffer - 1);
        *p++ = (u8)*pString; *p++ = '\0';
        nLength += 2;
    }
    m_StringDescriptorBuffer[0] = (u8)nLength;
    m_StringDescriptorBuffer[1] = DESCRIPTOR_STRING;
    *pLength = nLength;
    return m_StringDescriptorBuffer;
}

int CUSBCDGadget::OnClassOrVendorRequest(const TSetupData* pSetupData, u8* pData) {
    MLOGNOTE("CUSBCDGadget::OnClassOrVendorRequest", "entered");
    if (pSetupData->bmRequestType == 0xA1 && pSetupData->bRequest == 0xFE) { // Get Max LUN
        pData[0] = 0; return 1;
    }
    return -1;
}

void CUSBCDGadget::OnTransferComplete(boolean bIn, size_t nLength) {
    assert(m_nState != TCDState::Init);
    if (bIn) { // IN transfer (to host) complete
        switch (m_nState) {
            case TCDState::SentCSW:
                m_nState = TCDState::ReceiveCBW;
                m_pEP[EPOut]->BeginTransfer(CUSBCDGadgetEndpoint::TransferCBWOut, m_OutBuffer, SIZE_CBW);
                break;
            case TCDState::DataIn: // Data sent to host, check if more blocks or send CSW
                if (m_nnumber_blocks > 0 && m_CDReady) {
                    m_nState = TCDState::DataInRead; // Trigger Update() for next chunk
                } else if (m_nnumber_blocks > 0 && !m_CDReady) {
                     MLOGERR("OnXferComplete DataIn", "CD not ready for more blocks");
                     m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
                     SetSenseParameters(0x02, 0x04, 0x00);
                     SendCSW();
                } else { // All blocks for current command sent
                    SendCSW();
                }
                break;
            case TCDState::SendReqSenseReply: // Request Sense data sent, now send CSW
                SendCSW();
                break;
            default: MLOGERR("OnXferComplete", "dir=in, unhandled state = %i", m_nState); assert(0); break;
        }
    } else { // OUT transfer (from host) complete
        switch (m_nState) {
            case TCDState::ReceiveCBW:
                if (nLength != SIZE_CBW) {
                    MLOGERR("ReceiveCBW", "Invalid CBW len = %i", nLength);
                    m_pEP[EPIn]->StallRequest(true); // Stall IN EP for protocol error
                    m_pEP[EPOut]->StallRequest(true); // Stall OUT EP as well
                    break;
                }
                memcpy(&m_CBW, m_OutBuffer, SIZE_CBW);
                if (m_CBW.dCBWSignature != VALID_CBW_SIG) {
                    MLOGERR("ReceiveCBW", "Invalid CBW sig = 0x%x", m_CBW.dCBWSignature);
                    m_pEP[EPIn]->StallRequest(true); m_pEP[EPOut]->StallRequest(true);
                    break;
                }
                m_CSW.dCSWTag = m_CBW.dCBWTag;
                if (m_CBW.bCBWCBLength > 0 && m_CBW.bCBWCBLength <= 16 && m_CBW.bCBWLUN == 0) {
                    HandleSCSICommand();
                } else { // Invalid CBW fields
                     MLOGERR("ReceiveCBW", "Invalid CBW LUN(%d) or CBLength(%d)", m_CBW.bCBWLUN, m_CBW.bCBWCBLength);
                     m_pEP[EPIn]->StallRequest(true); m_pEP[EPOut]->StallRequest(true);
                     // According to Bulk-Only spec, may need to send CSW with Phase Error.
                     // For now, just stall.
                }
                break;
            case TCDState::DataOut: {
                MLOGDEBUG("OnXferComplete", "DataOut state, received len=%u", nLength);
                if (m_pCurrentCommandHandler != nullptr) {
                    ScsiCmdModeSelect10* mode_select_handler = dynamic_cast<ScsiCmdModeSelect10*>(m_pCurrentCommandHandler);
                    if (mode_select_handler) {
                        mode_select_handler->process_received_data(this, nLength);
                    } else {
                        MLOGWARN("OnXferComplete", "DataOut, but no specific handler like ModeSelect10 active or type mismatch.");
                        // ProcessOut(nLength); // ProcessOut is removed
                        SendCSW();
                        m_pCurrentCommandHandler = nullptr;
                    }
                } else {
                    MLOGERR("OnXferComplete", "DataOut received but no m_pCurrentCommandHandler set!");
                    // ProcessOut(nLength); // ProcessOut is removed
                    SendCSW();
                }
                break;
            }
            default: MLOGERR("OnXferComplete", "dir=out, unhandled state = %i", m_nState); assert(0); break;
        }
    }
}

// ProcessOut is removed. Logic moved to ScsiCmdModeSelect10::process_received_data

void CUSBCDGadget::OnActivate() {
    MLOGNOTE("CD OnActivate", "state = %i", m_nState);
    m_CDReady = true; // Assuming media is ready on activation by default
    m_nState = TCDState::ReceiveCBW;
    m_pEP[EPOut]->BeginTransfer(CUSBCDGadgetEndpoint::TransferCBWOut, m_OutBuffer, SIZE_CBW);
}

void CUSBCDGadget::SendCSW() {
    MLOGDEBUG("CUSBCDGadget::SendCSW", "status 0x%02X, residue %u", m_CSW.bmCSWStatus, m_CSW.dCSWDataResidue);
    memcpy(m_InBuffer, &m_CSW, SIZE_CSW);
    m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferCSWIn, m_InBuffer, SIZE_CSW);
    m_nState = TCDState::SentCSW;
}

void CUSBCDGadget::StartDataInTransfer(void* buffer, size_t length) {
    m_pEP[EPIn]->BeginTransfer(CUSBCDGadgetEndpoint::TransferDataIn, (u8*)buffer, length);
}

bool CUSBCDGadget::IsCDReady() const { return m_CDReady; }

void CUSBCDGadget::SetSenseParameters(u8 sense_key, u8 asc, u8 ascq) {
    m_SenseParams.bSenseKey = sense_key;
    m_SenseParams.bAddlSenseCode = asc;
    m_SenseParams.bAddlSenseCodeQual = ascq;
}

u8 CUSBCDGadget::GetCurrentCSWStatus() const { return bmCSWStatus; }
const char* CUSBCDGadget::GetHardwareSerialNumber() const { return m_HardwareSerialNumber; }

u32 CUSBCDGadget::msf_to_lba(u8 minutes, u8 seconds, u8 frames) {
    u32 lba = ((u32)minutes * 60 * 75) + ((u32)seconds * 75) + (u32)frames;
    return lba - 150; // Adjust for 2-second offset
}

u32 CUSBCDGadget::lba_to_msf(u32 lba, boolean relative) {
    if (!relative) lba += 150;
    u8 minutes = lba / (75 * 60);
    u8 seconds = (lba / 75) % 60;
    u8 frames = lba % 75;
    return (frames << 24) | (seconds << 16) | (minutes << 8) | 0;
}

u32 CUSBCDGadget::GetAddress(u32 lba, int msf, boolean relative) {
    if (msf) return lba_to_msf(lba, relative);
    return htonl(lba);
}

int CUSBCDGadget::GetSectorLengthFromMCS(uint8_t mainChannelSelection) {
    int total = 0;
    if (mainChannelSelection & 0x10) total += 12;   // SYNC
    if (mainChannelSelection & 0x08) total += 4;    // HEADER
    if (mainChannelSelection & 0x04) total += 2048; // USER DATA
    if (mainChannelSelection & 0x02) total += 288;  // EDC + ECC
    return total;
}

int CUSBCDGadget::GetSkipBytesFromMCS(uint8_t mainChannelSelection) {
    int offset = 0;
    if (!(mainChannelSelection & 0x10)) offset += 12; // Skip SYNC
    if (!(mainChannelSelection & 0x08)) offset += 4;  // Skip HEADER
    // User data is next. If not requested, it means we skip it *if other parts are requested after it*.
    // This interpretation is tricky. The original logic assumed skip_bytes is what's *before* the desired data.
    // If USER DATA (0x04) is NOT requested, but EDC/ECC (0x02) IS, then we'd skip user data.
    // But the current skip_bytes is based on what to remove from the *start* of a full sector.
    // This MCS skip might need more refinement if complex MCS combinations are used.
    // For now, this matches the original simple prefix skipping.
    return offset;
}

void CUSBCDGadget::HandleSCSICommand() {
    MLOGDEBUG("CUSBCDGadget::HandleSCSICommand", "SCSI Command is 0x%02x", m_CBW.CBWCB[0]);

    u8 command_opcode = m_CBW.CBWCB[0];
    auto it = m_scsi_handlers.find(command_opcode);

    if (it != m_scsi_handlers.end()) {
        m_pCurrentCommandHandler = it->second.get();
        m_pCurrentCommandHandler->handle_command(m_CBW, this);
    } else {
        MLOGNOTE("CUSBCDGadget::HandleSCSICommand", "Unknown SCSI Command is 0x%02x", command_opcode);
        SetSenseParameters(0x05, 0x20, 0x00); // ILLEGAL REQUEST, INVALID COMMAND OPERATION CODE
        m_CSW.bmCSWStatus = CD_CSW_STATUS_FAIL;
        SendCSW();
        m_pCurrentCommandHandler = nullptr;
    }
    // The original switch statement is now entirely replaced by the dispatcher.
}

void CUSBCDGadget::Update() {
    if (m_pCurrentCommandHandler != nullptr) {
        m_pCurrentCommandHandler->update(this);
    }
    // The old switch(m_nState) for DataInRead is removed from here.
    // Its logic is now within the update() method of specific handlers like ScsiCmdRead10/ScsiCmdReadCD.
    // If m_nState was DataInRead and m_pCurrentCommandHandler was null or didn't handle it,
    // nothing happens in this Update cycle for that old state. This is the desired behavior
    // as handlers are now responsible for their own update cycles.
}

// ... (rest of the file, if any, after Update()) ...
