#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (c) 2026 the wine-wayland-desktop contributors
"""Exercise the actual dedicated-sandbox option of a disposable test bottle named by BROKER_GATE_BOTTLE (never a bottle in daily use):
it temporarily edits that bottle's bottle.yml, launches its registered NsProbe program twice and puts the file back unless something else changed it meanwhile."""
from pathlib import Path
import subprocess,time,os,json,sys
name=os.environ.get('BROKER_GATE_BOTTLE') or sys.exit('set BROKER_GATE_BOTTLE to a disposable test bottle')
root=Path.home()/'.var/app/com.usebottles.bottles/data'
p=root/'bottles/bottles'/name
config=p/'bottle.yml'
original=config.read_bytes()
log=Path(os.environ.get('BROKER_GATE_LOG') or root/'broker-proto/gate-logs/sandbox')
log.mkdir(parents=True,exist_ok=True)
assert b'    sandbox: false' in original
# Profile changes require a completely idle prefix, including the owner.
for proc in Path('/proc').iterdir():
    if not proc.name.isdigit():
        continue
    try:
        env=(proc/'environ').read_bytes().split(b'\0')
        active=any(x.rstrip(b'/')==b'WINEPREFIX='+os.fsencode(p) for x in env)
    except OSError:
        continue
    assert not active, f'Test prefix is still active in PID {proc.name}; wait for owner exit'
# Use the real Bottles option, retaining the user's selected network/sound restrictions.
modified=original.replace(b'    sandbox: false',b'    sandbox: true',1)
try:
    config.write_bytes(modified)
    def launch(tag):
        f=(log/(tag+'.log')).open('w')
        proc=subprocess.Popen(['flatpak','run','--command=bottles-cli','com.usebottles.bottles','run','-p','NsProbe','-b',name,'--'],stdout=f,stderr=subprocess.STDOUT)
        f.close();return proc
    def probes():
        found=[]
        for proc in Path('/proc').iterdir():
            if not proc.name.isdigit():continue
            try:
                if (proc/'comm').read_text().strip()!='nsprobe.exe':continue
                env=(proc/'environ').read_bytes().split(b'\0')
                if not any(x.startswith(b'WINEPREFIX=') and x.rstrip(b'/')==b'WINEPREFIX='+os.fsencode(p) for x in env):continue
                found.append(dict(pid=int(proc.name),ns=os.readlink(proc/'ns/pid')))
            except (OSError,PermissionError):pass
        return found
    a=launch('A'); time.sleep(8); b=launch('B');time.sleep(10)
    running=probes();(log/'running.json').write_text(json.dumps(running,indent=2)+'\n')
    print('sandbox probes',running,flush=True)
    assert len(running)==2 and len({x['ns'] for x in running})==1, 'two probes in one namespace required'
    for x in (a,b):
        assert x.wait(timeout=65)==0, 'Bottles CLI failed'
    deadline=time.monotonic()+65
    endpoint=p/'.wine-wayland-broker/socket-c2'
    while (probes() or endpoint.exists()) and time.monotonic()<deadline:
        time.sleep(.5)
    assert not probes() and not endpoint.exists(), 'probe or broker did not exit'
    print('remaining probes',probes(),flush=True)
    print('PASS actual Bottles sandbox=true repeated CLI launch',flush=True)
finally:
    if config.read_bytes()==modified:
        config.write_bytes(original)
        print('Restored original test bottle configuration',flush=True)
    else:  # Bottles or the owner changed the file during the run: do not discard that change.
        (log/'bottle.yml.original').write_bytes(original)
        print(f'bottle.yml changed during the run; left as is, original saved to {log}/bottle.yml.original',flush=True)
