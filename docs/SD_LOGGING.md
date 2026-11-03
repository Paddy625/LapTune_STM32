# Zapis na microSD — P0

Moduł ze zdjęcia: **3,3 V**, wspólne SPI1 z TFT, osobne CS. Aktualny kontrakt
logu i sesji: [P0_LOGGER.md](P0_LOGGER.md). Nie formatuj karty z firmware.
Obsługiwane FAT12/16/32, 512-bajtowe sektory; exFAT nie jest obsługiwany.

## Połączenia

| Czytnik SD | Nucleo-F103RB |
| --- | --- |
| 3V3 | 3V3 |
| GND | GND |
| CLK | PA5 / D13 / SPI1_SCK |
| MOSI | PA7 / D11 / SPI1_MOSI |
| MISO | PA6 / D12 / SPI1_MISO |
| CS | PB0 / A3 / CN8/4 |

TFT pozostaje na SPI1 (CS PB6), IMU na SPI2 (CS PB12). Sterownik podnosi
CS drugiego urządzenia i dobiera szybkość: inicjalizacja SD 250 kHz,
zapis SD 2 MHz, TFT 8 MHz. Nie podłączaj modułu 3,3 V do sygnałów 5 V.
[Dotychczasowy rysunek SPI/SD](wiring_sd.svg) nie zawiera nowych przewodów
INT1/INT2 — te dodaje schemat w [P0_LOGGER.md](P0_LOGGER.md).

## Pliki i obsługa

Każde uruchomienie ma własny folder `S<UID>-<BOOT_COUNTER>` z `SESSION.json`
i plikami `LOG0001.TXT`, `LOG0002.TXT` itd. Numery rosną także po ponownym
włożeniu karty. Plik obraca się co 180 s, synchronizacja jest wykonywana
co około 1 s. B1 zatrzymuje zapis i zamyka plik; ponowne naciśnięcie wznawia
zapis w nowym pliku tej samej sesji. Zmiana mocowania wymaga nowej sesji/resetu.
Po błędzie montowanie jest ponawiane co 2 s. Nie odtwarzamy cache starego
systemu plików na nowo włożonej karcie.

DRDY i SPI2 DMA działają także podczas SD/TFT. Pierścień 255 rekordów
przechowuje około 2,45 s IMU przy 104 Hz. Kolejka zapisu ma 16 rekordów; jej zapełnienie zatrzymuje pobieranie z pierścienia.
Na mapowanie GPS próbki czekają 700 ms w tym samym pierścieniu; [opis PPS i budżet opóźnień](GPS_PPS.md). Przed pierwszym uzbrojeniem IMU wymagany jest przygotowany plik SD. Brak fixa
lub UTC nie zatrzymuje zapisu. Szczegóły diagnostyki startu i pamięci:
[P0_REVIEW_2026-11-02.md](P0_REVIEW_2026-11-02.md).
Po przepełnieniu nie wstawiamy sztucznych próbek: luki NR oraz liczniki
`IMU_DROPPED`, `IMU_MISSED_DRDY`, `SD_QUEUE_DROPPED` ujawniają utratę danych.
Błędy osi są oddzielone od błędów karty; sześć pól wektorów jest wtedy pustych.

`sd_log_written` oznacza rekordy przyjęte przez `f_write`, nie gwarancję
trwałości flash. Rotacja i synchronizacja ograniczają skutki odłączenia
zasilania; nagłe wyłączenie może uszkodzić aktualizowane struktury FAT.
Stopka `REC S0007/LOG0001.TXT` pokazuje skrót sesji i nazwę pliku.
B1 opróżnia także próbki czekające na GPS. Przed wyjęciem podczas pracy nadal użyj B1 i zaczekaj na `SD SAVED`.

## Diagnostyka błędów

Przykład `SD E03 CMD0 00:FF` zawiera kod FatFs (`03`), etap/błąd sterownika
(`CMD0`), numer ostatniego polecenia w zapisie dziesiętnym (`00`) i odpowiedź
R1 w zapisie szesnastkowym (`FF`). `FF` oznacza brak poprawnej odpowiedzi R1.
Nie identyfikuje sam w sobie konkretnego uszkodzonego przewodu ani przyczyny
elektrycznej. Gdy komunikacja działa, lecz FatFs nie rozpoznaje woluminu,
pokazywane jest np. `SD E13 FAT16/32`, bez dodatkowych pól SPI. Gdy CMD58
zostało przyjęte, lecz kontrola OCR zawiodła, komunikat ma postać
**`SD E03 OCR 00FF8000`**: osiem cyfr oznacza całe cztery bajty rejestru,
a nie numer polecenia i R1. Tę pełną wartość należy zanotować do diagnostyki.

| Przykład / oznaczenie | Znaczenie |
| --- | --- |
| `SD E03 CMD0 00:FF` | Karta nie odpowiedziała poprawnie na reset / wejście w tryb SPI |
| `SD E03 CMD8 08:00` | Nietypowa odpowiedź na rozpoznanie wersji karty przed odczytem FAT; aktualny kod ponawia rozpoznanie i weryfikuje tryb starszej SD |
| `SD E03 ACMD41 41:01` | Karta pozostała w stanie inicjalizacji do upływu limitu czasu |
| `SD E03 OCR 00FF8000` | Przykład OCR bez ustawionego bitu gotowości 31 po upływie limitu |
| `SD E03 OCR 80000000` | Przykład OCR bez deklaracji obsługi napięcia 3,3 V |
| `SD E03 OCR C0FF8000` | Gdy CMD8 nie potwierdziło SD v2: OCR deklaruje sprzeczne adresowanie blokowe |
| `SD E13 FAT16/32` | SPI zainicjalizowane, ale brak rozpoznanego woluminu FAT16/FAT32; możliwy inny format lub uszkodzone struktury |
| `SD E01 CRC 17:00` | Polecenie odczytu przyjęte, lecz suma CRC danych się nie zgadza |
| `SD E01 TOKEN 17:00` | Brak poprawnego tokenu danych po przyjęciu polecenia odczytu |
| `REJECT`, `BUSY`, `STATUS` | Odrzucenie danych, przekroczony czas gotowości lub zgłoszony błąd karty |
| `HAL`, `CLOCK` | Błąd transmisji HAL SPI lub zmiany częstotliwości magistrali |

Po błędzie najlepiej zanotować **całą treść stopki**. Sam napis „FAT” przy
karcie 256 MB nie jest powodem do ponownego formatowania.


W debuggerze: `sd_log_state`, `sd_log_filename`, `sd_log_session_dir`,
`sd_log_written`, `sd_log_dropped`, `sd_max_pause_us`, `sd_log_error`,
`sd_spi_diag`, `imu_diag`, `gps_diag`. Liczniki są wspólne dla sesji, nie pliku.

## Testy

`bash tests/run.sh` uruchamia 16 programów z ASan/UBSan, w tym rzeczywisty
FatFs na obrazie FAT w RAM, format 36 kolumn, JSON/LFN, rotację, B1,
wyjmowanie i zmianę karty, błędy, pełny wolumin oraz 30 minut czasu
symulowanego. Testy sprzętowe i ich ograniczenia: [P0_LOGGER.md](P0_LOGGER.md).
