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
 *---------------------------------------
 */

/**
 * \file    fsal_up.h
 * \date    $Date: 2011/09/29 $
 */

#ifndef _FSAL_UP_H
#define _FSAL_UP_H

#ifdef _USE_FSAL_UP

#include "fsal_types.h"
#include "cache_inode.h"
#include "nfs_exports.h"

#define FSAL_UP_EVENT_CREATE     1
#define FSAL_UP_EVENT_UNLINK     2
#define FSAL_UP_EVENT_RENAME     3
#define FSAL_UP_EVENT_COMMIT     4
#define FSAL_UP_EVENT_WRITE      5
#define FSAL_UP_EVENT_LINK       6
#define FSAL_UP_EVENT_LOCK_GRANT 7
#define FSAL_UP_EVENT_LOCK_AVAIL 8
#define FSAL_UP_EVENT_OPEN       9
#define FSAL_UP_EVENT_CLOSE      10
#define FSAL_UP_EVENT_SETATTR    11
#define FSAL_UP_EVENT_UPDATE     12
#define FSAL_UP_EVENT_INVALIDATE 13


/**                                                
 * Empty flags. 
 */                                     
static const uint32_t fsal_up_update_null = 0x0000;
                                                
/**                                             
 * Update the filesize only if the new size is greater than that
 * currently set.                                   
 */                                                  
static const uint32_t fsal_up_update_filesize_inc = 0x0001;

/**
 * Update the atime only if the new time is later than the currently
 * set time.
 */
static const uint32_t fsal_up_update_atime_inc = 0x0002;

/**
 * Update the creation time only if the new time is later than the
 * currently set time.
 */
static const uint32_t fsal_up_update_creation_inc = 0x0004;

/**
 * Update the ctime only if the new time is later than that currently
 * set.
 */
static const uint32_t fsal_up_update_ctime_inc = 0x0008;

/**
 * Update the mtime only if the new time is later than that currently
 * set.
 */
static const uint32_t fsal_up_update_mtime_inc = 0x0010;

/**
 * Update the chgtime only if the new time is later than that
 * currently set.
 */
static const uint32_t fsal_up_update_chgtime_inc = 0x0020;
/**
 * Update the spaceused only if the new size is greater than that
 * currently set.
 */
static const uint32_t fsal_up_update_spaceused_inc = 0x0040;

/**
 * The file link count is zero.
 */
static const uint32_t fsal_up_nlink = 0x0080;

static const uint32_t fsal_up_invalidate = 0x0100;


typedef struct fsal_up_event_bus_parameter_t_
{
} fsal_up_event_bus_parameter_t;

typedef struct fsal_up_event_bus_context_t_
{
  fsal_export_context_t FS_export_context;
} fsal_up_event_bus_context_t;

typedef struct fsal_up_event_data_context_t_
{
  cache_inode_fsal_data_t fsal_data;
} fsal_up_event_data_context_t;

typedef struct fsal_up_event_bus_filter_t_
{
} fsal_up_event_bus_filter_t;

typedef struct fsal_up_event_data_create_t_
{
} fsal_up_event_data_create_t;

typedef struct fsal_up_event_data_unlink_t_
{
} fsal_up_event_data_unlink_t;

typedef struct fsal_up_event_data_rename_t_
{
} fsal_up_event_data_rename_t;

typedef struct fsal_up_event_data_commit_t_
{
} fsal_up_event_data_commit_t;

typedef struct fsal_up_event_data_write_t_
{
} fsal_up_event_data_write_t;

typedef struct fsal_up_event_data_link_t_
{
} fsal_up_event_data_link_t;

typedef struct fsal_up_event_data_lock_grant_t_
{
  void              * lock_owner;
  fsal_lock_param_t   lock_param;
} fsal_up_event_data_lock_grant_t;

typedef struct fsal_up_event_data_open_t_
{
} fsal_up_event_data_open_t;

typedef struct fsal_up_event_data_close_t_
{
} fsal_up_event_data_close_t;

typedef struct fsal_up_event_data_setattr_
{
} fsal_up_event_data_setattr_t;

typedef struct fsal_up_event_data_invalidate_
{
} fsal_up_event_data_invalidate_t;

typedef struct fsal_up_event_data_update_
{
  fsal_attrib_list_t upu_attr;
  int upu_flags;
} fsal_up_event_data_update_t;

typedef struct fsal_up_event_data__
{
  union {
    fsal_up_event_data_create_t create;
    fsal_up_event_data_unlink_t unlink;
    fsal_up_event_data_rename_t rename;
    fsal_up_event_data_commit_t commit;
    fsal_up_event_data_write_t write;
    fsal_up_event_data_link_t link;
    fsal_up_event_data_lock_grant_t lock_grant;
    fsal_up_event_data_open_t open;
    fsal_up_event_data_close_t close;
    fsal_up_event_data_setattr_t setattr;
    fsal_up_event_data_update_t update;
    fsal_up_event_data_invalidate_t invalidate;
  } type;
  /* Common data most functions will need. */
  fsal_up_event_data_context_t event_context;
} fsal_up_event_data_t;

typedef fsal_status_t (fsal_up_event_process_func_t) (fsal_up_event_data_t * arg);

typedef struct fsal_up_event_t_
{
  struct glist_head event_list;
  fsal_up_event_process_func_t   * event_process_func;
  unsigned int event_type;
  fsal_up_event_data_t event_data;
} fsal_up_event_t;

typedef struct fsal_up_event_functions__
{
  fsal_status_t (*fsal_up_create) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_unlink) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_rename) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_commit) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_write) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_link) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_lock_grant) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_lock_avail) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_open) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_close) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_setattr) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_update) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_invalidate) (fsal_up_event_data_t * pevdata );
} fsal_up_event_functions_t;

/**************************
 * FSAL UP global variables
 **************************/

extern pool_t * fsal_up_event_pool;

/**************************
 * FSAL UP functions
 **************************/

#define FSAL_UP_DUMB_TYPE "DUMB"
fsal_up_event_functions_t *get_fsal_up_dumb_functions();

fsal_status_t process_event(fsal_up_event_t          * myevent,
                           fsal_up_event_functions_t * event_func);

#endif /* _USE_FSAL_UP */
#endif /* _FSAL_UP_H */
