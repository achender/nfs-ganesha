// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_lookup.c
// Description: FSAL lookup operations implementation
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

/**
 * \file    fsal_lookup.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:37 $
 * \version $Revision: 1.17 $
 * \brief   Lookup operations.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "pt_ganesha.h"
#include "pt_methods.h"


/**
 * FSAL_lookup :
 * Looks up for an object into a directory.
 *
 * Note : if parent handle and filename are NULL,
 *        this retrieves root's handle.
 *
 * \param parent_directory_handle (input)
 *        Handle of the parent directory to search the object in.
 * \param filename (input)
 *        The name of the object to find.
 * \param p_context (input)
 *        Authentication context for the operation (user,...).
 * \param object_handle (output)
 *        The handle of the object corresponding to filename.
 * \param object_attributes (optional input/output)
 *        Pointer to the attributes of the object we found.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        It can be NULL (increases performances).
 *
 * \return - ERR_FSAL_NO_ERROR, if no error.
 *         - Another error code else.
 *
 */
fsal_status_t PTFSAL_lookup(const struct req_op_context *p_context,
                              struct fsal_obj_handle *parent,
			      const char *p_filename,
			      struct attrlist *p_object_attr,
			      ptfsal_handle_t *fh)
{
  fsal_status_t status;
  int parent_fd;
  int mnt_fd;
  fsal_accessflags_t access_mask = 0;
  struct attrlist parent_dir_attrs;
  fsi_stat_struct buffstat;
  int rc;
  struct pt_fsal_obj_handle *parent_hdl;


  FSI_TRACE(FSI_DEBUG, "Begin##################################\n");
  if (p_filename != NULL)
    FSI_TRACE(FSI_DEBUG, "FSI - fsal_lookup file [%s]\n", p_filename);

  if (parent != NULL)
    FSI_TRACE(FSI_DEBUG, "FSI - fsal_lookup parent dir\n");

  if(!parent || !p_filename)
    return fsalstat(ERR_FSAL_FAULT, 0);

  mnt_fd = pt_get_root_fd(parent->export);
  parent_hdl = container_of(parent, struct pt_fsal_obj_handle, obj_handle);

  /* get directory metadata */
  parent_dir_attrs.mask =
                      parent->export->ops->fs_supported_attrs(parent->export);
  status = fsal_internal_handle2fd_at(p_context, parent_hdl, &parent_fd, O_RDONLY);

  if(FSAL_IS_ERROR(status))
    return status;

  FSI_TRACE(FSI_DEBUG, "FSI - lookup parent directory type = %d\n", 
            parent_dir_attrs.type);

  /* Be careful about junction crossing, symlinks, hardlinks,... */
  switch (parent_dir_attrs.type)
    {
    case DIRECTORY:
      // OK
      break;

    case FS_JUNCTION:
      // This is a junction
      return fsalstat(ERR_FSAL_XDEV, 0);

    case REGULAR_FILE:
    case SYMBOLIC_LINK:
      // not a directory
      close(parent_fd);
      return fsalstat(ERR_FSAL_NOTDIR, 0);

    default:
      return fsalstat(ERR_FSAL_SERVERFAULT, 0);
    }

  /* check rights to enter into the directory */

  /* Set both mode and ace4 mask */
  access_mask = FSAL_MODE_MASK_SET(FSAL_R_OK | FSAL_X_OK) |
                FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_LIST_DIR);
  if(!parent->export->ops->fs_supports(parent->export,
                                       fso_accesscheck_support))
    status = fsal_internal_testAccess(p_context, access_mask,
                                      &parent_dir_attrs);

  else
    status = fsal_internal_access(parent_fd, p_context, parent_hdl->handle,
                                  access_mask, &parent_dir_attrs);

  if(FSAL_IS_ERROR(status))
    return status;

  /* get file handle, it it exists */
  /* This might be a race, but it's the best we can currently do */
  rc = ptfsal_stat_by_parent_name(p_context, parent_hdl, 
                                  p_filename, &buffstat);
  if(rc < 0)
  {
    return fsalstat(ERR_FSAL_NOENT, errno);
  }
  memcpy(&fh->data.handle.f_handle, 
         &buffstat.st_persistentHandle.handle, 
         FSI_CCL_PERSISTENT_HANDLE_N_BYTES); 
  fh->data.handle.handle_size = FSI_CCL_PERSISTENT_HANDLE_N_BYTES;
  fh->data.handle.handle_type = posix2fsal_type(buffstat.st_mode);
  fh->data.handle.handle_key_size = OPENHANDLE_KEY_LEN;
  fh->data.handle.handle_version  = OPENHANDLE_VERSION;



  /* get object attributes */
  if(p_object_attr)
    {
      p_object_attr->mask =
                       parent->export->ops->fs_supported_attrs(parent->export);
      status = PTFSAL_getattrs(parent->export, p_context, fh, p_object_attr);
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_object_attr->mask);
          FSAL_SET_MASK(p_object_attr->mask, ATTR_RDATTR_ERR);
        }
    }

  FSI_TRACE(FSI_DEBUG, "End##################################\n");
  /* lookup complete ! */
  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}


#if 0 //???  not needed for now

/**
 * FSAL_lookupPath :
 * Looks up for an object into the namespace.
 *
 * Note : if path equals "/",
 *        this retrieves root's handle.
 *
 * \param path (input)
 *        The path of the object to find.
 * \param p_context (input)
 *        Authentication context for the operation (user,...).
 * \param object_handle (output)
 *        The handle of the object corresponding to filename.
 * \param object_attributes (optional input/output)
 *        Pointer to the attributes of the object we found.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        It can be NULL (increases performances).
 */

fsal_status_t 
PTFSAL_lookupPath(fsal_path_t * p_path,     /* IN */
                  fsal_op_context_t * p_context,    /* IN */
                  fsal_handle_t * object_handle,    /* OUT */
                  fsal_attrib_list_t * p_object_attributes  /* [ IN/OUT ] */)
{
  fsal_status_t status;
  ptfsal_handle_t *p_fsi_handle;

  FSI_TRACE(FSI_DEBUG, "Begin-------------------------------------\n");

  /* sanity checks
   * note : object_attributes is optionnal.
   */
  if(!object_handle || !p_context || !p_path)
    return fsalstat(ERR_FSAL_FAULT, 0);

  /* test whether the path begins with a slash */
  if(p_path->path[0] != '/')
    return fstat(ERR_FSAL_INVAL, 0);

  /* directly call the lookup function */
  FSI_TRACE(FSI_DEBUG, "FSI - lookupPath [%s] entered\n",p_path->path);

  status = fsal_internal_get_handle(p_context, p_path, object_handle);

  if(FSAL_IS_ERROR(status))
    return status;

  p_fsi_handle = (ptfsal_handle_t *)object_handle;
  ptfsal_print_handle(p_fsi_handle->data.handle.f_handle);

  /* get object attributes */
  if(p_object_attributes)
    {
      status = PTFSAL_getattrs(parent->export, object_handle, p_context,
                                 p_object_attributes);
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_object_attributes->mask);
          FSAL_SET_MASK(p_object_attributes->mask, FSAL_ATTR_RDATTR_ERR);
       }
    }

  FSI_TRACE(FSI_DEBUG, "End--------------------------------------\n");
  return fsalstat(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupPath);

}

/**
 * FSAL_lookupJunction :
 * Get the fileset root for a junction.
 *
 * \param p_junction_handle (input)
 *        Handle of the junction to be looked up.
 * \param p_context (input)
 *        Authentication context for the operation (user,...).
 * \param p_fsroot_handle (output)
 *        The handle of root directory of the fileset.
 * \param p_fsroot_attributes (optional input/output)
 *        Pointer to the attributes of the root directory
 *        for the fileset.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        It can be NULL (increases performances).
 *
 * \return - ERR_FSAL_NO_ERROR, if no error.
 *         - Another error code else.
 *
 */
fsal_status_t 
PTFSAL_lookupJunction(fsal_handle_t * p_junction_handle,    /* IN */
                      fsal_op_context_t * p_context,        /* IN */
                      fsal_handle_t * p_fsoot_handle,       /* OUT */
                      fsal_attrib_list_t * p_fsroot_attributes /*[IN/OUT] */)
{
  return fsalstat(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupJunction);
}
#endif
