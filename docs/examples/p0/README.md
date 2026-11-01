# Syntetyczny przykład kontraktu P0

Te dwa pliki wygenerował test integracyjny produkcyjnego loggera i FatFs.
Nie są zapisem z płytki. MON-VER zawiera celowe znaki cudzysłowu/backslash,
a dump rejestrów jest zerowy, ponieważ test SD nie uruchamia fizycznego IMU.
`imu_configuration_verified: false` i `firmware_commit: unspecified` są tutaj
oczekiwane. Wersja firmware jest zapisywana, a build skryptem repo osadza SHA.

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
