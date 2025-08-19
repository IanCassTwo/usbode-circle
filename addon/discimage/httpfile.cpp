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
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <circle/net/in.h>

LOGMODULE("HTTPDevice");

#define HTTP_PORT 80

// The URLs in the argument must be http only not https and may contain
// either a ip address or hostname
HTTPFileDevice::HTTPFileDevice(const char *pFileURL, const char *pCueURL) :
    m_FileType(FileType::UNKNOWN),
    m_pURL(0),
    m_pHost(0),
    m_nPort(HTTP_PORT),
    m_pPath(0),
    m_nSize(0),
    m_nPos(0),
    m_pCueSheet(0),
    m_pDNSClient(0),
    m_pSocket(0)
{

    m_pURL = new char[strlen(pFileURL) + 1];
    strcpy(m_pURL, pFileURL);

    if (!ParseURL(m_pURL))
    {
        LOGERR("Cannot parse URL: %s", m_pURL);
        return;
    }

    m_pNet = CNetSubSystem::Get();
    m_pDNSClient = new CDNSClient(m_pNet);
    assert(m_pDNSClient);

    if (haveHostname) {
	    LOGNOTE("We have a hostname to resolve");
	    if (m_pDNSClient->Resolve(m_pHost, &m_ServerIP))
	    {
		CString ip;
		m_ServerIP.Format(&ip);
		LOGNOTE("Resolved %s to %s", m_pHost, ip.c_str());
	    }
	    else
	    {
	    	// TODO improve error handling, assertion?
		LOGERR("Cannot resolve host: %s", m_pHost);
		delete m_pDNSClient;
		m_pDNSClient = 0;
		return;
	    }
    } else {
	    LOGNOTE("We have an ip address");
	    if (ConvertIPString(m_pHost, &m_ServerIP)) {
	        CString ip;
	        m_ServerIP.Format(&ip);
	        LOGNOTE("Converted %s to %s", m_pHost, ip.c_str());
	    } else {
	    	// TODO improve error handling, assertion?
		LOGERR("Cannot resolve host: %s", m_pHost);
		delete m_pDNSClient;
		m_pDNSClient = 0;
		return;
	    }
    }

    if (!Connect())
    {
	LOGERR("Cannot connect to server");
        return;
    }

    LOGNOTE("Connected to server, continuing with constructor");

    // Find content length
    // TODO grab this from the cue sheet response if we have one
    // else do this
    char request[256];
    sprintf(request, "HEAD %s HTTP/1.1\r\nHost: %s\r\n\r\n", m_pPath, m_pHost);

    LOGNOTE("Sending head request");

    m_nSize = 0;
    int responseCode = SendHeadRequest(request, &m_nSize);

    // TODO handle 404

    if (m_nSize)
    {
        LOGNOTE("File size: %u", m_nSize);
    }
    else
    {
        LOGERR("Cannot find Content-Length header or Content-Length is zero");
    }

    if (pCueURL == nullptr) {
        // If we were not given a cue sheet
        // make a copy of our default cue sheet
        size_t len = strlen(default_cue_sheet);
        m_pCueSheet = new char[len + 1];
        strcpy(m_pCueSheet, default_cue_sheet);
        m_FileType = FileType::ISO;
    } else {
        // If we were given a cue sheet URL, request it via HTTP and store it in
        // m_pCueSheet
        // This is a simplified implementation that assumes the cue sheet is small
        // enough to fit in a single response.
        char cueRequest[256];
        sprintf(cueRequest, "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", pCueURL, m_pHost);
        m_pCueSheet = new char[32 * 1024];
	size_t nBytes = sizeof(m_pCueSheet) - 1;
        int responseCode = SendRequest(cueRequest, (unsigned char*)m_pCueSheet, &nBytes);
        m_FileType = FileType::CUEBIN;
    }
}

HTTPFileDevice::~HTTPFileDevice(void) {
    if (m_pURL) delete[] m_pURL;
    if (m_pHost) delete[] m_pHost;
    if (m_pPath) delete[] m_pPath;
    if (m_pCueSheet) delete[] m_pCueSheet;
    if (m_pDNSClient) delete m_pDNSClient;
    if (m_pSocket)
    {
        delete m_pSocket;
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

    char *p = m_pHost;
    haveHostname = false;

    while (*p) {
        if (!isdigit((unsigned char)*p) && *p != '.') {
            haveHostname = true;
            break;
        }
        p++;
    }

    delete[] pURLCopy;
    return true;
}

boolean HTTPFileDevice::Connect(void)
{
    //LOGNOTE("Connecting to server");
    if (m_pSocket)
    {
	// how to test if it's working or not before we destroy it?
        delete m_pSocket;
    }

    m_pSocket = new CSocket(m_pNet, IPPROTO_TCP);
    if (!m_pSocket)
    {
        LOGERR("Cannot create socket");
        return false;
    }

    //LOGNOTE("Created a new connection");

    if (m_pSocket->Connect(m_ServerIP, m_nPort) != 0)
    {
        LOGERR("Cannot connect to %s:%d", m_pHost, m_nPort);
        delete m_pSocket;
        m_pSocket = 0;
        return false;
    }

    //LOGNOTE("Connected to %s:%d", m_pHost, m_nPort);
    return true;
}

THTTPStatus HTTPFileDevice::SendHeadRequest(const char *pRequest, size_t *pLength)
{
    LOGNOTE("SendHeadRequest");
    if (!m_pSocket)
    {
        if (!Connect())
        {
            LOGNOTE("Not connected!");
            return HTTPRequestTimeout;
        }
    }

    if (m_pSocket->Send(pRequest, strlen(pRequest), 0) < 0)
    {
        delete m_pSocket;
        m_pSocket = 0;
        return HTTPConnectionReset;
    }

    char response[4096];   // big enough for headers
    int totalBytes = 0;

    // Read until we see the end of headers (\r\n\r\n)
    while (totalBytes < (int)sizeof(response) - 1)
    {
        int nBytes = m_pSocket->Receive(response + totalBytes, sizeof(response) - 1 - totalBytes, 0);
        if (nBytes <= 0)
            return HTTPConnectionReset;

        totalBytes += nBytes;
        response[totalBytes] = 0;

        if (strstr(response, "\r\n\r\n"))
            break; // end of headers found
    }

    // Find Content-Length header (case-insensitive)
    const char *p = response;
    const char *pContentLength = 0;

    while (p && *p)
    {
        // Find the start of the next line
        const char* lineEnd = strstr(p, "\r\n");
        if (!lineEnd) break;

        // Check if this line starts with "Content-Length:" (case-insensitive)
        if (strncasecmp(p, "Content-Length:", 15) == 0)
        {
            // Move past "Content-Length:"
            const char* val = p + 15;

            // Skip any spaces or tabs
            while (*val == ' ' || *val == '\t') ++val;

            // Parse the number (using your custom parser or atoll)
            *pLength = atoll(val);
            break; // done
        }

        // Move to the next line
        p = lineEnd + 2; // skip \r\n
    }

    return HTTPOK;
}


THTTPStatus HTTPFileDevice::SendRequest(const char *pRequest, unsigned char *pBuffer, size_t *pLength)
{
    //LOGNOTE("SendRequest");
    if (!m_pSocket )
    {
        if (!Connect())
        {
            LOGNOTE("Not connected!");
            return HTTPRequestTimeout;
        }
    }

    	if (m_pSocket->Send (pRequest, strlen(pRequest), 0) < 0)
	{
		delete m_pSocket;
		m_pSocket = 0;

		return HTTPConnectionReset;
	}

	// receive HTTP response and parse it
	unsigned nState = 0;
	unsigned nLine = 0;
	unsigned nChar = 0;
	unsigned nLength = 0;
	boolean bChunked = FALSE;
	unsigned long ulBytes = 0;

	char Buffer[(16 * 2352) + 1024];
	char Line[HTTP_MAX_REQUEST_LINE];
	int nResult;
	char *pSavePtr;

	while (   nState < 5
	       && (nResult = m_pSocket->Receive (Buffer, sizeof Buffer, 0)) > 0)
	{
		for (int i = 0; i < nResult; i++)
		{
			u8 chChar = Buffer[i];

			switch (nState)
			{
			case 0:				// response header
				if (chChar == '\r')
				{
					continue;
				}

				if (chChar == '\n')		// end of line
				{
					if (nChar == 0)		// empty line is end of header
					{
						nState = bChunked ? 2 : 1;
						//LOGNOTE("End of headers, switching to state %d", nState);
						nChar = 0;
					}
					else
					{
						if (nLine++ == 0)	// first line?
						{
							// "HTTP/1.x 200 OK" or 206 Partial Content expected
							//LOGNOTE("First line %s", Line);
							char *pToken;
							if ((pToken = strtok_r(Line, "/", &pSavePtr)) == 0
							    || strcmp(pToken, "HTTP") != 0
							    || (pToken = strtok_r(0, " ", &pSavePtr)) == 0
							    || (pToken = strtok_r(0, " ", &pSavePtr)) == 0)
							{
							    // tokenization failed
							    LOGERR("Malformed HTTP status line: %s", Line);
							    delete m_pSocket;
							    m_pSocket = 0;
							    return HTTPInvalidResponseCode;
							}

							// tokenization succeeded, parse the status code
							char *pEnd;
							unsigned long ulStatus = strtoul(pToken, &pEnd, 10);
							if (pEnd == pToken || *pEnd != '\0' || ulStatus < 200 || ulStatus >= 300)
							{
							    LOGERR("Unexpected HTTP response: %s", Line);
							    delete m_pSocket;
							    m_pSocket = 0;
							    return HTTPInvalidResponseCode;
							}
						}
						else
						{
							//LOGNOTE("Header line %s", Line);
							// check for transfer encoding option
							char *pToken = strtok_r (Line, ": ", &pSavePtr);
							if (   pToken != 0
							    && strcasecmp (pToken, "Transfer-Encoding") == 0)
							{
								pToken = strtok_r (0, " ", &pSavePtr);
								if (pToken != 0
								    && strcasecmp (pToken, "chunked") == 0)
								{
									bChunked = TRUE;
								}
							}
						}

						nChar = 0;
					}
				}
				else
				{
					// accumulate option line
					if (nChar < sizeof Line-1)
					{
						Line[nChar++] = chChar;
						Line[nChar] = '\0';
					}
				}
				break;

			case 1:				// non-chunked data: simply copy it
				assert (pLength != 0);
				if (nLength >= *pLength)
				{
					delete m_pSocket;
					m_pSocket = 0;

					return HTTPContentBufferTooSmall;
				}

				*pBuffer++ = (u8) chChar;
				nLength++;
				break;

			case 2:				// chunk header
				if (chChar == '\r')
				{
					continue;
				}

				if (chChar == '\n')	// end of header?
				{
					char *pEnd;
					ulBytes = strtoul (Line, &pEnd, 16);	// convert chunk length
					if (   pEnd != 0
					    && *pEnd != '\0')
					{
						delete m_pSocket;
						m_pSocket = 0;

						return HTTPInvalidChunkHeader;
					}

					nState = ulBytes != 0 ? 3 : 5;	// length 0 is end of file
				}
				else
				{
					// accumulate chunk header line
					if (nChar < sizeof Line-1)
					{
						Line[nChar++] = chChar;
						Line[nChar] = '\0';
					}
				}
				break;

			case 3:				// chunk data: copy ulBytes
				assert (pLength != 0);
				if (nLength >= *pLength)
				{
					delete m_pSocket;
					m_pSocket = 0;

					return HTTPContentBufferTooSmall;
				}

				//LOGNOTE("chunked copying char %02x", chChar);
				*pBuffer++ = (u8) chChar;
				nLength++;

				if (--ulBytes == 0)
				{
					nState = 4;
				}
				break;

			case 4:				// chunk trailer
				if (chChar == '\r')
				{
					continue;
				}

				if (chChar != '\n')	// newline expected
				{
					delete m_pSocket;
					m_pSocket = 0;

					return HTTPInvalidChunkHeader;
				}

				nChar = 0;
				nState = 2;
				break;
			}
		}
	}

	// close everything and exit
	if (   nState < 5
	    && nState != 1)
	{
		delete m_pSocket;
		m_pSocket = 0;

		return HTTPConnectionReset;
	}

	delete m_pSocket;
	m_pSocket = 0;

	assert (pLength != 0);
	assert (nLength <= *pLength);
	*pLength = nLength;

	return HTTPOK;
}

int HTTPFileDevice::Read(void *pBuffer, size_t nSize) {

    //LOGNOTE("Request to read %d bytes", nSize);
    if (m_nPos >= m_nSize)
    {
        return 0; // End of file
    }

    if (m_nPos + nSize > m_nSize)
    {
        nSize = m_nSize - m_nPos;
    }

    char request[256];
    sprintf(request, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nRange: bytes=%u-%u\r\n\r\n",
            m_pPath, m_pHost, m_nPos, m_nPos + nSize - 1);

    //LOGNOTE("HTTP Request is %s", request);

    unsigned nBytes = nSize;
    int responseCode = SendRequest(request, (unsigned char *)pBuffer, &nBytes);

    //LOGNOTE("HTTP Response Code is %d", responseCode);

    // TODO handle response code
   
    if (nBytes <= 0)
    {
        LOGERR("Failed to receive a response");
        return -1;
    }

    //LOGNOTE("Got %d nBytes back", nBytes);
    //hexDump((char*)pBuffer, nBytes, 16);

    return nBytes;
}

void HTTPFileDevice::hexDump(const char *data, size_t len, size_t bytesPerLine) {
    char line[256];  // buffer for one line
    size_t i, j;

    if (bytesPerLine == 0) bytesPerLine = 16;

    for (i = 0; i < len; i += bytesPerLine) {
        char *ptr = line;
        int n;

        // Write offset
        n = sprintf(ptr, "%08zx  ", i);
        ptr += n;

        // Write hex bytes
        for (j = 0; j < bytesPerLine; ++j) {
            if (i + j < len)
                n = sprintf(ptr, "%02x ", (unsigned char)data[i + j]);
            else
                n = sprintf(ptr, "   ");
            ptr += n;
        }

        // Spacer
        n = sprintf(ptr, " ");
        ptr += n;

        // Write ASCII
        for (j = 0; j < bytesPerLine && i + j < len; ++j) {
            unsigned char c = data[i + j];
            n = sprintf(ptr, "%c", isprint(c) ? c : '.');
            ptr += n;
        }

        // Output the line with LOGNOTE
        LOGNOTE(line);
    }
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
    //LOGNOTE("Seeking to %u", m_nPos);
    return m_nPos;
}

u64 HTTPFileDevice::GetSize(void) const {
    return m_nSize;
}

const char* HTTPFileDevice::GetCueSheet() const {
    return m_pCueSheet;
}

char* HTTPFileDevice::findHttpBody(char *buffer, size_t len) {
    char *end = buffer + len;
    for (char *p = buffer; p + 3 < end; ++p) {
        if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') {
            return p + 4;  // start of body
        }
    }
    return NULL;  // not found
}

boolean HTTPFileDevice::ConvertIPString (const char *pIPString, CIPAddress *pIPAddress)
{
	u8 IPAddress[IP_ADDRESS_SIZE];

	for (unsigned i = 0; i <= 3; i++)
	{
		char *pEnd = 0;
		assert (pIPString != 0);
		unsigned long nNumber = strtoul (pIPString, &pEnd, 10);

		if (i < 3)
		{
			if (    pEnd == 0
			    || *pEnd != '.')
			{
				return FALSE;
			}
		}
		else
		{
			if (    pEnd != 0
			    && *pEnd != '\0')
			{
				return FALSE;
			}
		}

		if (nNumber > 255)
		{
			return FALSE;
		}

		IPAddress[i] = (u8) nNumber;

		assert (pEnd != 0);
		pIPString = pEnd + 1;
	}

	assert (pIPAddress != 0);
	pIPAddress->Set (IPAddress);

	return TRUE;
}
