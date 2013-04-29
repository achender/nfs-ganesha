// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_rename.c
// Description: Common FSI IPC Client and Server definitions
// Author:      FSI IPC dev team
// ----------------------------------------------------------------------------

/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "pt_ganesha.h"
#include "pt_methods.h"

/**
 * FSAL_rename:
 * Change name and/or parent dir of a filesystem object.
 *
 * \param old_hdle (input):
 *        Source parent directory of the object is to be moved/renamed.
 * \param p_old_name (input):
 *        Pointer to the current name of the object to be moved/renamed.
 * \param new_hdle (input):
 *        Target parent directory for the object.
 * \param p_new_name (input):
 *        Pointer to the new name for the object.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */

fsal_status_t PTFSAL_rename(struct fsal_obj_handle *old_hdl,    /* IN */
                          const char * p_old_name,                /* IN */
                          struct fsal_obj_handle *new_hdl,        /* IN */
                          const char * p_new_name,                /* IN */
                          const struct req_op_context * p_context) /* IN */
{

  int rc, errsv;
  struct stat buffstat;
  fsal_status_t status;
  fsi_stat_struct old_bufstat, new_bufstat;
  int src_equal_tgt = FALSE;
  fsal_accessflags_t access_mask = 0;
  struct attrlist src_dir_attrs, tgt_dir_attrs;
  int mount_fd;
  struct pt_fsal_obj_handle *old_pt_hdl, *new_pt_hdl;
  int stat_rc;

  FSI_TRACE(FSI_DEBUG, "FSI Rename--------------\n");

  /* sanity checks.
   * note : src/tgt_dir_attributes are optional.
   */
  if(!old_hdl || !new_hdl || !p_old_name || !p_new_name || !p_context)
    return fsalstat(ERR_FSAL_FAULT, 0);

  old_pt_hdl = container_of(old_hdl, struct pt_fsal_obj_handle, obj_handle);
  new_pt_hdl = container_of(new_hdl, struct pt_fsal_obj_handle, obj_handle);
  mount_fd = pt_get_root_fd(old_hdl->export);

  /* retrieve directory metadata for checking access rights */

  src_dir_attrs.mask = old_hdl->export->ops->fs_supported_attrs(old_hdl->export);
  status = PTFSAL_getattrs(old_hdl->export, p_context, old_pt_hdl->handle, &src_dir_attrs);
  if(FSAL_IS_ERROR(status))
    return status;

  /* optimisation : don't do the job twice if source dir = dest dir  */
  if(compare(old_hdl, new_hdl)) {
    // new_parent_fd = old_parent_fd;
    src_equal_tgt = TRUE;
    tgt_dir_attrs = src_dir_attrs;
  } else {
    /* retrieve destination attrs */
      tgt_dir_attrs.mask = new_hdl->export->ops->fs_supported_attrs(new_hdl->export);
      status = PTFSAL_getattrs(old_hdl->export, p_context, new_pt_hdl->handle,
                                 &tgt_dir_attrs);
    if(FSAL_IS_ERROR(status))
      return status;
  }

  /* check access rights */

  /* Set both mode and ace4 mask */
  access_mask = FSAL_MODE_MASK_SET(FSAL_W_OK | FSAL_X_OK) |
                FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_DELETE_CHILD);

  if(!old_hdl->export->ops->fs_supports(old_hdl->export, fso_accesscheck_support))
    status = fsal_internal_testAccess(p_context, access_mask, &src_dir_attrs);
  else
    status = fsal_internal_access(mount_fd, p_context, old_pt_hdl->handle,
                                  access_mask, &src_dir_attrs);
  if(FSAL_IS_ERROR(status)) {
    return status;
  }

  if(!src_equal_tgt) {
    access_mask = FSAL_MODE_MASK_SET(FSAL_W_OK | FSAL_X_OK) |
                  FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE |
                  FSAL_ACE_PERM_ADD_SUBDIRECTORY);

   if(!old_hdl->export->ops->fs_supports(old_hdl->export,
                                               fso_accesscheck_support))
     status = fsal_internal_testAccess(p_context, access_mask, &tgt_dir_attrs);
    else
     status = fsal_internal_access(mount_fd, p_context,
	                     new_pt_hdl->handle, access_mask, &tgt_dir_attrs);
    if(FSAL_IS_ERROR(status)) {
      return status;
    }
  }

  /* build file paths */
  stat_rc = fsal_internal_stat_name(mount_fd, old_pt_hdl->handle, p_old_name, &buffstat);

  errsv = errno;
  if(stat_rc) {
    return fsalstat(posix2fsal_error(errsv), errsv);
  }

  /* Check sticky bits */

  /* Sticky bit on the source directory => the user who wants to delete the 
   * file must own it or its parent dir 
   */
  if((fsal2unix_mode(src_dir_attrs.mode) & S_ISVTX) &&
     src_dir_attrs.owner != p_context->creds->caller_uid &&
     buffstat.st_uid != p_context->creds->caller_uid && p_context->creds->caller_uid != 0) {
    return fsalstat(ERR_FSAL_ACCESS, 0);
  }

  /* Sticky bit on the target directory => the user who wants to create the 
   * file must own it or its parent dir 
   */
  if(fsal2unix_mode(tgt_dir_attrs.mode) & S_ISVTX) {
      stat_rc = fsal_internal_stat_name(mount_fd, new_pt_hdl->handle, p_new_name, &buffstat);
    errsv = errno;

    if(stat_rc < 0) {
      if(errsv != ENOENT) {
        return fsalstat(posix2fsal_error(errsv), errsv);
      }
    } else {

      if(tgt_dir_attrs.owner != p_context->creds->caller_uid
             && buffstat.st_uid != p_context->creds->caller_uid
             && p_context->creds->caller_uid != 0) {
         return fsalstat(ERR_FSAL_ACCESS, 0);
      }
    }
  }

  /*************************************
   * Rename the file on the filesystem *
   *************************************/
  rc = ptfsal_rename(p_context, old_pt_hdl, p_old_name,
                                   new_pt_hdl, p_new_name);
  errsv = errno;

  if(rc)
    return status;

  /* OK */
  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}
