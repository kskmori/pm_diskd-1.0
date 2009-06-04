'''CTS: Cluster Testing System: AIS dependent modules...
'''

__copyright__='''
Copyright (C) 2007 Andrew Beekhof <andrew@suse.de>

'''

#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

import os,sys,CTS,CTSaudits,CTStests, warnings
from CTSvars import *
from CTS import *
from CM_lha import crm_lha
from CTSaudits import ClusterAudit
from CTStests import *
from CIB import *
try:
    from xml.dom.minidom import *
except ImportError:
    sys.__stdout__.write("Python module xml.dom.minidom not found\n")
    sys.__stdout__.write("Please install python-xml or similar before continuing\n")
    sys.__stdout__.flush()
    sys.exit(1)

#######################################################################
#
#  LinuxHA v2 dependent modules
#
#######################################################################


class crm_ais(crm_lha):
    '''
    The crm version 3 cluster manager class.
    It implements the things we need to talk to and manipulate
    crm clusters running on top of openais
    '''
    def __init__(self, Environment, randseed=None):
        crm_lha.__init__(self, Environment, randseed=randseed)

        self.update({
            "Name"           : "crm-ais",
            "StartCmd"       : CTSvars.INITDIR+"/openais start > /dev/null 2>&1",
            "StopCmd"        : CTSvars.INITDIR+"/openais stop > /dev/null 2>&1",

            "UUIDQueryCmd"   : "crmadmin -N",
            "EpocheCmd"      : "crm_node -e",
            "QuorumCmd"      : "crm_node -q",
            "ParitionCmd"    : "crm_node -p",

            "Pat:We_stopped"   : "%s.*openais.*pcmk_shutdown: Shutdown complete",
            "Pat:They_stopped" : "%s crmd:.*Node %s: .* state=lost .new",
            "Pat:All_stopped"  : "%s.*openais.*pcmk_shutdown: Shutdown complete",
            "Pat:They_dead"    : "openais:.*Node %s is now: lost",
            
            "Pat:ChildKilled"  : "%s openais.*Child process %s terminated with signal 9",
            "Pat:ChildRespawn" : "%s openais.*Respawning failed child process: %s",
            "Pat:ChildExit"    : "Child process .* exited",

            # Bad news Regexes.  Should never occur.
            "BadRegexes"   : (
                r"ERROR:",
                r"CRIT:",
                r"Shutting down\.",
                r"Forcing shutdown\.",
                r"Timer I_TERMINATE just popped",
                r"input=I_ERROR",
                r"input=I_FAIL",
                r"input=I_INTEGRATED cause=C_TIMER_POPPED",
                r"input=I_FINALIZED cause=C_TIMER_POPPED",
                r"input=I_ERROR",
                r", exiting\.",
                r"WARN.*Ignoring HA message.*vote.*not in our membership list",
                r"pengine.*Attempting recovery of resource",
                r"is taking more than 2x its timeout",
                r"Confirm not received from",
                r"Welcome reply not received from",
                r"Attempting to schedule .* after a stop",
                r"Resource .* was active at shutdown",
                r"duplicate entries for call_id",
                r"Search terminated:",
                r"No need to invoke the TE",
                r":global_timer_callback",
                r"Faking parameter digest creation",
                r"Parameters to .* action changed:",
                r"Parameters to .* changed",
                r"Child process .* terminated with signal 11",
                r"Executing .* fencing operation",
            ),
        })

    def errorstoignore(self):
        # At some point implement a more elegant solution that 
        #   also produces a report at the end
        '''Return list of errors which are known and very noisey should be ignored'''
        if 1:
            return [ 
                "crm_mon:",
                "crmadmin:",
                "async_notify: strange, client not found",
                "ERROR: Message hist queue is filling up"
                ]
        return []

    def Components(self):    
        complist = []
        common_ignore = [
                    "Pending action:",
                    "ERROR: crm_log_message_adv:",
                    "ERROR: MSG: No message to dump",
                    "pending LRM operations at shutdown",
                    "Lost connection to the CIB service",
                    "Connection to the CIB terminated...",
                    "Sending message to CIB service FAILED",
                    "apply_xml_diff: Diff application failed!",
                    "crmd: .*Action A_RECOVER .* not supported",
                    "pingd: .*ERROR: send_update: Could not send update",
                    "send_ipc_message: IPC Channel to .* is not connected",
                    "unconfirmed_actions: Waiting on .* unconfirmed actions",
                    "cib_native_msgready: Message pending on command channel",
                    "crmd:.*do_exit: Performing A_EXIT_1 - forcefully exiting the CRMd",
                    "verify_stopped: Resource .* was active at shutdown.  You may ignore this error if it is unmanaged.",
                    "ERROR: stonithd_op_result_ready: not signed on",
                    "ERROR: attrd_connection_destroy: Lost connection to attrd",
                    "nfo: te_fence_node: Executing .* fencing operation",
            ]

        complist.append(Process("cib", 0, [
                    "State transition S_IDLE",
                    "Respawning .* crmd",
                    "Respawning .* attrd",
                    "Lost connection to the CIB service",
                    "Connection to the CIB terminated...",
                    "Child process crmd exited .* rc=2",
                    "Child process attrd exited .* rc=1",
                    "crmd: .*Input I_TERMINATE from do_recover",
                    "crmd: .*I_ERROR.*crmd_cib_connection_destroy",
                    "crmd:.*do_exit: Could not recover from internal error",
                    ], [], common_ignore, 0, self))

        complist.append(Process("lrmd", 0, [
                    "State transition S_IDLE",
                    "LRM Connection failed",
                    "Respawning .* crmd",
                    "crmd: .*I_ERROR.*lrm_connection_destroy",
                    "Child process crmd exited .* rc=2",
                    "crmd: .*Input I_TERMINATE from do_recover",
                    "crmd:.*do_exit: Could not recover from internal error",
                    ], [], common_ignore, 0, self))
        complist.append(Process("crmd", 0, [
#                    "WARN: determine_online_status: Node .* is unclean",
#                    "Scheduling Node .* for STONITH",
#                    "Executing .* fencing operation",
# Only if the node wasn't the DC:  "State transition S_IDLE",
                    "State transition .* -> S_IDLE",
                    ], [], common_ignore, 0, self))

        complist.append(Process("attrd", 0, [
                    "crmd: .*ERROR: attrd_connection_destroy: Lost connection to attrd"
                    ], [], common_ignore, 0, self))

        aisexec_ignore = [
                    "ERROR: ais_dispatch: Receiving message .* failed",
                    "crmd: .*I_ERROR.*crmd_cib_connection_destroy",
                    "cib: .*ERROR: cib_ais_destroy: AIS connection terminated",
                    #"crmd: .*ERROR: crm_ais_destroy: AIS connection terminated",
                    "crmd:.*do_exit: Could not recover from internal error",
                    "crmd: .*I_TERMINATE.*do_recover",
                    "attrd: .*CRIT: attrd_ais_destroy: Lost connection to OpenAIS service!",
                    "stonithd: .*ERROR: AIS connection terminated",
            ]
        aisexec_ignore.extend(common_ignore)

        complist.append(Process("aisexec", 0, [
                    "ERROR: ais_dispatch: AIS connection failed",
                    "crmd: .*ERROR: do_exit: Could not recover from internal error",
                    "pengine: .*Scheduling Node .* for STONITH",
                    "stonithd: .*requests a STONITH operation RESET on node",
                    "stonithd: .*Succeeded to STONITH the node",
                    ], [], aisexec_ignore, 0, self))

        complist.append(Process("pengine", 0, [
                    ], [
                    "State transition S_IDLE",
                    "Respawning .* crmd",
                    "Child process crmd exited .* rc=2",
                    "crmd: .*pe_connection_destroy: Connection to the Policy Engine failed",
                    "crmd: .*I_ERROR.*save_cib_contents",
                    "crmd: .*Input I_TERMINATE from do_recover",
                    "crmd:.*do_exit: Could not recover from internal error",
                    ], common_ignore, 0, self))

        if self.Env["DoFencing"] == 1 :
            stonith_ignore = [
                "ERROR: stonithd_signon: ",
                "update_failcount: Updating failcount for child_DoFencing",
                "ERROR: te_connect_stonith: Sign-in failed: triggered a retry",
                ]
            
            stonith_ignore.extend(common_ignore)

            complist.append(Process("stonithd", 0, [], [
                        "tengine_stonith_connection_destroy: Fencing daemon connection failed",
                        "Attempting connection to fencing daemon",
                        "te_connect_stonith: Connected",
                        ], stonith_ignore, 0, self))
        return complist
    
    def NodeUUID(self, node):
        return node

#######################################################################
#
#   A little test code...
#
#   Which you are advised to completely ignore...
#
#######################################################################
if __name__ == '__main__': 
    pass
