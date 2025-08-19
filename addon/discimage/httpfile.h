#ifndef _HTTPDEVICE_H
#define _HTTPDEVICE_H

#include <circle/device.h>
#include <circle/fs/partitionmanager.h>
#include <circle/interrupt.h>
#include <circle/logger.h>
#include <circle/sysconfig.h>
#include <circle/timer.h>
#include <circle/types.h>
#include <circle/net/socket.h>
#include <circle/net/dnsclient.h>
#include <circle/net/ipaddress.h>
#include <circle/net/http.h>

#include "filetype.h"
#include "cuedevice.h"

class HTTPFileDevice : public ICueDevice {
   public:
    HTTPFileDevice(const char *pFileURL, const char *pCueURL = nullptr);
    ~HTTPFileDevice(void);

    int Read(void* pBuffer, size_t nCount);
    int Write(const void* pBuffer, size_t nCount);
    u64 Seek(u64 ullOffset);
    u64 GetSize(void) const;
    u64 Tell() const;
    const char* GetCueSheet() const;

   private:
    boolean Connect(void);
    boolean ParseURL(const char *pURL);
    THTTPStatus SendRequest(const char *pRequest, unsigned char *pBuffer, size_t *pLength);
    THTTPStatus SendHeadRequest(const char *pRequest, size_t *pLength);
    boolean ConvertIPString (const char *pIPString, CIPAddress *pIPAddress);
    char *findHttpBody(char *buffer, size_t len);
    void hexDump(const char *data, size_t len, size_t bytesPerLine);

    FileType m_FileType;
    char *m_pURL;
    char *m_pHost;
    u16 m_nPort = 80;
    char *m_pPath;
    unsigned m_nSize;
    unsigned m_nPos;
    char* m_pCueSheet;
    bool haveHostname = false;

    static constexpr const char* default_cue_sheet =
        "FILE \"image.iso\" BINARY\n"
        "  TRACK 01 MODE1/2048\n"
        "    INDEX 01 00:00:00\n";

    CNetSubSystem *m_pNet;
    CDNSClient *m_pDNSClient;
    CSocket *m_pSocket;
    CIPAddress m_ServerIP;
};

#endif
