# Diagnostyka NO FIX — firmware 2.1.1

`GPS NO FIX 5HZ S:00` oznacza zweryfikowaną konfigurację 5 Hz i brak
satelitów użytych w rozwiązaniu. Liczba S nie określa liczby odbieranych
sygnałów. Sam ten komunikat nie wskazuje przyczyny braku pozycji.

Gdy `gps_received_epochs` i `gps_invalid_fixes` rosną, a checksum,
przepełnienia, niepełne epoki i błędy UART w fazie pracy pozostają zerowe,
logger odbiera kompletne rozwiązania oznaczane jako nieważne. Do rozróżnienia
braku śledzenia satelitów od ograniczeń rozwiązania potrzebne są dane
odbiornika opisane poniżej. Brak PPS sam w sobie nie blokuje pozycji.

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
Nowe klucze JSON są dodatkowymi polami diagnostycznymi. Ekran i częstotliwości
akwizycji pozostają takie jak w 2.1.0.

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

## Zebranie danych

1. W CubeIDE wykonaj Clean/Build, wgraj 2.1.1 i uruchom test na zewnątrz.
2. Do zebrania diagnostyki wystarczy około 30 s; nie trzeba czekać kolejnych
   6 minut. Naciśnij B1 i zaczekaj na `SD SAVED`.
3. Odczytaj najnowszy `SESSION.json`, sprawdź wersję, maskę seen i datę
   migawki. Przy kontrolowanym zatrzymaniu oczekiwane jest clean_shutdown=true.
4. Gdy podejrzewasz wpływ nowo wyprowadzonego PPS, osobno powtórz próbę
   z całkowicie odłączonym przewodem GPS–PA0, przepinając przy wyłączonym
   zasilaniu. Pozycja jest od niego niezależna; timestampy UTC nie uzyskają
   wtedy lock PPS. Zmieniając jeden element na raz, zachowaj tę samą antenę
   i warunki odbioru.

Z C/N₀, flag SOL i konfiguracji będzie można wybrać dalszy krok: sprawdzenie
anteny/zasilania/zakłóceń lub konkretnej konfiguracji odbiornika. Nieważnej
pozycji nie wolno zastępować zerami ani sztucznie oznaczać jako fix.

## Pamięć i sprawdzenie programu

Nie zwiększono buforów UART, IMU ani kolejki SD. Usunięto lokalną kopię
GPS_Diagnostics w funkcji pobierającej migawkę: dane trafiają bezpośrednio
do bufora wywołującego pod krótką maską przerwań. Jest to istotne przy
diagnostyce stosu na STM32F103RB. Dokładny RAM i pojedyncze ramki stosu
podaje `memory-report.json` po budowaniu. W sprawdzonej kompilacji statyczny RAM wynosi 17072 B (Debug) i 17080 B (Release), po 48 B więcej niż w 2.1.0; pozostaje 3408/3400 B na stos i stertę przy rezerwacjach 2560/512 B. Rzeczywisty szczyt nadal wymaga
sprawdzenia pola `stack_free_min_bytes` na urządzeniu.

Testy obejmują długie SVINFO z maksimum C/N₀ w ostatnim kanale, checksum,
błędną długość, oba formaty MON-HW, surowe flagi SOL/UTC, odczyty bez zapisu
ustawień, NAK, timeout TX i ustanie odpytywania po odzyskaniu fixa.
