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

#include "ctx.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "kvm-pool.h"

#include "error.h"
#include "malloc.h"
#include "main.h"

#define debug_argv_dump(level, argv)\
	if (unlikely(ctx_p->flags[DEBUG] >= level))\
		argv_dump(level, argv)

static inline void argv_dump ( int debug_level, char **argv )
{
#ifdef _DEBUG_FORCE
	debug ( 19, "(%u, %p)", debug_level, argv );
#endif
	char **argv_p = argv;

	while ( *argv_p != NULL ) {
		debug ( debug_level, "%p: \"%s\"", *argv_p, *argv_p );
		argv_p++;
	}

	return;
}

static int newvncid ( ctx_t *ctx_p )
{
	int new_vnc_id = 0;

	while ( new_vnc_id < ctx_p->vms_max ) {
		int i = 0;

		while ( i < ctx_p->vms_count ) {
			if ( ctx_p->vms[i].vnc_id+256 == new_vnc_id )
				break;

			i++;
		}

		if ( i >= ctx_p->vms_count )
			break;

		new_vnc_id++;
	}

	critical_on ( new_vnc_id >= ctx_p->vms_max );
	return new_vnc_id+256;
}

static char **getargv ( ctx_t *ctx_p, kvm_args_t *args_p, int vnc_id )
{
	int d, s;
	char **argv = ( char ** ) xcalloc ( sizeof ( char * ), MAXARGUMENTS + 2 );
	debug ( 9, "args_p->c == %i", args_p->c );
	s = d = 0;
	argv[d++] = strdup ( KVM );
	argv[d++] = strdup ( "-vnc" );
	{
		char vncidstr[256];
		snprintf ( vncidstr, 256, ":%i", vnc_id );
		argv[d++] = strdup ( vncidstr );
	}

	while ( s < args_p->c ) {
		char *arg        = args_p->v[s];
		char  isexpanded = args_p->isexpanded[s];
		s++;
#ifdef _DEBUG_FORCE
		debug ( 30, "\"%s\" [%p]", arg, arg );
#endif

		if ( isexpanded ) {
#ifdef _DEBUG_FORCE
			debug ( 19, "\"%s\" [%p] is already expanded, just strdup()-ing it", arg, arg );
#endif
			argv[d++] = strdup ( arg );
			continue;
		}

#ifdef PARANOID

		if ( d >= MAXARGUMENTS ) {
			errno = E2BIG;
			critical ( "Too many arguments" );
		}

#endif
		argv[d] = parameter_expand ( ctx_p, strdup ( arg ), 0, NULL, NULL, parameter_get, ctx_p );
#ifdef _DEBUG_FORCE
		debug ( 19, "argv[%u] == %p \"%s\"", d, argv[d], argv[d] );
#endif
		d++;
	}

	argv[d]   = NULL;
#ifdef _DEBUG_FORCE
	debug ( 18, "return %p", argv );
#endif
	return argv;
}


int kvmpool_runspare ( ctx_t *ctx_p )
{
	int new_vnc_id = newvncid ( ctx_p );
	vm_t *vm = &ctx_p->vms[ctx_p->vms_count++];
	ctx_p->vms_spare_count++;
	memset ( vm, 0, sizeof ( *vm ) );
	vm->vnc_id = new_vnc_id;
	vm->pid = fork();

	switch ( vm->pid ) {
		case -1:
			error ( "Cannot fork()." );
			return errno;

		case  0: {
				exit ( execvp ( KVM, getargv ( ctx_p, &ctx_p->kvm_args[SHARGS_PRIMARY], vm->vnc_id ) ) );
			}
	}

	return 0;
}

int kvmpool_prepare_spare_vms ( ctx_t *ctx_p )
{
	while ( ctx_p->vms_spare_count < ctx_p->vms_spare_min ) {
		int rc;
		rc = kvmpool_runspare ( ctx_p );

		if ( rc ) return rc;
	}

	return 0;
}

int ipv4listen ( char *listen_addr )
{
	//char *host     = listen_addr;
	char *port_str = strchr ( listen_addr, ':' );
	critical_on ( port_str == NULL );
	port_str++;
	int port = atoi ( port_str );
	struct sockaddr_in sin;
	int sockfd;
	critical_on ( ( sockfd = socket ( AF_INET, SOCK_STREAM, 0 ) ) == -1 );
	int itrue = 1;
	critical_on ( setsockopt ( sockfd, SOL_SOCKET, SO_REUSEADDR, &itrue, sizeof ( int ) ) == -1 );
	sin.sin_family		= AF_INET;
	sin.sin_port		= htons ( port );
	sin.sin_addr.s_addr	= htonl(INADDR_ANY);//inet_addr ( host );
	memset ( &sin.sin_zero, '\0', 8 );
	critical_on ( bind ( sockfd, ( struct sockaddr * ) &sin, sizeof ( struct sockaddr ) ) == -1 )
	critical_on ( listen ( sockfd, BACKLOG ) == -1 );
	return sockfd;
}

vm_t *kvmpool_findsparevm (ctx_t *ctx_p) {
	return NULL;
}

int kvmpool_attach ( ctx_t *ctx_p, int client_fd )
{
	vm_t *vm = kvmpool_findsparevm ( ctx_p );

	critical_on (vm == NULL);

	vm->client_fd = client_fd;

	return 0;
}

int kvmpool ( ctx_t *ctx_p )
{
	debug ( 2, "" );
	ctx_p->vms = xcalloc ( ctx_p->vms_max, sizeof ( ctx_p->vms ) );
	SAFE ( kvmpool_prepare_spare_vms ( ctx_p ) , return _SAFE_rc );
	ctx_p->listen_fd = ipv4listen ( ctx_p->listen_addr );
	ctx_p->state = STATE_RUNNING;

	while ( ctx_p->state == STATE_RUNNING ) {
		int client_fd = accept ( ctx_p->listen_fd, NULL, NULL );

		if ( client_fd < 0 ) {
			warning ( "client_fd == %i", client_fd );
			continue;
		}

		if ( ctx_p->vms_spare_count == 0 )
			critical_on ( kvmpool_runspare ( ctx_p ) );

		critical_on ( kvmpool_attach ( ctx_p, client_fd ) );
	}

	free ( ctx_p->vms );
	ctx_p->vms = NULL;
	debug ( 2, "finish" );
	return 0;
}

