#!/usr/bin/env python3
"""Protocol tests with a disposable fake runner; no Wine or user prefix."""
import os
import re
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import json

with tempfile.TemporaryDirectory(prefix='wine-broker-test-') as tmp:
    root=Path(tmp)
    for name in ('wine','wineserver','winepath'):
        (root/name).symlink_to(Path(__file__).with_name('wine-dispatch').resolve())
    # /proc/self/exe determines the runner, so copy the executable beside targets.
    import shutil
    shutil.copy2(Path(__file__).with_name('wine-dispatch'), root/'wine-dispatch')
    for name in ('wine','wineserver','winepath'):
        (root/name).unlink(); (root/name).symlink_to('wine-dispatch')
    fake=root/'wine-real'
    fake.write_text('''#!/usr/bin/python3
import os,sys,json,time
if sys.argv[1:]==['sleep']:
 try: time.sleep(30)
 except KeyboardInterrupt: sys.exit(130)
elif sys.argv[1:]==['signal']: os.kill(os.getpid(),15)
elif sys.argv[1:]==['sigstate']:
 st=dict(l.split(':\t') for l in open('/proc/self/status').read().splitlines() if l.startswith(('SigIgn','SigBlk')))
 print(json.dumps({k:int(v,16) for k,v in st.items()}))
else:
 print(json.dumps(dict(args=sys.argv[1:],env=os.getenv('BROKER_TEST_VALUE'),cwd=os.getcwd(),stdin=sys.stdin.read())))
 sys.exit(7)
''')
    fake.chmod(0o755)
    (root/'wineserver-real').symlink_to('wine-real')
    env=dict(os.environ,WINEPREFIX=str(root/'prefix'),BROKER_TEST_VALUE='spaces\nand unicode 測試')
    env.pop('WINE_BROKER_PIDNS',None)
    a=subprocess.run([root/'wine','a b','',"';$(false)",'測試'],input='stdin\n',text=True,capture_output=True,env=env,cwd=root)
    assert a.returncode==7,(a.returncode,a.stderr)
    data=json.loads(a.stdout)
    assert data==dict(args=['a b','',"';$(false)",'測試'],env=env['BROKER_TEST_VALUE'],cwd=str(root),stdin='stdin\n'),data
    # A different runner may not silently reuse this owner's context.
    other=root/'other'; other.mkdir()
    shutil.copy2(root/'wine-dispatch',other/'wine-dispatch')
    (other/'wine').symlink_to('wine-dispatch')
    refused=subprocess.run([other/'wine','unexpected'],env=env,capture_output=True,text=True)
    assert refused.returncode==125 and 'refused' in refused.stderr,refused
    # A dead owner leaves a socket; the next election must recover it.
    blog=root/'prefix/.wine-wayland-broker/broker-c2.log'
    owner=int(re.findall(r'broker up pid=(\d+)',blog.read_text())[-1])
    os.kill(owner,signal.SIGKILL);time.sleep(.2)
    recovered=subprocess.run([root/'wine','recovered'],input='',text=True,capture_output=True,env=env)
    assert recovered.returncode==7,recovered.stderr
    # An owner elected by a launcher that ignores or blocks signals must not hand that state to the programs it runs.
    owner=int(re.findall(r'broker up pid=(\d+)',blog.read_text())[-1])
    os.kill(owner,signal.SIGKILL);time.sleep(.2)
    def odd_signals():
        signal.signal(signal.SIGINT,signal.SIG_IGN);signal.signal(signal.SIGQUIT,signal.SIG_IGN)
        signal.pthread_sigmask(signal.SIG_BLOCK,{signal.SIGTERM,signal.SIGHUP})
    state=subprocess.run([root/'wine','sigstate'],text=True,capture_output=True,env=env,preexec_fn=odd_signals)
    assert state.returncode==0,state.stderr
    st=json.loads(state.stdout)
    touched=sum(1<<(n-1) for n in (signal.SIGINT,signal.SIGQUIT,signal.SIGTERM,signal.SIGHUP))
    assert st['SigIgn']&touched==0 and st['SigBlk']==0,('signal state leaked into the program',st)
    # Concurrent requests must not serialize child execution.
    a=subprocess.Popen([root/'wine','sleep'],env=env)
    b=subprocess.Popen([root/'wine','sleep'],env=env)
    time.sleep(.5); a.send_signal(signal.SIGINT)
    a.wait(timeout=4); assert b.poll() is None
    b.send_signal(signal.SIGTERM);b.wait(timeout=4)
    result=subprocess.run([root/'wine','signal'],env=env)
    assert result.returncode==-signal.SIGTERM,result.returncode
    # Endpoint cleans itself up after all children finish.
    deadline=time.monotonic()+20
    while (root/'prefix/.wine-wayland-broker/socket-c2').exists() and time.monotonic()<deadline: time.sleep(.2)
    assert not (root/'prefix/.wine-wayland-broker/socket-c2').exists()
    print('PASS argv, environment, cwd, stdin, stdout, exit 7, runner mismatch rejection, stale recovery, concurrency, signals, idle cleanup')
