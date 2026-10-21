#!/usr/bin/env python3
"""Check actual linker symbols and every file-backed load segment."""
import subprocess
import sys
from pathlib import Path
elf=Path(sys.argv[1])
symbols={line.split()[-1]:int(line.split()[0],16) for line in subprocess.check_output(['arm-none-eabi-nm',str(elf)],text=True).splitlines() if len(line.split())==3}
assert symbols['__uwb_config_start__']==0x080c0000
assert symbols['__uwb_config_end__']==0x08100000
loads=0
for line in subprocess.check_output(['arm-none-eabi-readelf','-lW',str(elf)],text=True).splitlines():
    words=line.split()
    if words and words[0]=='LOAD':
        physical,size=int(words[3],16),int(words[4],16)
        if size:
            assert 0x08000000<=physical<0x080c0000 and physical+size<=0x080c0000, line
            loads+=1
assert loads
print(f'{elf.name}: {loads} load segments stay below configuration sectors; symbols verified')
