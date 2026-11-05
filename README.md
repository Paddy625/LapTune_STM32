# LapTune_STM32

Firmware loggera STM32. **2.1.6: powrót GPS do odbioru NMEA z `1fd016f`, 9600 baud / domyślnie 1 Hz** ([szczegóły przywrócenia](docs/GPS_RESTORE.md)). IMU i zapis SD pozostają w obecnej wersji, również przy braku fixa lub karty. PPS jest w tej wersji wyłączony; przewód PA0 może pozostać podłączony.
**P0: [format danych i DRDY](docs/P0_LOGGER.md)**; opisy UBX/PPS w starszych dokumentach dotyczą wersji przed tym powrotem.
Wymagane przewody IMU: **INT1 → PA1 (A1), INT2 → PA4 (A2)**.

IMU **KAmodLSM6DS3** (zastępuje LIS3DH): [połączenia, akcelerometr i żyroskop](docs/IMU_LSM6DS3.md).

Obsługa wyświetlacza **ST7735S 160×128**: [schemat połączeń, kod i uruchomienie](docs/TFT_ST7735S.md).

GPS **u-blox NEO-6M / GY-NEO6MV2**: [połączenia UART, pozycja, wysokość MSL i data/czas UTC](docs/GPS_NEO6M.md).
GPS normalnie wyłącznie odbiera GGA/RMC przy **9600 baud**, bez konfiguracji 5 Hz, przełączania na UBX po fixie, konfiguracji PPS i automatycznych resetów.
Przy pierwszym uruchomieniu 2.1.6 podejmuje jedną zarezerwowaną próbę przywrócenia ustawień odbiornika i zimnego startu, następnie odczytuje UART 9600, okres 1000 ms i sześć domyślnych komunikatów NMEA. Do tej jednorazowej operacji wymagane jest **PB10 → RX GPS**.
Aktualizację pobierz z `main`, wykonaj Clean/Build i wgraj w CubeIDE.
Nagłówek TFT pokazuje **LAPTUNE 2.1.6**. Weryfikacja na hoście nie potwierdza odbioru satelitów na fizycznym module.
[Archiwum HEX 2.1.1](firmware/README.md) dotyczy poprzedniej wersji, nie aktualnego kodu.

Logger **microSD na SPI1 wspólnym z TFT**: [połączenia i format TXT](docs/SD_LOGGING.md).
Brak karty lub błąd zapisu nie blokują odczytu IMU i wyświetlania. Po włożeniu karty zapis wznawia się automatycznie, bez resetowania numerów próbek.
Automatycznie zamyka plik co 3 minuty i rozpoczyna kolejny; synchronizuje dane
co około sekundę. Po błędzie ponawia montowanie co 2 sekundy i wznawia zapis
w nowym pliku. B1 zatrzymuje zapis przed wyjęciem karty; kolejne naciśnięcie
wznawia go bez resetowania płytki.
Log zawiera także pozycję GPS (szerokość/długość), prędkość względem ziemi
z NMEA RMC w km/h oraz **wysokość MSL w metrach**; bez ważnego fixa odpowiednie
pola pozostają puste.

IMU parameters:
	- Maximum acceleration +/- 16g
	- Accelerometer and gyroscope ODR 104Hz; DRDY on PA1/PA4 + SPI2 DMA; 64-bit timestamps independent of GPS/SD
	- Acceleration sensitivity 0.488mg/LSB
	- Angular velocity range +/- 2000 degrees/s, sensitivity 0.070 degrees/s/LSB
	- TFT shows AX/GX, AY/GY, AZ/GZ with MIN/NOW/MAX; GPS display refreshes every 500 ms
