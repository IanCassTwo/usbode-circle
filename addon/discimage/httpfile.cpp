//
// This device makes a remotely hosted file look like a local file to Circle
// using HTTP range requests. Performance is improved by using persistent
// connections
//
// Copyright (C) 2025 Ian Cass
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
#include "httpfile.h"

#include <assert.h>
#include <circle/stdarg.h>
#include <circle/util.h>
#include <stdlib.h>
#include <string.h>

LOGMODULE("HTTPDevice");

#define HTTP_PORT 80

static const char default_cue_sheet[] = "FILE \"image.iso\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n";

// The URLs in the argument must be http only not https and may contain
// either a ip address or hostname
HTTPFileDevice::HTTPFileDevice(CNetSubSystem *pNet, const char *pFileURL, const char *pCueURL) :
    m_FileType(FileType::UNKNOWN),
    m_pURL(0),
    m_pHost(0),
    m_nPort(HTTP_PORT),
    m_pPath(0),
    m_nSize(0),
    m_nPos(0),
    m_pCueSheet(0),
    m_pNet(pNet),
    m_pDNSClient(0),
    m_pTCPConnection(0)
{
    assert(pNet);

    m_pURL = new char[strlen(pFileURL) + 1];
    strcpy(m_pURL, pFileURL);

    if (!ParseURL(m_pURL))
    {
        LOG_ERROR("Cannot parse URL: %s", m_pURL);
        return;
    }

    m_pDNSClient = new CDNSClient(m_pNet);
    assert(m_pDNSClient);

    if (m_pDNSClient->Resolve(m_pHost, &m_ServerIP))
    {
        LOG("Resolved %s to %s", m_pHost, m_ServerIP.ToString().Get());
    }
    else
    {
        LOG_ERROR("Cannot resolve host: %s", m_pHost);
        delete m_pDNSClient;
        m_pDNSClient = 0;
        return;
    }

    if (!Connect())
    {
        return;
    }

    char request[256];
    sprintf(request, "HEAD %s HTTP/1.1\r\nHost: %s\r\n\r\n", m_pPath, m_pHost);

    char response[1024];
    int nBytes = SendRequest(request, response, sizeof(response) - 1);
    if (nBytes <= 0)
    {
        LOG_ERROR("Failed to get file size");
        return;
    }
    response[nBytes] = 0;

    const char *pContentLength = strstr(response, "Content-Length:");
    if (pContentLength)
    {
        m_nSize = atoll(pContentLength + 15);
        LOG("File size: %llu", m_nSize);
    }
    else
    {
        LOG_ERROR("Cannot find Content-Length header");
    }

    if (pCueURL != nullptr) {
        // If we were given a cue sheet URL, request it via HTTP and store it in
        // m_pCueSheet
        // This is a simplified implementation that assumes the cue sheet is small
        // enough to fit in a single response.
        char cueRequest[256];
        sprintf(cueRequest, "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", pCueURL, m_pHost);
        char cueResponse[4096];
        nBytes = SendRequest(cueRequest, cueResponse, sizeof(cueResponse) - 1);
        if (nBytes > 0)
        {
            cueResponse[nBytes] = 0;
            char *pBody = strstr(cueResponse, "\r\n\r\n");
            if (pBody)
            {
                pBody += 4;
                m_pCueSheet = new char[strlen(pBody) + 1];
                strcpy(m_pCueSheet, pBody);
                m_FileType = FileType::CUE;
            }
        }
    } else {
        // If we were not given a cue sheet
        // make a copy of our default cue sheet
        size_t len = strlen(default_cue_sheet);
        m_pCueSheet = new char[len + 1];
        strcpy(m_pCueSheet, default_cue_sheet);
        m_FileType = FileType::ISO;
    }
}

HTTPFileDevice::~HTTPFileDevice(void) {
    if (m_pURL) delete[] m_pURL;
    if (m_pHost) delete[] m_pHost;
    if (m_pPath) delete[] m_pPath;
    if (m_pCueSheet) delete[] m_pCueSheet;
    if (m_pDNSClient) delete m_pDNSClient;
    if (m_pTCPConnection)
    {
        m_pTCPConnection->Close();
        delete m_pTCPConnection;
    }
}

boolean HTTPFileDevice::ParseURL(const char *pURL)
{
    char *pURLCopy = new char[strlen(pURL) + 1];
    strcpy(pURLCopy, pURL);

    char *pProtocol = strtok(pURLCopy, "://");
    if (!pProtocol || strcmp(pProtocol, "http") != 0)
    {
        delete[] pURLCopy;
        return false;
    }

    char *pHostPort = strtok(NULL, "/");
    if (!pHostPort)
    {
        delete[] pURLCopy;
        return false;
    }

    char* pPathTemp = strtok(NULL, "");
    if (pPathTemp)
    {
        m_pPath = new char[strlen(pPathTemp) + 2];
        m_pPath[0] = '/';
        strcpy(m_pPath + 1, pPathTemp);
    }
    else
    {
        m_pPath = new char[2];
        strcpy(m_pPath, "/");
    }

    char *pPort = strchr(pHostPort, ':');
    if (pPort)
    {
        *pPort = 0;
        m_nPort = atoi(pPort + 1);
    }

    m_pHost = new char[strlen(pHostPort) + 1];
    strcpy(m_pHost, pHostPort);

    delete[] pURLCopy;
    return true;
}

boolean HTTPFileDevice::Connect(void)
{
    if (m_pTCPConnection)
    {
        if (m_pTCPConnection->IsConnected())
        {
            return true;
        }
        delete m_pTCPConnection;
    }

    m_pTCPConnection = new CTCPConnection(m_pNet->GetConfig(), m_pNet->GetNetworkLayer(), m_ServerIP, m_nPort, 0);
    if (!m_pTCPConnection)
    {
        LOG_ERROR("Cannot create TCP connection");
        return false;
    }

    if (m_pTCPConnection->Connect() != 0)
    {
        LOG_ERROR("Cannot connect to %s:%d", m_pHost, m_nPort);
        delete m_pTCPConnection;
        m_pTCPConnection = 0;
        return false;
    }

    LOG("Connected to %s:%d", m_pHost, m_nPort);
    return true;
}

int HTTPFileDevice::SendRequest(const char *pRequest, void *pBuffer, size_t nSize)
{
    if (!m_pTCPConnection || !m_pTCPConnection->IsConnected())
    {
        if (!Connect())
        {
            return -1;
        }
    }

    int nSent = m_pTCPConnection->Send(pRequest, strlen(pRequest));
    if (nSent != (int)strlen(pRequest))
    {
        LOG_ERROR("Failed to send request");
        return -1;
    }

    int nReceived = m_pTCPConnection->Receive(pBuffer, nSize);
    if (nReceived < 0)
    {
        LOG_ERROR("Failed to receive response");
        return -1;
    }

    return nReceived;
}

int HTTPFileDevice::Read(void *pBuffer, size_t nSize) {
    if (m_nPos >= m_nSize)
    {
        return 0; // End of file
    }

    if (m_nPos + nSize > m_nSize)
    {
        nSize = m_nSize - m_nPos;
    }

    char request[256];
    sprintf(request, "GET %s HTTP/1.1\r\nHost: %s\r\nRange: bytes=%llu-%llu\r\n\r\n",
            m_pPath, m_pHost, m_nPos, m_nPos + nSize - 1);

    // A bit of a hack, we assume the headers won't be more than 1K
    char response[nSize + 1024]; // Buffer for response headers + content
    int nBytes = SendRequest(request, response, sizeof(response) - 1);
    if (nBytes <= 0)
    {
        LOG_ERROR("Failed to read from file");
        return -1;
    }
    response[nBytes] = 0;

    char *pBody = strstr(response, "\r\n\r\n");
    if (!pBody)
    {
        LOG_ERROR("Invalid HTTP response");
        return -1;
    }
    pBody += 4;

    int headerLen = pBody - response;
    int bodyLen = nBytes - headerLen;

    memcpy(pBuffer, pBody, bodyLen);
    m_nPos += bodyLen;

    return bodyLen;
}

int HTTPFileDevice::Write(const void *pBuffer, size_t nSize) {
    // Read-only device
    // Not supported
    return -1;
}

u64 HTTPFileDevice::Tell() const {
    return m_nPos;
}

u64 HTTPFileDevice::Seek(u64 nOffset) {
    m_nPos = nOffset;
    return m_nPos;
}

u64 HTTPFileDevice::GetSize(void) const {
    return m_nSize;
}

const char *HTTPFileDevice::GetCueSheet() const {
    return m_pCueSheet;
}
