# Firmware 2.2.1 — zapis ma pierwszeństwo przed TFT

## Powód zmian

W dostarczonej sesji 2.2.0 GPS pracował z 5 Hz, PPS miał jeden lock bez
utraty synchronizacji. Jednocześnie pierścień IMU osiągnął 255 rekordów,
`imu_ring_dropped=7185`, a `stack_free_min_bytes=0`. W LOG0013 brakowało
950 numerów próbek. To uzasadnia zmniejszenie obciążenia pętli i stosu,
bez zmieniania konfiguracji odbiornika ani sztucznego poprawiania timestampów.
Zero w monitorze stosu oznacza wyczerpanie obserwowanego obszaru 2560 B;
samo w sobie nie dowodzi uszkodzenia danych statycznych.

Pozycja i czas pomiaru oparty na PPS mogą być dostępne przed ważnym UTC.
W LOG0004 pierwszych 832 rekordów miało puste UTC; pojawiło się ono przy
NR 56760. Bez historycznych ramek NAV-TIMEUTC nie można ustalić dokładnej
przyczyny tej konkretnej zwłoki. Zapis pozycji i IMU nie czeka na UTC.

## Zmiany

- CSV jest formatowany bez dużych tablic dla poszczególnych pól, za pomocą
  operacji całkowitoliczbowych. Pozostają dokładne uint64, skale, 36 kolumn i CRLF.
- Paczka SD 1024 B mieści tyle całych wierszy, ile faktycznie się zmieści.
  Wiersz, który nie mieści się na końcu, pozostaje w kolejce do kolejnej paczki.
  Nie zmienia to oznaczeń N/R ani numeracji próbek.
- Gdy najstarsza próbka w pierścieniu ma ponad 900 ms, pętla wykonuje do ośmiu
  rund pobierania/zapisu. Każda obsługuje również UART GPS i IMU. Przy dalszych
  zaległościach kolejne obroty kontynuują zapis bez TFT. Zwykłe oczekiwanie na
  historyczne przypisanie GPS pozostaje równe 700 ms.
- Migawki SESSION powstają co około 30 s zamiast co 5 s, po zmianie konfiguracji,
  przy rotacji i zatrzymaniu. Aktualizacje okresowe/konfiguracyjne/rotacyjne są
  odkładane przy zaległościach. Synchronizacja TXT co około 1 s nadal działa.
- SESSION używa tej samej pamięci co paczka SD, dopiero po zakończeniu
  synchronicznego `f_write`. Migawki liczników i roboczy tekst są w osobnych
  polach wspólnego obszaru. Ten obszar nie jest przekazywany do DMA; buforów
  SPI2 DMA nie zmieniono. Obsługa GPS podczas SD nie wywołuje zapisu plików.
- TFT pobiera migawki IMU/GPS najwyżej raz na sekundę. Co najmniej 100 ms
  dzieli porcje rysowania: najwyżej jeden wiersz IMU i jeden GPS oraz zmieniona
  stopka. Przy zaległościach ekran czeka. Błąd TFT wyłącza dalsze odświeżanie
  do restartu, lecz nie zatrzymuje akwizycji ani zapisu.

GPS pozostaje na 5 Hz / 38400 baud, IMU na natywnych 104 Hz. PPS, definicje
czasu pomiaru/odbioru, limit ważności modelu UTC oraz historia GPS nie zostały
wydłużone. Nie przeliczamy zegara przez założenie idealnej częstotliwości IMU.

## Nowe pola SESSION.json

| Pole | Znaczenie |
| --- | --- |
| `oldest_imu_age_us` | Wiek najstarszej próbki w surowym pierścieniu podczas ostatniej kontroli; 0 dla pustego pierścienia |
| `max_oldest_imu_age_us` | Maksimum zaobserwowane podczas tych kontroli; nie ciągły pomiar ISR |
| `sd_max_batch_records`, `sd_max_batch_bytes` | Największe przygotowane paczki przekazane do próby zapisu; sukces określają dotychczasowe liczniki |
| `snapshot_interval_ms` | Nominalny okres okresowych migawek JSON, 30000 ms |
| `utc_missing_anchor_records` | Przyjęte do kolejki SD rekordy bez UTC, ponieważ brak ważnej informacji NAV-TIMEUTC |
| `utc_missing_pps_records` | Rekordy bez UTC z dostępną informacją UTC, ale bez aktywnego modelu PPS |
| `utc_out_of_range_records` | Rekordy bez UTC, których czas wykracza poza dozwoloną ważność modelu/kotwicy UTC |

Liczniki UTC rosną jednokrotnie przy przyjęciu rekordu do kolejki SD,
nie przy ponownym formatowaniu. Nie obejmują próbek odrzuconych przed kolejką
ani braku SD; nie stanowią liczby potwierdzonych zapisów na kartę. Przy
jednoczesnym braku UTC i PPS pierwszeństwo ma `utc_missing_anchor_records`.
`UTC WAIT` na TFT dotyczy daty/godziny z odbiornika; `PPS?` obok godziny
sygnalizuje brak lock modelu. O wiarygodności czasu logu decydują jego pola.

## Pamięć i weryfikacja

| Kompilacja ARM GCC | Debug | Release |
| --- | ---: | ---: |
| Statyczny RAM | 16872 B | 16864 B |
| Pozostałe na stos/stertę | 3608 B | 3616 B |
| Rezerwacja stosu | 2560 B | 2560 B |
| Minimalna sterta | 512 B | 512 B |
| Zapas ponad rezerwacje linkera | 536 B | 544 B |
| Ramka stosu `Session_Write` | 464 B | 480 B |

W poprzednim Release ramka `Session_Write` miała 904 B. Formatter CSV w Debug
ma teraz ramkę 184 B. Są to pojedyncze ramki, nie suma zagnieżdżonych wywołań
z przerwaniami. Nie zastępują pomiaru `stack_free_min_bytes` na urządzeniu.
Pojemności pozostają: surowy pierścień 255 użytecznych rekordów po 32 B,
kolejka SD 16 rekordów po 96 B. Wspólny obszar paczki/SESSION ma 1024 B.

Testy ASan/UBSan obejmują m.in. dokładne wartości i granice liczb CSV,
ponawianie wierszy niepasujących do paczki, spójną migawkę podczas zmian
liczników w obsłudze SD, odraczanie JSON, rozróżnienie przyczyn braku UTC,
20 pauz SD po 1,163415 s z rzeczywistym kodem producenta ISR/DMA i konsumenta,
rotację, wyjmowanie karty i 30 minut zapisu w czasie symulowanym. W teście
pauz zachowano ciągłość po celowym odrzuceniu pierwszej pary DRDY.

Po wgraniu 2.2.1 należy sprawdzić długą sesję na tej samej karcie:
`imu_ring_dropped` powinno pozostać na początkowym 1, `sd_queue_dropped=0`,
`stack_free_min_bytes>0`, a zaległości powinny wracać w okolice 700 ms.
Porównać także `gps_history_misses`, nowe liczniki UTC i ciągłość NR w TXT.
Test programowy nie gwarantuje zachowania dowolnej fizycznej karty;
`timestamp_accuracy_validated` i `pps_reference_validated` pozostają false.
