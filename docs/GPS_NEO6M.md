# GPS u-blox NEO-6M — P0

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

P0 odczytuje MON-VER oraz ustawia i sprawdza CFG-MSG/CFG-RATE:

| Komunikat | Częstość | Zastosowanie |
| --- | --- | --- |
| NAV-POSLLH | 5 Hz | lon/lat, wysokość MSL |
| NAV-VELNED | 5 Hz | poziome gSpeed |
| NAV-SOL | 5 Hz | rodzaj fixa, gpsFixOK, weekSet, TOWSet, satelity |
| NAV-TIMEUTC | 1 Hz | pomocnicze data/czas UTC |
| GGA/GLL/GSA/GSV/RMC/VTG | wyłączone | unikanie duplikatów i mieszanego cache NMEA |

CFG-RATE: measRate=200 ms, navRate=1, timeRef=GPS. Strumień nominalny:
(36+44+60) × 5 + 28 = **728 B/s**, czyli 7280 bit/s w 8N1,
około 19% UART 38400. Pozostaje zapas na odpowiedzi konfiguracyjne.
Dodatkowe komunikaty wcześniej włączone innym narzędziem nie są globalnie
wyłączane; nadmiar ujawni się jako przepełnienie bufora.

Bufor UART: 1024 bajty, przerwanie odbioru jednego bajtu. Koniec ramki dostaje
64-bitowy czas w ISR; checksum i interpretacja są wykonywane poza ISR.
Konfiguracja TX korzysta z przerwań i trwałego bufora, bez blokującego
HAL_UART_Transmit. Sterownik SD wywołuje obsługę GPS podczas oczekiwania na kartę.
USART3 nie korzysta tutaj z DMA; IMU ma osobne DMA SPI2.

## Spójność i jednostki

Trzy komunikaty nawigacyjne są składane według identycznego iTOW. Deadline:
80 ms od pierwszego fragmentu. Wymagane POSLLH i SOL z poprawnym fixem
2D/3D i ważnym czasem tygodnia. Pozycja 2D nie daje wysokości; brak VELNED
nie daje prędkości. Brakujące pola są puste. Późny fragment nie poprawia
opublikowanej epoki. Brak świeżej poprawnej pozycji przez **800 ms**
unieważnia cache. Błędy checksum, UART, bufora, niepełne epoki i brak fixa
mają oddzielne liczniki. Brak komunikatów przez 3 s oznacza `GPS NO DATA`.

- lon/lat: oryginalne signed int32 × 1e-7 stopnia; zapis 7 miejsc.
- MSL: `hMSL × 0,001 m`; nie jest to wysokość elipsoidalna `height`.
- Prędkość: `gSpeed × 0,036 km/h`, z cm/s, bez różnicowania pozycji.
- Data/czas: UTC, uwzględnione ważności i znak nano; brak stałej poprawki GPS–UTC.

Widok TFT zachowuje układ i sposób odświeżania co 200 ms. Dane pochodzą teraz
z jednej epoki UBX. S oznacza satelity z NAV-SOL, ALT oznacza MSL.
`GPS NO FIX` to strumień bez poprawnego rozwiązania; `GPS NO DATA` to brak
świeżej komunikacji. Diagnostyka konfiguracji nadal jest pokazywana na TFT.

`T_IMU_US`, GPS_STATE, GPS_FIX_ID, GPS_ITOW_MS oraz ograniczenie czasów odbioru
opisano w [P0_LOGGER.md](P0_LOGGER.md). Jest sprzętowe przechwytywanie PPS, ale nie ma jeszcze zweryfikowanego
mapowania impuls–tydzień/iTOW ani skorygowania latencji GNSS. Model dynamiki, oszczędzanie energii i static
hold nie są w P0 zmieniane ani uznawane za zweryfikowane. Są do osobnej
weryfikacji na konkretnym firmware NEO-6 podczas kolejnego etapu.
