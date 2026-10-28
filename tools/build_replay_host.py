#!/usr/bin/env python3
"""Build host oracle or actual STM radio/runtime fixture; no device access."""
import argparse
import pathlib
import subprocess
ROOT = pathlib.Path(__file__).resolve().parents[1]
def build(output, fixture=False, sanitize=False):
    core = ROOT / 'ThirdParty/mul_core'
    sources = sorted(str(p) for p in core.glob('*.c') if p.name != 'mul_multi_receiver.c')
    entry = 'tests/runtime_config/test_replay.c' if fixture else 'tools/replay_native.c'
    flags = ['-std=c11', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-misleading-indentation', '-DMUL_PROP_MAX_AGE_MS=6000']
    if sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-pie', '-no-pie']
    if fixture:
        flags += ['-DMUL_TDOA_REPLAY=1', '-DMUL_UNIVERSAL=1', '-DMUL_UWB_ONLY=1', '-DMUL_TWR_ROLE=2']
        sources += ['Core/Src/mul_plan_radio.c', 'Core/Src/uwb_config.c']
    includes = ['tests/phy_interop/stubs', 'Core/Inc', 'ThirdParty/mul_core', 'ThirdParty/qorvo_dw3/driver', 'ThirdParty/qorvo_dw3/driver/dw3000']
    subprocess.run(['cc', *flags, *['-I'+p for p in includes], entry, 'Core/Src/mul_replay.c', *sources, '-lm', '-o', str(output)], cwd=ROOT, check=True)
if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--fixture', action='store_true')
    ap.add_argument('--sanitize', action='store_true')
    ap.add_argument('--output', required=True)
    a = ap.parse_args()
    build(a.output, a.fixture, a.sanitize)
