#!/usr/bin/env python3

import os
import shutil
import time
import signal
from TestHarness import Node, TestHelper, Utils

node_id = 1
funod = Node(TestHelper.LOCAL_HOST, TestHelper.DEFAULT_PORT, node_id)
data_dir = Utils.getNodeDataDir(node_id)
config_dir = Utils.getNodeConfigDir(node_id)
if os.path.exists(data_dir):
    shutil.rmtree(data_dir)
os.makedirs(data_dir)
if not os.path.exists(config_dir):
    os.makedirs(config_dir)

try:
    start_funod_cmd = f"{Utils.EosServerPath} -e -p eosio --data-dir={data_dir} --config-dir={config_dir} --blocks-log-stride 10" \
                        " --plugin=eosio::http_plugin --plugin=eosio::chain_api_plugin --http-server-address=localhost:8888"

    funod.launchCmd(start_funod_cmd, node_id)
    time.sleep(2)
    funod.waitForBlock(30)
    funod.kill(signal.SIGTERM)

    funod.relaunch(chainArg="--replay-blockchain")

    time.sleep(2)
    assert funod.waitForBlock(31)
finally:
    # clean up
    Node.killAllfunod()
