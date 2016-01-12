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
#include "FSAL/access_check.h"
#include <signal.h>

//linked list to cache handles
struct handle_cache_node {
   void *handle;
   void *parent_handle;
   char *name;
   struct handle_cache_node *next;
};

struct handle_cache_node *handle_cache = NULL;

//find a cached handle in the list
struct handle_cache_node *findCachedHandle(fsal_handle_t * p_handle) {
	struct handle_cache_node *next = handle_cache;
        int hdl_size = sizeof(p_handle->data.handle.f_handle);

	//look for handle in the list
	while (next != NULL) {
                if (memcmp(p_handle->data.handle.f_handle,
                    next->handle,hdl_size) == 0) {
			break;
		}
	}
	return next;
}

//add a handle to the list. Abort if bad handle is detected
void cache_handle(fsal_handle_t * p_parent_directory_handle,
		  fsal_handle_t * p_child_handle, 
                  fsal_name_t * p_filename) {
	struct handle_cache_node *new;
	struct handle_cache_node *next = handle_cache;
	struct handle_cache_node *prev = next;
	int hdl_size = sizeof(p_child_handle->data.handle.f_handle);
	int name_len = sizeof(p_filename->name);
	int parent_handles_match = 0;

	//if the file name is "..", do not cache. Verify it is the same as its grandparent.
	if (strcmp(p_filename->name, "..")==0) {
		//if it is ".." try to find the actual handle
		struct handle_cache_node *cachedHandle = findCachedHandle(p_child_handle);
		
		//dont have it yet?  nothing we can do.  do not cache ".." 
		if (cachedHandle == NULL)
			return;

		//the handle for ".." should match the grand parent.  Maybe null in the case of root
		parent_handles_match = 0;
                if (cachedHandle->parent_handle == NULL && p_child_handle->data.handle.f_handle == NULL)
                        parent_handles_match = 1;
                if ((cachedHandle->parent_handle != NULL && p_child_handle->data.handle.f_handle != NULL) &&
                          memcmp(cachedHandle->parent_handle,
                          p_child_handle->data.handle.f_handle,
                          hdl_size) == 0)
                        parent_handles_match = 1;

		//if they dont match there may be a bug
		if (parent_handles_match != 1){
			LogEvent(COMPONENT_FSAL,"ACH: ERROR bad parent handle detected");
                        if(cachedHandle->parent_handle != NULL)
                                log_handle("cached parent: ", next->parent_handle, hdl_size);
                        else
                                LogEvent(COMPONENT_FSAL,"cached parent:NULL");
                        log_handle("cached child: ", cachedHandle->handle, hdl_size);
                        LogEvent(COMPONENT_FSAL,"cached name:%s", next->name);

                        if (p_parent_directory_handle != NULL)
                                log_handle("this parent: ", p_parent_directory_handle->data.handle.f_handle, hdl_size);
                        else
                                LogEvent(COMPONENT_FSAL,"this parent:NULL");
                        log_handle("this child: ", p_child_handle->data.handle.f_handle, hdl_size);
                        LogEvent(COMPONENT_FSAL,"this name:%s",p_filename->name);

			raise (SIGABRT);
		}

		//do not cache ".."
		return;
	}

	//look for handle in the list
	while (next != NULL) {

		//if we found a handle check to see if it is the same
		if (memcmp(p_child_handle->data.handle.f_handle,
		    next->handle,
		    hdl_size) == 0) {

			//check to see if parent handles match.  Parent may be null in the case of root
			parent_handles_match = 0;

			if (p_parent_directory_handle == NULL && next->parent_handle == NULL)
				parent_handles_match = 1;
			if ((p_parent_directory_handle != NULL && next->parent_handle != NULL) &&
				  memcmp(p_parent_directory_handle->data.handle.f_handle,
                                  next->parent_handle,
                                  hdl_size) == 0)
				parent_handles_match = 1;

			//if everything matches, we have already cached this handle
			if (parent_handles_match == 1 &&
			     strncmp(next->name, p_filename->name, p_filename->len) == 0) {
				return;
			}

			//if they dont match, this may be a bug
			LogEvent(COMPONENT_FSAL,"ACH: ERROR duplicate file handle detected");
			if(next->parent_handle != NULL) 
				log_handle("cached parent: ", next->parent_handle, hdl_size);
			else
				LogEvent(COMPONENT_FSAL,"cached parent:NULL");
			log_handle("cached child: ", next->handle, hdl_size);
			LogEvent(COMPONENT_FSAL,"cached name:%s", next->name);

			if (p_parent_directory_handle != NULL)
                        	log_handle("this parent: ", p_parent_directory_handle->data.handle.f_handle, hdl_size);
			else
				LogEvent(COMPONENT_FSAL,"this parent:NULL");
                        log_handle("this child: ", p_child_handle->data.handle.f_handle, hdl_size);
                        LogEvent(COMPONENT_FSAL,"this name:%s",p_filename->name);

			raise (SIGABRT);
		}
		prev = next;
		next = next->next;
        }

	//otherwise store the handle in the cache
	new  = malloc(sizeof(struct handle_cache_node));
        if (new == NULL) {
           LogEvent(COMPONENT_FSAL,"ACH: ERROR malloc failed");
           raise (SIGABRT);
        }

        if (p_parent_directory_handle != NULL){
	        new->parent_handle = malloc(hdl_size);
	        if (new->parent_handle == NULL) {
	           LogEvent(COMPONENT_FSAL,"ACH: ERROR malloc parent handle failed");
	           raise (SIGABRT);
	        }

                memcpy( new->parent_handle,
                        p_parent_directory_handle->data.handle.f_handle,
                        hdl_size);
	}

        new->handle = malloc(hdl_size);
        if (new->handle == NULL) {
           LogEvent(COMPONENT_FSAL,"ACH: ERROR malloc child handle failed");
           raise (SIGABRT);
        }
        memcpy(new->handle,
               p_child_handle->data.handle.f_handle,
               hdl_size);


	new->name =  malloc(name_len);
        if (new->name == NULL) {
           LogEvent(COMPONENT_FSAL,"ACH: ERROR malloc name failed");
           raise (SIGABRT);
        }
	memcpy(new->name, p_filename->name, name_len);

	new->next = NULL;


	//append to cache
	if (handle_cache == NULL)
		handle_cache = new;
	else
		prev->next = new;
           
}


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
fsal_status_t GPFSFSAL_lookup(fsal_handle_t * p_parent_directory_handle,    /* IN */
                          fsal_name_t * p_filename,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_handle_t * object_handle,      /* OUT */
                          fsal_attrib_list_t * p_object_attributes      /* [ IN/OUT ] */
    )
{
  fsal_status_t status;
  int parentfd;
  fsal_attrib_list_t parent_dir_attrs;
  gpfsfsal_handle_t *p_object_handle = (gpfsfsal_handle_t *)object_handle;

  /* sanity checks
   * note : object_attributes is optional
   *        parent_directory_handle may be null for getting FS root.
   */
  if(!p_object_handle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

  /* filename AND parent handle are NULL => lookup "/" */
  if((p_parent_directory_handle && !p_filename)
     || (!p_parent_directory_handle && p_filename))
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

  /* get information about root */
  LogEvent(COMPONENT_FSAL,"ACH: Lookup for %s\n", p_filename->name);
  if(!p_parent_directory_handle)
    {
      gpfsfsal_handle_t *root_handle = &((gpfsfsal_op_context_t *)p_context)->export_context->mount_root_handle;

      /* get handle for the mount point  */
      memcpy(p_object_handle->data.handle.f_handle,
	     root_handle->data.handle.f_handle,
             sizeof(root_handle->data.handle.handle_size));
      p_object_handle->data.handle.handle_size = root_handle->data.handle.handle_size;
      p_object_handle->data.handle.handle_key_size = root_handle->data.handle.handle_key_size;


      log_handle("GPFSFSAL_lookup: root handle:",
                 p_object_handle->data.handle.f_handle,
                 sizeof(p_object_handle->data.handle.f_handle));

       cache_handle(NULL, p_object_handle, p_filename);


      /* get attributes, if asked */
      if(p_object_attributes)
        {
          status = GPFSFSAL_getattrs(object_handle, p_context, p_object_attributes);
          if(FSAL_IS_ERROR(status))
            {
              FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
              FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
            }
        }
      /* Done */
      Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookup);
    }

  log_handle("GPFSFSAL_lookup: parent handle:",
                 p_parent_directory_handle->data.handle.f_handle,
                 sizeof(p_parent_directory_handle->data.handle.f_handle));

  /* retrieve directory attributes */
  TakeTokenFSCall();
  status =
      fsal_internal_handle2fd(p_context, p_parent_directory_handle, &parentfd, O_RDONLY);
  ReleaseTokenFSCall();
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_lookup);

  /* get directory metadata */

  parent_dir_attrs.asked_attributes = GPFS_SUPPORTED_ATTRIBUTES;
  status = GPFSFSAL_getattrs(p_parent_directory_handle, p_context, &parent_dir_attrs);
  if(FSAL_IS_ERROR(status))
    {
      close(parentfd);
      ReturnStatus(status, INDEX_FSAL_lookup);
    }

  /* Be careful about junction crossing, symlinks, hardlinks,... */
  switch (parent_dir_attrs.type)
    {
    case FSAL_TYPE_DIR:
      // OK
      break;

    case FSAL_TYPE_JUNCTION:
      // This is a junction
      close(parentfd);
      Return(ERR_FSAL_XDEV, 0, INDEX_FSAL_lookup);

    case FSAL_TYPE_FILE:
    case FSAL_TYPE_LNK:
    case FSAL_TYPE_XATTR:
      // not a directory 
      close(parentfd);
      Return(ERR_FSAL_NOTDIR, 0, INDEX_FSAL_lookup);

    default:
      close(parentfd);
      Return(ERR_FSAL_SERVERFAULT, 0, INDEX_FSAL_lookup);
    }

  //  LogFullDebug(COMPONENT_FSAL,
  //               "lookup of %#llx:%#x:%#x/%s", p_parent_directory_handle->seq,
  //               p_parent_directory_handle->oid, p_parent_directory_handle->ver,
  //               p_filename->name);

  /* get file handle, it it exists */
  /* This might be a race, but it's the best we can currently do */
  status = fsal_internal_get_handle_at(parentfd, p_filename, object_handle,
      p_context);

  log_handle("GPFSFSAL_lookup handle:",
             object_handle->data.handle.f_handle,
             sizeof(object_handle->data.handle.f_handle));
  cache_handle(p_parent_directory_handle, object_handle, p_filename);

  close(parentfd);

  if (memcmp(p_parent_directory_handle->data.handle.f_handle,
             object_handle->data.handle.f_handle,
             sizeof(object_handle->data.handle.f_handle)) == 0) {
      LogEvent(COMPONENT_FSAL,"ACH: ERROR Duplicate parent and child handles detected.  Raising abort");
      log_handle("parent:", p_parent_directory_handle->data.handle.f_handle, sizeof(p_parent_directory_handle->data.handle.f_handle));
      log_handle("child:", object_handle->data.handle.f_handle, sizeof(object_handle->data.handle.f_handle));
      LogEvent(COMPONENT_FSAL,"ACH: p_filename:%s", p_filename->name); 
      raise (SIGABRT);
  }

  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_lookup);

  /* get object attributes */
  if(p_object_attributes)
    {
      status = GPFSFSAL_getattrs(object_handle, p_context, p_object_attributes);
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
          FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

  /* lookup complete ! */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookup);
}

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

fsal_status_t GPFSFSAL_lookupPath(fsal_path_t * p_path,     /* IN */
                              fsal_op_context_t * p_context,    /* IN */
                              fsal_handle_t * object_handle,    /* OUT */
                              fsal_attrib_list_t * p_object_attributes  /* [ IN/OUT ] */
    )
{
  fsal_status_t status;

  /* sanity checks
   * note : object_attributes is optionnal.
   */

  if(!object_handle || !p_context || !p_path)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookupPath);

  /* test whether the path begins with a slash */

  if(p_path->path[0] != '/')
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_lookupPath);

  /* directly call the lookup function */

  status = fsal_internal_get_handle(p_context, p_path, object_handle);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_lookupPath);

  /* get object attributes */
  if(p_object_attributes)
    {
      status = GPFSFSAL_getattrs(object_handle, p_context, p_object_attributes);
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
          FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupPath);

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
fsal_status_t GPFSFSAL_lookupJunction(fsal_handle_t * p_junction_handle,    /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_handle_t * p_fsoot_handle,       /* OUT */
                                  fsal_attrib_list_t * p_fsroot_attributes      /* [ IN/OUT ] */
    )
{
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupJunction);
}
