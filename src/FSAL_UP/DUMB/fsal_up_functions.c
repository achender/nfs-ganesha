/*
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
 * \file    fsal_up_thread.c
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
#include <unistd.h>
#include <sys/types.h>

#include "nfs_core.h"
#include "log.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "HashTable.h"
#include "fsal_up.h"
#include "sal_functions.h"

/**
 * @brief Compare two times
 *
 * Determine if @c t1 is less-than, equal-to, or greater-than @c t2.
 *
 * @param[in] t1 First time
 * @param[in] t2 Second time
 *
 * @retval -1 @c t1 is less-than @c t2
 * @retval 0 @c t1 is equal-to @c t2
 * @retval 1 @c t1 is greater-than @c t2
 */

static inline int gsh_time_cmp(fsal_time_t *t1,
                               fsal_time_t *t2)
{
        if (t1->seconds < t2->seconds) {
                return -1;
        } else if (t1->seconds > t2->seconds) {
                return 1;
        } else {
                if (t1->nseconds < t2->nseconds)
                        return -1;
                else if (t1->nseconds > t2->nseconds)
                        return 1;
        } 
 
        return 0;
}

/* Set the FSAL UP functions that will be used to process events.
 * This is called DUMB_FSAL_UP because it only invalidates cache inode
 * entires ... inode entries are not updated or refreshed through this
 * interface. */

fsal_status_t dumb_fsal_up_invalidate_step1(fsal_up_event_data_t * pevdata)
{
  cache_inode_status_t cache_status;

  LogFullDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DUMB: calling cache_inode_invalidate()");

  /* Lock the entry */
  cache_inode_invalidate(&pevdata->event_context.fsal_data,
                         &cache_status,
                         CACHE_INODE_INVALIDATE_CLEARBITS);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t dumb_fsal_up_invalidate_step2(fsal_up_event_data_t * pevdata)
{
  cache_inode_status_t cache_status;

  LogFullDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DUMB: calling cache_inode_invalidate()");

  /* Lock the entry */
  cache_inode_invalidate(&pevdata->event_context.fsal_data,
                         &cache_status,
                         CACHE_INODE_INVALIDATE_CLOSE);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t dumb_fsal_up_update(fsal_up_event_data_t * pevdata)
{
//  cache_inode_status_t cache_status;
  cache_entry_t *entry = NULL;
  int rc = 0;
  unsigned int flags = pevdata->type.update.upu_flags;
  /* Have necessary changes been made? */
  bool mutatis_mutandis = false;
  hash_buffer_t key, value;
  struct hash_latch latch;

  fsal_attrib_list_t *attr = &pevdata->type.update.upu_attr;

  /* These cannot be updated, changing any of them is
   * tantamount to destroying and recreating the file. */
  if (FSAL_TEST_MASK
      (attr->asked_attributes,
       FSAL_ATTR_TYPE | FSAL_ATTR_FSID | FSAL_ATTR_FILEID | FSAL_ATTR_RAWDEV |
       FSAL_ATTR_RDATTR_ERR | FSAL_ATTR_GENERATION)) {
    ReturnCode(ERR_FSAL_INVAL, 0);
  }

  /* Filter out garbage flags */

  if (flags &                                    
      ~(fsal_up_update_filesize_inc | fsal_up_update_atime_inc |
        fsal_up_update_creation_inc | fsal_up_update_ctime_inc |
        fsal_up_update_mtime_inc | fsal_up_update_chgtime_inc |
        fsal_up_update_spaceused_inc)) {       
    ReturnCode(ERR_FSAL_INVAL, 0);
   }                  

  /* Locate the entry in the cache */
  FSAL_ExpandHandle(NULL,  /* pcontext but not used... */
                    FSAL_DIGEST_SIZEOF,
                    &pevdata->event_context.fsal_data.fh_desc);

  /* Turn the input to a hash key */
  key.pdata = pevdata->event_context.fsal_data.fh_desc.start;
  key.len = pevdata->event_context.fsal_data.fh_desc.len;

  if ((rc = HashTable_GetLatch(fh_to_cache_entry_ht,
                               &key,
                               &value,
                               FALSE,
                               &latch)) == HASHTABLE_ERROR_NO_SUCH_KEY) {
       /* Entry is not cached */
       HashTable_ReleaseLatched(fh_to_cache_entry_ht, &latch);
       //*status = CACHE_INODE_NOT_FOUND;
       //return *status;
       ReturnCode(ERR_FSAL_INVAL, 0);
  } else if (rc != HASHTABLE_SUCCESS) {
       LogCrit(COMPONENT_CACHE_INODE,
               "Unexpected error %u while calling HashTable_GetLatch", rc) ;
       //*status = CACHE_INODE_INVALID_ARGUMENT;
       //return *status;
       ReturnCode(ERR_FSAL_INVAL, 0);
  }
  entry = value.pdata;
  if (cache_inode_lru_ref(entry, 0) != CACHE_INODE_SUCCESS) {
       HashTable_ReleaseLatched(fh_to_cache_entry_ht, &latch);
       //*status = CACHE_INODE_NOT_FOUND;
       //return *status;
       ReturnCode(ERR_FSAL_INVAL, 0);
  }

  PTHREAD_RWLOCK_WRLOCK(&entry->attr_lock);
  if (FSAL_TEST_MASK(attr->asked_attributes, FSAL_ATTR_SIZE)) {
    if (flags & fsal_up_update_filesize_inc) {
      if (attr->filesize > entry->attributes.filesize) {
        entry->attributes.filesize = attr->filesize;
        mutatis_mutandis = true;
      }
    } else {
      entry->attributes.filesize = attr->filesize;
      mutatis_mutandis = true;
    }
  }

  if (FSAL_TEST_MASK(attr->asked_attributes, FSAL_ATTR_SPACEUSED)) {
    if (flags & fsal_up_update_spaceused_inc) {
      if (attr->spaceused > entry->attributes.spaceused) {
        entry->attributes.spaceused = attr->spaceused;
        mutatis_mutandis = true;
      }
    } else {
      entry->attributes.spaceused = attr->spaceused;
      mutatis_mutandis = true;
    }
  }

  if (FSAL_TEST_MASK(attr->asked_attributes, FSAL_ATTR_ACL)) {
    /**
     * @todo Someone who knows the ACL code, please look
     * over this.  We assume that the FSAL takes a
     * reference on the supplied ACL that we can then hold
     * onto.  This seems the most reasonable approach in
     * an asynchronous call.
     */

     /* This idiom is evil. */
     fsal_acl_status_t acl_status;

     nfs4_acl_release_entry(entry->attributes.acl, &acl_status);
     entry->attributes.acl = attr->acl;
     mutatis_mutandis = true;
  }

  if (FSAL_TEST_MASK(attr->asked_attributes, FSAL_ATTR_MODE)) {
    entry->attributes.mode = attr->mode;
    mutatis_mutandis = true;
  }

  if (FSAL_TEST_MASK(attr->asked_attributes, FSAL_ATTR_NUMLINKS)) {
    entry->attributes.numlinks = attr->numlinks;
    mutatis_mutandis = true;
  }

  if (FSAL_TEST_MASK(attr->asked_attributes, FSAL_ATTR_OWNER)) {
    entry->attributes.owner = attr->owner;
    mutatis_mutandis = true;
  }

  if (FSAL_TEST_MASK(attr->asked_attributes, FSAL_ATTR_GROUP)) {
    entry->attributes.group = attr->group;
    mutatis_mutandis = true;
  }

  if (FSAL_TEST_MASK(attr->asked_attributes, FSAL_ATTR_ATIME)
      && ((flags & ~fsal_up_update_atime_inc) || (gsh_time_cmp 
                  (&attr->atime, &entry->attributes.atime) == 1))) {
     entry->attributes.atime = attr->atime;
     mutatis_mutandis = true;
  }

  if (FSAL_TEST_MASK(attr->asked_attributes, FSAL_ATTR_CREATION)
      && ((flags & ~fsal_up_update_creation_inc) || (gsh_time_cmp
            (&attr->creation, &entry->attributes.creation) == 1))) {
    entry->attributes.creation = attr->creation;
    mutatis_mutandis = true;
  }

  if (FSAL_TEST_MASK(attr->asked_attributes, FSAL_ATTR_CTIME)
      && ((flags & ~fsal_up_update_ctime_inc) || (gsh_time_cmp
                 (&attr->ctime, &entry->attributes.ctime) == 1))) {
     entry->attributes.ctime = attr->ctime;
     mutatis_mutandis = true;
  }

  if (FSAL_TEST_MASK(attr->asked_attributes, FSAL_ATTR_MTIME)
      && ((flags & ~fsal_up_update_mtime_inc) || (gsh_time_cmp
                 (&attr->mtime, &entry->attributes.mtime) == 1))) {
    entry->attributes.mtime = attr->mtime;
    mutatis_mutandis = true;
  }

  if (FSAL_TEST_MASK(attr->asked_attributes, FSAL_ATTR_CHGTIME)
      && ((flags & ~fsal_up_update_chgtime_inc) || (gsh_time_cmp
              (&attr->chgtime, &entry->attributes.chgtime) == 1))) {
     entry->attributes.chgtime = attr->chgtime;
     mutatis_mutandis = true;
  }
  if (FSAL_TEST_MASK(attr->asked_attributes, FSAL_ATTR_CHANGE)) {
    entry->attributes.change = attr->change;
    mutatis_mutandis = true;
  }

  if (mutatis_mutandis) {
    cache_inode_fixup_md(entry);
    /* If directory can not trust content anymore. */
    if (entry->type == DIRECTORY)
      atomic_clear_uint32_t_bits(&entry->flags,
                                 CACHE_INODE_TRUST_CONTENT |
                                 CACHE_INODE_DIR_POPULATED);
  } else {
    atomic_clear_uint32_t_bits(&entry->flags,
                               CACHE_INODE_TRUST_CONTENT |
                               CACHE_INODE_DIR_POPULATED);
  }

  PTHREAD_RWLOCK_UNLOCK(&entry->attr_lock);
  cache_inode_lru_unref(entry, 0);

  LogFullDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DUMB: Entered dumb_fsal_up_update");

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

#define INVALIDATE_STUB {                     \
    return dumb_fsal_up_invalidate_step1(pevdata);  \
  } while(0);

fsal_status_t dumb_fsal_up_create(fsal_up_event_data_t * pevdata)
{
  INVALIDATE_STUB;
}

fsal_status_t dumb_fsal_up_unlink(fsal_up_event_data_t * pevdata)
{
  INVALIDATE_STUB;
}

fsal_status_t dumb_fsal_up_rename(fsal_up_event_data_t * pevdata)
{
  INVALIDATE_STUB;
}

fsal_status_t dumb_fsal_up_commit(fsal_up_event_data_t * pevdata)
{
  INVALIDATE_STUB;
}

fsal_status_t dumb_fsal_up_write(fsal_up_event_data_t * pevdata)
{
  INVALIDATE_STUB;
}

fsal_status_t dumb_fsal_up_link(fsal_up_event_data_t * pevdata)
{
  INVALIDATE_STUB;
}

fsal_status_t dumb_fsal_up_lock_grant(fsal_up_event_data_t * pevdata)
{
#ifdef _USE_BLOCKING_LOCKS
  cache_inode_status_t   cache_status;
  cache_entry_t        * pentry = NULL;
  fsal_attrib_list_t     attr;

  LogFullDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DUMB: calling cache_inode_get()");
  pentry = cache_inode_get(&pevdata->event_context.fsal_data,
                           &attr, NULL, NULL,
                           &cache_status);
  if(pentry == NULL)
    {
      LogDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DUMB: cache inode get failed.");
      /* Not an error. Expecting some nodes will not have it in cache in
       * a cluster. */
      ReturnCode(ERR_FSAL_NO_ERROR, 0);
    }

  LogDebug(COMPONENT_FSAL_UP,
           "FSAL_UP_DUMB: Lock Grant found entry %p",
           pentry);

  grant_blocked_lock_upcall(pentry,
                            pevdata->type.lock_grant.lock_owner,
                            &pevdata->type.lock_grant.lock_param);


  if(pentry)
    cache_inode_put(pentry);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
#else
  INVALIDATE_STUB;
#endif
}

fsal_status_t dumb_fsal_up_lock_avail(fsal_up_event_data_t * pevdata)
{
#ifdef _USE_BLOCKING_LOCKS
  cache_inode_status_t   cache_status;
  cache_entry_t        * pentry = NULL;
  fsal_attrib_list_t     attr;

  LogFullDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DUMB: calling cache_inode_get()");
  pentry = cache_inode_get(&pevdata->event_context.fsal_data,
                           &attr, NULL, NULL, &cache_status);
  if(pentry == NULL)
    {
      LogDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DUMB: cache inode get failed.");
      /* Not an error. Expecting some nodes will not have it in cache in
       * a cluster. */
      ReturnCode(ERR_FSAL_NO_ERROR, 0);
    }

  LogFullDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DUMB: Lock Available found entry %p",
               pentry);

  available_blocked_lock_upcall(pentry,
                                pevdata->type.lock_grant.lock_owner,
                                &pevdata->type.lock_grant.lock_param);

  if(pentry)
    cache_inode_put(pentry);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
#else
  INVALIDATE_STUB;
#endif
}

fsal_status_t dumb_fsal_up_open(fsal_up_event_data_t * pevdata)
{
  INVALIDATE_STUB;
}

fsal_status_t dumb_fsal_up_close(fsal_up_event_data_t * pevdata)
{
  INVALIDATE_STUB;
}

fsal_status_t dumb_fsal_up_setattr(fsal_up_event_data_t * pevdata)
{
  INVALIDATE_STUB;
}

fsal_up_event_functions_t dumb_event_func = {
  .fsal_up_create = dumb_fsal_up_create,
  .fsal_up_unlink = dumb_fsal_up_unlink,
  .fsal_up_rename = dumb_fsal_up_rename,
  .fsal_up_commit = dumb_fsal_up_commit,
  .fsal_up_write = dumb_fsal_up_write,
  .fsal_up_link = dumb_fsal_up_link,
  .fsal_up_lock_grant = dumb_fsal_up_lock_grant,
  .fsal_up_lock_avail = dumb_fsal_up_lock_avail,
  .fsal_up_open = dumb_fsal_up_open,
  .fsal_up_close = dumb_fsal_up_close,
  .fsal_up_setattr = dumb_fsal_up_setattr,
  .fsal_up_update = dumb_fsal_up_update,
  .fsal_up_invalidate = dumb_fsal_up_invalidate_step1
};

fsal_up_event_functions_t *get_fsal_up_dumb_functions()
{
  return &dumb_event_func;
}
