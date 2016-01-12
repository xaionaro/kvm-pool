/*
    clsync - file tree sync utility based on inotify

    Copyright (C) 2013  Dmitry Yu Okunev <dyokunev@ut.mephi.ru> 0x8E30679C

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

#include "macros.h"

#include <stdlib.h>
#include <string.h>

#include <sys/ipc.h>			// shmget()
#include <sys/shm.h>			// shmget()

#include "malloc.h"
#include "error.h"


void *xmalloc ( size_t size )
{
	debug ( 20, "(%li)", size );
#ifdef PARANOID
	size++;	// Just in case
#endif
	void *ret = malloc ( size );

	if ( ret == NULL )
		critical ( "(%li): Cannot allocate memory.", size );

#ifdef PARANOID
	memset ( ret, 0, size );
#endif
	return ret;
}

void *xcalloc ( size_t nmemb, size_t size )
{
	debug ( 20, "(%li, %li)", nmemb, size );
#ifdef PARANOID
	nmemb++; // Just in case
	size++;	 // Just in case
#endif
	void *ret = calloc ( nmemb, size );

	if ( ret == NULL )
		critical ( "(%li): Cannot allocate memory.", size );

//	memset(ret, 0, nmemb*size);	// Just in case
	return ret;
}

void *xrealloc ( void *oldptr, size_t size )
{
	debug ( 20, "(%p, %li)", oldptr, size );
#ifdef PARANOID
	size++;	// Just in case
#endif
	void *ret = realloc ( oldptr, size );

	if ( ret == NULL )
		critical ( "(%p, %li): Cannot reallocate memory.", oldptr, size );

	return ret;
}

int memory_init()
{
	return 0;
}

void *shm_malloc_try ( size_t size )
{
	void *ret;
#ifdef PARANOID
	size++;
#endif
	int privileged_shmid = shmget ( 0, size, IPC_PRIVATE | IPC_CREAT | 0600 );
	struct shmid_ds shmid_ds;

	if ( privileged_shmid == -1 ) return NULL;

	ret = shmat ( privileged_shmid, NULL, 0 );

	if ( ( long ) ret == -1 ) return NULL;

	debug ( 15, "ret == %p", ret );
	// Forbidding access for others to the pointer
	shmctl ( privileged_shmid, IPC_STAT, &shmid_ds );
	shmid_ds.shm_perm.mode = 0;
	shmctl ( privileged_shmid, IPC_SET,  &shmid_ds );
	// Checking that nobody else attached to the shared memory before access forbidding
	shmctl ( privileged_shmid, IPC_STAT, &shmid_ds );

	if ( shmid_ds.shm_lpid != shmid_ds.shm_cpid ) {
		error ( "A process (pid %u) attached to my shared memory. It's a security problem. Emergency exit." );
		shmdt ( ret );
		return NULL;
	}

	return ret;
}

void *shm_malloc ( size_t size )
{
	void *ret;
	ret = shm_malloc_try ( size );
	critical_on ( ret == NULL );
	return ret;
}

void *shm_calloc ( size_t nmemb, size_t size )
{
	void *ret;
	size_t total_size;
#ifdef PARANOID
	nmemb++;
	size++;
#endif
	total_size = nmemb * size;
	ret = shm_malloc ( total_size );
	critical_on ( ret == NULL );
	memset ( ret, 0, total_size );
	return ret;
}

void shm_free ( void *ptr )
{
	debug ( 25, "(%p)", ptr );
	shmdt ( ptr );
}

