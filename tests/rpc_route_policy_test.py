# SPDX-License-Identifier: GPL-2.0-only
import os,subprocess,sys,tempfile
# The injected instant is 2000-01-01 UTC. Verify local-civil conversion on both
# sides of UTC, including the westward year rollover, in fresh CRT processes.
for zone,epoch in [('UTC0',946684800),('JST-9',946717200),('PST8',946656000)]:
    with tempfile.TemporaryDirectory(prefix='rpc-route-policy-') as root:
        subprocess.run([sys.argv[1],root,str(epoch)],check=True,env={**os.environ,'TZ':zone})
