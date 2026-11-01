#!/usr/bin/env python3
"""Reproducible Cortex-M3 build without CubeIDE; uses arm-none-eabi GCC."""
import argparse
import concurrent.futures
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('mode', choices=('Debug', 'Release'), default='Debug', nargs='?')
parser.add_argument('--out', type=Path, default=Path('build'))
args = parser.parse_args()
repo = Path(__file__).resolve().parent.parent
root = repo / 'LapTune'
out = args.out.resolve() / args.mode
out.mkdir(parents=True, exist_ok=True)
prefix = os.environ.get('ARM_GCC_PREFIX', 'arm-none-eabi-')
commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip()
if subprocess.check_output(['git', 'status', '--porcelain'], cwd=repo, text=True).strip():
    commit += '-dirty'
flags = ['-mcpu=cortex-m3', '-mthumb', '-mfloat-abi=soft', '-ffunction-sections',
         '-fdata-sections', '-fstack-usage', '-Wall', '-DUSE_HAL_DRIVER', '-DSTM32F103xB',
         f'-DFIRMWARE_COMMIT="{commit}"']
flags += ['-g3', '-O0', '-DDEBUG'] if args.mode == 'Debug' else ['-g0', '-Os']
for inc in ('Core/Inc', 'Drivers/STM32F1xx_HAL_Driver/Inc',
            'Drivers/STM32F1xx_HAL_Driver/Inc/Legacy',
            'Drivers/CMSIS/Device/ST/STM32F1xx/Include', 'Drivers/CMSIS/Include'):
    flags.append('-I' + str(root / inc))
sources = sorted((root / 'Core').rglob('*.c')) + sorted((root / 'Drivers').rglob('*.c'))
sources += sorted((root / 'Core').rglob('*.s'))

def compile_source(src):
    obj = out / (str(src.relative_to(root)).replace('/', '_') + '.o')
    language = ['-std=gnu11'] if src.suffix == '.c' else ['-x', 'assembler-with-cpp']
    subprocess.run([prefix + 'gcc', *flags, *language, '-c', str(src), '-o', str(obj)], check=True)
    return str(obj)

with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    objects = list(pool.map(compile_source, sources))
elf = out / 'LapTune.elf'
subprocess.run([prefix + 'gcc', '-mcpu=cortex-m3', '-mthumb', '-mfloat-abi=soft',
                '--specs=nano.specs', '--specs=nosys.specs', '-u', '_printf_float',
                '-T' + str(root / 'STM32F103RBTX_FLASH.ld'),
                '-Wl,--gc-sections,-Map=' + str(out / 'LapTune.map'), *objects,
                '-Wl,--start-group', '-lc', '-lm', '-Wl,--end-group', '-o', str(elf)], check=True)
for fmt, ext in (('ihex', 'hex'), ('binary', 'bin')):
    subprocess.run([prefix + 'objcopy', '-O', fmt, str(elf), str(out / ('LapTune.' + ext))], check=True)
subprocess.run([prefix + 'size', str(elf)], check=True)
print(f'{args.mode}: {len(sources)} sources; commit={commit}; {out}')
