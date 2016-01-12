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

#include "common.h"
#include <getopt.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <glib.h>

#include "ctx.h"

#include "malloc.h"
#include "error.h"
#include "kvm-pool.h"

static const struct option long_options[] = {
	{"version",		optional_argument,	NULL,	SHOW_VERSION},
	{"help",		optional_argument,	NULL,	HELP},
	{"debug",		optional_argument,	NULL,	DEBUG},

	{"config-file",		required_argument,	NULL,	CONFIG_FILE},
	{"config-group",	required_argument,	NULL,	CONFIG_GROUP},
	{"config-group-inherits", required_argument,	NULL,	CONFIG_GROUP_INHERITS},
	{"min-vms",		required_argument,	NULL,	VMS_MIN},
	{"max-vms",		required_argument,	NULL,	VMS_MAX},
	{"min-spare",		required_argument,	NULL,	VMS_SPARE_MIN},
	{"max-spare",		required_argument,	NULL,	VMS_SPARE_MAX},
	{"kill-vm-on-disconnect", required_argument,	NULL,	KILL_ON_DISCONNECT},
	{"listen",		required_argument,	NULL,	LISTEN},
	{"output-method",	required_argument,	NULL,	OUTPUT_METHOD},

	{NULL,			0,			NULL,	0}
};

int syntax()
{
	info ( "possible options:" );
	int i = -1;

	while ( long_options[++i].name != NULL ) {
		switch ( long_options[i].val ) {
			case KVM_ARGS:
				continue;
		}

		if ( long_options[i].val & OPTION_CONFIGONLY )
			continue;

		info ( "\t--%-24s%c%c%s", long_options[i].name,
		       long_options[i].val & OPTION_LONGOPTONLY ? ' ' : '-',
		       long_options[i].val & OPTION_LONGOPTONLY ? ' ' : long_options[i].val,
		       ( long_options[i].has_arg == required_argument ? " argument" : "" ) );
	}

	exit ( EINVAL );
}

int ncpus;
pid_t parent_pid = 0;

pid_t waitpid_timed ( pid_t child_pid, int *status_p, long sec, long nsec )
{
	struct timespec ts;
	int status;
	ts.tv_sec  = sec;
	ts.tv_nsec = nsec;

	while ( ts.tv_sec >= 0 ) {
		if ( waitpid ( child_pid, &status, WNOHANG ) < 0 ) {
			if ( errno == ECHILD )
				return child_pid;

			return -1;
		} else if ( status_p != NULL )
			*status_p = status;

		ts.tv_nsec -= WAITPID_TIMED_GRANULARITY;

		if ( ts.tv_nsec < 0 ) {
			ts.tv_nsec += 1000 * 1000 * 1000;
			ts.tv_sec--;
		}
	}

	return 0;
}

void child_sigchld()
{
	if ( getppid() != 1 )
		return;

	debug ( 1, "Got SIGCHLD (parent ended). Exit." );
	exit ( -1 );
	return;
}

int sethandler_sigchld ( void ( *handler ) () )
{
	struct sigaction sa;
	sa.sa_handler = handler;
	sigemptyset ( &sa.sa_mask );
	sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	critical_on ( sigaction ( SIGCHLD, &sa, 0 ) == -1 );
	return 0;
}

# ifndef __linux__
void *watchforparent ( void *parent_pid_p )
{
	while ( 1 ) {
		child_sigchld();
		sleep ( SLEEP_SECONDS );
	}

	return NULL;
}
# endif

pthread_t pthread_watchforparent;
pid_t fork_helper()
{
	pid_t pid = fork();

	if ( !pid ) {	// is child?
		parent_pid = getppid();
		// Anti-zombie:
# ifdef __linux__
		// Linux have support of "prctl(PR_SET_PDEATHSIG, signal);"
		sethandler_sigchld ( child_sigchld );
		prctl ( PR_SET_PDEATHSIG, SIGCHLD );
# else
		pthread_create ( &pthread_watchforparent, NULL, watchforparent, &parent_pid );
# endif
		debug ( 20, "parent_pid == %u", parent_pid );
		return 0;
	}

	return pid;
}

int version()
{
	info ( PROGRAM" v%i.%i.%i"REVISION"\n\t"AUTHOR
	       , VERSION_MAJ, VERSION_MID, VERSION_MIN );
	exit ( 0 );
}

/**
 * @brief 			Gets raw (string) an option value by an option name
 *
 * @param[in]	_ctx_p		Context
 @ @param[in]	variable_name	The name of the option
 *
 * @retval	char *		Pointer to newly allocated string, if successful
 * @retval	NULL		On error
 *
 */
const char *parameter_get ( const char *variable_name, void *_ctx_p )
{
	const ctx_t *ctx_p = _ctx_p;
	const struct option *long_option_p = long_options;
	int param_id = -1;
	debug ( 8, "(\"%s\", %p)", variable_name, ctx_p );

	while ( long_option_p->name != NULL ) {
		if ( !strcmp ( long_option_p->name, variable_name ) ) {
			param_id = long_option_p->val;
			break;
		}

		long_option_p++;
	}

	if ( param_id == -1 ) {
		errno = ENOENT;
		return NULL;
	}

	debug ( 9, "ctx_p->flags_values_raw[%i] == \"%s\"", param_id, ctx_p->flags_values_raw[param_id] );
	return ctx_p->flags_values_raw[param_id];
}

/**
 * @brief 			Expands option values
 *
 * @param[in]	ctx_p		Context
 * @param[in]	arg		An allocated string with unexpanded value. Will be free'd
 * @param[out]	macro_count_p	A pointer to count of found macro-s
 * @param[out]	expand_count_p	A pointer to count of expanded macro-s
 * @param[in]	parameter_get	A function to resolve macro-s
 * @param[in]	parameter_get_arg An argument to the function
 *
 * @retval	char *		Pointer to newly allocated string, if successful
 * @retval	NULL		On error
 *
 */
char *parameter_expand (
    ctx_t *ctx_p,
    char *arg,
    int exceptionflags,
    int *macro_count_p,
    int *expand_count_p,
    const char * ( *parameter_get ) ( const char *variable_name, void *arg ),
    void *parameter_get_arg
)
{
	debug ( 9, "(ctx_p, \"%s\" [%p], ...)", arg, arg );
	char *ret = NULL;
	size_t ret_size = 0, ret_len = 0;
#ifdef PARANOID

	if ( arg == NULL ) {
		errno = EINVAL;
		return NULL;
	}

#endif

	if ( macro_count_p != NULL )
		*macro_count_p  = 0;

	if ( expand_count_p != NULL )
		*expand_count_p = 0;

	char *ptr = &arg[-1];

	while ( 1 ) {
		ptr++;

		switch ( *ptr ) {
			case 0:
				if ( ret == NULL ) {
					debug ( 3, "Expanding value \"%s\" to \"%s\" (case #1)", arg, arg );
					return arg;
				}

				ret[ret_len] = 0;
				debug ( 3, "Expanding value \"%s\" to \"%s\" (case #0)", arg, ret );
				free ( arg );
				return ret;

			case '%': {
					if ( ptr[1] == '%' ) {
						ret[ret_len++] = * ( ptr++ );
						break;
					}

					debug ( 25, "A macro" );
					char nest_searching = 1;
					char *ptr_nest = ptr;

					while ( nest_searching ) {
						ptr_nest++;

						switch ( *ptr_nest ) {
							case 0:
								ret[ret_len] = 0;

								if ( ! ( exceptionflags & 1 ) )
									warning ( "Unexpected end of macro-substitution \"%s\" in value \"%s\"; result value is \"%s\"", ptr, arg, ret );

								free ( arg );
								return ret;

							case '%': {
									char       *variable_name;
									const char *variable_value;
									size_t      variable_value_len;

									if ( macro_count_p != NULL )
										( *macro_count_p )++;

									nest_searching = 0;
									*ptr_nest = 0;
									variable_name  = &ptr[1];
									debug ( 15, "The macro is \"%s\"", variable_name );

									if ( !strcmp ( variable_name, "PID" ) ) {
										debug ( 35, "\"PID\"", variable_name );

										if ( !*ctx_p->pid_str ) {
											snprintf ( ctx_p->pid_str, 64, "%u", ctx_p->pid );
											ctx_p->pid_str_len = strlen ( ctx_p->pid_str );
										}

										variable_value     = ctx_p->pid_str;
										variable_value_len = ctx_p->pid_str_len;
									} else if ( *variable_name >= 'A' && *variable_name <= 'Z' && ( exceptionflags & 4 ) ) {	// Lazy substitution, preserving the value
										debug ( 35, "Lazy substitution", variable_name );
										variable_value     =  ptr;
										variable_value_len = ( ptr_nest - ptr + 1 );
										parameter_get ( variable_name, parameter_get_arg );
									} else {									// Substituting
										debug ( 35, "Substitution", variable_name );
										errno = 0;
										variable_value = parameter_get ( variable_name, parameter_get_arg );

										if ( variable_value == NULL ) {
											if ( ! ( exceptionflags & 2 ) && ( errno != ENOENT ) )
												warning ( "Variable \"%s\" is not set (%s)", variable_name, strerror ( errno ) );

											*ptr_nest = '%';
											errno = 0;
											break;
										}

										variable_value_len = strlen ( variable_value );

										if ( expand_count_p != NULL )
											( *expand_count_p )++;
									}

									*ptr_nest = '%';

									if ( ret_len + variable_value_len + 1 >= ret_size ) {
										ret_size = ret_len + variable_value_len + 1 + ALLOC_PORTION;
										ret      = xrealloc ( ret, ret_size );
									}

									memcpy ( &ret[ret_len], variable_value, variable_value_len );
									ret_len += variable_value_len;
									break;
								}
						}
					}

					ptr = ptr_nest;
					break;
				}

			default: {
					if ( ret_len + 2 >= ret_size ) {
						ret_size += ALLOC_PORTION + 2;
						ret       = xrealloc ( ret, ret_size );
					}

					ret[ret_len++] = *ptr;
					break;
				}
		}
	}

	error ( "Unknown internal error" );
	return arg;
}

static inline int kvm_arg ( char *arg, size_t arg_len, void *_ctx_p, enum shargsid shargsid )
{
	ctx_t *ctx_p = _ctx_p;
	debug ( 9, "(\"%s\" [%p], %u, %p, %u)", arg, arg, arg_len, _ctx_p, shargsid );

	if ( ctx_p->kvm_args[shargsid].c >= MAXARGUMENTS - 2 ) {
		errno = E2BIG;
		error ( "There're too many kvm arguments "
		        "(%u > "XTOSTR ( MAXARGUMENTS - 2 ) "; arg == \"%s\").",
		        arg );
		return errno;
	}

#ifdef _DEBUG_FORCE
	debug ( 14, "ctx_p->kvm_args[%u].v[%u] = %p", shargsid, ctx_p->kvm_args[shargsid].c, arg );
#endif
	ctx_p->kvm_args[shargsid].v[ctx_p->kvm_args[shargsid].c++] = arg;
	return 0;
}

static int kvm_arg0 ( char *arg, size_t arg_len, void *_ctx_p )
{
	return kvm_arg ( arg, arg_len, _ctx_p, SHARGS_PRIMARY );
}

/* strtol wrapper with error checks */
static inline long xstrtol ( const char *str, int *err )
{
	long res;
	char *endptr;
	errno = 0;
	res = strtol ( str, &endptr, 0 );

	if ( errno || *endptr ) {
		error ( "argument \"%s\" can't be parsed as a number", str );
		*err = EINVAL;
	}

	return res;
}

static int parse_parameter ( ctx_t *ctx_p, uint16_t param_id, char *arg, paramsource_t paramsource )
{
	int ret = 0;
#ifdef _DEBUG_FORCE
	fprintf ( stderr, "Force-Debug: parse_parameter(): %i: %i = \"%s\"\n", paramsource, param_id, arg );
#endif

	switch ( paramsource ) {
		case PS_CONTROL:
		case PS_ARGUMENT:
			if ( param_id & OPTION_CONFIGONLY ) {
				syntax();
				return 0;
			}

			ctx_p->flags_set[param_id] = 1;
			break;

		case PS_CONFIG:
			if ( ctx_p->flags_set[param_id] )
				return 0;

			ctx_p->flags_set[param_id] = 1;
			break;

		case PS_DEFAULTS:
#ifdef VERYPARANOID
			if ( ctx_p->flags_set[param_id] ) {
				error ( "Parameter #%i is already set. No need in setting the default value.", param_id );
				return 0;
			}

#endif
			break;

		/*		case PS_REHASH:
					arg = ctx_p->flags_values_raw[param_id];
		#ifdef VERYPARANOID
					critical_on (arg == NULL);
		#endif

					debug(9, "Rehash setting %i -> \"%s\"", param_id, arg);
					break;*/
		case PS_CORRECTION:
			critical_on ( arg == NULL );
			debug ( 9, "Correcting setting %i -> \"%s\"", param_id, arg );
			break;

		default:
			error ( "Unknown parameter #%i source (value \"%s\").", param_id, arg != NULL ? arg : "" );
			break;
	}

	if ( ( arg != NULL ) /*&& (paramsource != PS_REHASH)*/ ) {
		if ( param_id != KVM_ARGS )
			arg = parameter_expand ( ctx_p, arg, 0, NULL, NULL, parameter_get, ctx_p );

		if ( ctx_p->flags_values_raw[param_id] != NULL )
			free ( ctx_p->flags_values_raw[param_id] );

		ctx_p->flags_values_raw[param_id] = arg;
	}

	switch ( param_id ) {
		case '?':
		case HELP:
			syntax();
			break;

		case SHOW_VERSION:
			version();
			break;

		case CONFIG_FILE:
			ctx_p->config_path	= *arg ? arg : NULL;
			break;

		case CONFIG_GROUP:
			ctx_p->config_group	= *arg ? arg : NULL;
			break;

		case CONFIG_GROUP_INHERITS:
			break;

		case VMS_MIN:
			ctx_p->vms_min		= ( unsigned int ) xstrtol ( arg, &ret );
			break;

		case VMS_MAX:
			ctx_p->vms_max		= ( unsigned int ) xstrtol ( arg, &ret );
			break;

		case VMS_SPARE_MIN:
			ctx_p->vms_spare_min	= ( unsigned int ) xstrtol ( arg, &ret );
			break;

		case VMS_SPARE_MAX:
			ctx_p->vms_spare_max	= ( unsigned int ) xstrtol ( arg, &ret );
			break;

		default:
			if ( arg == NULL )
				ctx_p->flags[param_id]++;
			else
				ctx_p->flags[param_id] = xstrtol ( arg, &ret );

#ifdef _DEBUG_FORCE
			fprintf ( stderr, "Force-Debug: flag %i is set to %i\n", param_id & 0xff, ctx_p->flags[param_id] );
#endif
			break;
	}

	return ret;
}

int arguments_parse ( int argc, char *argv[], struct ctx *ctx_p )
{
	int c;
	int option_index = 0;
	// Generating "optstring" (man 3 getopt_long) with using information from struct array "long_options"
	char *optstring     = alloca ( ( ( 'z' - 'a' + 1 ) * 3 + '9' - '0' + 1 ) * 3 + 1 );
	char *optstring_ptr = optstring;
	const struct option *lo_ptr = long_options;

	while ( lo_ptr->name != NULL ) {
		if ( ! ( lo_ptr->val & ( OPTION_CONFIGONLY | OPTION_LONGOPTONLY ) ) ) {
			* ( optstring_ptr++ ) = lo_ptr->val & 0xff;

			if ( lo_ptr->has_arg == required_argument )
				* ( optstring_ptr++ ) = ':';

			if ( lo_ptr->has_arg == optional_argument ) {
				* ( optstring_ptr++ ) = ':';
				* ( optstring_ptr++ ) = ':';
			}
		}

		lo_ptr++;
	}

	*optstring_ptr = 0;
#ifdef _DEBUG_FORCE
	fprintf ( stderr, "Force-Debug: %s\n", optstring );
#endif

	// Parsing arguments
	while ( 1 ) {
		c = getopt_long ( argc, argv, optstring, long_options, &option_index );

		if ( c == -1 ) break;

		int ret = parse_parameter ( ctx_p, c, optarg == NULL ? NULL : strdup ( optarg ), PS_ARGUMENT );

		if ( ret ) return ret;
	}

	if ( optind < argc ) {
		kvm_args_t *args_p = &ctx_p->kvm_args[SHARGS_PRIMARY];

		while ( args_p->c )
			free ( args_p->v[--args_p->c] );

		if ( ( optind + 1 != argc ) || ( *argv[optind] ) ) {	// If there's only "" after the "--", just reset "kvm_argc" to "0", otherwise:
			do {
				if ( kvm_arg0 ( strdup ( argv[optind++] ), 0, ctx_p ) )
					return errno;
			} while ( optind < argc );
		}
	}

	return 0;
}

void gkf_parse ( ctx_t *ctx_p, GKeyFile *gkf, paramsource_t paramsource )
{
	debug ( 9, "" );
	char *config_group = ( char * ) ctx_p->config_group;

	while ( config_group != NULL ) {
		const struct option *lo_ptr = long_options;

		if ( config_group != ctx_p->config_group ) {
			ctx_p->flags_values_raw[CONFIG_GROUP_INHERITS] = NULL;
			ctx_p->flags_set[CONFIG_GROUP_INHERITS] = 0;
		}

		while ( lo_ptr->name != NULL ) {
			gchar *value = g_key_file_get_value ( gkf, config_group, lo_ptr->name, NULL );

			if ( value != NULL ) {
				int ret = parse_parameter ( ctx_p, lo_ptr->val, value, paramsource );

				if ( ret ) exit ( ret );
			}

			lo_ptr++;
		}

		if ( config_group != ctx_p->config_group )
			free ( config_group );

		config_group = ctx_p->flags_values_raw[CONFIG_GROUP_INHERITS];

		if ( config_group != NULL )
			debug ( 2, "Next block is: %s", config_group );
	};

	return;
}

int configs_parse ( ctx_t *ctx_p, paramsource_t paramsource )
{
	GKeyFile *gkf;
	gkf = g_key_file_new();

	if ( ctx_p->config_path ) {
		GError *g_error = NULL;

		if ( !strcmp ( ctx_p->config_path, "/NULL/" ) ) {
			debug ( 2, "Empty path to config file. Don't read any of config files." );
			return 0;
		}

		debug ( 1, "Trying config-file \"%s\" (case #0)", ctx_p->config_path );

		if ( !g_key_file_load_from_file ( gkf, ctx_p->config_path, G_KEY_FILE_NONE, &g_error ) ) {
			error ( "Cannot open/parse file \"%s\" (g_error #%u.%u: %s)", ctx_p->config_path, g_error->domain, g_error->code, g_error->message );
			g_key_file_free ( gkf );
			return -1;
		} else
			gkf_parse ( ctx_p, gkf, paramsource );
	} else {
		char  *config_paths[] = CONFIG_PATHS;
		char **config_path_p = config_paths, *config_path_real = xmalloc ( PATH_MAX );
		size_t config_path_real_size = PATH_MAX;
		char  *homedir     = getenv ( "HOME" );
		size_t homedir_len = ( homedir == NULL ? 0 : strlen ( homedir ) );

		while ( *config_path_p != NULL ) {
			size_t config_path_len = strlen ( *config_path_p );

			if ( config_path_len + homedir_len + 3 > config_path_real_size ) {
				config_path_real_size = config_path_len + homedir_len + 3;
				config_path_real      = xmalloc ( config_path_real_size );
			}

			if ( ( *config_path_p[0] != '/' ) && ( homedir_len >= 0 ) ) {
				memcpy ( config_path_real, homedir, homedir_len );
				config_path_real[homedir_len] = '/';
				memcpy ( &config_path_real[homedir_len + 1], *config_path_p, config_path_len + 1 );
			} else
				memcpy ( config_path_real, *config_path_p, config_path_len + 1 );

			debug ( 1, "Trying config-file \"%s\" (case #1)", config_path_real );

			if ( !g_key_file_load_from_file ( gkf, config_path_real, G_KEY_FILE_NONE, NULL ) ) {
				debug ( 1, "Cannot open/parse file \"%s\"", config_path_real );
				config_path_p++;
				continue;
			}

			gkf_parse ( ctx_p, gkf, paramsource );
			break;
		}

		free ( config_path_real );
	}

	g_key_file_free ( gkf );
	return 0;
}

int ctx_check ( ctx_t *ctx_p )
{
	int ret = 0;
#ifndef _DEBUG_SUPPORT

	if ( ctx_p->flags[DEBUG] ) {
		ret = errno = EINVAL;
		error ( "kvm-pool was compiled without debugging support" );
	}

#endif
	return ret;
}

int config_group_parse ( ctx_t *ctx_p, const char *const config_group_name )
{
	int rc;
	debug ( 1, "(ctx_p, \"%s\")", config_group_name );
	ctx_p->config_group = config_group_name;
	rc = configs_parse ( ctx_p, PS_CONTROL );

	if ( !rc )
		rc = ctx_check ( ctx_p );

	return errno = rc;
}

int ctx_set ( ctx_t *ctx_p, const char *const parameter_name, const char *const parameter_value )
{
	int ret = ENOENT;
	const struct option *lo_ptr = long_options;

	while ( lo_ptr->name != NULL ) {
		if ( !strcmp ( lo_ptr->name, parameter_name ) ) {
			ret = parse_parameter ( ctx_p, lo_ptr->val, strdup ( parameter_value ), PS_CONTROL );
			break;
		}

		lo_ptr++;
	}

	ret = ctx_check ( ctx_p );

	if ( ret )
		critical ( "Cannot continue with this setup" );

	return ret;
}

void ctx_cleanup ( ctx_t *ctx_p )
{
	int i = 0;
	debug ( 9, "" );

	while ( i < OPTION_FLAGS ) {
		if ( ctx_p->flags_values_raw[i] != NULL ) {
			free ( ctx_p->flags_values_raw[i] );
			ctx_p->flags_values_raw[i] = NULL;
		}

		i++;
	}

	{
		int n = 0;

		while ( n < SHARGS_MAX ) {
			int i = 0,  e = ctx_p->kvm_args[n].c;

			while ( i < e ) {
#ifdef _DEBUG_FORCE
				debug ( 14, "kvm args: %u, %u: free(%p)", n, i, ctx_p->kvm_args[n].v[i] );
#endif
				free ( ctx_p->kvm_args[n].v[i] );
				ctx_p->kvm_args[n].v[i] = NULL;
				i++;
			}

			ctx_p->kvm_args[n].c = 0;
			n++;
		}
	}

	return;
}

int becomedaemon()
{
	int pid;
	signal ( SIGPIPE, SIG_IGN );

	switch ( ( pid = fork() ) ) {
		case -1:
			error ( "Cannot fork()." );
			return ( errno );

		case 0:
			setsid();
			break;

		default:
			debug ( 1, "fork()-ed, pid is %i.", pid );
			errno = 0;
			exit ( 0 );
	}

	return 0;
}

int main_cleanup ( ctx_t *ctx_p )
{
	return 0;
}

int main_rehash ( ctx_t *ctx_p )
{
	debug ( 3, "" );
	int ret = 0;
	main_cleanup ( ctx_p );
	return ret;
}

FILE *main_statusfile_f;
int main_status_update ( ctx_t *ctx_p )
{
	static state_t state_old = STATE_UNKNOWN;
	state_t        state     = ctx_p->state;
	debug ( 4, "%u", state );

	if ( state == state_old ) {
		debug ( 3, "State unchanged: %u == %u", state, state_old );
		return 0;
	}

#ifdef VERYPARANOID

	if ( status_descr[state] == NULL ) {
		error ( "status_descr[%u] == NULL.", state );
		return EINVAL;
	}

#endif
	debug ( 3, "Setting status to %i.", state );
	state_old = state;
	int ret = 0;
	return ret;
}

int argc;
char **argv;
#define UGID_PRESERVE (1<<16)
int main ( int _argc, char *_argv[] )
{
	struct ctx *ctx_p = xcalloc ( 1, sizeof ( *ctx_p ) );
	argv = _argv;
	argc = _argc;
	int ret = 0, nret;
	//SAFE ( posixhacks_init(), errno = ret = _SAFE_rc );
	ctx_p->config_group			 = DEFAULT_CONFIG_GROUP;
	ncpus					 = sysconf ( _SC_NPROCESSORS_ONLN ); // Get number of available logical CPUs
	memory_init();
	ctx_p->pid				 = getpid();
	int quiet = 0, verbose = 3;
	error_init ( &ctx_p->flags[OUTPUT_METHOD], &quiet, &verbose, &ctx_p->flags[DEBUG] );
	nret = arguments_parse ( argc, argv, ctx_p );

	if ( nret ) ret = nret;

	if ( !ret ) {
		nret = configs_parse ( ctx_p, PS_CONFIG );

		if ( nret ) ret = nret;
	}

	if ( !ctx_p->kvm_args[SHARGS_PRIMARY].c ) {
		char *args_line = strdup ( DEFAULT_KVM_ARGS );
		parse_parameter ( ctx_p, KVM_ARGS, args_line, PS_DEFAULTS );
	}

	debug ( 4, "ncpus == %u", ncpus );
	debug ( 4, "debugging flags: %u 0 3 %u", ctx_p->flags[OUTPUT_METHOD], ctx_p->flags[DEBUG] );
	{
		int n = 0;

		while ( n < SHARGS_MAX ) {
			kvm_args_t *args_p = &ctx_p->kvm_args[n++];
			debug ( 9, "Custom arguments %u count: %u", n - 1, args_p->c );
			int i = 0;

			while ( i < args_p->c ) {
				int macros_count = -1, expanded = -1;
				args_p->v[i] = parameter_expand ( ctx_p, args_p->v[i], 4, &macros_count, &expanded, parameter_get, ctx_p );
				debug ( 12, "args_p->v[%u] == \"%s\" (t: %u; e: %u)", i, args_p->v[i], macros_count, expanded );

				if ( macros_count == expanded )
					args_p->isexpanded[i]++;

				i++;
			}
		}
	}
	ctx_p->state = STATE_STARTING;
	nret = main_rehash ( ctx_p );

	if ( nret )
		ret = nret;

	{
		int rc = ctx_check ( ctx_p );

		if ( !ret ) ret = rc;
	}
	debug ( 3, "Current errno is %i.", ret );

	// == RUNNING ==
	if ( ret == 0 )
		ret = kvmpool_start ( ctx_p );

	// == /RUNNING ==
	main_cleanup ( ctx_p );
	error_deinit();
	ctx_cleanup ( ctx_p );
	debug ( 1, "finished, exitcode: %i: %s.", ret, strerror ( ret ) );
	free ( ctx_p );
#ifndef __FreeBSD__	// Hanging up with 100%CPU eating, https://github.com/xaionaro/clsync/issues/97
	//SAFE ( posixhacks_deinit(), errno = ret = _SAFE_rc );
#endif
	return ret;
}


