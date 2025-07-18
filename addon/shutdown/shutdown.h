//
// syslogdaemon.h
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2017  R. Stange <rsta2@o2online.de>
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
#ifndef _cshutdown_h
#define _cshutdown_h

#include <circle/sched/task.h>
#include <circle/logger.h>
#include <devicestate/devicestate.h>

class CShutdown : public CTask {
   public:
    CShutdown(TShutdownMode mode, int msdelay);
    ~CShutdown(void);
    void Run(void);

   private:
    const TShutdownMode m_mode;
    const int m_msdelay;
};

#endif
