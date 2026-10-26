# Zapis pomiarów na microSD

Czytnik ze zdjęcia ma piny **3V3, CS, MOSI, CLK, MISO, GND** i pracuje przy
3,3 V. SD współdzieli **SPI1 z TFT**. IMU pozostaje na osobnym SPI2.

![Połączenia SD i TFT](wiring_sd.png)

## Podłączenie

| Czytnik SD | NUCLEO-F103RB | Złącze |
| --- | --- | --- |
| 3V3 | 3V3 | CN6/4 |
| CS | PB0 / A3 | CN8/4, także CN7/34 |
| MOSI | PA7 / D11 | CN5/4, wspólne z SDA TFT |
| CLK | PA5 / D13 | CN5/6, wspólne z SCK TFT |
| MISO | PA6 / D12 | CN5/5 |
| GND | GND | np. CN6/6 |

CS wyświetlacza pozostaje na PB6 / D10. TFT nie wymaga MISO; GT_SO pozostaje
niepodłączone. Kod nigdy nie wybiera obu urządzeń jednocześnie. SPI1 pracuje
w trybie 0, 8 MHz; tylko inicjalizacja karty odbywa się przy 250 kHz, po czym
wraca częstotliwość TFT. Nie zmienia się żadne połączenie GPS ani IMU.

Ten czytnik zasilaj **3,3 V**, nie 5 V. Przewody SPI powinny być krótkie.
Przy niestabilnym zasilaniu sprawdź je podczas zapisu; karta może pobierać
większy prąd chwilowy niż wartość średnia z opisu modułu.

## Pliki i format

Karta musi mieć już system plików **FAT16 lub FAT32**, sektor 512 B. Zalecana
karta microSDHC do 32 GB. Karty SDXC są możliwe po przygotowaniu FAT32;
fabryczny exFAT nie jest obsługiwany. Firmware **nie formatuje karty**.

Przy każdym uruchomieniu tworzony jest pierwszy wolny plik od `LOG0001.TXT`
do `LOG9999.TXT`, bez nadpisywania istniejących plików. Numeracja próbek
zaczyna się od 1 w każdym pliku. Pierwszy wiersz jest nagłówkiem:

```text
NR;DATA;CZAS;ACC_X;ACC_Y;ACC_Z;GYRO_X;GYRO_Y;GYRO_Z
1;2026-10-26;12:34:56.123;0.000488;-1.000000;16.000000;0.070;-70.000;2000.000
```

Pola oddziela średnik, separator dziesiętny to kropka, końce wierszy to CRLF.
ACC jest w g z 6 miejscami po przecinku; GYRO w °/s z 3 miejscami. Precyzja
zapisu jest większa niż na ekranie i nie oznacza takiej dokładności czujnika.
Powtórzone ACC_Y i dwukropek z pierwotnej wiadomości zinterpretowano jako
ACC_Z i średnik, czyli dokładnie 9 kolumn.

DATA i CZAS są w **UTC**. Czas zawiera milisekundy. Bez ważnej, świeżej daty
i godziny z GPS oba pola są puste — pomiary nadal są zapisywane:

```text
2;;;0.000488;-1.000000;16.000000;0.070;-70.000;2000.000
```

Parser zachowuje ułamek sekundy RMC. Między komunikatami UTC jest interpolowany
zegarem STM32. Chwila próbki z kolejki FIFO jest oszacowana według liczby
oczekujących próbek i nominalnej ODR 104 Hz. Nadal nie ma obsługi PPS:
opóźnienie UART/GPS, faza próbkowania i dryf zegarów powodują błąd czasu.
**Trzy cyfry milisekund nie oznaczają dokładności 1 ms względem UTC.**
Kolejna ramka GPS może skorygować interpolowany czas; kolejność rekordów
określa NR. Do ścisłego powiązania próbek z UTC potrzebna jest synchronizacja PPS.
Bez świeżego odniesienia czasu (3 s), dla zbyt starej próbki lub sekundy
przestępnej pola czasu pozostają puste. Nie są zastępowane datą z komputera.

## Odczyt i zapis

LSM6DS3 zapisuje gyro XYZ i accel XYZ do własnego FIFO przy 104 Hz,
bez decymacji. Pętla odczytuje kompletne zestawy. Dzięki FIFO zapis na kartę
nie wymaga zatrzymania czujnika; zniknęło wcześniejsze odpytywanie rejestrów
wyjściowych co 10 ms. Min/max i TFT korzystają z tych samych odczytanych próbek.

Sterownik SD podczas oczekiwania na kartę co kilka ms obsługuje SPI2/FIFO i
parser GPS. Ten callback nie rysuje TFT i nie wywołuje FatFs. Osobna kolejka
RAM mieści 128 rekordów (około 1,2 s przy 104 Hz). Zapis jest grupowany w
buforze 1024 B; `f_sync()` odbywa się co około sekundę, a `f_close()` po B1.
FatFs i logger pracują tylko w głównej pętli, nie w przerwaniu. Podczas długiego
zapisu rysowanie TFT może chwilowo zwolnić; obsługa IMU/GPS jest kontynuowana.

Nie ma gwarancji zapisu przy dowolnie długim zablokowaniu/uszkodzeniu karty.
Przepełnienie FIFO/RAM, utrata wyrównania danych, błąd SPI IMU lub brak próbek
przez sekundę zatrzymują logowanie ze statusem `SD DATA LOSS`. Nie kontynuujemy
pozornie kompletnego pliku po wykrytej utracie danych. Przy błędzie zapisu
ostatni wiersz pliku może być niepełny.

## Obsługa na płytce

1. Włóż przygotowaną kartę przed włączeniem/resetem Nucleo.
2. Po utworzeniu pliku w stopce pojawi się `SD REC LOGxxxx.TXT`.
3. **Naciśnij i puść B1**, aby zakończyć sesję. Przerwanie przycisku tylko
   zgłasza żądanie; główna pętla dopisuje kolejkę i zamyka plik.
4. Wyjmuj kartę po pojawieniu się **`SD SAVED`**. Pomiary i ekran nadal działają.
5. Nową sesję rozpocznij przez reset Nucleo z włożoną kartą.

`SD ERROR` oznacza m.in. brak karty, zły system plików lub błąd komunikacji.
`SD FULL` oznacza brak miejsca. `SD DATA LOSS` oznacza problem z ciągłością
danych. Te statusy nie oznaczają bezpiecznie zamkniętej, kompletnej sesji.
Bez karty IMU/GPS i wyświetlacz nadal pracują, lecz nie ma automatycznego
ponawiania montowania włożonej później karty — należy zresetować urządzenie.

Zanik zasilania lub wyjęcie karty podczas `SD REC` może utracić ostatni bufor
i uszkodzić otwarty plik/system plików. Okresowy sync ogranicza ryzyko,
ale nie zastępuje zatrzymania przez B1.

W debuggerze: `sd_log_state`, `sd_log_filename`, `sd_log_written`,
`sd_log_error` (FRESULT) i `sd_log_dropped`. Written liczy wiersze przyjęte
przez `f_write`, nie potwierdzenie trwałości pamięci flash. Dropped sygnalizuje
odrzucenie rekordu lub zdarzenie utraty ciągłości; nie jest dokładnym licznikiem
próbek utraconych przy przepełnieniu sprzętowego FIFO.

## Biblioteka i testy

FatFs R0.16 jest dołączony do `Core/FatFs` wraz z licencją i identyfikatorem
źródłowego commita ST. Nie trzeba pobierać go przy budowaniu ani zmieniać
ścieżek include w CubeIDE; folder Core jest już objęty kompilacją.
Konfiguracja produkcyjna nie zawiera funkcji formatowania karty.

```sh
bash tests/run.sh
python3 /workspace/.laptune-setup/build.py Debug
python3 /workspace/.laptune-setup/build.py Release
```

Dziewięć programów testowych obejmuje dotychczasowe IMU/GPS/TFT, FIFO,
kalendarz i znaczniki czasu, zapis rzeczywistym FatFs na obrazie FAT w RAM,
format rekordów, ochronę wcześniejszych plików, błędy i pełną kartę,
dodawanie próbek podczas zapisu, protokół SDHC/SDSC, CRC, osobne CS i timeouty.
Testy programowe nie potwierdzają elektryki modułu ani działania fizycznej karty.
