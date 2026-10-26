# KAmodLSM6DS3 — akcelerometr i żyroskop

LSM6DS3 zastępuje wcześniejszy LIS3DH. Pozostaje **SPI2, tryb 3, 8 MHz**,
z programowym CS na PB12. Na TFT są trzy pary wierszy: AX/GX, AY/GY i AZ/GZ.
W każdej parze akcelerometr jest nad żyroskopem, a wartości są w kolejności
**minimum, chwilowa, maksimum**. Widok GPS pozostaje bez zmian.

## Połączenia według nazw sygnałów

| Sygnał LSM6DS3 na module | Nucleo | Morpho |
| --- | --- | --- |
| Zasilanie 3,3 V | 3V3 | np. CN7/16 |
| GND | GND | np. CN7/20 |
| SCL / SPC / SCK | PB13 / SPI2_SCK | CN10/30 |
| SDA / SDI / MOSI | PB15 / SPI2_MOSI | CN10/26 |
| SDO / SA0 / MISO | PB14 / SPI2_MISO | CN10/28 |
| CS | PB12 / GPIO | CN10/16 |
| INT1, INT2 | NC | Odczyt przez polling, bez przerwań czujnika |

Nazwy powyżej opisują główny interfejs układu. `SCx/SDx`, jeśli występują,
to interfejs pomocniczy sensor-hub, nie zamiennik głównego SPI. Dla SPI
CS musi być sterowane przez PB12; nie może być na stałe zwarte do zasilania
jak przy I2C. SDO/SA0 podłącz do MISO, a nie do stałego poziomu wybierającego
adres I2C. Zasilanie i sygnały STM32 w tym połączeniu: **3,3 V**.

Dokumentacja konkretnej płytki i jej złączy/zworek:
[KAmodLSM6DS3 — Kamami](https://wiki.kamamilabs.com/extensions/JZPDFGen/pdf/KAmodLSM6DS3-208.pdf).
Podczas tej zmiany serwer/proxy zwracał HTTP 403 dla tego PDF. Nie potwierdzono
więc numeracji pinów złączy ani ewentualnych zworek KAmod; tabela celowo podaje
sygnały układu. Przed zasileniem sprawdź je na swojej płytce/dokumentacji.

## Konfiguracja i jednostki

| Parametr | Akcelerometr | Żyroskop |
| --- | --- | --- |
| Zakres | ±16 g | ±2000°/s |
| ODR | 104 Hz | 104 Hz |
| Format wyjściowy | signed 16 bit, little-endian | signed 16 bit, little-endian |
| Przelicznik | 0,000488 g/LSB | 0,070°/s/LSB |
| Zmienne w `main.c` | `ax`, `ay`, `az` | `gx`, `gy`, `gz` |

104 Hz to najbliższa dostępna ODR do poprzednich 100 Hz. Logger korzysta teraz
z FIFO IMU: oba źródła bez decymacji, tryb ciągły przy 104 Hz, wzorzec gyro XYZ
potem accel XYZ. Kolejka jest odczytywana także podczas oczekiwania na SD.
Przepełnienie i utrata wyrównania wzorca zatrzymują logowanie zamiast
kontynuować plik z nieznaną utratą danych. Szczegóły: [SD_LOGGING.md](SD_LOGGING.md).
Przyspieszenie obejmuje grawitację. Żyroskop daje prędkość kątową, nie kąty
obrotu; nie dodano całkowania, fuzji danych ani kalibracji biasu.

## Sterownik

`acc.h` / `acc.c` udostępniają:

```c
uint8_t LSM6DS3_Init(void);
LSM6DS3_ReadResult LSM6DS3_ReadSample(LSM6DS3_Sample *sample);
```

Inicjalizacja sprawdza WHO_AM_I = 0x69, wykonuje reset programowy z limitem
oczekiwania, ustawia BDU i IF_INC, wyłącza I2C, włącza osie XYZ obu czujników
i sprawdza zapisane rejestry. Konfiguracja dotyczy **LSM6DS3**; innych modeli,
np. LSM6DS3TR-C z innym identyfikatorem, sterownik nie akceptuje automatycznie.

Przy odczycie wymagane są obie flagi gotowości: XLDA i GDA. Następnie jeden
12-bajtowy odczyt od 0x22 pobiera gyro XYZ, a potem accel XYZ. LSM6DS3 używa
IF_INC w CTRL3_C; dawnego bitu MULTI 0x40 z LIS3DH nie wolno dokładać do adresu.
Nie wykonujemy też przesunięcia danych o 4 bity jak dla starego czujnika.

Główna pętla używa `LSM6DS3_EnableFIFO()` i `LSM6DS3_ReadFIFO()`.
`LSM6DS3_ReadSample()` pozostaje funkcją bezpośredniego odczytu rejestrów.
Wynik `READ_OK` aktualizuje sześć zmiennych i dotychczasowe min/max przyspieszeń.
`READ_NOT_READY` zachowuje ostatnią próbkę i nie uruchamia ponownie inicjalizacji.
`READ_ERROR` również zachowuje próbkę, ale oznacza czujnik jako niedostępny;
główna pętla ponawia inicjalizację. Nie ma częściowej aktualizacji danych
przy nieudanej transmisji SPI. Wszystkie ścieżki kończą się zwolnieniem CS.

Rejestry i czułości zweryfikowano w
[oficjalnym sterowniku ST](https://github.com/STMicroelectronics/lsm6ds3-pid/tree/1b0caf4bf522c7ae33b7f00b23fad531e58d5e90).
Sterownik ST był źródłem odniesienia; projekt nie wymaga jego pobierania.

## Wyświetlanie

Dla każdej osi są dwa wiersze: A — przyspieszenie w g, G — prędkość kątowa
w °/s. Każda kolumna ma siedem znaków; przyspieszenie ma trzy miejsca po
przecinku, a żyroskop jedno. Przykład jednej pary (MIN, chwilowa, MAX):

```text
AX-16.000 +00.123 +16.000
GX-2000.0 -0070.1 +2000.0
```

Wiersze AX/GX, AY/GY, AZ/GZ zaczynają się odpowiednio na y=18/26, 36/44,
54/62, czcionka 5×7 ze skalą 1. Każdy zajmuje 8 pikseli wysokości.
GPS nadal zajmuje y=72..119; jego kod, kolory, teksty i odświeżanie nie
zostały zmienione. Nagłówek jest zachowany; stopka pokazuje teraz status SD.

Minima i maksima obu czujników są inicjalizowane pierwszą poprawną próbką
oraz aktualizowane po każdym udanym odczycie. Zmienne żyroskopu to
`gx/gy/gz`, `min_gx/min_gy/min_gz`, `max_gx/max_gy/max_gz`. Cała ramka IMU
korzysta ze wspólnej migawki danych co 100 ms, a jedno wywołanie rysuje jeden
wiersz. Przy błędzie wszystkie trzy pola danego wiersza są zastępowane
kreskami. Stała szerokość tekstu usuwa poprzednie cyfry.

## Sprawdzenie

Po wgraniu i poprawnym podłączeniu na TFT powinny pojawiać się pary AX/GX,
AY/GY, AZ/GZ z min/max obu czujników oraz dotychczasowy GPS. Na nieruchomej płytce moduł wektora przyspieszenia
`sqrt(ax*ax + ay*ay + az*az)` powinien wynosić około 1 g, a prędkości kątowe
być bliskie 0°/s, z możliwym
biasem. `gx`, `gy`, `gz` można obserwować w debuggerze STM32CubeIDE.
Sprawdź znaki przy obrocie wokół kolejnych osi; orientacja nowej płytki może
różnić się od orientacji poprzedniego LIS3DH.

```sh
bash tests/run.sh
python3 /workspace/.laptune-setup/build.py Debug
python3 /workspace/.laptune-setup/build.py Release
```

Test sterownika sprawdza identyfikator, reset i jego timeout, konfigurację,
obie flagi gotowości, adres i długość burstu, kolejność osi, skrajne liczby
ze znakiem, przeliczniki oraz brak nadpisywania próbki po błędzie SPI.
Testy programowe nie zastępują sprawdzenia konkretnej płytki KAmod.
