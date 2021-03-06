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
 * ---------------------------------------
 */

/**
 * \file    nfs_Remove.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:53 $
 * \version $Revision: 1.16 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs_Remove.c : Routines used for managing the NFS4 COMPOUND functions.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_proto_tools.h"

/**
 *
 * @brief The NFS PROC2 and PROC3 REMOVE
 *
 * Implements the NFS PROC REMOVE function (for V2 and V3).
 *
 * @param[in]  parg     NFS arguments union
 * @param[in]  pexport  NFS export list
 * @param[in]  pcontext Credentials to be used for this request
 * @param[in]  pworker  Worker thread data
 * @param[in]  preq     SVC request related to this call
 * @param[out] pres     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs_Remove(nfs_arg_t *parg,
               exportlist_t *pexport,
               fsal_op_context_t *pcontext,
               nfs_worker_data_t *pworker,
               struct svc_req *preq,
               nfs_res_t *pres)
{
  cache_entry_t *parent_pentry = NULL;
  cache_entry_t *pentry_child = NULL;
  fsal_attrib_list_t pre_parent_attr;
  fsal_attrib_list_t pentry_child_attr;
  fsal_attrib_list_t parent_attr;
  fsal_attrib_list_t *pparent_attr = NULL;
  cache_inode_file_type_t filetype;
  cache_inode_file_type_t childtype;
  cache_inode_status_t cache_status;
  char *file_name = NULL;
  fsal_name_t name;
  int rc = NFS_REQ_OK;

  if(isDebug(COMPONENT_NFSPROTO))
    {
      char str[LEN_FH_STR];

      switch (preq->rq_vers)
        {
        case NFS_V2:
          file_name = parg->arg_remove2.name;
          break;
        case NFS_V3:
          file_name = parg->arg_remove3.object.name;
          break;
        }

      nfs_FhandleToStr(preq->rq_vers,
                       &(parg->arg_create2.where.dir),
                       &(parg->arg_create3.where.dir),
                       NULL,
                       str);
      LogDebug(COMPONENT_NFSPROTO,
               "REQUEST PROCESSING: Calling nfs_Remove handle: %s name: %s",
               str, file_name);
    }

  if(preq->rq_vers == NFS_V3)
    {
      /* to avoid setting it on each error case */
      pres->res_remove3.REMOVE3res_u.resfail.dir_wcc.before.attributes_follow = FALSE;
      pres->res_remove3.REMOVE3res_u.resfail.dir_wcc.after.attributes_follow = FALSE;
      pparent_attr = NULL;
    }

  /* Convert file handle into a pentry */
  if((parent_pentry = nfs_FhandleToCache(preq->rq_vers,
                                         &(parg->arg_remove2.dir),
                                         &(parg->arg_remove3.object.dir),
                                         NULL,
                                         &(pres->res_dirop2.status),
                                         &(pres->res_remove3.status),
                                         NULL,
                                         &pre_parent_attr,
                                         pcontext, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      goto out;
    }

  if((preq->rq_vers == NFS_V3) && (nfs3_Is_Fh_Xattr(&(parg->arg_remove3.object.dir)))) {
    rc = nfs3_Remove_Xattr(parg, pexport, pcontext, preq, pres);
    goto out;
  }

  /* get directory attributes before action (for V3 reply) */
  pparent_attr = &pre_parent_attr;

  /* Extract the filetype */
  filetype = cache_inode_fsal_type_convert(pre_parent_attr.type);

  /*
   * Sanity checks: new directory name must be non-null; parent must be
   * a directory. 
   */
  if(filetype != DIRECTORY)
    {
      switch (preq->rq_vers)
        {
        case NFS_V2:
          pres->res_stat2 = NFSERR_NOTDIR;
          break;

        case NFS_V3:
          pres->res_remove3.status = NFS3ERR_NOTDIR;
          break;
        }
      rc = NFS_REQ_OK;
      goto out;
    }

  switch (preq->rq_vers)
    {
    case NFS_V2:
      file_name = parg->arg_remove2.name;
      break;
    case NFS_V3:
      file_name = parg->arg_remove3.object.name;
      break;
    }

  //if(file_name == NULL || strlen(file_name) == 0)
  if(file_name == NULL || *file_name == '\0' )
    {
      cache_status = CACHE_INODE_INVALID_ARGUMENT;      /* for lack of better... */
    }
  else
    {

      if((cache_status = cache_inode_error_convert(FSAL_str2name(file_name,
                                                                 FSAL_MAX_NAME_LEN,
                                                                 &name))) ==
         CACHE_INODE_SUCCESS)
        {
          /*
           * Lookup to the child entry to check if it is a directory
           *
           */
          if((pentry_child = cache_inode_lookup(parent_pentry,
                                                &name,
                                                &pentry_child_attr,
                                                pcontext,
                                                &cache_status)) != NULL)
            {
              /* Extract the filetype */
              childtype = cache_inode_fsal_type_convert(pentry_child_attr.type);

              /*
               * Sanity check: make sure we are about to remove a directory
               */
              if(childtype == DIRECTORY)
                {
                  switch (preq->rq_vers)
                    {
                    case NFS_V2:
                      pres->res_stat2 = NFSERR_ISDIR;
                      break;

                    case NFS_V3:
                      pres->res_remove3.status = NFS3ERR_ISDIR;
                      break;
                    }
                  rc = NFS_REQ_OK;
                  goto out;
                }

              LogFullDebug(COMPONENT_NFSPROTO,
                           "==== NFS REMOVE ====> Trying to remove file %s",
                           name.name);

              /*
               * Remove the entry.
               */
              if(cache_inode_remove(parent_pentry,
                                    &name,
                                    &parent_attr,
                                    pcontext, &cache_status) == CACHE_INODE_SUCCESS)
                {
                  switch (preq->rq_vers)
                    {
                    case NFS_V2:
                      pres->res_stat2 = NFS_OK;
                      break;

                    case NFS_V3:
                      /* Build Weak Cache Coherency data */
                      nfs_SetWccData(pexport,
                                     pparent_attr,
                                     &parent_attr,
                                     &(pres->res_remove3.REMOVE3res_u.resok.dir_wcc));

                      pres->res_remove3.status = NFS3_OK;
                      break;
                    }
                  rc = NFS_REQ_OK;
                  goto out;
                }
            }
        }
    }

  /* If we are here, there was an error */
  nfs_SetFailedStatus(pcontext, pexport,
                      preq->rq_vers,
                      cache_status,
                      &pres->res_stat2,
                      &pres->res_remove3.status,
                      NULL, NULL,
                      parent_pentry,
                      pparent_attr,
                      &(pres->res_remove3.REMOVE3res_u.resfail.dir_wcc),
                      NULL, NULL, NULL);

  if(nfs_RetryableError(cache_status))
    {
      rc = NFS_REQ_DROP;
      goto out;
    }

  rc = NFS_REQ_OK;

out:
  /* return references */
  if (pentry_child)
      cache_inode_put(pentry_child);

  if (parent_pentry)
      cache_inode_put(parent_pentry);

  return (rc);

}                               /* nfs_Remove */

/**
 * nfs_Remove_Free: Frees the result structure allocated for nfs_Remove.
 * 
 * Frees the result structure allocated for nfs_Remove.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs_Remove_Free(nfs_res_t * resp)
{
  /* Nothing to do here */
  return;
}                               /* nfs_Remove_Free */
