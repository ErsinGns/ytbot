
# ytbot — YouTube / Telegram bildirim aracı

Bu proje, YouTube kaynaklı içerik haberlerini toplayıp Telegram üzerinden bildirim/işleme yapan küçük, C ile yazılmış bir araç setidir. Proje; RSS/parsing, veritabanı yazma ve Telegram ile haberleşme gibi bileşenleri içerir.

## Öne çıkan bileşenler

- `src/core/` — Projenin çekirdek kaynakları:
	- `db_core.c` — basit veritabanı/kalıcılık yardımcıları
	- `rss_parser.c` — RSS öğelerini ayrıştırma
	- `telegram_core.c` — Telegram ile iletişim yardımcıları
- `src/programs/` — Derlenebilir programlar:
	- `notifier.c` — Bildirim gönderme
	- `telegram_db_writer.c` — Telegram üzerinden gelen verileri veritabanına yazma
	- `telegram_listener.c` — Telegram botu için dinleyici
	- `youtube_fetcher.c` — YouTube/RSS kaynaklarını periyodik olarak çekme
- `include/` — Proje başlık dosyaları
- `utils/` — Yardımcı kodlar
- `build/` — Derleme çıktılarını beklenen dizin
- `logs/` — Çalışma zamanında oluşturulan log dosyaları

## Gereksinimler

- C derleyicisi (GCC/Clang) ve `make`.
- Linux için en sorunsuz deneyim; Windows'ta derlemek ve çalıştırmak için WSL önerilir.


## Derleme

1. Kaynak dizinine gidin:

```powershell
cd \ytbot
```
2. Düzeltin
 include/telegram_core.h
```powershell
#define TOKEN "Token"
```
 ve 
 include/db_core.h içerisini düzeltin
 ```powershell
    #define DB_HOST "localhost"
    #define DB_USER "root"
    #define DB_PASS "password"
    #define DB_NAME "telegram_youtube_bot"
    #define DB_PORT 3306
```
3. Derleyin ve çalıştırın:

```powershell
make run
```
