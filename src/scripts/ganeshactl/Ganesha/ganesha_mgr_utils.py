#!/usr/bin/python
#
# ganesha_mgr.py - commandline tool utils for managing nfs-ganesha.
#
# Copyright (C) 2014 IBM.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Author: Allison Henderson <achender@vnet.linux.ibm.com>
#-*- coding: utf-8 -*-

import gobject
import sys
import time
import traceback
import dbus.mainloop.glib
import dbus
from dbus import glib
from collections import namedtuple

glib.init_threads()

Client = namedtuple('Client',
                    ['ClientIP',
                     'HasNFSv3',
                     'HasMNT',
                     'HasNLM4',
                     'HasRQUOTA',
                     'HasNFSv40',
                     'HasNFSv41',
                     'HasNFSv42',
                     'Has9P',
                     'LastTime'])

class ClientMgr():
    
    def __init__(self, service, path, interface, show_status, show_clients, parent=None):
        self.dbus_service_name = service
        self.dbus_path = path
        self.dbus_interface = interface

        self.bus = dbus.SystemBus()
        self.show_status = show_status
        self.show_clients = show_clients
        try:
            self.dbusobj = self.bus.get_object(self.dbus_service_name,
                                self.dbus_path)
        except:
            print "Error: Can't talk to ganesha service on d-bus." \
                  " Looks like Ganesha is down"
            sys.exit()

    # catch the reply and forward it to the UI
    def clientmgr_reply(self, status, msg):
        self.show_status.emit(status, msg)

    def clientmgr_error(self, dbus_exception):
        self.show_status.emit(False, "DBUS error:" + str(dbus_exception))

    def clientshow_reply(self, time, client_array):
        ts = (time[0],
              time[1])
        interval_nsecs = ts[0] * 1000000000L + ts[1]
        clients = []
        for client in client_array:
            cl = client
            lasttime = cl[9]
            clt = Client(ClientIP = str(cl[0]),
                         HasNFSv3 = cl[1],
                         HasMNT = cl[2],
                         HasNLM4 = cl[3],
                         HasRQUOTA = cl[4],
                         HasNFSv40 = cl[5],
                         HasNFSv41 = cl[6],
                         HasNFSv42 = cl[7],
                         Has9P = cl[8],
                         LastTime = (lasttime[0],
                                     lasttime[1]))
            clients.append(clt)
        self.show_clients.emit(ts, clients)

    def clientshow_error(self, dbus_exception):
        self.show_status.emit(False, "DBUS error:" + str(dbus_exception))


    def AddClient(self, ipaddr):
        add_client_method = self.dbusobj.get_dbus_method("AddClient",
                                                         self.dbus_interface)
        add_client_method(ipaddr,
                          reply_handler=self.clientmgr_reply,
                          error_handler=self.clientmgr_error)

    def RemoveClient(self, ipaddr):
        remove_client_method = self.dbusobj.get_dbus_method("RemoveClient",
                                                            self.dbus_interface)
        remove_client_method(ipaddr,
                             reply_handler=self.clientmgr_reply,
                             error_handler=self.clientmgr_error)

    def ShowClients(self):
        show_client_method = self.dbusobj.get_dbus_method("ShowClients",
                                                          self.dbus_interface)
        show_client_method(reply_handler=self.clientshow_reply,
                           error_handler=self.clientshow_error)


Export = namedtuple('Export',
                    ['ExportID',
                     'ExportPath',
                     'HasNFSv3',
                     'HasMNT',
                     'HasNLM4',
                     'HasRQUOTA',
                     'HasNFSv40',
                     'HasNFSv41',
                     'HasNFSv42',
                     'Has9P',
                     'LastTime'])

class ExportMgr():
    '''
    org.ganesha.nfsd.exportmgr
    '''
    def __init__(self, service, path, interface,
                 show_status, show_exports, display_export, parent=None):
        self.dbus_service_name = service
        self.dbus_path = path
        self.dbus_interface = interface
        self.display_export = display_export
        self.show_exports = show_exports
        self.show_status = show_status
        self.bus = dbus.SystemBus()
        try:
            self.dbusobj = self.bus.get_object(self.dbus_service_name,
                                self.dbus_path)
        except:
            print "Error: Can't talk to ganesha service on d-bus." \
                  " Looks like Ganesha is down"
            sys.exit()

    def export_error(self, dbus_exception):
       self.show_status.emit(False, "Error:" + str(dbus_exception))
    def exportadd_reply(self, message):
       self.show_status.emit(True, "Done: " + message)
    def exportrm_reply(self):
       self.show_status.emit(True, "Done")
    def exportdisplay_reply(self, id, fullpath, pseudopath, tag):
        self.display_export.emit(id, fullpath, pseudopath, tag)
    def exportshow_reply(self, time, export_array):
        ts = (time[0],
              time[1])
        interval_nsecs = ts[0] * 1000000000L + ts[1]
        exports = []
        for export in export_array:
            ex = export
            lasttime = ex[10]
            exp = Export(ExportID = ex[0],
                         ExportPath = str(ex[1]),
                         HasNFSv3 = ex[2],
                         HasMNT = ex[3],
                         HasNLM4 = ex[4],
                         HasRQUOTA = ex[5],
                         HasNFSv40 = ex[6],
                         HasNFSv41 = ex[7],
                         HasNFSv42 = ex[8],
                         Has9P = ex[9],
                         LastTime = (lasttime[0],
                                     lasttime[1]))
            exports.append(exp)
        self.show_exports.emit(ts, exports)


    def AddExport(self, conf_path, exp_expr):
        add_export_method = self.dbusobj.get_dbus_method("AddExport",
                                                         self.dbus_interface)
        add_export_method(conf_path, exp_expr,
                          reply_handler=self.exportadd_reply,
                          error_handler=self.export_error)

    def RemoveExport(self, exp_id):
        rm_export_method = self.dbusobj.get_dbus_method("RemoveExport",
                                                        self.dbus_interface)
        rm_export_method(int(exp_id),
                         reply_handler=self.exportrm_reply,
                         error_handler=self.export_error)

    def DisplayExport(self, exp_id):
        display_export_method = self.dbusobj.get_dbus_method("DisplayExport",
                                                             self.dbus_interface)
        display_export_method(int(exp_id),
                              reply_handler=self.exportdisplay_reply,
                              error_handler=self.export_error)
    def ShowExports(self):
        show_export_method = self.dbusobj.get_dbus_method("ShowExports",
                                                          self.dbus_interface)
        show_export_method(reply_handler=self.exportshow_reply,
                           error_handler=self.export_error)

class AdminInterface():
    '''
    org.ganesha.nfsd.admin interface
    '''
    def __init__(self, service, path, interface, show_status, parent=None):
        self.dbus_service_name = service
        self.dbus_path = path
        self.dbus_interface = interface

        self.bus = dbus.SystemBus()
        self.show_status = show_status
        try:
            self.dbusobj = self.bus.get_object(self.dbus_service_name,
                                               self.dbus_path)
        except:
            print "Error: Can't talk to ganesha service on d-bus." \
                  " Looks like Ganesha is down"
            sys.exit()

    def admin_error(self, dbus_exception):
       self.show_status.emit(False, "Error:" + str(dbus_exception))

    def admin_reply(self, status, msg):
        self.show_status.emit(status, msg)


    def grace(self, ipaddr):
        grace_method = self.dbusobj.get_dbus_method("grace",
                                                    self.dbus_interface)
        grace_method(ipaddr,
                          reply_handler=self.admin_reply,
                          error_handler=self.admin_error)

    def shutdown(self):
        shutdown_method = self.dbusobj.get_dbus_method("shutdown",
                                                       self.dbus_interface)
        shutdown_method(reply_handler=self.admin_reply,
                        error_handler=self.admin_error)



LOGGER_PROPS = 'org.ganesha.nfsd.log.component'

class LogManager():
    '''
    org.ganesha.nfsd.log.component
    '''
        
    def __init__(self, service, path, interface, show_status,
                 show_components, show_level, parent=None):
        self.dbus_service_name = service
        self.dbus_path = path
        self.dbus_interface = interface

        self.bus = dbus.SystemBus()
        self.show_status = show_status
        self.show_components = show_components
        self.show_level = show_level
        try:
            self.dbusobj = self.bus.get_object(self.dbus_service_name,
                                               self.dbus_path)
        except:
            print "Error: Can't talk to ganesha service on d-bus. " \
                  "Looks like Ganesha is down"
            sys.exit()

    def logmgr_error(self, dbus_exception):
        self.show_status.emit(False, "DBUS error:" + str(dbus_exception))

    def logmgr_set_reply(self):
       self.show_status.emit(True, "Done")

    def logmgr_getall_reply(self, dictionary):
       prop_dict = {}
       for key in dictionary.keys():
                prop_dict[key] = dictionary[key]
       self.show_components.emit(prop_dict)

    def logmgr_get_reply(self, level):
       self.show_level.emit(level)

    def GetAll(self):
        getall_method = self.dbusobj.get_dbus_method("GetAll",
                                                     self.dbus_interface)
        getall_method(LOGGER_PROPS,
                      reply_handler=self.logmgr_getall_reply,
                      error_handler=self.logmgr_error)

    def Get(self, property):
        get_method = self.dbusobj.get_dbus_method("Get",
                                                  self.dbus_interface)
        get_method(LOGGER_PROPS, property,
                   reply_handler=self.logmgr_get_reply,
                   error_handler=self.logmgr_error)

    def Set(self, property, setval):
        set_method = self.dbusobj.get_dbus_method("Set",
                                                  self.dbus_interface)
        set_method(LOGGER_PROPS, property, setval,
                   reply_handler=self.logmgr_set_reply,
                   error_handler=self.logmgr_error)
