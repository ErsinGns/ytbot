#ifndef DB_CORE_H
#define DB_CORE_H

#include <mysql/mysql.h>

#define DB_HOST "localhost"
#define DB_USER "root"
#define DB_PASS "password"
#define DB_NAME "telegram_youtube_bot"
#define DB_PORT 3306

typedef struct
{
    char channel_id[256];
    char name[256];
    char link[512];
    char created_at[32];
} Channel;

typedef struct
{
    Channel *channels;
    int count;
} ChannelList;

typedef struct
{
    long long user_chat_id;
    char channel_id[256];
    char subscribed_at[32];
} UserChannel;

typedef struct
{
    UserChannel *user_channels;
    int count;
} UserChannelList;

void insert_user(long long chat_id, const char *username);
void insert_channel(const char *channel_id, const char *name, const char *link);
void user_channel_subscribe(long long chat_id, const char *channel_id);
int insert_video_and_create_status(const char *channel_id, const char *video_id, const char *title,
                                   const char *published_at, const char *video_url);
void get_unsent_videos_for_user(long long chat_id);
void get_all_unsent_videos();
ChannelList *get_channels_list();
void free_channels_list(ChannelList *list);
void create_database();

void mark_video_as_sent(long long chat_id, int video_id);
void send_unsent_videos_to_users();

#endif