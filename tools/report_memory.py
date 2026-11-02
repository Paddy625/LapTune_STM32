#!/usr/bin/env python3
"""Report linked RAM and individual GCC stack frames, without claiming a runtime bound."""
import json
from pathlib import Path
import subprocess
import sys

def report(prefix, elf):
    elf = Path(elf)
    symbols = {}
    for line in subprocess.check_output([prefix + 'nm', '-n', str(elf)], text=True).splitlines():
        parts = line.split()
        if len(parts) == 3:
            try:
                symbols[parts[2]] = int(parts[0], 16)
            except ValueError:
                pass
    static = symbols['_ebss'] - symbols['_sdata']
    free = symbols['_estack'] - symbols['_ebss']
    stack, heap = symbols['_Min_Stack_Size'], symbols['_Min_Heap_Size']
    assert free >= stack + heap
    frames = []
    for path in elf.parent.glob('*.su'):
        for line in path.read_text().splitlines():
            fields = line.split('\t')
            if len(fields) == 3:
                frames.append({'function': fields[0].rsplit(':', 1)[-1], 'bytes': int(fields[1]), 'kind': fields[2]})
    result = dict(static_ram_bytes=static, ram_bytes=20480, free_for_heap_and_stack_bytes=free,
                  reserved_stack_bytes=stack, minimum_heap_bytes=heap,
                  extra_after_linker_reservations_bytes=free-stack-heap,
                  largest_individual_frames=sorted(frames, key=lambda f: f['bytes'], reverse=True)[:8],
                  runtime_stack_peak_measured=False)
    (elf.parent/'memory-report.json').write_text(json.dumps(result, indent=2)+'\n')
    print(f'RAM: static={static}, free={free}, reserved stack={stack}, min heap={heap}, extra={free-stack-heap}')
    return result

if __name__ == '__main__':
    report(sys.argv[1], sys.argv[2])
