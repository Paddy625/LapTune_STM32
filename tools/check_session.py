#!/usr/bin/env python3
"""Inspect monotonic sample timing and independently captured PPS diagnostics."""
import argparse
import csv
import json
from pathlib import Path
import statistics

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('folder', type=Path)
a = p.parse_args()
metadata = json.loads((a.folder/'SESSION.json').read_text())
last_t = last_nr = first_t = first_nr = None
intervals = []
rows = gaps = 0
for path in sorted(a.folder.glob('LOG*.TXT')):
    with path.open(newline='') as stream:
        for row in csv.DictReader(stream, delimiter=';'):
            t, nr = int(row['T_IMU_US']), int(row['NR'])
            if last_t is not None:
                if t <= last_t or nr <= last_nr:
                    raise SystemExit(f'Non-monotonic sample in {path.name}: NR={nr}, T={t}')
                intervals.append(t-last_t)
                gaps += nr-last_nr-1
            else:
                first_t, first_nr = t, nr
            last_t, last_nr = t, nr
            rows += 1
print(f'Rows={rows}, first NR={first_nr}, last NR={last_nr}, gaps inside recorded range={gaps}')
if intervals:
    print(f'MCU time dt [us]: min={min(intervals)}, median={statistics.median(intervals)}, max={max(intervals)}')
    print(f'Rate from MCU timestamps [Hz]: {(rows-1)*1e6/(last_t-first_t):.6f}; no imposed 104 Hz')
print('Startup [us]:', {key: metadata.get(key) for key in ('sd_ready_us','imu_start_us','first_written_us')})
print('Last write/sync:', metadata.get('last_written_nr'), metadata.get('last_synced_nr'), 'clean=', metadata.get('clean_shutdown'))
print('Drop:', metadata.get('last_drop_nr'), metadata.get('last_drop_reason'))
print('Queues high water:', metadata.get('raw_ring_high_water'), metadata.get('sd_queue_high_water'))
print('Runtime minimum free reserved stack [bytes]:', metadata.get('stack_free_min_bytes'))
periods = metadata.get('pps_periods',0)
mean = metadata.get('pps_mean_period_ticks',0)
if periods and mean:
    print(f'PPS: {periods} accepted periods, mean={mean} ticks, nominal scale offset={mean-1000000:+d} ppm')
    print('Scale estimate requires an independently verified, GNSS-locked 1 Hz PPS; it does not discipline the MCU.')
else:
    print('No usable PPS measurement; timestamp accuracy remains unvalidated.')
print('TIME_SYNC_STATE=0: PPS is not yet mapped to GPS week/iTOW. GPS_T_MEAS_US must be empty.')
