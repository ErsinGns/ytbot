#ifndef TELEGRAM_H
#define TELEGRAM_H

#define TOKEN "Token"

struct mem {
    char *data;
    size_t size;
};

struct current_message{
    long long chat_id;
    char msg_text[256];
    char username[64];
};

int send_message(long long chat_id, const char *text);
char* http_get(const char *url);

#endif