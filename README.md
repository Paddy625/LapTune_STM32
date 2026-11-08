# LapTune_STM32

Firmware loggera STM32, **2.2.3: zakres IMU ±8 g i ±500°/s**.
IMU zapisuje rzeczywiste próbki DRDY przy natywnych 104 Hz. UTC próbek jest
wyznaczane z PPS + TIM-TP + NAV-TIMEUTC, a czas pomiaru GPS jest oddzielony
od czasu odbioru UART. [Zasady czasu i uruchomienie](docs/GPS_PPS.md).
Przyczyna problemu NO FIX została ustalona na sprzęcie: przewody INT1/INT2
prowadzone blisko toru antenowego. Zachowaj poprawione prowadzenie przewodów.

[Diagnostyka UTC w 2.2.2](docs/GPS_UTC_DIAGNOSTICS.md): moment pierwszej ramki i poprawnego UTC, flagi oraz powody odrzucenia w `SESSION.json`.

[Zmiany 2.2.1 i diagnostyka po wgraniu](docs/LOGGER_2_2_1.md). TFT służy do diagnostyki i może odświeżać się z opóźnieniem.

**P0: [format danych i DRDY](docs/P0_LOGGER.md)**. Wymagane przewody IMU: **INT1 → PA1 (A1), INT2 → PA4 (A2)**.

IMU **KAmodLSM6DS3** (zastępuje LIS3DH): [połączenia, akcelerometr i żyroskop](docs/IMU_LSM6DS3.md).

Obsługa wyświetlacza **ST7735S 160×128**: [schemat połączeń, kod i uruchomienie](docs/TFT_ST7735S.md).

GPS **u-blox NEO-6M / GY-NEO6MV2**: [połączenia UART, pozycja, wysokość MSL i data/czas UTC](docs/GPS_NEO6M.md).
GPS jest automatycznie rozpoznawany przy **9600/38400 baud**, a następnie
konfigurowany na **38400 baud, UBX 5 Hz**. **PB10 → GPS RX**, **GPS TX → PB11**,
**GPS PPS → PA0 (A0)** i wspólne GND muszą pozostać podłączone.
Konfiguracja działa niezależnie od fixa i karty SD. Nie ma automatycznego
resetowania GPS, przywracania fabrycznego ani zapisu ustawień GPS na stałe.
Eksperymentalny raport `GPS_CFG.TXT` nie jest już generowany przy starcie.

Pobierz `main`, wykonaj **Clean/Build** i wgraj w CubeIDE. TFT pokaże
**LAPTUNE 2.2.3**. Po uzyskaniu sygnału sprawdź w `SESSION.json`:
`gps_configuration_state=1`, `gps_measurement_ms=200`, `pps_configuration_state=1`,
rosnące `pps_pulses` i `pps_matched`, a następnie `time_sync_state=1`.
W TXT po synchronizacji pojawią się `GPS_T_MEAS_US` i UTC z milisekundami.
Bez PPS odczyt IMU i pozycji nadal działa, ale zsynchronizowane czasy pozostają puste.
[Archiwum HEX 2.1.1](firmware/README.md) jest historyczne — nie wgrywaj go zamiast aktualnego kodu.

Logger **microSD na SPI1 wspólnym z TFT**: [połączenia i format TXT](docs/SD_LOGGING.md).
Brak karty lub błąd zapisu nie blokują odczytu IMU i wyświetlania. Po włożeniu karty zapis wznawia się automatycznie, bez resetowania numerów próbek.
Automatycznie zamyka plik co 3 minuty i rozpoczyna kolejny; synchronizuje dane
co około sekundę. Po błędzie ponawia montowanie co 2 sekundy i wznawia zapis
w nowym pliku. B1 zatrzymuje zapis przed wyjęciem karty; kolejne naciśnięcie
wznawia go bez resetowania płytki.
Log zawiera także pozycję GPS (szerokość/długość), prędkość względem ziemi
z UBX NAV-VELNED w km/h oraz **wysokość MSL w metrach**; bez ważnego fixa odpowiednie
pola pozostają puste.

IMU parameters:
	- Maximum acceleration +/- 8g
	- Accelerometer and gyroscope ODR 104Hz; DRDY on PA1/PA4 + SPI2 DMA; 64-bit timestamps independent of GPS/SD
	- Acceleration sensitivity 0.244mg/LSB
	- Angular velocity range +/- 500 degrees/s, sensitivity 0.0175 degrees/s/LSB
	- TFT shows AX/GX, AY/GY, AZ/GZ with MIN/NOW/MAX; TFT snapshots refresh at most once per second; row rendering yields to storage
