# GPS NEO-6M — połączenie i uruchomienie

![Cały układ z GPS](wiring_st7735s.png)

[Schemat SVG](wiring_st7735s.svg). Schemat obejmuje Nucleo, LSM6DS3, ST7735S
i dodatkowy moduł GY-NEO6MV2 ze zdjęcia.

## Połączenia

| Pin płytki GPS | NUCLEO-F103RB | Uwagi |
| --- | --- | --- |
| VCC | 5V, CN6/5 | Tylko wejście VCC płytki GY-NEO6MV2 z regulatorem dopuszczającym 5 V — patrz poniżej |
| RX | PB10 / USART3_TX, CN10/25, także D6 | Opcjonalne; obecny kod nie wysyła konfiguracji do GPS |
| TX | PB11 / USART3_RX, CN10/18 | Wymagane; tędy odbieramy NMEA |
| GND | GND Nucleo | Wspólna masa z TFT i LSM6DS3 |

Łączymy **TX GPS z RX STM32**. Piny UART pracują w logice **3,3 V**.
VCC płytki i poziom sygnałów UART to różne rzeczy. Nie podawaj 5 V na RX/TX.
USART3 nie koliduje z istniejącym SPI1/SPI2 ani z USART2 połączonym z ST-Link.

Na zdjęciu jest płytka nośna GY-NEO6MV2 z elementami zasilania. Schemat zakłada
jej wersję z regulatorem i wejściem VCC dopuszczającym 5 V. Przed podłączeniem
5 V sprawdź oznaczenie regulatora/specyfikację swojej płytki, ponieważ występują
różne wersje i kopie. **Sam moduł u-blox NEO-6M wymaga 2,7–3,6 V**, więc 5 V
nie wolno podawać bezpośrednio na jego zasilanie. Jeśli Twoja płytka nie ma
regulatora, użyj 3V3 zgodnie z jej dokumentacją. Zasilenie wejścia regulatora
z 3V3 może z kolei dać za niskie napięcie za regulatorem i powodować restarty.

Podłącz antenę GPS do modułu. Pierwsze uruchomienie wykonaj na zewnątrz,
z anteną zwróconą w stronę otwartego nieba. W pomieszczeniu może nie być fixa.
Początkowe ustalanie pozycji może trwać kilka minut.

## Odczyty i ekran

UART: **9600 baud, 8 bitów, bez parzystości, 1 bit stopu (8N1)**, bez kontroli
przepływu. To domyślna konfiguracja NEO-6M; moduł wcześniej przestawiony na
inną prędkość lub wyłącznie UBX wymaga przywrócenia NMEA albo zgodnej zmiany
`MX_USART3_UART_Init()` oraz `.ioc`.

Pozostaje nagłówek, Twój kompaktowy układ ACC i stopka. Pomiędzy nimi,
w sześciu liniach y=72..119, pojawiają się np.:

```text
GPS FIX S:08
LAT +52.2297000
LON +021.0122000
ALT +123.4 M
UTC 12:34:56
DATE 2026-10-20
```

- LAT/LON to stopnie dziesiętne, dodatnie dla N/E i ujemne dla S/W.
  Siedem miejsc po przecinku opisuje format liczby, nie dokładność odbiornika.
- ALT to wysokość **nad średnim poziomem morza (MSL)** z pola wysokości GGA,
  w metrach. Nie jest to wysokość nad elipsoidą WGS84 ani nad lokalnym gruntem.
  Do stabilnego pomiaru wysokości potrzebny jest dobry odbiór i rozwiązanie 3D;
  wysokość GPS zazwyczaj ma większy błąd niż pozycja pozioma.
- UTC to czas satelitarny przeliczony przez odbiornik na UTC. Data i godzina
  nie są przestawiane automatycznie na czas polski. Dla Polski czas lokalny
  jest UTC+1 zimą, UTC+2 latem; obsługa strefy i zmiany czasu nie jest dodana.
- S to liczba satelitów użytych w rozwiązaniu według GGA.
- `GPS NO FIX` oznacza poprawne komunikaty RMC/GGA bez ważnej pozycji.
  `GPS NO DATA` oznacza brak świeżych, poprawnych RMC/GGA — sprawdź zasilanie,
  masę, TX→PB11, prędkość i ustawienia wyjściowe modułu.
- Nieprawidłowe albo przeterminowane pola mają kreski. Data i czas są
  uznawane za ważne dopiero po poprawnym RMC ze statusem A.

## Prędkość i zapis na SD

NEO-6M podaje **prędkość poziomą względem ziemi** (speed over ground, SOG)
w komunikacie RMC, w polu po długości geograficznej i E/W. Nie trzeba liczyć
jej z różnic położenia: jest wyznaczana przez odbiornik GPS. W NMEA jednostką
są węzły, a kod przelicza je według **1 kn = 1,852 km/h**. Przykład:
`22.4` kn daje `41.485` km/h po zaokrągleniu do 3 miejsc.

`GPS_Data.speed_millikmh` przechowuje tysięczne km/h jako liczbę całkowitą,
np. `41485`; `speed_valid` i `speed_ms` określają ważność i czas odebrania.
Puste pole prędkości oznacza brak wartości. RMC ze statusem V, GGA bez fixa
oraz 3 s bez świeżej prędkości unieważniają ją; ważne zero pozostaje zerem.

Logger SD dopisuje `GPS_LAT`, `GPS_LON` i `GPS_SPEED_KMH` do każdego wiersza
IMU, z migawką nawigacji przy odczycie z FIFO. Przy domyślnej aktualizacji
GPS 1 Hz wartości będą powtarzane w wielu wierszach IMU 104 Hz. Puste pola
oznaczają brak ważnych, świeżych danych. Szczegóły: [format pliku SD](SD_LOGGING.md).
Prędkość trafia do pliku i struktury danych; układ ekranu GPS pozostaje taki jak wyżej.

## Jak działa kod

- `gps.c`: odbiór jednego bajtu przez `HAL_UART_Receive_IT`, kolejka 512 B.
  Przerwanie USART3 tylko zapisuje bajt i ponawia odbiór. Parsowanie odbywa
  się w głównej pętli, poza przerwaniem. Obsłużono przepełnienie kolejki,
  zbyt długą linię, ponowną synchronizację na `$` oraz ponawianie odbioru
  po błędzie UART. Aktualny kod nie wysyła komend do GPS.
- `gps_nmea.c`: kontrola XOR `*HH`, rozpoznawanie GP/GN RMC oraz GGA,
  walidacja współrzędnych, jednostek i daty. Nie używa `sscanf` z floatami,
  pamięci dynamicznej ani funkcji blokujących odbiór.
- `GPS_Data`: współrzędne jako stopnie × 10⁷, wysokość w mm, prędkość
  jako km/h × 1000 oraz pola
  kalendarza. Struktura jest dostępna przez `GPS_GetData()` po `GPS_Process()`.
- RMC dostarcza datę/czas, pozycję i prędkość; GGA dostarcza pozycję, wysokość MSL,
  jakość fixa i liczbę satelitów. Pola mają osobne znaczniki świeżości.
  Po **3 s** bez nowej wartości odpowiednia flaga ważności jest zerowana.
  RMC ze statusem V i GGA z jakością 0 unieważniają odpowiednie pomiary.
- `gps_display.c`: migawka odczytów co 500 ms i jedno małe pole na wywołanie.
  Wszystkie linie są dopełniane spacjami, żeby po zmianie wartości nie zostawały
  stare cyfry. Blokujące transmisje SPI nie zatrzymują przerwań odbioru GPS.
- `.ioc`, MSP, IRQ i callbacki HAL obejmują USART3 na PB10/PB11.

## Sprawdzenie

W STM32CubeIDE zbuduj i wgraj projekt. Po podłączeniu GPS powinien pojawić się
`NO FIX`, a po ustaleniu pozycji `FIX`, współrzędne, wysokość i data/czas.
Odłączenie TX powinno po około 3 s (plus odświeżenie ekranu) dać `NO DATA`
i kreski. Ponowne podłączenie powinno przywrócić odczyt bez resetowania Nucleo.

W chmurze:

```sh
bash tests/run.sh
python3 /workspace/.laptune-setup/build.py Debug
python3 /workspace/.laptune-setup/build.py Release
```

Testy GPS obejmują przykładowe pakiety NMEA, błędne sumy, puste pola,
brak fixa, S/W, ujemną wysokość, poprawność dat, zerowe współrzędne,
prędkość RMC i konwersję na km/h, brak/zero/błędne pole prędkości,
przepełnienie licznika czasu, ramki dzielone między odczytami, przepełnienie
kolejki, błędy UART i układ ekranu. To testy programowe z atrapą HAL;
nie potwierdzają odbioru satelitów, zasilania, anteny ani dokładności pomiaru.
