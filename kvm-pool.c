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
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#ifdef USE_SPLICE
#	include <fcntl.h>
#endif

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

#ifdef USE_SPLICE
static inline int setsock_nonblock ( int sock )
{
	int flags;

	if ( -1 == ( flags = fcntl ( sock, F_GETFL, 0 ) ) )
		flags = 0;

	return fcntl ( sock, F_SETFL, flags | O_NONBLOCK );
}
#endif

static int newvncid ( ctx_t *ctx_p )
{
	int new_vnc_id = 0;

	while ( new_vnc_id < ctx_p->vms_max ) {
		int i = 0;
		int f = 0;

		while ( f < ctx_p->vms_count ) {
			debug ( 30, "i == %i; f == %i, ctx_p->vms_count == %i; new_vnc_id == %i, ctx_p->vms[%i].vnc_id == %i", i, f, ctx_p->vms_count, new_vnc_id, i, ctx_p->vms[i].vnc_id );

			if ( ctx_p->vms[i].pid == 0 ) {
				i++;
				continue;
			}

			if ( ctx_p->vms[i].vnc_id == 256 + new_vnc_id )
				break;

			f++;
			i++;
		}

		debug ( 28, "f == %i; ctx_p->vms_count == %i", f, ctx_p->vms_count );

		if ( f >= ctx_p->vms_count )
			break;

		new_vnc_id++;
	}

	debug ( 20, "new_vnc_id + 256 == %i", new_vnc_id + 256 );
	critical_on ( new_vnc_id >= ctx_p->vms_max );
	return new_vnc_id + 256;
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
	argv[d++] = strdup ( "-net" );
	{
		char tapstr[256];
		snprintf (tapstr, 256, "nic,macaddr=52:54:00:31:14:%02x", vnc_id-256);
		argv[d++] = strdup ( tapstr );
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
	debug ( 4, "" );

	if ( ctx_p->vms_count >= ctx_p->vms_max )
		return ENOMEM;

	int new_vnc_id = newvncid ( ctx_p );
	vm_t *vm = ctx_p->vms;

	while ( vm->pid ) vm++;

	ctx_p->vms_spare_count++;
	ctx_p->vms_count++;
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
	debug ( 18, "" );

	while ( ctx_p->vms_spare_count < ctx_p->vms_spare_min ) {
		int rc;
		rc = kvmpool_runspare ( ctx_p );

		if ( rc ) return rc;
	}

	return 0;
}

int ipv4listen ( char *listen_addr ) // TODO: add support of arbitrary hosts
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
	sin.sin_addr.s_addr	= htonl ( INADDR_ANY ); //inet_addr ( host );
	memset ( &sin.sin_zero, '\0', 8 );
	critical_on ( bind ( sockfd, ( struct sockaddr * ) &sin, sizeof ( struct sockaddr ) ) == -1 )
	critical_on ( listen ( sockfd, BACKLOG ) == -1 );
	return sockfd;
}

int ipv4connect_s ( const char const *host, const uint16_t port ) // TODO: add support of arbitrary hosts
{
	struct sockaddr_in dest = {0};
	int sock;
	SAFE ( ( sock = socket ( AF_INET, SOCK_STREAM, 0 ) ) < 0, return -1 );
	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = htonl ( INADDR_LOOPBACK );
	dest.sin_port = htons ( port );
	SAFE ( connect ( sock, ( struct sockaddr * ) &dest, sizeof ( struct sockaddr ) ) < 0, return -1 );
#ifdef USE_SPLICE
	setsock_nonblock ( sock );
#endif
	return sock;
}

vm_t *kvmpool_findsparevm ( ctx_t *ctx_p )
{
	int i = 0;
	int f = 0;
	debug ( 15, "ctx_p->vms_count == %i", ctx_p->vms_count );

	while ( f < ctx_p->vms_count ) {
		debug ( 25, "ctx_p->vms[i].pid == %i; ctx_p->vms[i].client_fd == %i", ctx_p->vms[i].pid, ctx_p->vms[i].client_fd );

		if ( ctx_p->vms[i].pid == 0 ) {
			i++;
			continue;
		}

		if ( ctx_p->vms[i].client_fd == 0 )
			return &ctx_p->vms[i];

		f++;
		i++;
	}

	return NULL;
}

int kvmpool_closevm ( vm_t *vm )
{
	if ( vm->client_fd ) {
		close ( vm->client_fd );
		vm->client_fd = 0;
	}

	if ( vm->vnc_fd ) {
		close ( vm->vnc_fd );
		vm->vnc_fd = 0;
	}

	if ( vm->pid > 0 ) {
		kill ( vm->pid, 9 );
		int status = 0;
		waitpid ( vm->pid, &status, 0 );
		vm->pid = -1;
	}

	if ( vm->buf != NULL ) {
		free ( vm->buf );
		vm->buf = NULL;
	}

	return 0;
}

static inline int passthrough_dataportion ( int dst, int src, char *buf )
{
	int r;
	debug ( 8, "passthrough_dataportion(%i, %i, buf)", dst, src );

	while ( 1 ) {
#ifdef USE_SPLICE
		debug ( 9,  "splice(%i, NULL, %i, NULL, %i, 0x%x)", src, dst, KVMPOOL_NET_BUFSIZE, SPLICE_F_NONBLOCK );
		r = splice ( src, NULL, dst, NULL, KVMPOOL_NET_BUFSIZE, SPLICE_F_NONBLOCK );
		if (r == 0) {
			error ( "unimplemented case while forwarding a data from %i to %i", src, dst );
			return -1;
		}
		if (r < 0) {
			error ( "got error while forwarding a data from %i to %i", src, dst );
			return r;
		}
#else
		debug ( 9,  "recv(%i, buf, %i, 0x%x)", src, KVMPOOL_NET_BUFSIZE, MSG_DONTWAIT );
		errno = 0;
		r = recv ( src, buf, KVMPOOL_NET_BUFSIZE, MSG_DONTWAIT );
		debug ( 10, "recv() -> %i", r );

		if ( r == 0 )
			return -1;

		if ( errno == EAGAIN )
			break;

		if ( r < 0 ) {
			error ( "got error while receiving from fd == %i", src );
			return r;
		}

		int s = 0;
		while (s<r) {
			debug ( 9, "send(%i, &buf[%i], %i, 0x%x)", dst, s, r-s, 0 );
			s += send ( dst, &buf[s], r-s, 0 );
		}

		if ( s < 0 ) {
			error ( "got error while sending to fd == %i", dst );
			return s;
		}

		if ( s != r ) {
			error ( "sent (%i) != received (%i)", s, r );
			return -1;
		}

#endif
	};

	debug ( 9, "finish" );

	return 0;
}

pthread_mutex_t kvmpool_globalmutex;

void *kvmpool_connectionhandler ( void *_vm )
{
	vm_t *vm = _vm;
	debug ( 3, "" );
	int vnc_fd = 0;
	int connect_try = 0;

	while ( vm->pid > 0 ) {
		vnc_fd = ipv4connect_s ( "127.0.0.1", vm->vnc_id + 5900 );

		if ( vnc_fd > 0 )
			break;

		if ( connect_try > KVMPOOL_CONNECT_TIMEOUT ) {
			error ( "Cannot connect to 127.0.0.1:%u", vm->vnc_id + 5900 );
			vnc_fd = 0;
			break;
		}

		sleep ( 1 );
		connect_try++;
	};

	pthread_mutex_lock ( &kvmpool_globalmutex );

	vm->vnc_fd = vnc_fd;

	pthread_mutex_unlock ( &kvmpool_globalmutex );

	int max_fd = MAX ( vm->client_fd, vm->vnc_fd ) + 1;

	debug ( 3, "vm->client_fd == %i; vm->vnc_fd == %i", vm->client_fd, vm->vnc_fd );

	while ( vm->client_fd && vm->vnc_fd ) {
		fd_set rfds;
		FD_ZERO ( &rfds );
		FD_SET ( vm->client_fd, &rfds );
		FD_SET ( vm->vnc_fd, &rfds );
		debug ( 7, "select(): vm->client_fd == %i; vm->vnc_fd == %i", vm->client_fd, vm->vnc_fd );
		int sret = select ( max_fd, &rfds, NULL, NULL, NULL );
		debug ( 8, "select() -> %i", sret );

		if ( !sret )
			continue;

		if ( sret < 0 )
			break;

		debug ( 7, "FD_ISSET()s" );

		if ( FD_ISSET ( vm->client_fd, &rfds ) )
			if ( passthrough_dataportion ( vm->vnc_fd, vm->client_fd, vm->buf ) )
				break;

		if ( FD_ISSET ( vm->vnc_fd, &rfds ) )
			if ( passthrough_dataportion ( vm->client_fd, vm->vnc_fd, vm->buf ) )
				break;
	}

	debug ( 3, "finish" );
	pthread_mutex_lock ( &kvmpool_globalmutex );
	kvmpool_closevm ( vm );
	pthread_mutex_unlock ( &kvmpool_globalmutex );
	return NULL;
}

int kvmpool_attach ( ctx_t *ctx_p, int client_fd )
{
	vm_t *vm = kvmpool_findsparevm ( ctx_p );
	debug ( 3, "vm == %p", vm );

	if ( vm == NULL )
		return ENOMEM;

	ctx_p->vms_spare_count--;
	vm->client_fd = client_fd;
	vm->buf = xmalloc ( KVMPOOL_NET_BUFSIZE );
	pthread_create ( &vm->handler, NULL, kvmpool_connectionhandler, vm );
	return 0;
}

int kvmpool_gc ( ctx_t *ctx_p )
{
	debug ( 23, "start: ctx_p->vms_count == %i; ctx_p->vms_spare_count == %i", ctx_p->vms_count, ctx_p->vms_spare_count );
	int i = 0;
	int f = 0;

	while ( f < ctx_p->vms_count ) {
		debug ( 30, "ctx_p->vms[%i].pid: %i; ctx_p->vms[%i].vnc_id: %i", i, ctx_p->vms[i].pid, i, ctx_p->vms[i].vnc_id );

		if ( ctx_p->vms[i].pid == 0 ) {
			i++;
			continue;
		}

		if ( ctx_p->vms[i].pid == -1 ) {
			ctx_p->vms[i].pid = 0;
			pthread_join ( ctx_p->vms[i].handler, NULL );
			ctx_p->vms_count--;
			//if (i != ctx_p->vms_count)
			//	memcpy ( &ctx_p->vms[i], &ctx_p->vms[ctx_p->vms_count], sizeof ( *ctx_p->vms ) );
		}

		f++;
		i++;
	}

	debug ( 20, "finish: ctx_p->vms_count == %i; ctx_p->vms_spare_count == %i", ctx_p->vms_count, ctx_p->vms_spare_count );
	return 0;
}

int kvmpool_idle ( ctx_t *ctx_p )
{
	SAFE ( kvmpool_gc ( ctx_p ), ( void ) 0 );
	SAFE ( kvmpool_prepare_spare_vms ( ctx_p ) , ( void ) 0 );
	return 0;
}

void *kvmpool_idlehandler ( void *_ctx_p )
{
	ctx_t *ctx_p = _ctx_p;
	int i = 0;

	while ( ctx_p->state == STATE_RUNNING ) {
		usleep ( 100000 );

		if ( ! ( i++ % 10 ) ) {
			pthread_mutex_lock ( &kvmpool_globalmutex );
			kvmpool_idle ( ctx_p );
			pthread_mutex_unlock ( &kvmpool_globalmutex );
		}
	}

	return NULL;
}

int kvmpool ( ctx_t *ctx_p )
{
	debug ( 2, "" );
	ctx_p->vms = xcalloc ( ctx_p->vms_max, sizeof ( ctx_p->vms ) );
	SAFE ( kvmpool_prepare_spare_vms ( ctx_p ) , return _SAFE_rc );
	ctx_p->listen_fd = ipv4listen ( ctx_p->listen_addr );
	ctx_p->state = STATE_RUNNING;
	pthread_t idlehandler;
	pthread_create ( &idlehandler, NULL, kvmpool_idlehandler, ctx_p );

	while ( ctx_p->state == STATE_RUNNING ) {
		int client_fd = accept ( ctx_p->listen_fd, NULL, NULL );

		if ( client_fd < 0 ) {
			warning ( "client_fd == %i", client_fd );
			continue;
		}

		pthread_mutex_lock ( &kvmpool_globalmutex );
		kvmpool_idle ( ctx_p );

		if ( ctx_p->vms_spare_count == 0 )
			if ( kvmpool_runspare ( ctx_p ) ) {
				close ( client_fd );
				pthread_mutex_unlock ( &kvmpool_globalmutex );
				continue;
			}

#ifdef USE_SPLICE
		setsock_nonblock ( client_fd );
#endif

		if ( kvmpool_attach ( ctx_p, client_fd ) )
			close ( client_fd );

		pthread_mutex_unlock ( &kvmpool_globalmutex );
	}

	ctx_p->state = STATE_EXIT;
	int i = 0;

	while ( i < ctx_p->vms_count )
		kvmpool_closevm ( &ctx_p->vms[i++] );

	ctx_p->vms_count = 0;
	ctx_p->vms_spare_count = 0;
	close ( ctx_p->listen_fd );
	free ( ctx_p->vms );
	ctx_p->vms = NULL;
	debug ( 2, "finish" );
	pthread_join ( idlehandler, NULL );
	return 0;
}

