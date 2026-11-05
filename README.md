# LapTune_STM32

Firmware loggera STM32. **2.1.7: odczyt konfiguracji GPS do `GPS_CFG.TXT`**, bez zmiany ustawień ani resetowania odbiornika ([instrukcja](docs/GPS_CONFIG_QUERY.md)). Odbiór GGA/RMC nadal korzysta z parsera `1fd016f` przy 9600 baud. IMU i zapis SD pozostają aktywne.
PPS jest wyłączony; przewód PA0 może pozostać podłączony. Skorygowano licznik diagnostyczny, aby nie uznawał flag porównania TIM2 za impulsy PPS.
**P0: [format danych i DRDY](docs/P0_LOGGER.md)**; opisy konfiguracji UBX/PPS w starszych dokumentach dotyczą poprzednich wersji.
Wymagane przewody IMU: **INT1 → PA1 (A1), INT2 → PA4 (A2)**.

IMU **KAmodLSM6DS3** (zastępuje LIS3DH): [połączenia, akcelerometr i żyroskop](docs/IMU_LSM6DS3.md).

Obsługa wyświetlacza **ST7735S 160×128**: [schemat połączeń, kod i uruchomienie](docs/TFT_ST7735S.md).

GPS **u-blox NEO-6M / GY-NEO6MV2**: [połączenia UART, pozycja, wysokość MSL i data/czas UTC](docs/GPS_NEO6M.md).
GPS odbiera dane przy **9600 baud**. Przy każdym starcie wykonuje jedną serię 21 zapytań odczytu: wersja, UART, częstotliwość, nawigacja, PPS, antena, częstotliwości wiadomości i stan odbioru.
**PB10 (TX Nucleo) → RX GPS musi być podłączony**, żeby zapytania dotarły do modułu. TX GPS pozostaje połączony z PB11. Program nie zawiera nadawania CFG-CFG, CFG-RST ani setterów konfiguracji.
Po około 2 minutach z kartą SD naciśnij B1, zaczekaj na SD SAVED i prześlij **GPS_CFG.TXT oraz SESSION.json** z tej samej sesji. Nie trzeba czekać na fix.
Aktualizację pobierz z `main`, wykonaj Clean/Build i wgraj w CubeIDE.
Nagłówek TFT pokazuje **LAPTUNE 2.1.7**.
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
