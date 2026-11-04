# Diagnostyka NO FIX — firmware 2.1.3

## Wersja programu i status na ekranie

Od 2.1.3 nagłówek TFT pokazuje wersję, np. `LAPTUNE 2.1.3`.
To wersja uruchomionego firmware; sama obecność zmian na GitHubie nie
oznacza, że CubeIDE przebudowało i wgrało właściwy ELF. Potwierdzenie
stanowią też `firmware_version` i `firmware_commit` w najnowszej sesji.
Przesłana po aktualizacji sesja z około 814 s pracy nadal deklarowała
2.1.1 / `112cbbf`, więc nie testowała zmian uruchamiania z 2.1.2.

Naprawiono błąd 2.1.2: podczas profilu NMEA liczba satelitów na TFT
czekała na wyłączony wtedy NAV-SOL i pozostawała zerowa. Teraz pochodzi
z poprawnego checksum GGA; status fixa pochodzi z GGA/RMC. Gdy NMEA
zgłasza fix, a spójna pozycja UBX jeszcze nie dotarła, ekran pokazuje
`GPS FIX WAIT DATA`. Współrzędne logu nadal pochodzą wyłącznie z epok UBX.
Po utracie komunikacji wygaszane są także status fixa i liczba satelitów.
Ta poprawka usuwa mylący status; sama nie naprawia odbioru RF.

Ponowne porównanie z `461c935` objęło zasoby sprzętowe:

| Element | Wynik porównania |
| --- | --- |
| Zegar procesora i UART | HSI/PLL 64 MHz, USART3 8N1 9600/38400 bez zmiany |
| Piny GPS | PB10 TX i PB11 RX bez zmiany |
| Nowe wejścia | PPS PA0, DRDY PA1/PA4; nie pokrywają się z UART GPS |
| DMA IMU | DMA1 kanały 4/5 dla SPI2; GPS odbierany przerwaniami UART, bez DMA |
| SPI | Preskalery bez zmiany; ilość przesyłanych danych zmieniła się w P0 |

Nie znaleziono konfliktu pinów ani polecenia wyłączającego odbiór
satelitów. To nie wyklucza wpływu aktywności elektroniki na RF, ale
nie stanowi też dowodu takiego wpływu. Brak fixa w sesji 2.1.1 nie
potwierdza skuteczności ani nieskuteczności zmian od 2.1.2.

`GPS NO FIX 5HZ S:00` oznacza zweryfikowaną konfigurację 5 Hz i brak
satelitów użytych w rozwiązaniu. Liczba S nie określa liczby odbieranych
sygnałów. Sam ten komunikat nie wskazuje przyczyny braku pozycji.

Gdy `gps_received_epochs` i `gps_invalid_fixes` rosną, a checksum,
przepełnienia, niepełne epoki i błędy UART w fazie pracy pozostają zerowe,
logger odbiera kompletne rozwiązania oznaczane jako nieważne. Do rozróżnienia
braku śledzenia satelitów od ograniczeń rozwiązania potrzebne są dane
odbiornika opisane poniżej. Brak PPS sam w sobie nie blokuje pozycji.

## Zmiana startu i odzyskiwanie w 2.1.2

Porównanie z ostatnią działającą wersją `461c935` wskazało zmianę
protokołu wyjściowego: GGA/RMC zastąpiono od początku strumieniem UBX.
W ostatniej analizowanej sesji po ponad 12 minutach odbiornik zgłosił
3838 nieważnych epok, zero śledzonych kanałów i zero C/N₀, przy zerowych
błędach checksum, przepełnieniach oraz błędach UART w fazie pracy.
Odczyty surowego statusu nie potwierdzają, żeby logger ukrywał poprawny fix.
Nie ustalono jednak przyczyny braku śledzenia sygnału na fizycznym zestawie.

Wersja 2.1.2 przywraca na początku sprawdzony profil **GGA/RMC 5 Hz**.
Wyłącza na ten czas strumienie UBX NAV i TIM-TP; po odebraniu ważnego
fixa NMEA włącza spójne epoki UBX oraz konfigurację PPS. GGA/RMC zostają
włączone także później. Parser NMEA odczytuje status i sprawdza checksum;
nie tworzy konkurencyjnego cache współrzędnych i nie miesza danych NMEA
z rekordami UBX. W krótkiej fazie przejścia TFT i log czekają na ważną
epokę UBX. Nie uzależniono rejestrowania IMU od tego oczekiwania.

Dodano jedną ograniczoną próbę odzyskania odbioru na uruchomienie MCU.
Warunkiem jest **120 s ciągłych świeżych odczytów NAV-SVINFO** z zerowym
C/N₀ i zerowym śledzeniem kodu oraz brak ważnego fixa. Brak odpowiedzi,
przestarzałe SVINFO lub śledzone sygnały nie spełniają warunku. Operacja
używa CFG-RST z `navBbrMask=0`, `resetMode=2`: gorący restart tylko GNSS,
bez kasowania podtrzymywanych danych, bez fabrycznego resetu i bez zapisu
konfiguracji do pamięci nieulotnej. Po 1 s sterownik ponownie rozpoznaje
UART i weryfikuje ustawienia. Nie uruchamia pętli resetów co 120 s.

To przywrócenie zgodnego profilu i ograniczenie sposobu odzyskiwania,
nie potwierdzona na sprzęcie naprawa odbioru RF. Testy hosta sprawdzają
protokół i przepływ danych; czas uzyskania fixa wymaga sprawdzenia na NEO-6.
Nie zadeklarowano fizycznej dokładności timestampów ani PPS.

## Dodatkowe odczyty

Podczas NO FIX firmware wysyła raz na sekundę kolejno cztery żądania:
NAV-SVINFO, MON-HW, CFG-NAV5 i CFG-RXM. Pełny cykl trwa około 4 s.
Są to żądania odczytu z pustym payloadem, bez zapisu ustawień GPS.
Odczyty ustają, kiedy SOL zgłosi fix lub dostępna jest ważna pozycja.
Odpowiedź NAK, brak odpowiedzi i błąd transmisji diagnostycznej nie
unieważniają zweryfikowanej konfiguracji nawigacji ani PPS.

Bufor payloadu pozostaje 64-bajtowy. NAV-SVINFO jest podsumowywane podczas
parsowania całej ramki, również dla kanałów poza pierwszymi 64 bajtami.
Wynik staje się widoczny dopiero po sprawdzeniu checksum oraz zgodności
długości z liczbą kanałów. MON-HW obsługuje format **68 B NEO-6** (jamInd
w bajcie 53) i format 60 B (jamInd w bajcie 45). Ramki diagnostyczne
nie są łączone z pozycją, prędkością ani wysokością pomiarów.

Format TXT pozostaje 36-kolumnowy, `format_version` JSON pozostaje 4.
Nowe klucze JSON są dodatkowymi polami diagnostycznymi. GPS pozostaje
przy 5 Hz, IMU przy natywnej ODR 104 Hz; układ TFT pozostaje bez zmian.

## Pola SESSION.json

Najpierw sprawdź `gps_receiver_seen`. To maska odebranych typów odpowiedzi:

| Bit | Wiadomość | Jakie pola uznawać za znane |
| --- | --- | --- |
| 1 | NAV-SOL | gps_sol_fix_type, gps_sol_flags, gps_sol_week, gps_sol_used_sats |
| 2 | NAV-TIMEUTC | gps_utc_valid_flags |
| 4 | NAV-SVINFO | gps_svinfo_channels, gps_sats_with_cno, gps_sats_code_locked, gps_max_cno_dbhz, gps_svinfo_rx_us |
| 8 | MON-HW | gps_antenna_status/power, gps_noise_per_ms, gps_agc_count, gps_jamming_indicator, gps_hw_flags, gps_mon_hw_rx_us |
| 16 | CFG-NAV5 | gps_dynamic_model, gps_fix_mode, gps_min_elevation_deg, gps_pdop_limit_x10 |
| 32 | CFG-RXM | gps_power_mode |

Brak bitu oznacza **nieznany wynik**, a nie zmierzone zero. Odczyty są
ostatnimi znanymi wartościami; czas SKY i MON-HW jest zapisany w tej samej
64-bitowej skali MCU co próbki. Porównaj go ze `snapshot_t_us`, zwłaszcza
po uzyskaniu fixa, kiedy dalsze odpytywanie ustaje.

- `gps_sol_fix_type`: 0=brak, 1=dead reckoning, 2=2D, 3=3D,
  4=GNSS+DR, 5=tylko czas. Logger akceptuje pozycję dla 2/3.
- `gps_sol_flags`: bit 0=gpsFixOK, bit 1=rozwiązanie różnicowe,
  bit 2=znany tydzień, bit 3=znany TOW. Dla ważnej pozycji wymagane są bity
  0, 2, 3; pozostałe warunki obejmują kompletną pozycję z tej samej epoki.
- `gps_utc_valid_flags`: bity 0/1/2 oznaczają odpowiednio ważne TOW,
  tydzień i UTC; wszystkie trzy są wymagane przez dekoder UTC.
- `gps_svinfo_channels` to liczba bloków kanałów z odpowiedzi, nie liczba
  satelitów widocznych na niebie. `gps_sats_with_cno` liczy bloki z C/N₀ > 0,
  `gps_sats_code_locked` bloki z quality 4–7, a `gps_max_cno_dbhz` jest
  największą zgłoszoną wartością C/N₀. Te liczby mogą być dodatnie przy S:00.
- `gps_antenna_status`: 0=inicjalizacja, 1=nieznany, 2=OK, 3=zwarcie,
  4=obwód otwarty. Interpretacja zależy od układu monitorowania anteny
  na danej płytce i rodzaju anteny; sam kod nie jest pomiarem jej sprawności.
- `gps_antenna_power`: 0=wyłączone, 1=włączone, 2=nieznane.
- `gps_jamming_indicator`, `gps_hw_flags`, `gps_noise_per_ms` i
  `gps_agc_count` są surową diagnostyką odbiornika. Jamming state jest
  w bitach 2–3 hw_flags dla firmware NEO-6 7.03. Wartości nie są
  skalibrowanym pomiarem zakłóceń zewnętrznym analizatorem.
- `gps_dynamic_model`, `gps_fix_mode`, `gps_power_mode` to odczytane kody
  u-blox, nie ustawienia narzucone przez logger. Fix mode: 1=tylko 2D,
  2=tylko 3D, 3=automatyczny. Min elevation jest w stopniach, limit pDOP
  w jednostkach 0,1 (np. 250 oznacza 25,0).

## Stan NMEA i próby odzyskiwania w SESSION.json

| Pole | Znaczenie |
| --- | --- |
| gps_acquisition_only | 1: profil GGA/RMC aktywny, oczekiwanie na pierwszy ważny fix przed włączeniem epok UBX i PPS |
| gps_nmea_sentences | licznik rozpoznanych zdań GGA/RMC z poprawną checksum; zdania, nie epoki |
| gps_nmea_fix_sentences | licznik takich zdań zgłaszających ważny fix; również nie licznik unikalnych epok |
| gps_nmea_last_us | czas przetworzenia ostatniego poprawnego zdania NMEA w skali MCU |
| gps_nmea_last_fix_us | czas przetworzenia ostatniego zdania NMEA z ważnym fixem w skali MCU |
| gps_recovery_attempts | liczba prób ograniczonego restartu GNSS, najwyżej jedna na uruchomienie MCU |
| gps_recovery_us | czas próby restartu w skali MCU; zero przed próbą |

`gps_received_epochs`, `gps_valid_epochs` i `gps_invalid_fixes` nadal
liczą epoki UBX. Podczas oczekiwania w profilu GGA/RMC mogą pozostawać
zerowe mimo działającej komunikacji. NMEA i UBX mają oddzielne liczniki.
Historia liczników nie jest kasowana przy próbie odzyskiwania. Czasy
NMEA służą ocenie świeżości komunikacji; nie są czasem pomiaru ani
źródłem synchronizacji PPS.
`gps_configuration_state=1` potwierdza ustawienia aktywnego etapu;
nie oznacza jeszcze fixa, zakończenia etapu NMEA ani lock PPS.

## Sprawdzenie aktualizacji

1. Pobierz `main`, wykonaj Clean/Build w CubeIDE i wgraj **2.1.3**.
   Na TFT musi być `LAPTUNE 2.1.3`. Archiwalny HEX 2.1.1 nie zawiera tych zmian.
   Jeśli wersja na TFT jest starsza, sprawdź w Debug Configurations →
   C/C++ Application, czy ELF pochodzi z aktualnie budowanego projektu
   (`LapTune/Debug/LapTune.elf` albo odpowiedniej konfiguracji Release).
2. Po próbie naciśnij B1 i zaczekaj na `SD SAVED`. Zachowaj najnowszy
   `SESSION.json` i ostatni plik TXT. Sprawdź wersję oraz `snapshot_t_us`;
   przy kontrolowanym zatrzymaniu oczekiwane jest `clean_shutdown=true`.
3. Najpierw sprawdź, czy rosną liczniki NMEA i czy pojawiły się zdania
   z ważnym fixem. Następnie sprawdź przejście `gps_acquisition_only=0`,
   poprawne epoki UBX i, niezależnie, lock PPS. Podczas NO FIX pola RF
   i licznik odzyskiwania pokazują stan próby.

Samo zwiększenie licznika odebranych wiadomości nie potwierdza fixa.
Nieważnej pozycji nie wolno zastępować zerami ani sztucznie oznaczać jako fix.

## Pamięć i sprawdzenie programu

Nie zwiększono buforów UART, IMU ani kolejki SD. Usunięto lokalną kopię
GPS_Diagnostics w funkcji pobierającej migawkę: dane trafiają bezpośrednio
do bufora wywołującego pod krótką maską przerwań. Jest to istotne przy
diagnostyce stosu na STM32F103RB. Dokładny RAM i pojedyncze ramki stosu
podaje `memory-report.json` po budowaniu. W kompilacji **2.1.2** statyczny
RAM wynosi 17128 B (Debug) i 17136 B (Release); pozostaje 3352/3344 B
na stos i stertę przy rezerwacjach 2560/512 B. Parser statusu NMEA
przetwarza bajty bez bufora zdania i bez lokalnej kopii GPS_Data;
jego ramka stosu Debug ma 32 B. Session_Write ma ramkę 920 B.
Są to rozmiary pojedynczych ramek, nie pomiar maksymalnego stosu całego
łańcucha wywołań z przerwaniami. Rzeczywisty szczyt nadal wymaga
sprawdzenia pola `stack_free_min_bytes` na urządzeniu; ostatni pomiar
starszego firmware wynosił tylko 176 B wolnego z rezerwy stosu.

Testy obejmują długie SVINFO z maksimum C/N₀ w ostatnim kanale, checksum,
błędną długość, oba formaty MON-HW, surowe flagi SOL/UTC, odczyty bez zapisu
ustawień, NAK, timeout TX i ustanie odpytywania po odzyskaniu fixa.
Nowe testy sprawdzają osobno start przez GGA lub RMC, brak publikacji
współrzędnych z NMEA, przejście do UBX/PPS oraz jeden restart po 120 s
zerowego śledzenia. Brak PPS, nieaktualne SVINFO i śledzone sygnały
nie powodują restartów. Pełny zestaw testów hosta (ASan/UBSan) oraz
kompilacje ARM Debug i Release przeszły; nie jest to test odbioru RF.
