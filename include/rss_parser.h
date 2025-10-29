#ifndef RSS_PARSER_H
#define RSS_PARSER_H


typedef struct {
    char video_id[100];
    char title[500];
    char published_at[50];
    char video_url[300];
} RSSVideo;

typedef struct {
    RSSVideo *videos;
    int count;
} RSSVideoList;

typedef struct {
    char channel_id[100];
    char title[300];
    char link[300];
} RSSChannel;

typedef struct {
    RSSChannel channel;
    RSSVideoList video_list;
} RSSFeedResult;

void list_rss(const char* url);
RSSFeedResult* parse_rss_to_list(const char* url);
void free_rss_feed_result(RSSFeedResult *result);
void print_rss_feed_result(RSSFeedResult *result);

#endif