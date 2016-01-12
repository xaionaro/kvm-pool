/*
    kvm-pool  -  utility  to  manage  KVM (Kernel Virtual Machine)
    instances as a pool for rdesktop.

    Copyright (C) 2016 Dmitry Yu Okunev <dyokunev@ut.mephi.ru> 0x8E30679C

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef __KVMPOOL_COMMON_H
#define __KVMPOOL_COMMON_H

#include "macros.h"

#define _DEBUG_SUPPORT
#define _DEBUG_FORCE

#define PROGRAM "kvm-pool"
#define REVISION ""
#define AUTHOR "Dmitry Yu Okunev <dyokunev@ut.mephi.ru>"
#define VERSION_MAJ 0
#define VERSION_MID 0
#define VERSION_MIN 0

#define MAXARGUMENTS 255
#define ALLOC_PORTION (1<<10) /* 1  KiX */

#define OUTPUT_LOCK_TIMEOUT             (100*1000*1000)
#define WAITPID_TIMED_GRANULARITY        (30*1000*1000)

#define KVM "kvm"

#define DEFAULT_VMS_MIN 1
#define DEFAULT_VMS_MAX 64
#define DEFAULT_VMS_SPARE_MIN 1
#define DEFAULT_VMS_SPARE_MAX 8
#define DEFAULT_KILL_ON_DISCONNECT 1
#define DEFAULT_LISTEN "0.0.0.0:5900"

#define SYSLOG_BUFSIZ                   (1<<16)
#define SYSLOG_FLAGS                    (LOG_PID|LOG_CONS)
#define SYSLOG_FACILITY                 LOG_DAEMON

#define BACKLOG 10

#define CONFIG_PATHS                    { ".kvm-pool.conf", "/etc/kvm-pool/kvm-pool.conf", "/etc/kvm-pool.conf", "/usr/local/etc/kvm-pool/kvm-pool.conf", "/usr/local/etc/kvm-pool.conf", NULL }

#define DEFAULT_CONFIG_GROUP "default"

#define DEFAULT_KVM_ARGS ""

#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE

enum paramsource_enum {
	PS_UNKNOWN       = 0,
	PS_ARGUMENT,
	PS_CONFIG,
	PS_CONTROL,
	PS_DEFAULTS,
	PS_CORRECTION,
};
typedef enum paramsource_enum paramsource_t;

#endif

