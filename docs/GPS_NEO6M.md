# GPS u-blox NEO-6M — firmware 2.1.4

Moduł GY-NEO6MV2 komunikuje się przez USART3. Zasilanie podaj według
specyfikacji konkretnego breakouta; poziomy UART przy STM32 muszą wynosić
3,3 V. Wspólna masa jest wymagana.

| GPS | Nucleo-F103RB |
| --- | --- |
| TX | PB11 / CN10/18 / USART3_RX |
| RX | PB10 / D6 / CN9/7 / USART3_TX |
| GND | GND |
| VCC | 3V3, jeżeli moduł dopuszcza zasilanie 3,3 V |
| PPS | **PA0 / A0 / CN8 pin 1 / TIM2_CH1**, przechwytywanie zboczy |

## Konfiguracja odbiornika

Start UART: 9600 bit/s, 8N1. Automat sprawdza 9600/38400 i ustawia
**38400 bit/s**, oba kierunki UBX. PB10 → GPS RX jest potrzebne do konfiguracji.
Po restarcie odbiornika lub MCU rozpoznanie jest ponawiane. Nie zapisujemy
konfiguracji trwale w pamięci GPS. Wymagamy ACK dla CFG-MSG/CFG-RATE, sprawdzamy NAK i odczyt ustawień.

Start nawigacji wraca do konfiguracji wiadomości używanej przed P0
(commit `461c935`): GGA/RMC po 5 Hz, GLL/GSA/GSV/VTG wyłączone.
Strumienie NAV-POSLLH/VELNED/SOL/TIMEUTC i TIM-TP są początkowo wyłączone,
również jeśli ich ustawienia zachował zasilany bateryjnie odbiornik.
`gps_acquisition_only=1` oznacza oczekiwanie na ważny fix NMEA.
Dopiero po jego odebraniu firmware włącza i weryfikuje poniższe strumienie
UBX oraz konfiguruje PPS. GGA/RMC pozostają włączone jako niezależna
kontrola odbioru; ich wartości nie są domieszane do rekordów UBX.

| Komunikat | Częstość po pierwszym fixie | Zastosowanie |
| --- | --- | --- |
| GGA/RMC | 5 Hz | potwierdzenie fixa na sprawdzonej ścieżce NMEA |
| NAV-POSLLH | 5 Hz | lon/lat, wysokość MSL |
| NAV-VELNED | 5 Hz | poziome gSpeed |
| NAV-SOL | 5 Hz | rodzaj fixa, gpsFixOK, weekSet, TOWSet, satelity |
| NAV-TIMEUTC | 1 Hz | data/czas UTC do modelu PPS |
| TIM-TP | 1 Hz | oznaczenie następnego impulsu PPS |
| GLL/GSA/GSV/VTG | wyłączone | ograniczenie ruchu UART |

CFG-RATE: measRate=200 ms, navRate=1, timeRef=GPS, tak jak przed P0.
Część UBX ma nominalnie (36+44+60) × 5 + 28 + 24 = **752 B/s**.
Dochodzi zmienna długość GGA/RMC po 5 Hz; przy UART 38400 bit/s
(3840 B/s w 8N1) pozostaje zapas na konfigurację i diagnostykę.
Dodatkowe komunikaty wcześniej włączone innym narzędziem nie są globalnie
wyłączane; nadmiar ujawni się jako przepełnienie bufora.

Podczas braku fixa działa [diagnostyka odbiornika](GPS_DIAGNOSTICS.md).
Od 2.1.4 brak fixa lub śledzenia nie powoduje automatycznego restartu GNSS.
Odbiornik zachowuje nieprzerwane okno akwizycji, także gdy potrzebuje
3–5 minut lub dłużej. Wszystkie elementy loggera działają równocześnie.

Bufor UART: 1024 bajty, przerwanie odbioru jednego bajtu. Koniec ramki dostaje
64-bitowy czas w ISR; checksum i interpretacja są wykonywane poza ISR.
Konfiguracja TX korzysta z przerwań i trwałego bufora, bez blokującego
HAL_UART_Transmit. Sterownik SD wywołuje obsługę GPS podczas oczekiwania na kartę.
USART3 nie korzysta tutaj z DMA; IMU ma osobne DMA SPI2.

## Spójność i jednostki

Trzy komunikaty nawigacyjne są składane według identycznego iTOW. Deadline:
180 ms od pierwszego fragmentu. Wymagane POSLLH i SOL z poprawnym fixem
2D/3D i ważnym czasem tygodnia. Pozycja 2D nie daje wysokości; brak VELNED
nie daje prędkości. Brakujące pola są puste. Późny fragment nie poprawia
opublikowanej epoki. Brak świeżej poprawnej pozycji przez **800 ms**
unieważnia cache. Błędy checksum, UART, bufora, niepełne epoki i brak fixa
mają oddzielne liczniki. Brak komunikatów przez 3 s oznacza `GPS NO DATA`.

- lon/lat: oryginalne signed int32 × 1e-7 stopnia; zapis 7 miejsc.
- MSL: `hMSL × 0,001 m`; nie jest to wysokość elipsoidalna `height`.
- Prędkość: `gSpeed × 0,036 km/h`, z cm/s, bez różnicowania pozycji.
- Data/czas: UTC, uwzględnione ważności i znak nano; brak stałej poprawki GPS–UTC.

Widok TFT zachowuje układ i odświeżanie co 200 ms, rysując cały obszar z jednej migawki. Dane pochodzą teraz
z jednej epoki UBX. S oznacza satelity z NAV-SOL, ALT oznacza MSL.
`GPS NO FIX` to strumień bez poprawnego rozwiązania; `GPS NO DATA` to brak
świeżej komunikacji. Diagnostyka konfiguracji nadal jest pokazywana na TFT.

`T_IMU_US`, GPS_STATE i identyfikatory opisano w [P0_LOGGER.md](P0_LOGGER.md).
**PPS na PA0 jest już używane do mapowania czasu pomiaru GPS i UTC próbek**:
[warunki lock, konfiguracja i diagnostyka](GPS_PPS.md). Błąd opcjonalnego
CFG-TP/TIM-TP nie blokuje nawigacji 5 Hz. Odbiornik nie jest kalibrowany
na podstawie ODR IMU. Model dynamiki, oszczędzanie energii i static hold
nie są zmieniane; wymagają osobnej weryfikacji na konkretnym odbiorniku.

Diagnostyka odbiornika podczas NO FIX (od 2.1.1, rozszerzona w 2.1.2): [C/N₀, antena, SOL i konfiguracja](GPS_DIAGNOSTICS.md).
