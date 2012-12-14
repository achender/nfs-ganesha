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

/* file.c
 * File I/O methods for PT module
 */

#include "config.h"

#include <assert.h>
#include "fsal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include "fsal_internal.h"
#include <unistd.h>
#include <fcntl.h>
#include "FSAL/fsal_commonlib.h"
#include "pt_methods.h"

/** pt_open
 * called with appropriate locks taken at the cache inode level
 */

fsal_status_t pt_open(struct fsal_obj_handle *obj_hdl,
		       const struct req_op_context *opctx,
		       fsal_openflags_t openflags)
{
	struct pt_fsal_obj_handle *myself;
	int fd;
        fsal_status_t status;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct pt_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd == -1
	       && myself->u.file.openflags == FSAL_O_CLOSED);

        status = PTFSAL_open(obj_hdl, opctx, openflags, &fd, NULL);
        if(FSAL_IS_ERROR(status))
          return(status);

	myself->u.file.fd = fd;
	myself->u.file.openflags = openflags;

	return fsalstat(fsal_error, retval);	
}

/* pt_status
 * Let the caller peek into the file's open/close state.
 */

fsal_openflags_t pt_status(struct fsal_obj_handle *obj_hdl)
{
	struct pt_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct pt_fsal_obj_handle, obj_handle);
	return myself->u.file.openflags;
}

/* pt_read
 * concurrency (locks) is managed in cache_inode_*
 */

fsal_status_t pt_read(struct fsal_obj_handle *obj_hdl,
                       const struct req_op_context *opctx,
		       uint64_t offset,
                       size_t buffer_size,
                       void *buffer,
		       size_t *read_amount,
		       bool *end_of_file)
{
	struct pt_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;
        fsal_status_t status;
	myself = container_of(obj_hdl, struct pt_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0 && myself->u.file.openflags != FSAL_O_CLOSED);

        status =  PTFSAL_read(myself, opctx, offset, buffer_size, buffer,
                                read_amount, end_of_file);
        if(FSAL_IS_ERROR(status))
          return(status);

        *end_of_file = *read_amount == 0 ? true : false;

	return fsalstat(fsal_error, retval);
}

/* pt_write
 * concurrency (locks) is managed in cache_inode_*
 */

fsal_status_t pt_write(struct fsal_obj_handle *obj_hdl,
                               const struct req_op_context *opctx,
                               uint64_t offset,
                               size_t buffer_size,
                               void *buffer,
                               size_t *wrote_amount,
                               bool *fsal_stable)

{
	struct pt_fsal_obj_handle *myself;
        fsal_status_t status;

	myself = container_of(obj_hdl, struct pt_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0 &&
	       myself->u.file.openflags != FSAL_O_CLOSED);

        status =  PTFSAL_write(myself, opctx, offset, buffer_size, buffer,
                                 wrote_amount, fsal_stable);
        return(status);
}

/* pt_commit
 * Commit a file range to storage.
 * for right now, fsync will have to do.
 */
fsal_status_t pt_commit(struct fsal_obj_handle *obj_hdl, /* sync */
			 off_t offset,
			 size_t len)
{
	struct pt_fsal_obj_handle *myself;
	fsal_status_t fsal_error;

	myself = container_of(obj_hdl, struct pt_fsal_obj_handle, obj_handle);

	assert(myself->u.file.fd >= 0 &&
	       myself->u.file.openflags != FSAL_O_CLOSED);

        fsal_error = PTFSAL_commit(myself, offset, len);
	
	return fsal_error;	
}

/* pt_lock_op
 * lock a region of the file
 * throw an error if the fd is not open.  The old fsal didn't
 * check this.
 */
#if 0 //pt does not support locking
fsal_status_t pt_lock_op(struct fsal_obj_handle *obj_hdl,
	        	  const struct req_op_context *opctx,
			  void * p_owner,
			  fsal_lock_op_t lock_op,
			  fsal_lock_param_t *request_lock,
			  fsal_lock_param_t *conflicting_lock)
{
	fsal_status_t status;
	struct pt_fsal_obj_handle *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(obj_hdl, struct pt_fsal_obj_handle, obj_handle);
	if(myself->u.file.fd < 0 || myself->u.file.openflags == FSAL_O_CLOSED) {
		LogDebug(COMPONENT_FSAL,
			 "Attempting to lock with no file descriptor open");
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	if(conflicting_lock == NULL && lock_op == FSAL_OP_LOCKT) {
		LogDebug(COMPONENT_FSAL,
			 "conflicting_lock argument can't"
			 " be NULL with lock_op  = LOCKT");
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	LogFullDebug(COMPONENT_FSAL,
		     "Locking: op:%d type:%d start:%"PRIu64" length:%lu ",
		     lock_op,
		     request_lock->lock_type,
		     request_lock->lock_start,
		     request_lock->lock_length);

        status = PTFSAL_lock_op(obj_hdl, p_owner, lock_op, *request_lock,
                                  conflicting_lock);
        return(status);

out:
	return fsalstat(fsal_error, retval);	
}
#endif

/* pt_close
 * Close the file if it is still open.
 * Yes, we ignor lock status.  Closing a file in POSIX
 * releases all locks but that is state and cache inode's problem.
 */

fsal_status_t pt_close(struct fsal_obj_handle *obj_hdl)
{
	struct pt_fsal_obj_handle *myself;
	fsal_status_t status = fsalstat(ERR_FSAL_NO_ERROR, 0);

	assert(obj_hdl->type == REGULAR_FILE);
	myself = container_of(obj_hdl, struct pt_fsal_obj_handle, obj_handle);
	if(myself->u.file.fd >= 0 &&
	   myself->u.file.openflags != FSAL_O_CLOSED){
                status = PTFSAL_close(myself->u.file.fd);
		myself->u.file.fd = -1;
		myself->u.file.openflags = FSAL_O_CLOSED;
	}
	return status;	
}

/* pt_lru_cleanup
 * free non-essential resources at the request of cache inode's
 * LRU processing identifying this handle as stale enough for resource
 * trimming.
 */

fsal_status_t pt_lru_cleanup(struct fsal_obj_handle *obj_hdl,
			      lru_actions_t requests)
{
	struct pt_fsal_obj_handle *myself;
	fsal_status_t status;

	myself = container_of(obj_hdl, struct pt_fsal_obj_handle, obj_handle);
	if(obj_hdl->type == REGULAR_FILE && myself->u.file.fd >= 0) {
                status = PTFSAL_close(myself->u.file.fd);
		myself->u.file.fd = -1;
		myself->u.file.openflags = FSAL_O_CLOSED;
	}
	return status;	
}
