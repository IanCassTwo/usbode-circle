//
// scsi_cmd_D8_tb_set_next_cd.h
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
#ifndef _CIRCLE_USB_GADGET_SCSI_CMD_D8_TB_SET_NEXT_CD_H
#define _CIRCLE_USB_GADGET_SCSI_CMD_D8_TB_SET_NEXT_CD_H

#include <usbcdgadget/scsi_command_handler.h>

class ScsiCmdTbSetNextCd : public ScsiCommandHandler {
public:
    ScsiCmdTbSetNextCd();
    ~ScsiCmdTbSetNextCd() override;

    void handle_command(const TUSBCDCBW& cbw, CUSBCDGadget* gadget) override;
};

#endif // _CIRCLE_USB_GADGET_SCSI_CMD_D8_TB_SET_NEXT_CD_H
