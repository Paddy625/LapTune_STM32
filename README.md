# LapTune_STM32

**Gotowy HEX do wgrania przez ST-LINK:** [archiwum 2.1.1](firmware/README.md).

Firmware loggera STM32. **2.1.1: [diagnostyka NO FIX — sygnał, antena i tryb GPS](docs/GPS_DIAGNOSTICS.md)**. **Synchronizacja 2.1.0: [poprawki GPS, synchronizacja PPS i uproszczenia](docs/GPS_PPS.md)**. **P0: [format danych, DRDY i instrukcja wgrania](docs/P0_LOGGER.md)**.
**Przegląd P0 i diagnostyka:** [raport zmian](docs/P0_REVIEW_2026-11-02.md).
Wymagane przewody: **INT1 → PA1 (A1), INT2 → PA4 (A2)**. PPS GPS: **PA0 (A0)**.

IMU **KAmodLSM6DS3** (zastępuje LIS3DH): [połączenia, akcelerometr i żyroskop](docs/IMU_LSM6DS3.md).

Obsługa wyświetlacza **ST7735S 160×128**: [schemat połączeń, kod i uruchomienie](docs/TFT_ST7735S.md).

GPS **u-blox NEO-6M / GY-NEO6MV2**: [połączenia UART, pozycja, wysokość MSL i data/czas UTC](docs/GPS_NEO6M.md).
Firmware konfiguruje **5 Hz, UART 38400 baud**, z automatycznym rozpoznaniem
9600/38400 przy starcie. Do konfiguracji wymagane jest **PB10 → RX GPS**.

Logger **microSD na SPI1 wspólnym z TFT**: [połączenia i format TXT](docs/SD_LOGGING.md).
Automatycznie zamyka plik co 3 minuty i rozpoczyna kolejny; synchronizuje dane
co około sekundę. Po błędzie ponawia montowanie co 2 sekundy i wznawia zapis
w nowym pliku. B1 zatrzymuje zapis przed wyjęciem karty; kolejne naciśnięcie
wznawia go bez resetowania płytki.
Log zawiera także pozycję GPS (szerokość/długość), prędkość względem ziemi
z UBX NAV-VELNED w km/h oraz **wysokość MSL w metrach**; bez ważnego fixa odpowiednie
pola pozostają puste.

IMU parameters:
	- Maximum acceleration +/- 16g
	- Accelerometer and gyroscope ODR 104Hz; DRDY on PA1/PA4 + SPI2 DMA; 64-bit timestamps independent of GPS/SD
	- Acceleration sensitivity 0.488mg/LSB
	- Angular velocity range +/- 2000 degrees/s, sensitivity 0.070 degrees/s/LSB
	- TFT shows AX/GX, AY/GY, AZ/GZ with MIN/NOW/MAX; GPS refreshes every 200 ms
