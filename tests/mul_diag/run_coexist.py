#!/usr/bin/env python3
"""Interleave real MUL RX task with existing robot regressions, without editing them.
Mocks prove cooperative state/queue behavior, not physical IRQ/UART latency.
"""
from pathlib import Path
import re
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'tests/esp_link/test.c').read_text()
for name in ['epuck_link','usb_console','esp_link']:
    source=source.replace('../../../Core/Src/'+name+'.c',str(root/'Core/Src'/ (name+'.c')))
hook='static void tasks(void) { esp_link_task(); epuck_link_task(); }'
assert hook in source
source=source.replace(hook,'static void tasks(void) { mul_diag_process(); esp_link_task(); epuck_link_task(); }')
source='void mul_diag_process(void);\nint mul_diag_init(void);\nvoid coexist_check(void);\n'+source
source=source.replace(' epuck_link_init(); esp_link_init();',' epuck_link_init(); esp_link_init(); assert(mul_diag_init()==0);')
source=source.replace(' puts("PASS");','''
 for (unsigned i=0;i<64;i++) { tick+=1000; tasks(); }
 coexist_check();
 assert(console_message_count<=CONSOLE_MESSAGES && console_dropped_messages>0);
 puts("PASS with interleaved MUL RX, recoverable PHY errors and bounded USB overflow");''')
cases=re.findall(r'(?:if|else if)\(strcmp\(argv\[1\],"([^"]+)"\)==0\)',source)
assert cases
with tempfile.TemporaryDirectory(prefix='mul-coexist-') as work:
    work=Path(work);(work/'test.c').write_text(source)
    command=['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-pie','-no-pie','-g','-DMUL_DIAG_ROLE=2']
    command+=['-I'+str(root/p) for p in ['tests/esp_link/stubs','Core/Inc','tests/phy_interop/stubs','ThirdParty/mul_core','ThirdParty/qorvo_dw3/driver','ThirdParty/qorvo_dw3/driver/dw3000']]
    command +=[str(work/'test.c'),str(root/'tests/mul_diag/coexist_radio.c'),str(root/'Core/Src/uwb_diag_profile.c'),str(root/'ThirdParty/mul_core/mul_core.c'),str(root/'ThirdParty/mul_core/mul_common.c'),'-o',str(work/'test')]
    subprocess.run(command,check=True)
    for case in cases:
        print(case,flush=True);subprocess.run([str(work/'test'),case],check=True)
