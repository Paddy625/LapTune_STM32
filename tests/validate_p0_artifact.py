"""Read real test output using an independent CSV/JSON consumer."""
import csv
from decimal import Decimal
import json
from pathlib import Path
import sys

folder = Path(sys.argv[1])
metadata = json.loads((folder / 'SESSION.json').read_text(encoding='ascii'))
with (folder / 'LOG0001.TXT').open(encoding='ascii', newline='') as stream:
    reader = csv.DictReader(stream, delimiter=';')
    rows = list(reader)
    assert len(reader.fieldnames) == 36
    assert reader.fieldnames[:13] == [
        'NR', 'DATA', 'CZAS', 'ACC_X', 'ACC_Y', 'ACC_Z', 'GYRO_X', 'GYRO_Y', 'GYRO_Z',
        'GPS_LAT', 'GPS_LON', 'GPS_SPEED_KMH', 'GPS_ALT_MSL_M']
assert metadata['format_version'] == 4 and metadata['pps_enabled'] is False
assert metadata['time_sync_state'] == 0 and metadata['pair_limit_us'] == 500
assert len(rows) == 6
assert [row['GPS_STATE'] for row in rows] == ['I', 'N', 'R', 'N', 'N', 'I']
assert all(None not in row and None not in row.values() for row in rows)
assert all(int(a['T_IMU_US']) < int(b['T_IMU_US']) for a, b in zip(rows, rows[1:]))
for row in rows:
    for key, value in row.items():
        if value and key not in ('DATA', 'CZAS', 'GPS_STATE'):
            Decimal(value)
    if row['GPS_STATE'] == 'I':
        assert all(not row[key] for key in ('GPS_LAT', 'GPS_LON', 'GPS_SPEED_KMH',
                                            'GPS_ALT_MSL_M'))
    if row['IMU_VALID'] == '0':
        assert all(not row[key] for key in ('ACC_X', 'ACC_Y', 'ACC_Z', 'GYRO_X', 'GYRO_Y', 'GYRO_Z'))
assert rows[1]['GPS_LAT'] == rows[2]['GPS_LAT'] == rows[3]['GPS_LAT']
assert rows[1]['GPS_FIX_ID'] == rows[2]['GPS_FIX_ID'] != rows[3]['GPS_FIX_ID']
assert rows[1]['T_IMU_US'] == '7200000001'
assert rows[1]['ACC_X'] == '0.999424' and rows[1]['GYRO_Y'] == '-70.000000'
assert not rows[4]['GPS_ALT_MSL_M'] and not rows[4]['GPS_SPEED_KMH']
print('PASS independent CSV/JSON consumer: schema, numeric fields, exact values, N/R/I and invalid vectors')

assert metadata['clean_shutdown'] is True
assert metadata['last_written_nr'] == metadata['last_synced_nr'] == 7
assert metadata['sd_written'] == metadata['sd_synced_records'] == 6
assert metadata['last_acquired_nr'] == 7
assert len(metadata['firmware_commit']) == 40
assert rows[-1]['GPS_FIX_ID'] == '3' and rows[-1]['GPS_ITOW_MS'] == '1400'
assert all(row['GPS_T_MEAS_US'] == '' and row['TIME_SYNC_STATE'] == '0' for row in rows)
assert rows[4]['GPS_T_RX_US'] != rows[4]['T_IMU_US']  # Assembly deadline differs from actual RX.
