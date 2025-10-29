
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <time.h>
#include <unistd.h>

#include "telegram_core.h"
#include "db_core.h"
#include "rss_parser.h"
#include "logger.h"

// gcc src/programs/notifier.c src/core/telegram_core.c src/core/db_core.c src/core/rss_parser.c src/utils/logger.c -o build/notifier -lcurl -ljson-c -I/usr/include/mysql -lmysqlclient $(pkg-config --cflags --libs libxml-2.0) -Iinclude

int main()
{
    log_init("logs/notifier.log", LOG_LEVEL_DEBUG);

    while (1)
    {

        get_all_unsent_videos();
        log_info("Gönderilmemiş videolar Telegram'a gönderiliyor.");
        send_unsent_videos_to_users();

        sleep(60);
    }
    return 0;
}
