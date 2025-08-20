#ifndef _ICUEDEVICE_H
#define _ICUEDEVICE_H

#include <circle/device.h>

enum class Type {
    ICueDevice,
    CueBinFileDevice,
    HTTPFileDevice,
};

class ICueDevice : public CDevice {
public:
    ICueDevice() = default;
    virtual ~ICueDevice() = default;

    /// \return Current offset in the device, (u64)-1 on error
    virtual u64 Tell() const = 0;

    /// \return Cue sheet string, or nullptr if not available
    virtual const char* GetCueSheet() const = 0;

    virtual Type GetType() const { return Type::ICueDevice; }
};
#endif
