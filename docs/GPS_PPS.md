# GPS i PPS — firmware 2.1.0

GPS pracuje z częstotliwością **5 Hz**. LSM6DS3 pracuje z natywną ODR
**104 Hz**; nie ma ustawienia 100 Hz. Rejestrowane są rzeczywiste zbocza DRDY,
bez zmieniania timestampów w celu wymuszenia idealnego okresu. Dokładnie
100 punktów na sekundę wymaga osobnego resamplingu w aplikacji.

## Co poprawiono

- Niepoprawny iTOW przed uzyskaniem fixa nie blokuje późniejszego poprawnego
  czasu, również po korekcie wstecz. Restart strumienia pozwala ponownie
  odebrać iTOW występujący przed restartem.
- Fragmenty POSLLH/VELNED/SOL mają 180 ms na złożenie jednej epoki, zamiast
  80 ms. Brak pojedynczego fragmentu nie usuwa od razu poprzedniej dobrej
  pozycji; jej ważność kończy się po 800 ms. Dane różnych iTOW nie są łączone.
- Cały obszar GPS na TFT jest rysowany z jednej lokalnej migawki co 200 ms.
  `GPS NO FIX` oznacza brak rozwiązania odbiornika; `GPS FIX WAIT DATA`
  oznacza fix zgłoszony przez SOL i oczekiwanie na spójną pozycję.
  `GPS NO DATA` oznacza brak komunikacji. Odczyt satelitów nie czeka na POSLLH.
- Konfiguracja PPS jest opcjonalnym krokiem po ustawieniu nawigacji 5 Hz.
  Jej błąd nie blokuje odbioru pozycji i logowania IMU.
- Jest jeden model powiązania czasu PPS–GPS i jedna funkcja przeliczania UTC;
  usunięto wcześniejszą interpolację UTC względem chwili odbioru UART.

Poprawki usuwają odtworzone błędy programowe. Nie stanowią pomiaru czasu
uzyskania fixa anteny w rzeczywistych warunkach.

## Połączenie i konfiguracja PPS

**NEO-6 PPS → PA0 / A0 / CN8 pin 1**, wspólne GND, sygnał 3,3 V.
PA0 jest wejściem sprzętowego przechwytywania TIM2_CH1. UART pozostaje
GPS TX → PB11, GPS RX ← PB10. Szczegóły: [GPS_NEO6M.md](GPS_NEO6M.md).

Po konfiguracji UART 38400 i CFG-RATE 200 ms firmware odczytuje legacy
CFG-TP, zachowuje opóźnienia anteny/RF/użytkownika, ustawia impuls dodatni
1 Hz, długość 100 ms, odniesienie GPS. Sprawdza ACK i odczyt ustawień.
Następnie włącza i sprawdza TIM-TP (klasa 0D, ID 01). Konfiguracja nie jest
zapisywana w pamięci nieulotnej GPS. Model dynamiki i tryby oszczędzania
energii odbiornika pozostają niezmienione.

TIM-TP opisuje **następny** impuls. Numer tygodnia i czas tego impulsu
zostają połączone z następnym numerem sprzętowego capture. Implementację
protokołu można porównać z [interpretacją TIM-TP w Satpulse](https://github.com/jclark/satpulse/blob/main/gps/internal/ubx/ubxtime.go)
(`PrePulse`). Sama najbliższa wiadomość UART nie jest podstawą synchronizacji.

Mapowanie wymaga trzech kolejnych zgodnych par, odstępu 970000–1030000
ticków MCU i aktualnego NAV-SOL z ważnym tygodniem/iTOW. Weryfikacja NAV
sprawdza, czy wyliczony pomiar poprzedza odbiór o 0–500 ms; chroni m.in.
przed pomyleniem impulsów o sekundę. Zanik PPS na ponad 1,5 s lub świeżej
kwalifikowanej nawigacji na ponad 800 ms wyłącza lock. Nie ma deklarowanego
holdover ani automatycznego ustawienia lock tylko dlatego, że PA0 zmienia stan.

## Co oznaczają czasy w TXT

Schemat TXT pozostaje **36-kolumnowy**; `SESSION.json` ma `format_version=4`.

| Pole | Znaczenie |
| --- | --- |
| T_IMU_US, T_ACC_US, T_GYRO_US | Monotoniczny licznik MCU, nominalnie 1 tick/µs; nigdy nie jest przestawiany przez GPS |
| GPS_T_RX_US | Timestamp końca ostatniej odebranej ramki danej epoki, w tej samej skali MCU |
| GPS_T_MEAS_US | iTOW epoki przeliczony przez model PPS na tę samą skalę MCU; puste bez lock |
| TIME_SYNC_STATE | 0: brak mapowania tej epoki; 1: epoka została powiązana przez model PPS |
| DATA, CZAS | UTC próbki IMU, z NAV-TIMEUTC i modelu PPS; zapis z milisekundami, puste przed lock lub przy utracie ważności |

Pomiary IMU czekają **700 ms** w istniejącym pierścieniu, żeby później
odebrany GPS przypisać do historycznego wiersza najbliższego jego czasowi
pomiaru. Pierwsze `N` danej epoki trafia do tego wiersza, kolejne są `R` z
niezmiennymi danymi/ID. Dla nieprzerwanej serii 104 Hz różnica czasu wynosi
najwyżej około połowy odstępu próbek, czyli 4,8 ms; luka w IMU może ją
zwiększyć. To błąd wyboru wiersza, bez uwzględnienia opóźnień filtrów IMU
i wewnętrznego rozwiązania nawigacyjnego GNSS.

Bez lock epoka pozostaje przypisana według czasu publikacji, z pustym
GPS_T_MEAS_US i stanem 0. Wtedy aplikacja nie powinna uznawać korekty GPS
za zsynchronizowaną. Nowy lock nie zmienia wcześniej opublikowanych epok.
Wyświetlacz korzysta z bieżących danych, niezależnie od opóźnienia zapisu.
Minima/maksima obejmują także próbki opróżniane po pauzie SD.

Surowe `T_IMU_US` zachowują dryf HSI. Przy integracji `ΔT_IMU_US / 1e6`
jest czasem nominalnym. Dla skorygowanej skali aplikacja musi użyć
pomiaru PPS, np. `ΔT_IMU_US / pps_model_period_ticks` daje sekundy przy
stabilnej częstotliwości i aktywnym lock. Metadane to migawka, nie historia
zmian częstotliwości. Nie zadeklarowano kalibracji całego przebiegu;
UTC próbek jest już interpolowane ze zmierzoną skalą PPS.

## Pamięć, SD i sesje

Nie zwiększono buforów. Surowy rekord ma 32 B, pierścień 256 slotów
(255 użytecznych). Rekord SD ma 96 B, kolejka mieści 16 rekordów.
700 ms wymaga około 73 rekordów, a pauza SD 1,163 s kolejnych około 121;
łącznie około 194–195 z 255. Pełna kolejka SD zatrzymuje konsumenta, pozostawiając
własność niewysłanych rekordów w pierścieniu. Zapis może stracić dane,
jeśli rzeczywiste opóźnienia przekroczą ten zapas; liczniki i NR pokazują luki.

B1 opróżnia również rekordy oczekujące na GPS, synchronizuje i zamyka plik.
Zaczekaj na `SD SAVED`. Rotacja nadal następuje co 3 minuty, synchronizacja
około co sekundę. Akwizycja nie czeka na fix ani UTC.

Każde włączenie tworzy nowy folder sesji. `LOG0001.TXT` w nowym folderze
nie nadpisuje pliku z poprzedniej sesji. Stopka, np. `REC S0007/LOG0001.TXT`,
pokazuje skrót licznika uruchomień i plik; pełny identyfikator jest nazwą
folderu oraz `boot_id` w JSON. Skrót nie jest unikalnym identyfikatorem importu.

## Diagnostyka i sprawdzenie po wgraniu

`gps_configuration_state=1` oznacza zweryfikowaną konfigurację nawigacji.
`pps_configuration_state`: 0=trwa, 1=ACK/odczyt PPS i TIM-TP potwierdzony,
2=niedostępne/błąd (szczegół w `pps_configuration_error`).
`pps_pulses` liczy capture, `pps_timtp_packets` wiadomości, `pps_matched`
połączone pary, `pps_rejected` odrzucone próby, `pps_locks/losses` zmiany lock.
`pps_model_period_ticks` to ostatni okres użyty w modelu;
`pps_period_change_ticks` to różnica kolejnych okresów, nie granica błędu
timestampu. `pps_mean_period_ticks` to średnia capture od uruchomienia.

Po wgraniu wykonaj Clean/Build w CubeIDE. Na zewnątrz, przy odsłoniętej
antenie, sprawdź GPS FIX i `UTC PPS`; następnie po B1 sprawdź metadane i
wiersze z `TIME_SYNC_STATE=1`, niepustym GPS_T_MEAS_US oraz `GPS_STATE=N`.
Sama godzina UTC bez napisu PPS nie potwierdza powiązania zegarów.

Testy ASan/UBSan obejmują m.in. 15 minut po korekcie iTOW, rozrzut fragmentów
90 ms, skalę MCU +4700 ppm, granicę tygodnia/roku UTC, zanik referencji,
błędne oznaczenie impulsu i przypisanie do historycznego wiersza. Test
całego potoku utrzymuje około 73 oczekujących próbek i dodaje pauzę SD
1,163 s, bez ukrywania strat czy zmiany numeracji.

Nie wykonano pomiaru na fizycznym zestawie. Pozostawiono
`timestamp_accuracy_validated=false` i `pps_reference_validated=false`.
Lock potwierdza warunki programowe; dokładność fizyczną, opóźnienia torów
i maksymalne zużycie stosu trzeba zmierzyć na Nucleo. Automatyczny raport
RAM i pojedynczych ramek stosu jest generowany podczas budowania.

W sprawdzonych kompilacjach ARM GCC statyczny RAM wynosi 17024 B (Debug)
i 17032 B (Release). Z 20480 B pozostaje odpowiednio 3456/3448 B na stos
i stertę: rezerwacja stosu 2560 B, minimalna sterta 512 B, dodatkowe
384/376 B. Największe pojedyncze ramki stosu to Session_Write 832 B
oraz formatowanie rekordu 752 B (Debug). To nie jest suma zagnieżdżonych
wywołań ani zmierzony szczyt stosu; runtime `stack_free_min_bytes` trzeba
sprawdzić z rzeczywistego urządzenia.
