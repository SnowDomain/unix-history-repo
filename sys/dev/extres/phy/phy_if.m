#-
# Copyright 2016 Michal Meloun <mmel@FreeBSD.org>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#

#ifdef FDT
#include <sys/types.h>
#include <dev/ofw/ofw_bus.h>
#endif

INTERFACE phy;

#ifdef FDT
HEADER {
int phy_default_map(device_t , phandle_t, int, pcell_t *, intptr_t *);
}

#
# map fdt property cells to phy number
# Returns 0 on success or a standard errno value.
#
METHOD int map {
	device_t	provider_dev;
	phandle_t 	xref;
	int		ncells;
	pcell_t		*cells;
	intptr_t	*id;
} DEFAULT phy_default_map;
#endif

#
# Init/deinit phy
# Returns 0 on success or a standard errno value.
#
METHOD int init {
	device_t	provider_dev;
	intptr_t	id;
	bool		inti;
};

#
# Enable/disable phy
# Returns 0 on success or a standard errno value.
#
METHOD int enable {
	device_t	provider_dev;
	intptr_t	id;
	bool		enable;
};

#
# Get phy status
# Returns 0 on success or a standard errno value.
#
METHOD int status {
	device_t	provider_dev;
	intptr_t	id;
	int		*status;    /* PHY_STATUS_* */
};


