/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ------------- 
 */

/* main.c
 * Module core functions
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include <libgen.h>             /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include "nlm_list.h"
#include "fsal_internal.h"
#include "FSAL/fsal_init.h"
#include "fsal_api.h"

/* PT FSAL module private storage
 */

struct pt_fsal_module {	
	struct fsal_module fsal;
	struct fsal_staticfsinfo_t fs_info;
	fsal_init_info_t fsal_info;
	 /* ptfs_specific_initinfo_t specific_info;  placeholder */
};

const char myname[] = "PT";

/* filesystem info for PT */
static struct fsal_staticfsinfo_t default_posix_info = {
	.maxfilesize = 0xFFFFFFFFFFFFFFFFLL, /* (64bits) */
	.maxlink = _POSIX_LINK_MAX,
	.maxnamelen = 1024,
	.maxpathlen = 1024,
	.no_trunc = true,
	.chown_restricted = true,
	.case_insensitive = false,
	.case_preserving = true,
	.link_support = true,
	.symlink_support = true,
	.lock_support = true,
	.lock_support_owner = true,
	.lock_support_async_block = true,
	.named_attr = true,
	.unique_handles = true,
	.lease_time = {10, 0},
	.acl_support = FSAL_ACLSUPPORT_ALLOW,
	.cansettime = true,
	.homogenous = true,
	.supported_attrs = PT_SUPPORTED_ATTRIBUTES,
	.maxread = 1048576,
	.maxwrite = 1048576,
	.umask = 0,
	.auth_exportpath_xdev = false,
	.xattr_access_rights = 0400, /* root=RW, owner=R */
        .accesscheck_support = true,
        .share_support = true,
        .share_support_owner = false,
	.dirs_have_sticky_bit = true
};

/* private helper for export object
 */

struct fsal_staticfsinfo_t *pt_staticinfo(struct fsal_module *hdl)
{
	struct pt_fsal_module *myself;

	myself = container_of(hdl, struct pt_fsal_module, fsal);
	return &myself->fs_info;
}

/* Module methods
 */

/* init_config
 * must be called with a reference taken (via lookup_fsal)
 */

static fsal_status_t init_config(struct fsal_module *fsal_hdl,
				 config_file_t config_struct)
{
	struct pt_fsal_module *pt_me
		= container_of(fsal_hdl, struct pt_fsal_module, fsal);
	fsal_status_t fsal_status;

	pt_me->fs_info = default_posix_info; /* get a copy of the defaults */

        fsal_status = fsal_load_config(fsal_hdl->ops->get_name(fsal_hdl),
                                       config_struct,
                                       &pt_me->fsal_info,
                                       &pt_me->fs_info,
                                       NULL);

	if(FSAL_IS_ERROR(fsal_status))
		return fsal_status;
	/* if we have fsal specific params, do them here
	 * fsal_hdl->name is used to find the block containing the
	 * params.
	 */

	display_fsinfo(&pt_me->fs_info);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes constant = 0x%"PRIx64,
                     (uint64_t) PT_SUPPORTED_ATTRIBUTES);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes default = 0x%"PRIx64,
		     default_posix_info.supported_attrs);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%"PRIx64,
		 pt_me->fs_info.supported_attrs);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Internal PT method linkage to export object
 */

fsal_status_t pt_create_export(struct fsal_module *fsal_hdl,
                                const char *export_path,
                                const char *fs_options,
                                struct exportlist *exp_entry,
                                struct fsal_module *next_fsal,
                                const struct fsal_up_vector *up_ops,
                                struct fsal_export **export);
/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* my module private storage
 */

static struct pt_fsal_module PT;

/* linkage to the exports and handle ops initializers
 */

MODULE_INIT void pt_init(void) {
	int retval;
	struct fsal_module *myself = &PT.fsal;

	retval = register_fsal(myself, myname,
			       FSAL_MAJOR_VERSION,
			       FSAL_MINOR_VERSION);
	if(retval != 0) {
		fprintf(stderr, "PT module failed to register");
		return;
	}
	myself->ops->create_export = pt_create_export;
	myself->ops->init_config = init_config;
        init_fsal_parameters(&PT.fsal_info);
}

MODULE_FINI void pt_unload(void) {
	int retval;

	retval = unregister_fsal(&PT.fsal);
	if(retval != 0) {
		fprintf(stderr, "PT module failed to unregister");
		return;
	}
}
