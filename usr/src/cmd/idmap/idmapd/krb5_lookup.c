/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2014 Nexenta Systems, Inc.  All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <syslog.h>

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/note.h>
#include <synch.h>
#include <thread.h>

#include "idmapd.h"
#include "libadutils.h"
#include "locate_plugin.h"

/* osconf.h - sigh */
#define	KRB5_DEFAULT_PORT	88
#define	DEFAULT_KADM5_PORT	749
#define	DEFAULT_KPASSWD_PORT	464

/*
 * This is an "override plugin" used by libkrb5.  See:
 * lib/gss_mechs/mech_krb5/krb5/os/locate_kdc.c
 *
 * The interface is based on:
 * http://web.mit.edu/~kerberos/krb5-1.12/doc/plugindev/locate.html
 */

/*
 * Called by krb5int_locate_server / override_locate_server
 */

krb5_error_code
_krb5_override_service_locator(
    void *arg0,
    enum locate_service_type svc,
    const char *realm,
    int socktype,
    int family,
    int (*cbfunc)(void *, int, struct sockaddr *),
    void *cbdata)
{
	_NOTE(ARGUNUSED(arg0))
	idmap_pg_config_t *pgcfg;
	ad_disc_ds_t *ds;
	int rc = KRB5_PLUGIN_NO_HANDLE;
	short port;

	/*
	 * Is this a service we want to override?
	 */
	switch (svc) {
	case locate_service_kdc:
	case locate_service_master_kdc:
		port = htons(KRB5_DEFAULT_PORT);
		break;
	case locate_service_kadmin:
		port = htons(DEFAULT_KADM5_PORT);
		break;
	case locate_service_kpasswd:
		port = htons(DEFAULT_KPASSWD_PORT);
		break;
	case locate_service_krb524:
	default:
		return (rc);
	}

	RDLOCK_CONFIG();
	pgcfg = &_idmapdstate.cfg->pgcfg;

	/*
	 * Is this a realm we want to override?
	 */
	if (pgcfg->domain_name == NULL)
		goto out;
	if (0 != strcasecmp(realm, pgcfg->domain_name))
		goto out;

	/*
	 * Yes, this is our domain.  Have a DC?
	 */
	if ((ds = pgcfg->domain_controller) == NULL) {
		rc = KRB5_REALM_CANT_RESOLVE;
		goto out;
	}

	switch (family) {
	case AF_UNSPEC:
		break;	/* OK */
	case AF_INET:
	case AF_INET6:
		if (family == ds->addr.ss_family)
			break;	/* OK */
		/* else fallthrough */
	default:
		rc = KRB5_ERR_NO_SERVICE;
		goto out;
	}

	/*
	 * Provide the service address we have.
	 */
	switch (ds->addr.ss_family) {
	case AF_INET: {
		struct sockaddr_in sin;
		struct sockaddr_in *dsa = (void *)&ds->addr;
		(void) memset(&sin, 0, sizeof (sin));
		sin.sin_family = AF_INET;
		sin.sin_port = port;
		(void) memcpy(&sin.sin_addr, &dsa->sin_addr,
		    sizeof (sin.sin_addr));
		rc = cbfunc(cbdata, socktype, (struct sockaddr *)&sin);
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6 sin6;
		struct sockaddr_in6 *dsa6 = (void *)&ds->addr;
		(void) memset(&sin6, 0, sizeof (sin6));
		sin6.sin6_family = AF_INET6;
		sin6.sin6_port = port;
		(void) memcpy(&sin6.sin6_addr, &dsa6->sin6_addr,
		    sizeof (sin6.sin6_addr));
		rc = cbfunc(cbdata, socktype, (struct sockaddr *)&sin6);
		break;
	}
	default:
		rc = KRB5_ERR_NO_SERVICE;
		goto out;
	}
	/* rc from cbfunc is special. */
	if (rc)
		rc = ENOMEM;

out:
	UNLOCK_CONFIG();

	return (rc);
}
