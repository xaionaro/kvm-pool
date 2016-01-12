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


#ifndef __KVMPOOL_CTX_H
#define __KVMPOOL_CTX_H

#include "common.h"

#include <sys/types.h>
#include <unistd.h>


#define OPTION_FLAGS		(1<<10)
#define OPTION_LONGOPTONLY	(1<<9)
#define OPTION_CONFIGONLY	(1<<8)
#define NOTOPTION		(3<<8)
enum flags_enum {
	HELP			= 'h',
	SHOW_VERSION		= 'V',
	DEBUG			= 'd',
	CONFIG_FILE		= 'c',
	CONFIG_GROUP		= 'K',
	VMS_MIN			= 'm',
	VMS_MAX			= 'M',
	VMS_SPARE_MIN		= 's',
	VMS_SPARE_MAX		= 'S',
	KILL_ON_DISCONNECT	= 'r',
	LISTEN			= 'L',
	OUTPUT_METHOD		= 'o',

	CONFIG_GROUP_INHERITS	=  0 | OPTION_LONGOPTONLY,
	KVM_ARGS		=  1 | OPTION_LONGOPTONLY,
};
typedef enum flags_enum flags_t;


enum shargsid {
	SHARGS_PRIMARY = 0,
	SHARGS_MAX,
};

struct kvm_args {
	char	*v[MAXARGUMENTS];
	int	 c;
	char	 isexpanded[MAXARGUMENTS];
};
typedef struct kvm_args kvm_args_t;

#define STATE_STARTING(state_p) (state_p == NULL)
enum state_enum {
	STATE_EXIT      = 0,
	STATE_STARTING,
	STATE_RUNNING,
	STATE_SYNCHANDLER_ERR,
	STATE_REHASH,
	STATE_PREEXIT,
	STATE_TERM,
	STATE_THREAD_GC,
	STATE_INITSYNC,
	STATE_HOLDON,
	STATE_UNKNOWN
};
typedef enum state_enum state_t;

struct vm {
	pid_t	pid;
	int	vnc_id;
	int	vnc_fd;
	int	client_fd;
};
typedef struct vm vm_t;

struct ctx {
	volatile state_t state;

	pid_t		 pid;
	char		 pid_str[65];
	size_t		 pid_str_len;

	char		*config_path;
	const char	*config_group;

	int		 vms_min;
	int		 vms_max;
	int		 vms_spare_min;
	int		 vms_spare_max;
	vm_t		*vms;
	int		 vms_count;
	int		 vms_spare_count;

	char		 listen_addr[256];

	kvm_args_t kvm_args[SHARGS_MAX];

	char *flags_values_raw[OPTION_FLAGS];

	int flags[OPTION_FLAGS];
	int flags_set[OPTION_FLAGS];
};
typedef struct ctx ctx_t;

#endif

