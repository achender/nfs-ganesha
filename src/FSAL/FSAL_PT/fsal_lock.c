// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_lock.c
// Description: FSAL locking operations implementation
// Author:      FSI IPC dev team
// ----------------------------------------------------------------------------

/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
 *
 *
 * This software is a server that implements the NFS protocol.
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

/**
 * PTFSAL_lock_op:
 * Lock/unlock/test an owner independent (anonymous) lock for a region in a file.
 *
 * \param p_file_descriptor (input):
 *        File descriptor of the file to lock.
 * \param p_filehandle (input):
 *        File handle of the file to lock.
 * \param p_context (input):
 *        Context
 * \param p_owner (input):
 *        Owner for the requested lock; Opaque to FSAL.
 * \param lock_op (input):
 *        Can be either FSAL_OP_LOCKT, FSAL_OP_LOCK, FSAL_OP_UNLOCK.
 *        The operations are test if a file region is locked, lock a
 *        file region, unlock a file region.
 * \param lock_type (input):
 *        Can be either FSAL_LOCK_R, FSAL_LOCK_W.
 *        Either a read lock or write lock.
 * \param lock_start (input):
 *        Start of lock region measured as offset of bytes from start of file.
 * \param lock_length (input):
 *        Number of bytes to lock.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR: no error.
 *      - ERR_FSAL_FAULT: One of the in put parameters is NULL.
 *      - ERR_FSAL_PERM: lock_op was FSAL_OP_LOCKT and the result was that the operation would not be possible.
 */
fsal_status_t PTFSAL_lock_op( fsal_file_t       * p_file_descriptor, /* IN */
                                fsal_handle_t     * p_filehandle,      /* IN */
                                fsal_op_context_t * p_context,         /* IN */
                                void              * p_owner,           /* IN */
                                fsal_lock_op_t      lock_op,           /* IN */
                                fsal_lock_param_t   request_lock,      /* IN */
                                fsal_lock_param_t * conflicting_lock)  /* OUT */
{
     Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_lock_op);
}
