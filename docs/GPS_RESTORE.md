> **2.1.7:** aktualna wersja tylko odczytuje ustawienia do [GPS_CFG.TXT](GPS_CONFIG_QUERY.md). Nie uruchamia procedury przywracania ani resetu opisanej w historii poniżej.

# Powrót GPS do profilu sprzed 5 Hz — 2.1.6

Normalna obsługa GPS ponownie odbiera GGA/RMC przy 9600 baud, jak w `1fd016f`.
Plik `gps_nmea.c` jest identyczny z tamtym commitem. Odbiór jest przerwaniowy;
nie ma przełączenia na UBX po fixie, konfiguracji 5 Hz, diagnostycznego
odpytywania odbiornika, konfiguracji PPS ani resetów przy NO FIX czy utracie danych.
IMU, DMA, pierścienie próbek, obsługa SD, rotacja i numeracja sesji pozostają.

## Pierwszy start po aktualizacji

Sam kod z `1fd016f` nie wysyłał nic do odbiornika, więc nie cofał ustawień
pozostawionych w module. Dlatego 2.1.6 wykonuje jedną zarezerwowaną próbę:

1. Odczyt CFG-PRT UART1, szukanie odpowiedzi przy 9600 i 38400 baud.
2. CFG-CFG: `clearMask=loadMask=0x061F`, `saveMask=0`, `deviceMask=0x17`.
   Payload: `1F 06 00 00 00 00 00 00 1F 06 00 00 17`.
   Maska obejmuje opisane dla NEO-6 sekcje 0–4, 9 i 10, z zerami w bitach
   zarezerwowanych. 2.1.5 miało tutaj niepoprawne `0xFFFF`.
   Clear usuwa konfigurację z wybranych obsługiwanych nośników
   BBR/Flash/EEPROM/SPI Flash i przywraca domyślne wartości; save=0 nie oznacza
   braku zmian pamięci nieulotnej. Load ładuje wynik do konfiguracji bieżącej.
3. Po 1,5 s ponowne rozpoznanie UART; CFG-RST `FF FF 01 00`:
   zimny start z pełnym kontrolowanym resetem programowym.
4. Po 1,5 s odczyt UART, CFG-RATE i CFG-MSG: oczekiwane 9600 baud, wyjście NMEA,
   1000 ms/navRate=1/timeRef=GPS oraz GGA/GLL/GSA/GSV/RMC/VTG z rate=1.
5. Wyłącznie odbiór. Nie ustawiamy ponownie 38400 ani 5 Hz.

Po resecie lub zmianie UART ACK może zaginąć. NAK, brak odpowiedzi, błąd
nadawania lub niezgodny odczyt kończą procedurę bez ponawiania resetów.
Odczyt potwierdza wymienione parametry, nie każdy rejestr modułu ani fix.
Pozostałe domyślne wartości zależą od firmware/ROM konkretnego odbiornika.

Rezerwacja próby jest zapisywana przed akwizycją w istniejącym dzienniku
startów STM32. Znacznik `0x4753` odróżnia tę operację od 2.1.5 (`0x4752`),
które po przywróceniu ponownie ustawiało 5 Hz. Rezerwacja oznacza próbę,
nie sukces: utrata zasilania po jej zapisaniu lub nieobecny GPS nie uruchamia
kolejnej próby po restarcie. Pełne kasowanie pamięci MCU usuwa rezerwację.
Nie powstaje nowy mechanizm numerowania plików ani nadpisywania sesji.

## Wgranie i diagnostyka

Pobierz `main`, wykonaj Clean/Build i wgraj w CubeIDE. TFT: **LAPTUNE 2.1.6**.
Przejściowo może pojawić się `GPS RESTORE 1/3`–`3/3`, następnie NO DATA/NO FIX/FIX.
TFT odświeża GPS co 500 ms; pozycja od odbiornika domyślnie przychodzi co sekundę.

W SESSION.json:

- `gps_configuration_state`: 0=przywracanie, 1=odczyt ustawień zgodny,
  2=błąd przywracania, 3=pasywny odbiór bez odczytu ustawień w tym starcie.
- `gps_restore_state`: 0=niezlecone, 1=szukanie portu, 2=wysłanie defaults,
  3=wysłanie cold start, 4=potwierdzone 9600/1 Hz/sześć NMEA, 5=błąd.
- `gps_restore_error`: 1=brak odpowiedzi, 2=NAK, 3=niezgodny odczyt, 4=UART.
- `gps_restore_ack`: bit 0=ACK CFG-CFG, bit 1=ACK CFG-RST; brak ACK nie dowodzi
  niewykonania resetu. Stan 4 nie dowodzi odbioru RF ani uzyskania fixa.
- `gps_measurement_ms=0` przy pasywnym kolejnym starcie oznacza brak odczytu,
  nie zmierzony okres zero. `gps_nominal_hz=1` opisuje oczekiwany profil.
- Liczniki diagnostyki UBX RF/anteny pozostają zerowe: firmware nie odpytuje
  już tych komunikatów. Zero nie jest pomiarem braku satelitów czy uszkodzenia anteny.

## Dane SD i czas

Układ 36 kolumn TXT, format_version=4, jednostki IMU i GPS pozostają.
GGA i RMC są łączone według UTC tej samej epoki, aby nie mieszać wysokości
z poprzedniego pomiaru z nową prędkością. Brakująca część po 800 ms pozostaje
pusta. GPS_FIX_ID to lokalny kolejny numer epoki, nie iTOW.
GPS_T_RX_US oznacza odbiór w skali TIM2; GPS_ITOW_MS i GPS_T_MEAS_US są puste.
TIME_SYNC_STATE=0, PPS jest wyłączony również w przechwytywaniu TIM2; przewód
PA0 można pozostawić. TIM2 nadal mierzy czas IMU.
UTC jest przybliżone, ekstrapolowane z odebranego RMC jak w starej wersji.
Cyfry milisekund nie oznaczają potwierdzonej dokładności milisekundowej.
Flagi ważności i wyświetlanie danych pochodzą z parsera `1fd016f`;
ważność danych wygasa po 3 s bez aktualizacji.

## Sprawdzenie

Testy obejmują pasywny odbiór bez TX przez 15 minut, przywrócenie z obu baudrate,
niezgodny odczyt, brak ACK, NAK, brak modułu, błędy TX, czas życia bufora TX,
przepełnienie licznika ms, migrację rezerwacji i restart bez ponawiania.
Testy NMEA obejmują fix/no fix, dane TFT, parowanie epok, brakujące pola,
UTC, nieprawidłową sumę, przepełnienie odbioru i ponowienie odbioru po błędzie UART.
To weryfikacja kodu na hoście, nie pomiar odbioru satelitów na płytce.
