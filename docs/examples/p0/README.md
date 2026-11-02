# Syntetyczny przykład kontraktu P0

Te dwa pliki wygenerował test integracyjny produkcyjnego loggera i FatFs.
Nie są zapisem z płytki. MON-VER zawiera celowe znaki cudzysłowu/backslash,
a dump rejestrów jest zerowy, ponieważ test SD nie uruchamia fizycznego IMU.
`imu_configuration_verified: false` jest tutaj oczekiwane. SHA identyfikuje
bazę testowanego kodu, a firmware_dirty sygnalizuje zmiany w tej roboczej
wersji. Dane pamięci/PPS pochodzą ze stubów hosta i wynoszą 0; nie opisują
rzeczywistego stanu Nucleo. Aktualny format ma 36 kolumn i format_version=3.
Przy nieważnej pozycji znane GPS_FIX_ID, GPS_ITOW_MS i GPS_T_RX_US pozostają
wartościami diagnostycznymi. TIME_SYNC_STATE=0, czas pomiaru GPS jest pusty.

Sześć wierszy obejmuje: brak GPS, nową epokę, jej niezmienione powtórzenie,
nową epokę o identycznych współrzędnych, epokę 2D bez prędkości/wysokości
oraz błąd IMU z sześcioma pustymi polami i nieważnym GPS. Celowa luka NR
oraz skok do 7 200 000 001 µs sprawdzają integralność liczb 64-bitowych.
Nie interpretuj tego pliku jako ciągłego testu jazdy ani pomiaru dokładności.

Wygenerowanie i niezależna walidacja:

```sh
P0_ARTIFACT_DIR=/tmp/laptune-p0-example bash tests/run.sh
python3 tests/validate_p0_artifact.py /tmp/laptune-p0-example
```

Aplikacji desktopowej wskaż ten folder, a nie cały katalog `docs`.
