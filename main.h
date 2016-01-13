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

#ifndef __KVMPOOL_MAIN_H
#define __KVMPOOL_MAIN_H

extern char *parameter_expand (
    ctx_t *ctx_p,
    char *arg,
    int exceptionflags,
    int *macro_count_p,
    int *expand_count_p,
    const char * ( *parameter_get ) ( const char *variable_name, void *arg ),
    void *parameter_get_arg
);
extern const char *parameter_get ( const char *variable_name, void *_ctx_p );

#endif
