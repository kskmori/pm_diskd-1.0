#!/usr/bin/python

'''CTS: Cluster Testing System: Tests module

There are a few things we want to do here:

 '''

__copyright__='''
Copyright (C) 2000, 2001 Alan Robertson <alanr@unix.sh>
Licensed under the GNU GPL.
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

import CTS
import time, os, re, types


#	List of all class objects for tests which we ought to
#	consider running.

AllTestClasses = [ ]

class CTSTest:
    '''
    A Cluster test.
    We implement the basic set of properties and behaviors for a generic
    cluster test.

    Cluster tests track their own statistics.
    We keep each of the kinds of counts we track as separate {name,value}
    pairs.
    '''

    def __init__(self, cm):
        #self.name="the unnamed test"
        self.Stats = {"calls":0
        ,	"success":0
        ,	"failure":0
        ,	"skipped":0
        ,	"auditfail":0}

#        if not issubclass(cm.__class__, ClusterManager):
#            raise ValueError("Must be a ClusterManager object")
        self.CM = cm
        self.timeout=120

    def has_key(self, key):
        return self.Stats.has_key(key)

    def __setitem__(self, key, value):
        self.Stats[key] = value
        
    def __getitem__(self, key):
        return self.Stats[key]

    def incr(self, name):
        '''Increment (or initialize) the value associated with the given name'''
        if not self.Stats.has_key(name):
            self.Stats[name]=0
        self.Stats[name] = self.Stats[name]+1

    def failure(self, reason="none"):
        '''Increment the failure count'''
        self.incr("failure")
        self.CM.log("Test " + self.name + " failed [reason:" + reason + "]")
        return None

    def success(self):
        '''Increment the success count'''
        self.incr("success")
        return 1

    def skipped(self):
        '''Increment the skipped count'''
        self.incr("skipped")
        return 1

    def __call__(self):
        '''Perform the given test'''
        raise ValueError("Abstract Class member (__call__)")
        self.incr("calls")
        return self.failure()

    def is_applicable(self):
        '''Return TRUE if we are applicable in the current test configuration'''
        raise ValueError("Abstract Class member (is_applicable)")
        return 1

    def canrunnow(self):
        '''Return TRUE if we can meaningfully run right now'''
        return 1

###################################################################
class StopTest(CTSTest):
###################################################################
    '''Stop (deactivate) the cluster manager on a node'''
    def __init__(self, cm):
        CTSTest.__init__(self, cm)
        self.name="stop"
        self.uspat   = self.CM["Pat:We_stopped"]
        self.thempat = self.CM["Pat:They_stopped"]
        self.allpat = self.CM["Pat:All_stopped"]

    def __call__(self, node):
        '''Perform the 'stop' test. '''
        self.incr("calls")
        if self.CM.ShouldBeStatus[node] != self.CM["up"]:
            return self.skipped()


        if node == self.CM.OurNode:
            self.incr("us")
            pat = self.uspat
        else:
            if self.CM.upcount() <= 1:
                self.incr("all")
                pat = (self.allpat % node)
            else:
                self.incr("them")
                pat = (self.thempat % node)

        watch = CTS.LogWatcher(self.CM["LogFileName"], [pat]
        ,	timeout=self.CM["DeadTime"]+10)
        watch.setwatch()
        self.CM.StopaCM(node)
        if watch.look():
            return self.success()
        else:
            return self.failure("no match against %s "% pat)
#
# We don't register StopTest because it's better when called by
# another test...
#

###################################################################
class StartTest(CTSTest):
###################################################################
    '''Start (activate) the cluster manager on a node'''
    def __init__(self, cm, debug=None):
        CTSTest.__init__(self,cm)
        self.name="start"
        self.uspat   = self.CM["Pat:We_started"]
        self.thempat = self.CM["Pat:They_started"]
        self.debug = debug

    def __call__(self, node):
        '''Perform the 'start' test. '''
        self.incr("calls")

        if self.CM.ShouldBeStatus[node] != self.CM["down"]:
            return self.skipped()

        if node == self.CM.OurNode or self.CM.upcount() < 1:
            self.incr("us")
            pat = (self.uspat % node)
        else:
            self.incr("them")
            pat = (self.thempat % node)

        watch = CTS.LogWatcher(self.CM["LogFileName"], [pat]
        ,	timeout=self.CM["StartTime"]+10, debug=self.debug)
        watch.setwatch()

        self.CM.StartaCM(node)

        if watch.look():
            return self.success()
        else:
            self.CM.log("START FAILURE: did not find pattern " + pat)
            self.CM.log("START TIMEOUT = %d " % self.CM["StartTime"])
            return self.failure("did not find pattern " + pat)

    def is_applicable(self):
        '''StartTest is always applicable'''
        return 1
#
# We don't register StartTest because it's better when called by
# another test...
#

###################################################################
class FlipTest(CTSTest):
###################################################################
    '''If it's running, stop it.  If it's stopped start it.
       Overthrow the status quo...
    '''
    def __init__(self, cm):
        CTSTest.__init__(self,cm)
        self.name="flip"
        self.start = StartTest(cm)
        self.stop = StopTest(cm)

    def __call__(self, node):
        '''Perform the 'flip' test. '''
        self.incr("calls")
        if self.CM.ShouldBeStatus[node] == self.CM["up"]:
            self.incr("stopped")
            ret = self.stop(node)
            type="up->down"
            # Give the cluster time to recognize it's gone...
            time.sleep(self.CM["DeadTime"]+2)
        elif self.CM.ShouldBeStatus[node] == self.CM["down"]:
            self.incr("started")
            ret = self.start(node)
            type="down->up"
        else:
            return self.skipped()

        self.incr(type)
        if ret:
            return self.success()
        else:
            return self.failure("%s failure" % type)

    def is_applicable(self):
        '''FlipTest is always applicable'''
        return 1

#	Register FlipTest as a good test to run
AllTestClasses.append(FlipTest)

###################################################################
class RestartTest(CTSTest):
###################################################################
    '''Stop and restart a node'''
    def __init__(self, cm):
        CTSTest.__init__(self,cm)
        self.name="Restart"
        self.start = StartTest(cm)
        self.stop = StopTest(cm)

    def __call__(self):
        '''Perform the 'restart' test. '''
        self.incr("calls")

        node = self.CM.Env.RandomNode()
        self.incr("node:" + node)

        if self.CM.ShouldBeStatus[node] == self.CM["down"]:
            self.incr("WasStopped")
            self.start(node)

        ret1 = self.stop(node)
        # Give the cluster time to recognize we're gone...
        time.sleep(self.CM["DeadTime"]+2)
        ret2 = self.start(node)

        if not ret1:
            return self.failure("stop failure")
        if not ret2:
            return self.failure("start failure")
        return self.success()

    def is_applicable(self):
        '''RestartTest is always applicable'''
        return 1

#	Register RestartTest as a good test to run
AllTestClasses.append(RestartTest)

###################################################################
class StonithTest(CTSTest):
###################################################################
    '''Reboot a node by whacking it with stonith.'''
    def __init__(self, cm, timeout=600):
        CTSTest.__init__(self,cm)
        self.name="Stonith"
        self.theystopped  = self.CM["Pat:They_stopped"]
        self.allstopped  = self.CM["Pat:All_stopped"]
        self.usstart   = self.CM["Pat:We_started"]
        self.themstart = self.CM["Pat:They_started"]
        self.timeout = timeout

    def __call__(self, node):
        '''Perform the 'stonith' test. (whack the node)'''
        self.incr("calls")
        stopwatch = None


        #	Figure out what log message to look for when/if it goes down

        if self.CM.ShouldBeStatus[node] != self.CM["down"]:
            if self.CM.upcount() != 1:
                stopwatch = (self.theystopped % node)

        #	Figure out what log message to look for when it comes up

        if (self.CM.upcount() <= 1):
            uppat = (self.usstart % node)
        else:
            uppat = (self.themstart % node)

        upwatch = CTS.LogWatcher(self.CM["LogFileName"], [uppat]
        ,	timeout=self.timeout)

        if stopwatch:
            watch = CTS.LogWatcher(self.CM["LogFileName"], [stopwatch]
            ,	timeout=self.CM["DeadTime"]+10)
            watch.setwatch()

        #	Reset (stonith) the node

        StonithWorked=None
        for tries in 1,2,3,4,5:
          if self.CM.Env.ResetNode(node):
            StonithWorked=1
            break
        if not StonithWorked:
            return self.failure("Stonith failure")

        upwatch.setwatch()

        #	Look() and see if the machine went down

        if stopwatch:
            if watch.look():
                ret1=1
            else:
                reason="Did not find " + stopwatch
                ret1=0
        else:
            ret1=1

        #	Look() and see if the machine came back up

        if upwatch.look():
            ret2=1
        else:
            reason="Did not find " + uppat
            ret2=0

        self.CM.ShouldBeStatus[node] = self.CM["up"]

        # I can't remember why I put this in here :-(

        time.sleep(10)

        if ret1 and ret2:
            return self.success()
        else:
            return self.failure(reason)

    def is_applicable(self):
        '''StonithTest is applicable unless suppressed by CM.Env["DoStonith"] == FALSE'''

        if self.CM.Env.has_key("DoStonith"):
            return self.CM.Env["DoStonith"]
        return 1

#	Register StonithTest as a good test to run
AllTestClasses.append(StonithTest)


###################################################################
class IPaddrtest(CTSTest):
###################################################################
    '''Find the machine supporting a particular IP address, and knock it down.

    [Hint:  This code isn't finished yet...]
    '''

    def __init__(self, cm, IPaddrs):
        CTSTest.__init__(self,cm)
        self.name="IPaddrtest"
        self.IPaddrs = IPaddrs

        self.start = StartTest(cm)
        self.stop = StopTest(cm)

    def __call__(self, IPaddr):
        '''
        Perform the IPaddr test...
        '''
        self.incr("calls")

        node = self.CM.Env.RandomNode()
        self.incr("node:" + node)

        if self.CM.ShouldBeStatus[node] == self.CM["down"]:
            self.incr("WasStopped")
            self.start(node)

        ret1 = self.stop(node)
        # Give the cluster time to recognize we're gone...
        time.sleep(self.CM["DeadTime"]+10)
        ret2 = self.start(node)


        if not ret1:
            return self.failure("Could not stop")
        if not ret2:
            return self.failure("Could not start")

        return self.success()

    def is_applicable(self):
        '''IPaddrtest is always applicable (but shouldn't be)'''
        return 1

###################################################################
class SimulStart(CTSTest):
###################################################################
    '''Start all the nodes ~ simultaneously'''
    def __init__(self, cm):
        CTSTest.__init__(self,cm)
        self.name="SimulStart"

    def __call__(self, dummy):
        '''Perform the 'SimulStart' test. '''
        self.incr("calls")

        #	We ignore the "node" parameter...

        #	Shut down all the nodes...
        for node in self.CM.Env["nodes"]:
          if self.CM.ShouldBeStatus[node] != self.CM["down"]:
            self.incr("stops")
            self.stop = StopTest(self.CM)
            self.stop(node)


         
        watchpats = [ ]
        waitingfornodes = { }
        pat = self.CM["Pat:They_started"]
        for node in self.CM.Env["nodes"]:
          thispat = (pat % node)
          watchpats.append(thispat)
          waitingfornodes[node] = node

        #	Start all the nodes - at about the same time...
        watch = CTS.LogWatcher(self.CM["LogFileName"], watchpats
        ,	timeout=self.CM["DeadTime"]+10)
        watch.ReturnOnlyMatch()

        watch.setwatch()
        for node in self.CM.Env["nodes"]:
          self.CM.StartaCM(node)

        #	We need to look matches to our patterns, and then
        #	remove the corresponding node from "waitingfornodes".
        #	We quit when there are no more nodes in "waitingfornodes"
        while len(waitingfornodes) > 0:
          match = watch.look()
          if not match:
            return self.failure("did not find start message")
          if waitingfornodes.has_key(match):
            del waitingfornodes[match]

 	return self.success()

    def is_applicable(self):
        '''SimulStart is always applicable'''
        return 1

#	Register SimulStart as a good test to run
AllTestClasses.append(SimulStart)

###################################################################
class StandbyTest(CTSTest):
###################################################################
    '''Put a node in standby mode'''
    def __init__(self, cm):
        CTSTest.__init__(self,cm)
        self.name="standby"
        self.successpat		= self.CM["Pat:StandbyOK"]
        self.nostandbypat	= self.CM["Pat:StandbyNONE"]
        self.transient	        = self.CM["Pat:StandbyTRANSIENT"]

    def __call__(self, node):
        '''Perform the 'standby' test. '''
        self.incr("calls")

        if self.CM.ShouldBeStatus[node] == self.CM["down"]:
            return self.skipped()

        if self.CM.upcount() < 2:
            self.incr("nostandby")
            pat = self.nostandbypat;
        else:
            self.incr("standby")
            pat = self.successpat;

        #
        # You could make a good argument that the cluster manager
        # ought to give us good clues on when its a bad time to
        # switch over to the other side, but heartbeat doesn't...
        # It could also queue the request.  But, heartbeat
        # doesn't do that either :-)
        #
        retrycount=0
        while (retrycount < 10):
            watch = CTS.LogWatcher(self.CM["LogFileName"]
            ,	[pat, self.transient]
            ,	timeout=self.CM["DeadTime"]+10)
            watch.setwatch()

            self.CM.rsh(node, self.CM["Standby"])

            match = watch.look()
            if match:
                if re.search(self.transient, match):
                    self.incr("retries")
                    time.sleep(2);
                    retrycount=retrycount+1
                else:
                    return self.success()
            else:
                break  # No point in retrying...
        return self.failure("did not find pattern " + pat)

    def is_applicable(self):
        '''StandbyTest is applicable when the CM has a Standby command'''

        if not self.CM.has_key("Standby"):
           return None
        else:

            if self.CM.Env.has_key("DoStandby"):
                flag=self.CM.Env["DoStandby"]
                if type(flag) == types.IntType:
		    return flag
                if not re.match("[yt]", flag, re.I):
                    return None
            #
            # We need to strip off everything after the first blank
            #
            cmd=self.CM["Standby"];
            cmd = cmd.split()[0]
            return os.access(cmd, os.X_OK)

#	Register StandbyTest as a good test to run
AllTestClasses.append(StandbyTest)


def TestList(cm):
    result = []
    for testclass in AllTestClasses:
        bound_test = testclass(cm)
        if bound_test.is_applicable():
        	result.append(bound_test)
    # result = [StandbyTest(cm)]
    return result
