# KAmodLSM6DS3 — akcelerometr i żyroskop

LSM6DS3 zastępuje wcześniejszy LIS3DH. Pozostaje **SPI2, tryb 3, 8 MHz**,
z programowym CS na PB12. TFT i GPS oraz ich wyświetlane pola są zachowane.
Odczyt żyroskopu nie dodaje nowych wartości na ekranie.

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

104 Hz to najbliższa dostępna ODR do poprzednich 100 Hz. Pętla nadal sprawdza
próbki co co najmniej 10 ms, więc może pomijać próbki; nie jest to rejestrator
wszystkich danych 104 Hz. Do takiego logowania potrzebny byłby FIFO lub DRDY.
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

Wynik `READ_OK` aktualizuje sześć zmiennych i dotychczasowe min/max przyspieszeń.
`READ_NOT_READY` zachowuje ostatnią próbkę i nie uruchamia ponownie inicjalizacji.
`READ_ERROR` również zachowuje próbkę, ale oznacza czujnik jako niedostępny;
główna pętla ponawia inicjalizację. Nie ma częściowej aktualizacji danych
przy nieudanej transmisji SPI. Wszystkie ścieżki kończą się zwolnieniem CS.

Rejestry i czułości zweryfikowano w
[oficjalnym sterowniku ST](https://github.com/STMicroelectronics/lsm6ds3-pid/tree/1b0caf4bf522c7ae33b7f00b23fad531e58d5e90).
Sterownik ST był źródłem odniesienia; projekt nie wymaga jego pobierania.

## Sprawdzenie

Po wgraniu i poprawnym podłączeniu na TFT powinny nadal pojawiać się X/Y/Z,
ich min/max oraz GPS. Na nieruchomej płytce moduł wektora przyspieszenia
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
