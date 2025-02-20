#!/usr/bin/env python3

import random
import subprocess
import signal

from TestHarness import Cluster, TestHelper, Utils
from os import getpid
from pathlib import Path

###############################################################
# validate-dirty-db
#
# Test for validating the dirty db flag sticks repeated funod restart attempts
#
###############################################################


Print=Utils.Print
errorExit=Utils.errorExit

args = TestHelper.parse_args({"--keep-logs","--dump-error-details","-v","--leave-running","--unshared"})
debug=args.v
pnodes=1
topo="mesh"
delay=1
chainSyncStrategyStr=Utils.SyncResyncTag
total_nodes = pnodes
killCount=1
killSignal=Utils.SigKillTag

dumpErrorDetails=args.dump_error_details

seed=1
Utils.Debug=debug
testSuccessful=False

def runfunodAndGetOutput(myTimeout=3, funodLogPath=f"{Utils.TestLogRoot}"):
    """Startup funod, wait for timeout (before forced shutdown) and collect output. Stdout, stderr and return code are returned in a dictionary."""
    Print("Launching funod process.")
    cmd=f"programs/funod/funod --config-dir etc/eosio/node_bios --data-dir {funodLogPath}/node_bios --verbose-http-errors --http-validate-host=false --resource-monitor-not-shutdown-on-threshold-exceeded"
    Print("cmd: %s" % (cmd))
    proc=subprocess.Popen(cmd.split(), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if debug: Print("funod process launched.")

    output={}
    try:
        if debug: Print("Setting funod process timeout.")
        outs,errs = proc.communicate(timeout=myTimeout)
        if debug: Print("funod process has exited.")
        output["stdout"] = outs.decode("utf-8")
        output["stderr"] = errs.decode("utf-8")
        output["returncode"] = proc.returncode
    except (subprocess.TimeoutExpired) as _:
        Print("ERROR: funod is running beyond the defined wait time. Hard killing funod instance.")
        proc.send_signal(signal.SIGKILL)
        return (False, None)

    if debug: Print("Returning success.")
    return (True, output)

random.seed(seed) # Use a fixed seed for repeatability.
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setChainStrategy(chainSyncStrategyStr)

    Print ("producing nodes: %d, topology: %s, delay between nodes launch(seconds): %d, chain sync strategy: %s" % (
        pnodes, topo, delay, chainSyncStrategyStr))

    Print("Stand up cluster")
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, topo=topo, delay=delay, dontBootstrap=True) is False:
        errorExit("Failed to stand up eos cluster.")

    node=cluster.getNode(0)

    Print("Kill cluster nodes.")
    for node in cluster.nodes:
        node.kill(signal.SIGKILL)
    cluster.biosNode.kill(signal.SIGKILL)

    Print("Restart funod repeatedly to ensure dirty database flag sticks.")
    timeout=6

    for i in range(1,4):
        Print("Attempt %d." % (i))
        ret = runfunodAndGetOutput(timeout, cluster.funodLogPath)
        assert(ret)
        assert(isinstance(ret, tuple))
        if not ret[0]:
            errorExit("Failed to startup funod successfully on try number %d" % (i))
        assert(ret[1])
        assert(isinstance(ret[1], dict))
        # pylint: disable=unsubscriptable-object
        stderr= ret[1]["stderr"]
        retCode=ret[1]["returncode"]
        expectedRetCode=2
        if retCode != expectedRetCode:
            errorExit("Expected return code to be %d, but instead received %d. output={\n%s\n}" % (expectedRetCode, retCode, ret))
        db_dirty_msg="atabase dirty flag set"
        if db_dirty_msg not in stderr:
            errorExit("stderr should have contained \"%s\" but it did not. stderr=\n%s" % (db_dirty_msg, stderr))

    if debug: Print("Setting test result to success.")
    testSuccessful=True
finally:
    if debug: Print("Cleanup in finally block.")
    TestHelper.shutdown(cluster, None, testSuccessful, dumpErrorDetails)

if debug: Print("Exiting test, exit value 0.")

exitCode = 0 if testSuccessful else 1
exit(exitCode)
