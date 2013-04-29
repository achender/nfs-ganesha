// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_attrs.c
// Description: FSAL attributes operations implementation
// Author:      FSI IPC dev team
// ----------------------------------------------------------------------------
/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_attrs.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/17 15:53:39 $
 * \version $Revision: 1.31 $
 * \brief   HPSS-FSAL type translation functions.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "fsal_types.h"
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
#include <sys/time.h>
#include "pt_methods.h"
#include "pt_ganesha.h"

extern fsal_status_t 
ptfsal_xstat_2_fsal_attributes(ptfsal_xstat_t     * p_buffxstat,
                               struct attrlist    * p_fsalattr_out);

#ifdef _USE_NFS4_ACL
extern fsal_status_t fsal_acl_2_ptfs_acl(fsal_acl_t     * p_fsalacl, 
                                         ptfsal_xstat_t * p_buffxstat);
#endif                          /* _USE_NFS4_ACL */

/**
 * PTFSAL_getattrs:
 * Get attributes for the object specified by its filehandle.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t PTFSAL_getattrs(struct fsal_export *export,              
                              const struct req_op_context * p_context,  
                              ptfsal_handle_t *p_filehandle,   
                              struct attrlist *p_object_attributes)
{
  fsal_status_t   st;
  int             stat_rc;
  struct stat buffstat;

  int mntfd;
#ifdef _USE_NFS4_ACL
  fsal_accessflags_t access_mask = 0;
#endif

  FSI_TRACE(FSI_DEBUG, "Begin-------------------\n");

  /* sanity checks.
   * note : object_attributes is mandatory in PTFSAL_getattrs.
   */
  if (!p_filehandle || !export || !p_object_attributes) { 
    return fsalstat(ERR_FSAL_FAULT, 0);
  }

  mntfd = pt_get_root_fd(export);
  stat_rc = ptfsal_get_stat_by_handle(mntfd, p_filehandle, &buffstat);

  if (stat_rc) {
    return fsalstat(ERR_FSAL_INVAL, errno);
  }

  /* convert attributes */

  st = posix2fsal_attributes(&buffstat, p_object_attributes);
  FSI_TRACE(FSI_DEBUG, "Handle type=%d st_mode=%o (octal)", 
            p_object_attributes->type, buffstat.st_mode);

  p_object_attributes->mounted_on_fileid = 
    export->exp_entry->id;

  if (FSAL_IS_ERROR(st)) {
    FSAL_CLEAR_MASK(p_object_attributes->mask);
    FSAL_SET_MASK(p_object_attributes->mask, 
                  ATTR_RDATTR_ERR);
    return st;
  }

  FSI_TRACE(FSI_DEBUG, "End-----------------------------\n");
  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

/**
 * PTFSAL_setattrs:
 * Set attributes for the object specified by its filehandle.
 *
 * \param dir_hdl (input): 
 *        The handle of the object to get parameters.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param p_attrib_set (mandatory input):
 *        The attributes to be set for the object.
 *        It defines the attributes that the caller
 *        wants to set and their values.
 * \param p_object_attributes (optionnal input/output):
 *        The post operation attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t PTFSAL_setattrs(struct fsal_obj_handle *dir_hdl,           /* IN */
                              const struct req_op_context * p_context,   /* IN */
                              struct attrlist * p_attrib_set,            /* IN */
                              struct attrlist * p_object_attributes)     /* IN/OUT */

{
  unsigned int i, mntfd;
  fsal_status_t status;

  struct pt_fsal_obj_handle *myself;
  ptfsal_xstat_t buffxstat;

  fsal_accessflags_t access_mask = 0;
  struct attrlist    wanted_attrs, current_attrs;
  mode_t             st_mode_in_cache = 0;
  char               fsi_name[PATH_MAX];
  int                rc;
  int fd;

  FSI_TRACE(FSI_DEBUG, "Begin-----------------------------------------\n");

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if (!dir_hdl || !p_context || !p_attrib_set) {
    return fsalstat(ERR_FSAL_FAULT, 0);
  }

  myself = container_of(dir_hdl, struct pt_fsal_obj_handle, obj_handle);
  mntfd = pt_get_root_fd(dir_hdl->export);

  /* local copy of attributes */
  wanted_attrs = *p_attrib_set;

  /* First, check that FSAL attributes changes are allowed. */
  if(!dir_hdl->export->ops->fs_supports(dir_hdl->export, fso_cansettime))
  {
    if (wanted_attrs.mask
       & (ATTR_ATIME | ATTR_CREATION | ATTR_CTIME 
       | ATTR_MTIME)) {
       /* handled as an unsettable attribute. */
       return fsalstat(ERR_FSAL_INVAL, 0);
    }
  }

  /* apply umask, if mode attribute is to be changed */
  if (FSAL_TEST_MASK(wanted_attrs.mask, ATTR_MODE)) {
    wanted_attrs.mode &= ~dir_hdl->export->ops->fs_umask(dir_hdl->export);
  }

  /* get current attributes */
  current_attrs.mask = dir_hdl->export->ops->fs_supported_attrs(dir_hdl->export);
  status = PTFSAL_getattrs(dir_hdl->export, p_context, myself->handle,
                             &current_attrs);

  if (FSAL_IS_ERROR(status))
    return status;

  /**************
   *  TRUNCATE  *
   **************/

  if(FSAL_TEST_MASK(wanted_attrs.mask, ATTR_SIZE)) {

      status = fsal_internal_handle2fd(p_context, myself, &fd, O_RDONLY);

      if (FSAL_IS_ERROR(status)) {
        return status;
      }

      status = PTFSAL_truncate( dir_hdl->export,
                                myself,
                                p_context,   
                                wanted_attrs.filesize,                
                                p_object_attributes);

      if (FSAL_IS_ERROR(status)) {
        return status;
      }
    
   }
 

  /***********
   *  CHMOD  *
   ***********/
  if (FSAL_TEST_MASK(wanted_attrs.mask, ATTR_MODE)) {
    FSI_TRACE(FSI_DEBUG, "Begin chmod------------------\n");
    /* The POSIX chmod call don't affect the symlink object, but
     * the entry it points to. So we must ignore it.
     */
    if (current_attrs.type != SYMBOLIC_LINK) {

      /* For modifying mode, user must be root or the owner */
      if ((p_context->creds->caller_uid != 0)
         && (p_context->creds->caller_uid != current_attrs.owner)) {
        FSI_TRACE(FSI_DEBUG,
                  "Permission denied for CHMOD opeartion: " 
                  "current owner=%d, credential=%d",
                  current_attrs.owner, 
                  p_context->creds->caller_uid);
        return fsalstat(ERR_FSAL_PERM, 0);
      }

      /* Fill wanted mode. */
      buffxstat.buffstat.st_mode = fsal2unix_mode(wanted_attrs.mode);
      FSI_TRACE(FSI_DEBUG,
                "current mode = %o, new mode = %o",
                fsal2unix_mode(current_attrs.mode), 
                buffxstat.buffstat.st_mode);

      rc = fsi_get_name_from_handle(p_context, 
                                    myself, 
                                    fsi_name,
                                    NULL);
      if (rc < 0) {
        FSI_TRACE(FSI_ERR, 
                  "Failed to convert file handle back to filename" );
        FSI_TRACE(FSI_DEBUG, "Handle to name failed for hanlde %s", 
                  myself->handle->data.handle.f_handle);
        return fsalstat(ERR_FSAL_BADHANDLE, 0);
      }
      FSI_TRACE(FSI_DEBUG, "Handle to name: %s for handle %s", 
                fsi_name, myself->handle->data.handle.f_handle);

      rc = ptfsal_chmod(p_context, dir_hdl->export, fsi_name, 
                        buffxstat.buffstat.st_mode);
      if (rc == -1) {
        FSI_TRACE(FSI_ERR, "chmod FAILED");
        return fsalstat(ERR_FSAL_PERM, 0);
      } else {
        st_mode_in_cache = (buffxstat.buffstat.st_mode 
                            | fsal_type2unix(current_attrs.type));
        fsi_update_cache_stat(fsi_name, 
                              st_mode_in_cache, 
                              dir_hdl->export->exp_entry->id);
        FSI_TRACE(FSI_INFO, 
                  "Chmod SUCCEED with st_mode in cache being %o",
                  st_mode_in_cache);
      }

    }
    FSI_TRACE(FSI_DEBUG, "End chmod-------------------\n");
  }

  /***********
   *  CHOWN  *
   ***********/
  FSI_TRACE(FSI_DEBUG, "Begin chown------------------------------\n");
  /* Only root can change uid and A normal user must be in the group 
   * he wants to set 
   */
  if (FSAL_TEST_MASK(wanted_attrs.mask, ATTR_OWNER)) {

    /* For modifying owner, user must be root or current 
     * owner==wanted==client 
     */
    if ((p_context->creds->caller_uid != 0) &&
       ((p_context->creds->caller_uid != current_attrs.owner) ||
       (p_context->creds->caller_uid != wanted_attrs.owner))) {
      FSI_TRACE(FSI_DEBUG,
                "Permission denied for CHOWN opeartion: " 
                "current owner=%d, credential=%d, new owner=%d",
                current_attrs.owner, p_context->creds->caller_uid, 
                wanted_attrs.owner);
       return fsalstat(ERR_FSAL_PERM, 0);
    }
  }

  if (FSAL_TEST_MASK(wanted_attrs.mask, ATTR_GROUP)) {

    /* For modifying group, user must be root or current owner */
    if ((p_context->creds->caller_uid != 0)
       && (p_context->creds->caller_uid != current_attrs.owner)) {
       return fsalstat(ERR_FSAL_PERM, 0);
    }

    int in_grp = 0;
    /* set in_grp */
    if (p_context->creds->caller_gid == wanted_attrs.group) {
      in_grp = 1;
    } else {
      for(i = 0; i < p_context->creds->caller_glen; i++) {
        if ((in_grp = (wanted_attrs.group == 
            p_context->creds->caller_garray[i])))
          break;
      }
    }
    /* it must also be in target group */
    if (p_context->creds->caller_uid = 0 && !in_grp) {
      FSI_TRACE(FSI_DEBUG,
                "Permission denied for CHOWN operation: " 
                "current group=%d, credential=%d, new group=%d",
                current_attrs.group, p_context->creds->caller_gid, 
                wanted_attrs.group);
      return fsalstat(ERR_FSAL_PERM, 0);
    }
  }

  if (FSAL_TEST_MASK(wanted_attrs.mask, ATTR_OWNER | 
     ATTR_GROUP)) {

    /* Fill wanted owner. */
    if (FSAL_TEST_MASK(wanted_attrs.mask, ATTR_OWNER)) {
      buffxstat.buffstat.st_uid = (int)wanted_attrs.owner;
    } else {
      buffxstat.buffstat.st_uid = (int)current_attrs.owner;
    }
    FSI_TRACE(FSI_DEBUG,
              "current uid = %d, new uid = %d",
              current_attrs.owner, buffxstat.buffstat.st_uid);

    /* Fill wanted group. */
    if (FSAL_TEST_MASK(wanted_attrs.mask, ATTR_GROUP)) {
      buffxstat.buffstat.st_gid = (int)wanted_attrs.group;
    } else {
      buffxstat.buffstat.st_gid = (int)current_attrs.group;
    }
    FSI_TRACE(FSI_DEBUG,
              "current gid = %d, new gid = %d",
              current_attrs.group, buffxstat.buffstat.st_gid);

    rc = fsi_get_name_from_handle(p_context, 
                                  myself, 
                                  fsi_name,
                                  NULL);
    if (rc < 0) {
      FSI_TRACE(FSI_ERR, "Failed to convert file handle back to filename" );
      FSI_TRACE(FSI_DEBUG, "Handle to name failed for hanlde %s", 
                myself->handle->data.handle.f_handle);
      return fsalstat(ERR_FSAL_BADHANDLE, 0);
    }
   
    FSI_TRACE(FSI_DEBUG, "handle to name: %s for handle %s", 
              fsi_name, myself->handle->data.handle.f_handle);
    rc = ptfsal_chown(p_context, dir_hdl->export, fsi_name, buffxstat.buffstat.st_uid, 
                      buffxstat.buffstat.st_gid);
    if (rc == -1) {
      FSI_TRACE(FSI_ERR, "chown FAILED");
      return fsalstat(ERR_FSAL_PERM, 1);
    } else {
      FSI_TRACE(FSI_INFO, "Chown SUCCEED");
    }
  }
  FSI_TRACE(FSI_DEBUG, "End chown-----------------------------------\n");

  /***********
   *  UTIME  *
   ***********/
  FSI_TRACE(FSI_DEBUG, "Begin UTIME-----------------------------------\n");
  /* user must be the owner or have read access to modify 'atime' */
  access_mask = FSAL_MODE_MASK_SET(FSAL_R_OK) |
                FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_ATTR);

  if(!dir_hdl->export->ops->fs_supports(dir_hdl->export, fso_accesscheck_support)) {
    status = fsal_internal_testAccess(p_context, access_mask, &current_attrs);
  } else {
    status = fsal_internal_access(mntfd, p_context, myself->handle, access_mask,
                                  &current_attrs);
  }

  if (FSAL_TEST_MASK(wanted_attrs.mask, ATTR_ATIME) 
     && (p_context->creds->caller_uid= 0)
     && (p_context->creds->caller_uid != current_attrs.owner)
     && (status.major
         != ERR_FSAL_NO_ERROR)) {
    return status;
  }

  /* user must be the owner or have write access to modify 'mtime' */
  access_mask = FSAL_MODE_MASK_SET(FSAL_W_OK) |
                FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_ATTR);

  if(!dir_hdl->export->ops->fs_supports(dir_hdl->export, fso_accesscheck_support)) {
    status = fsal_internal_testAccess(p_context, access_mask, &current_attrs);
  } else {
    status = fsal_internal_access(mntfd, p_context, myself->handle, access_mask,
                                  &current_attrs);
  }
  if (FSAL_TEST_MASK(wanted_attrs.mask, ATTR_MTIME)
     && (p_context->creds->caller_uid != 0)
     && (p_context->creds->caller_uid != current_attrs.owner)
     && (status.major
         != ERR_FSAL_NO_ERROR)) {
     return status;
  }

  if (FSAL_TEST_MASK(wanted_attrs.mask, ATTR_ATIME | 
                    ATTR_MTIME)) {

    /* Fill wanted atime. */
    if (FSAL_TEST_MASK(wanted_attrs.mask, ATTR_ATIME)) {
      buffxstat.buffstat.st_atime = (time_t) wanted_attrs.atime.tv_sec;
      FSI_TRACE(FSI_DEBUG,
                "current atime = %lu, new atime = %lu",
                (unsigned long)current_attrs.atime.tv_sec, 
                (unsigned long)buffxstat.buffstat.st_atime);
    } else {
      buffxstat.buffstat.st_atime = (time_t) current_attrs.atime.tv_sec;
    }
    FSI_TRACE(FSI_DEBUG,
              "current atime = %lu, new atime = %lu",
              (unsigned long)current_attrs.atime.tv_sec,
              (unsigned long)buffxstat.buffstat.st_atime);

    /* Fill wanted mtime. */
    if (FSAL_TEST_MASK(wanted_attrs.mask, ATTR_MTIME)) {
      buffxstat.buffstat.st_mtime = (time_t) wanted_attrs.mtime.tv_sec;
    } else {
      buffxstat.buffstat.st_mtime = (time_t) current_attrs.mtime.tv_sec;
    }
    FSI_TRACE(FSI_DEBUG,
              "current mtime = %lu, new mtime = %lu",
              (unsigned long)current_attrs.mtime.tv_sec,
              (unsigned long)buffxstat.buffstat.st_mtime);

    rc = fsi_get_name_from_handle(p_context, 
                                  myself, 
                                  fsi_name,
                                  NULL);
    if (rc < 0) {
      FSI_TRACE(FSI_ERR, 
                "Failed to convert file handle back to filename "  
                "from cache" );
      FSI_TRACE(FSI_DEBUG, "Handle to name failed for hanlde %s",
                myself->handle->data.handle.f_handle);
      return fsalstat(ERR_FSAL_BADHANDLE, 0);
    }

    FSI_TRACE(FSI_DEBUG, "Handle to name: %s for handle %s", 
              fsi_name, myself->handle->data.handle.f_handle); 
   
    rc = ptfsal_ntimes(p_context, dir_hdl->export, fsi_name, buffxstat.buffstat.st_atime, 
                       buffxstat.buffstat.st_mtime);
    if (rc == -1) {
      FSI_TRACE(FSI_ERR, "ntime FAILED");
      return fsalstat(ERR_FSAL_PERM, 2);
    } else {
      FSI_TRACE(FSI_INFO, "ntime SUCCEED");
    }

  }
  FSI_TRACE(FSI_DEBUG, "End UTIME------------------------------\n");
  /* Optionaly fills output attributes. */

  if (p_object_attributes) {
      status = PTFSAL_getattrs(dir_hdl->export, p_context, myself->handle,
                                 p_object_attributes);

    /* on error, we set a special bit in the mask. */
    if (FSAL_IS_ERROR(status)) {
      FSAL_CLEAR_MASK(p_object_attributes->mask);
      FSAL_SET_MASK(p_object_attributes->mask, 
                    ATTR_RDATTR_ERR);
    }

  }
  FSI_TRACE(FSI_DEBUG, "End--------------------------------------------\n");
  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}
