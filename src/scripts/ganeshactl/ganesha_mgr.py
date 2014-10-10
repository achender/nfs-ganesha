#!/usr/bin/python
#
# ganesha_mgr.py - commandline tool for managing nfs-ganesha.
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

import sys
import time
import gobject
import dbus.mainloop.glib
from dbus import glib
from Ganesha.ganesha_mgr_utils import ClientMgr
from Ganesha.ganesha_mgr_utils import ExportMgr
from Ganesha.ganesha_mgr_utils import AdminInterface
from Ganesha.ganesha_mgr_utils import LogManager

glib.init_threads()


SERVICE = 'org.ganesha.nfsd'
class Signal():
   emit=None
   def connect(self, callback):
      self.emit = callback


class ManageClients():

    show_status = Signal() 
    show_clients = Signal()
    
    def __init__(self, parent=None):
        self.clientmgr = ClientMgr(SERVICE,
                                   '/org/ganesha/nfsd/ClientMgr',
                                   'org.ganesha.nfsd.clientmgr',
                                   self.show_status, self.show_clients)
        self.show_status.connect(self.status_message)
        self.show_clients.connect(self.proc_clients)

    def addclient(self, ipaddr):
        self.clientmgr.AddClient(ipaddr)
        print "Add a client %s" % (ipaddr)

    def removeclient(self, ipaddr):
        self.clientmgr.RemoveClient(ipaddr)
        print "Remove a client %s" % (ipaddr)

    def showclients(self):
        self.clientmgr.ShowClients()
        print "Show clients"

    def proc_clients(self, ts, clients):
        print "Timestamp: ", time.ctime(ts[0]), ts[1], " nsecs"
        if len(clients) == 0:
            print "No clients"
        else:
            print "Clients:"
            print " IP addr,  nfsv3, mnt, nlm4, rquota,nfsv40, nfsv41, nfsv42, 9p, last"
            for client in clients:
                print (" %s,  %s,  %s,  %s,  %s,  %s,  %s,  %s,  %s, %s %d nsecs" %
                       (client.ClientIP,
                        client.HasNFSv3,
                        client.HasMNT,
                        client.HasNLM4,
                        client.HasRQUOTA,
                        client.HasNFSv40,
                        client.HasNFSv41,
                        client.HasNFSv42,
                        client.Has9P,
                        time.ctime(client.LastTime[0]), client.LastTime[1]))
        sys.exit()

    def status_message(self, status, errormsg):
        print "Error: status = %s, %s" % (str(status), errormsg)
        sys.exit()

class ShowExports():

    show_status = Signal()
    show_exports = Signal()
    display_export = Signal()
 
    def __init__(self, parent=None):
        self.exportmgr = ExportMgr(SERVICE,
                                   '/org/ganesha/nfsd/ExportMgr',
                                   'org.ganesha.nfsd.exportmgr',
                                   self.show_status, self.show_exports,
                                   self.display_export)
        self.show_status.connect(self.status_message)
        self.show_exports.connect(self.proc_exports)
        self.display_export.connect(self.proc_export)

    def showexports(self):
        self.exportmgr.ShowExports()
        print "Show exports"

    def addexport(self, conf_path, exp_expr):
        self.exportmgr.AddExport(conf_path, exp_expr)
        print "Add Export in %s" % conf_path

    def removeexport(self, exp_id):
        self.exportmgr.RemoveExport(exp_id)
        print "Remove Export with id %d" % int(exp_id)

    def displayexport(self, exp_id):
        self.exportmgr.DisplayExport(exp_id)
        print "Display export with id %d" % int(exp_id)

    def proc_export(self, id, path, pseudo, tag):
        print "export %d: path = %s, pseudo = %s, tag = %s" % (id, path, pseudo, tag)
        sys.exit()

    def proc_exports(self, ts, exports):
        print "Timestamp: ", time.ctime(ts[0]), ts[1], " nsecs"
        if len(exports) == 0:
            print "No exports"
        else:
            print "Exports:"
            print "  Id, path,    nfsv3, mnt, nlm4, rquota,nfsv40, nfsv41, nfsv42, 9p, last"
            for export in exports:
                print (" %d,  %s,  %s,  %s,  %s,  %s,  %s,  %s,  %s,  %s, %s, %d nsecs" %
                       (export.ExportID,
                        export.ExportPath,
                        export.HasNFSv3,
                        export.HasMNT,
                        export.HasNLM4,
                        export.HasRQUOTA,
                        export.HasNFSv40,
                        export.HasNFSv41,
                        export.HasNFSv42,
                        export.Has9P,
                        time.ctime(export.LastTime[0]), export.LastTime[1]))
        sys.exit()

    def status_message(self, status, errormsg):
        print "Error: status = %s, %s" % (str(status), errormsg)
        sys.exit()
        

class ServerAdmin():

    show_status = Signal()
    
    def __init__(self, parent=None):
        self.admin = AdminInterface(SERVICE,
                                    '/org/ganesha/nfsd/admin',
                                    'org.ganesha.nfsd.admin',
                                    self.show_status)
        self.show_status.connect(self.status_message)

    def shutdown(self):
        self.admin.shutdown()
        print "Shutting down server."

    def reload(self):
        self.admin.reload()
        print "Reload server configuration."

    def grace(self, ipaddr):
        self.admin.grace(ipaddr)
        print "Start grace period."

    def status_message(self, status, errormsg):
        print "Returns: status = %s, %s" % (str(status), errormsg)
        sys.exit()

class ManageLogs():

    show_status = Signal() 
    show_components = Signal()
    show_level = Signal()
    
    def __init__(self, parent=None):
        self.logmgr = LogManager(SERVICE,
                                 '/org/ganesha/nfsd/admin', 
                                 'org.freedesktop.DBus.Properties',
                                 self.show_status, self.show_components,
                                 self.show_level)
        self.show_status.connect(self.status_message)
        self.show_level.connect(self.show_loglevel)
        self.show_components.connect(self.print_components)

    def set(self, property, value):
        self.logmgr.Set(property, value)
        print "Set log %s to %s" % (property, value)

    def get(self, property):
        self.logmgr.Get(property)
        print "Get property %s" % (property)

    def getall(self):
        self.logmgr.GetAll()
        print "Get all"

    def show_loglevel(self, level):
        print "Log level: %s"% (str(level))
        sys.exit()

    def status_message(self, status, errormsg):
        print "Error: status = %s, %s" % (str(status), errormsg)
        sys.exit()

    def print_components(self, properties):
       for prop in properties:
          print str(prop)
       sys.exit()

# Main
if __name__ == '__main__':
    exportmgr = ShowExports()
    clientmgr = ManageClients()
    ganesha = ServerAdmin()
    logmgr = ManageLogs()

    USAGE = \
       "\nganesha_mgr.py command [OPTIONS]\n\n"                                \
       "COMMANDS\n\n"                                                        \
       "   add_client ipaddr: Adds the client with the given IP\n\n"         \
       "   remove_client ipaddr: Removes the client with the given IP\n\n"   \
       "   show_client: Shows the current clients\n\n"                       \
       "   display_export export_id: \n"                                     \
       "      Displays the export with the given ID\n\n"                     \
       "   show_exports: Displays all current exports\n\n"                   \
       "   add_export conf expr:\n"                                          \
       "      Adds an export from the given config file that contains\n"     \
       "      the given expression\n"                                        \
       "      Example: \n"                                                   \
       "      add_export /etc/ganesha/gpfs.conf \"EXPORT(Export_ID=77)\"\n\n"\
       "   remove_export path: Removes the export with the given path\n\n"   \
       "   shutdown: Shuts down the ganesha nfs server\n\n"                  \
       "   grace ipaddr: Begins grace for the given IP\n\n"                  \
       "   get_log component: Gets the log level for the given component\n\n"\
       "   set_log component level: \n"                                      \
       "       Sets the given log level to the given component\n\n"          \
       "   getall_logs: Prints all log components\n\n"

    loop = gobject.MainLoop()
    if sys.argv[1] == "add_client":
        if len(sys.argv) < 3:
           print "add_client requires an IP."\
                 " Try \"ganesha_mgr.py help\" for more info"
           sys.exit()
        clientmgr.addclient(sys.argv[2])
    elif sys.argv[1] == "remove_client":
        if len(sys.argv) < 3:
           print "remove_client requires an IP."\
                 " Try \"ganesha_mgr.py help\" for more info"
           sys.exit()
        clientmgr.removeclient(sys.argv[2])
    elif sys.argv[1] == "show_client":
        clientmgr.showclients()

    elif sys.argv[1] == "add_export":
        if len(sys.argv) < 4:
           print "add_export requires a config file and an expression."\
                 " Try \"ganesha_mgr.py help\" for more info"
           sys.exit()
        exportmgr.addexport(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "remove_export":
        if len(sys.argv) < 3:
           print "remove_export requires an export ID."\
                 " Try \"ganesha_mgr.py help\" for more info"
           sys.exit()
        exportmgr.removeexport(sys.argv[2])
    elif sys.argv[1] == "display_export":
        if len(sys.argv) < 3:
           print "display_export requires an export ID."\
                 " Try \"ganesha_mgr.py help\" for more info"
           sys.exit()
        exportmgr.displayexport(sys.argv[2])
    elif sys.argv[1] == "show_exports":
        exportmgr.showexports()

    elif sys.argv[1] == "shutdown":
        ganesha.shutdown()
    elif sys.argv[1] == "grace":
        if len(sys.argv) < 3:
           print "grace requires an IP."\
                 " Try \"ganesha_mgr.py help\" for more info"
           sys.exit()
        ganesha.grace(sys.argv[2])

    elif sys.argv[1] == "set_log":
        if len(sys.argv) < 4:
           print "set_log requires a component and a log level."\
                 " Try \"ganesha_mgr.py help\" for more info"
           sys.exit()
        logmgr.set(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "get_log":
        if len(sys.argv) < 3:
           print "get_log requires a component."\
                 " Try \"ganesha_mgr.py help\" for more info"
           sys.exit()
        logmgr.get(sys.argv[2])
    elif sys.argv[1] == "getall_logs":
        logmgr.getall()

    elif sys.argv[1] == "help":
       print USAGE
       sys.exit()

    else:
        print "Unknown/missing command."\
              " Try \"ganesha_mgr.py help\" for more info"
        sys.exit()
    loop.run()

