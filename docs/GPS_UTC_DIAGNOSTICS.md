# Diagnostyka opóźnionego UTC — firmware 2.2.2

Wersja 2.2.2 dodaje historię odbioru i odrzucania UBX NAV-TIMEUTC (01/21)
do `SESSION.json`. Nie zmienia profilu GPS 5 Hz, PPS, kryteriów akceptacji
UTC ani 36 kolumn TXT. Nie skraca jeszcze oczekiwania na godzinę: ma wskazać
jego przyczynę bez zgadywania wartości czasu lub korekty GPS–UTC.

## Interpretacja pól

Wszystkie pola poniżej mają prefiks `gps_timeutc_`.

| Końcówka nazwy | Znaczenie |
| --- | --- |
| `packets`, `accepted` | Liczba ramek przekazanych do parsera UTC i liczba zaakceptowanych informacji UTC |
| `first_rx_us`, `last_rx_us` | Znaczniki końca odbioru pierwszej/ostatniej ramki w skali MCU, nie czas pomiaru GPS |
| `first_valid_us` | Znacznik odbioru pierwszej zaakceptowanej informacji UTC; istotny, gdy `accepted>0` |
| `rejected_flags` | Brak kompletu trzech flag ważności: TOW, numer tygodnia, UTC |
| `rejected_calendar` | Flagi poprawne, ale data/godzina poza zakresem obecnego parsera |
| `rejected_nano` | Flagi i kalendarz poprawne, ale ułamek `nano` poza otwartym przedziałem (-1e9, +1e9) ns |
| `rejected_length` | Payload ma inną długość niż wymagane 20 bajtów |
| `flags_histogram` | Osiem liczników, indeks oznacza wartość trzech najmłodszych bitów flag, od 0 do 7 |
| `last_rejected_us` | Znacznik odbioru ostatniej odrzuconej ramki |
| `last_rejected_reason` | 0=brak, 1=flagi, 2=kalendarz, 3=nano, 4=długość |
| `last_rejected_length` | Oryginalna długość ostatniego odrzuconego payloadu |
| `last_rejected_payload` | Pierwsze maksymalnie 20 bajtów tego payloadu jako tablica liczb dziesiętnych, bez nagłówka i checksum UBX |

Histogram liczy tylko payloady 20-bajtowe. Indeks **3** oznacza ważne TOW
i numer tygodnia, ale nieważne UTC. Indeks **7** oznacza komplet flag;
taka ramka może jeszcze nie przejść kontroli kalendarza lub ułamka sekundy.
Dotychczasowe `gps_utc_valid_flags` nadal pokazuje tylko ostatnio odebrane flagi.

Odrzucenia są rozłączne: najpierw długość, następnie flagi, kalendarz, nano.
`packets = accepted + rejected_length + rejected_flags + rejected_calendar + rejected_nano`.
Obecny parser dopuszcza lata 1980–2099 i sekundy 0–59; nie zmieniono obsługi
sekund przestępnych. Ujemne prawidłowe `nano` nadal koryguje datę/godzinę.

Liczniki dotyczą ramek z poprawną sumą UBX i długością dopuszczoną przez
warstwę transportową (dla NAV-TIMEUTC do 64 bajtów). Uszkodzone sumy nadal
liczy `gps_checksum_errors`; ucięta lub zbyt długa ramka nie musi dotrzeć do
parsera UTC. To nie jest pełny zapis surowego UART.

Historia przeżywa ponowne odkrywanie UART i reset strumienia w ramach tego
samego uruchomienia. Poprawna ramka nie usuwa ostatniej odrzuconej ramki.
Dzięki temu końcowy JSON pokazuje również oczekiwanie sprzed uzyskania godziny.
Początek nowej sesji zeruje historię. Same znaczniki równe 0 nie dowodzą
odebrania ramki — należy czytać je razem z licznikami.

## Jak odczytać wynik testu

- `packets=0`: parser UTC nie otrzymał żadnej kwalifikującej się ramki.
- Rosnące `rejected_flags` i histogram[3]: odbiornik nie potwierdza ważności UTC,
  mimo że zna czas GPS. To pasuje do oczekiwania na parametry GPS–UTC;
  sam histogram nie potwierdza, dlaczego odbiornik ich jeszcze nie ma.
- Rosnące `rejected_calendar` lub `rejected_nano`: zbadać zapisany payload
  względem parsera. W payloadzie: `nano` w bajtach 8–11 (signed little-endian),
  rok 12–13, miesiąc/dzień/godzina/minuta/sekunda 14–18, flagi 19.
- `accepted>0`, a TXT nadal bez UTC: porównać dotychczasowe
  `utc_missing_pps_records`, `utc_out_of_range_records` oraz stan PPS.

`first_valid_us` oznacza odbiór ramki, nie czas pierwszego wiersza TXT z UTC.
Próbki oczekujące w buforze mogą otrzymać UTC także dla wcześniejszej chwili.

Po zwykłej próbie zewnętrznej zakończyć zapis B1, poczekać na `SD SAVED`
i przekazać `SESSION.json` oraz TXT obejmujący pojawienie się godziny.

## Pamięć i testy

Dodatkowa diagnostyka zajmuje **112 B statycznego RAM**. Migawka mieści się
w istniejącym współdzielonym obszarze 1024 B; nie powiększono kolejek ani
rezerwacji stosu. ARM GCC: Debug 16984 B / Release 16976 B statycznego RAM;
zapas ponad rezerwacje linkera odpowiednio 424 B / 432 B.
To nie jest pomiar maksymalnego stosu na urządzeniu.

Testy ASan/UBSan obejmują wszystkie kombinacje flag, błędny kalendarz,
granice ułamka sekundy, złą długość (także 0), zachowanie historii po resecie
strumienia, znacznik 0 i znaczniki ponad 32 bity, odrzucanie złych checksum
przed diagnostyką oraz eksport rzeczywistych liczników i payloadu do JSON.
