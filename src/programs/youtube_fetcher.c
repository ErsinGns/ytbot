#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>
#include <time.h>

#include "db_core.h"
#include "rss_parser.h"
#include "logger.h"

// gcc src/programs/youtube_fetcher.c src/core/telegram_core.c src/core/db_core.c src/utils/logger.c src/core/rss_parser.c -Iinclude -I/usr/include/mysql -o build/youtube_fetcher $(pkg-config --cflags --libs libxml-2.0) -lmysqlclient -ljson-c -lcurl -pthread -lrt

int main()
{
    log_init("logs/youtube_fetcher.log", LOG_LEVEL_DEBUG);

    while (1)
    {
        const char *rss_url = "https://www.youtube.com/feeds/videos.xml?channel_id=";

        ChannelList *list = get_channels_list();
        if (list)
        {
            for (int i = 0; i < list->count; i++)
            {
                char full_rss_url[1024];
                snprintf(full_rss_url, sizeof(full_rss_url), "%s%s", rss_url, list->channels[i].channel_id);

                RSSFeedResult *rss_result = parse_rss_to_list(full_rss_url);
                if (rss_result)
                {

                    print_rss_feed_result(rss_result);

                    free_rss_feed_result(rss_result);
                }
            }
            free_channels_list(list);
        }
        else
        {
            log_error("Kanal listesi alınamadı veya boş.");
        }
        sleep(60);
    }
    return 0;
}