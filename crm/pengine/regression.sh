#!/bin/bash

 # Copyright (C) 2004 Andrew Beekhof <andrew@beekhof.net>
 # 
 # This program is free software; you can redistribute it and/or
 # modify it under the terms of the GNU General Public
 # License as published by the Free Software Foundation; either
 # version 2.1 of the License, or (at your option) any later version.
 # 
 # This software is distributed in the hope that it will be useful,
 # but WITHOUT ANY WARRANTY; without even the implied warranty of
 # MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 # General Public License for more details.
 # 
 # You should have received a copy of the GNU General Public
 # License along with this library; if not, write to the Free Software
 # Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 #

. regression.core.sh
create_mode="true"
echo Generating test outputs for these tests...
echo ""

echo Done.
echo ""
echo Performing the following tests...
create_mode="false"

echo ""
do_test simple1 "Offline     "
do_test simple2 "Start       "
do_test simple3 "Start 2     "
do_test simple4 "Start Failed"
do_test simple6 "Stop Start  "
do_test simple7 "Shutdown    "
#do_test simple8 "Stonith	"
#do_test simple9 "Lower version"
#do_test simple10 "Higher version"
do_test simple11 "Priority (ne)"
do_test simple12 "Priority (eq)"
do_test simple8 "Stickiness"

echo ""
do_test date-1 "Dates"
do_test standby "Standby"
do_test comments "Comments"
do_test params-0 "Params: No change"
do_test params-1 "Params: Changed"
do_test params-2 "Params: Resource definition"
do_test orphan-0 "Orphan ignore"
do_test orphan-1 "Orphan stop"

do_test probe-0 "Probe (anon clone)"
do_test target-0 "Target Role : baseline"
do_test target-1 "Target Role : test"


echo ""
do_test master-0 "Stopped -> Slave"
do_test master-1 "Stopped -> Promote"
do_test master-2 "Stopped -> Promote : notify"
do_test master-3 "Stopped -> Promote : master location"
do_test master-4 "Started -> Promote : master location"
do_test master-5 "Promoted -> Promoted"
do_test master-6 "Promoted -> Promoted (2)"
do_test master-7 "Promoted -> Fenced"
do_test master-8 "Promoted -> Fenced -> Moved"
do_test master-9 "Stopped + Promotable + No quorum"
do_test master-10 "Stopped -> Promotable : notify with monitor"

echo ""
do_test rsc_dep1 "Must not     "
do_test rsc_dep3 "Must         "
do_test rsc_dep5 "Must not 3   "
do_test rsc_dep7 "Must 3       "
do_test rsc_dep10 "Must (but cant)"
do_test rsc_dep2  "Must (running) "
do_test rsc_dep8  "Must (running : alt) "
do_test rsc_dep4  "Must (running + move)"

echo ""
do_test order1 "Order start 1     "
do_test order2 "Order start 2     "
do_test order3 "Order stop	  "
do_test order4 "Order (multiple)  "
do_test order5 "Order (move)  "
do_test order6 "Order (move w/ restart)  "

#echo ""
#do_test agent1 "version: lt (empty)"
#do_test agent2 "version: eq	"
#do_test agent3 "version: gt	"

echo ""
do_test attrs1 "string: eq (and)     "
do_test attrs2 "string: lt / gt (and)"
do_test attrs3 "string: ne (or)      "
do_test attrs4 "string: exists       "
do_test attrs5 "string: not_exists   "
do_test attrs6 "is_dc: true          "
do_test attrs7 "is_dc: false         "

echo ""
do_test mon-rsc-1 "Schedule Monitor - start"
do_test mon-rsc-2 "Schedule Monitor - move "
do_test mon-rsc-3 "Schedule Monitor - pending start     "
do_test mon-rsc-4 "Schedule Monitor - move/pending start"

echo ""
do_test rec-rsc-0 "Resource Recover - no start     "
do_test rec-rsc-1 "Resource Recover - start        "
do_test rec-rsc-2 "Resource Recover - monitor      "
do_test rec-rsc-3 "Resource Recover - stop - ignore"
do_test rec-rsc-4 "Resource Recover - stop - block "
do_test rec-rsc-5 "Resource Recover - stop - fence "
do_test rec-rsc-6 "Resource Recover - multiple - restart"
do_test rec-rsc-7 "Resource Recover - multiple - stop   "
do_test rec-rsc-8 "Resource Recover - multiple - block  "

echo ""
do_test quorum-1 "No quorum - ignore"
do_test quorum-2 "No quorum - freeze"
do_test quorum-3 "No quorum - stop  "
do_test quorum-4 "No quorum - start anyway"
do_test quorum-5 "No quorum - start anyway (group)"
do_test quorum-6 "No quorum - start anyway (clone)"

echo ""
do_test rec-node-1 "Node Recover - Startup   - no fence"
do_test rec-node-2 "Node Recover - Startup   - fence   "
do_test rec-node-3 "Node Recover - HA down   - no fence"
do_test rec-node-4 "Node Recover - HA down   - fence   "
do_test rec-node-5 "Node Recover - CRM down  - no fence"
do_test rec-node-6 "Node Recover - CRM down  - fence   "
do_test rec-node-7 "Node Recover - no quorum - ignore  "
do_test rec-node-8 "Node Recover - no quorum - freeze  "
do_test rec-node-9 "Node Recover - no quorum - stop    "
do_test rec-node-10 "Node Recover - no quorum - stop w/fence"
do_test rec-node-11 "Node Recover - CRM down w/ group - fence   "
do_test rec-node-12 "Node Recover - nothing active - fence   "
do_test rec-node-13 "Node Recover - failed resource + shutdown - fence   "

echo ""
do_test multi1 "Multiple Active (stop/start)"

#echo ""
#do_test complex1 "Complex	"

echo ""
do_test group1 "Group		"
do_test group2 "Group + Native	"
do_test group3 "Group + Group	"
do_test group4 "Group + Native (nothing)"
do_test group5 "Group + Native (move)   "
do_test group6 "Group + Group (move)    "
do_test group7 "Group colocation"
do_test group8 "Group anti-colocation"
do_test group9 "Group recovery"
do_test group10 "Group partial recovery"

echo ""
do_test inc0 "Incarnation start					" 
do_test inc1 "Incarnation start order				" 
do_test inc2 "Incarnation silent restart, stop, move		"
do_test inc3 "Inter-incarnation ordering, silent restart, stop, move"
do_test inc4 "Inter-incarnation ordering, silent restart, stop, move (ordered)"
do_test inc5 "Inter-incarnation ordering, silent restart, stop, move (restart 1)"
do_test inc6 "Inter-incarnation ordering, silent restart, stop, move (restart 2)"
do_test inc7 "Clone colocation"
do_test inc8 "Clone anti-colocation"
do_test inc9 "Non-unique clone"

echo ""

do_test managed-0 "Managed (reference)"
do_test managed-1 "Not managed - down "
do_test managed-2 "Not managed - up   "

echo ""

do_test interleave-0 "Interleave (reference)"
do_test interleave-1 "coloc - not interleaved"
do_test interleave-2 "coloc - interleaved   "
do_test interleave-3 "coloc - interleaved (2)"

echo ""
do_test notify-0 "Notify reference"
do_test notify-1 "Notify simple"
do_test notify-2 "Notify simple, confirm"
do_test notify-3 "Notify move, confirm"
#do_test notify-2 "Notify - 764"

echo ""

do_test 594 "Bugzilla 594"
do_test 662 "Bugzilla 662"
do_test 696 "Bugzilla 696"
do_test 726 "Bugzilla 726"
do_test 735 "Bugzilla 735"
do_test 764 "Bugzilla 764"
do_test 797 "Bugzilla 797"
do_test 829 "Bugzilla 829"
do_test 994 "Bugzilla 994"
do_test unrunnable-1 "Unrunnable"

echo ""
do_test bad1 "Bad node		"
do_test bad2 "Bad rsc		"
do_test bad3 "No rsc class	"
do_test bad4 "Bad data		"
do_test bad5 "Bad data		"
do_test bad6 "Bad lrm_rsc	"

echo ""

test_results
