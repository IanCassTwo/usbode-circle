//
// usbmstgadgetendpoint.h
//
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
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#ifndef _circle_usb_gadget_usbmsdgadgetendpoint_h
#define _circle_usb_gadget_usbmsdgadgetendpoint_h

#include <circle/usb/gadget/dwusbgadgetendpoint.h>
#include <circle/usb/usb.h>
#include <circle/types.h>

class CUSBMMSDGadget;


class CUSBMMSDGadgetEndpoint : public CDWUSBGadgetEndpoint /// Endpoint of the USB mass storage gadget
{
public:
	CUSBMMSDGadgetEndpoint (const TUSBEndpointDescriptor *pDesc, CUSBMMSDGadget *pGadget);
	~CUSBMMSDGadgetEndpoint (void);

	void OnActivate (void) override;

	void OnTransferComplete (boolean bIn, size_t nLength) override;

private:
	friend class CUSBMMSDGadget;

	enum TMMSDTransferMode
	{
		TransferCBWOut,
		TransferDataOut,
		TransferDataIn,
		TransferCSWIn
	};

	void BeginTransfer (TMMSDTransferMode Mode, void *pBuffer, size_t nLength);

	void StallRequest(boolean bIn);

private:
	CUSBMMSDGadget *m_pGadget;
};

#endif
