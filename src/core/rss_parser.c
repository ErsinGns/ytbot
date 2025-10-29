#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include "db_core.h"
#include "rss_parser.h"
#include "logger.h"

struct MemoryStruct {
    char *memory;
    size_t size;
};

typedef struct {
    char *video_id;
    char *title;
    char *published_at;
    char *video_url;
} VideoInfo;

static char* fetch_rss(const char* url);
static xmlChar* parse_channel_info(xmlNode *root);
static void parse_entries(xmlChar *channel_id, xmlNode *root);
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);
static void parse_channel_info_to_struct(xmlNode *node, RSSChannel *channel);
static int count_videos(xmlNode *node);
static void parse_entries_to_list(xmlNode *node, RSSVideoList *video_list);

void list_rss(const char* url) {
    RSSFeedResult *result = parse_rss_to_list(url);
    if (result) {
        free_rss_feed_result(result);
    } else {
        log_error("RSS verisi alınamadı.");
    }
}

char* fetch_rss(const char* url) {
    CURL *curl_handle;
    CURLcode res;
    struct MemoryStruct chunk;

    chunk.memory = malloc(1);
    chunk.size = 0;

    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "C_RSS_Reader/1.0");

    res = curl_easy_perform(curl_handle);

    if(res != CURLE_OK) {
        log_error("RSS verisi alınamadı: %s", curl_easy_strerror(res));
        free(chunk.memory);
        chunk.memory = NULL;
    }

    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();

    return chunk.memory;
}


void parse_rss(char *xml_data) {
    xmlDocPtr doc = xmlReadMemory(xml_data, strlen(xml_data), "noname.xml", NULL, 0);
    if (doc == NULL) {
        log_error("XML parse hatası!");
        free(xml_data);
        return;
    }

    xmlNode *root = xmlDocGetRootElement(doc);
    if (root == NULL) {
        log_error("Boş XML!");
        xmlFreeDoc(doc);
        free(xml_data);
        return;
    }

    xmlChar *channel_id = parse_channel_info(root);
    parse_entries(channel_id, root); 

    if (channel_id) xmlFree(channel_id);
    xmlFreeDoc(doc);
    xmlCleanupParser();
    free(xml_data);
}

xmlChar* parse_channel_info(xmlNode *node) {
    xmlChar *channel_id = NULL, *title = NULL, *href = NULL;
    for (xmlNode *cur = node->children; cur; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE) {
            if (xmlStrcmp(cur->name, (const xmlChar *)"title") == 0) {
                title = xmlNodeGetContent(cur);
            }
            else if (xmlStrcmp(cur->name, (const xmlChar *)"link") == 0) {
                xmlChar *rel = xmlGetProp(cur, (const xmlChar *)"rel");
                if (rel && xmlStrcmp(rel, (const xmlChar *)"alternate") == 0) {
                    href = xmlGetProp(cur, (const xmlChar *)"href");
                }
                if (rel) xmlFree(rel);
            }
            else if (xmlStrcmp(cur->name, (const xmlChar *)"channelId") == 0 ||
                     xmlStrcmp(cur->name, (const xmlChar *)"yt:channelId") == 0) {
                channel_id = xmlNodeGetContent(cur);
                    snprintf(channel_id, sizeof(channel_id), "UC%s", (char*)channel_id);
            }
        }
    }
    insert_channel((char*)channel_id, (char*)title, (char*)href);
    log_info("Kanal bilgileri veritabanına eklendi.");

    if (title) xmlFree(title);
    if (href) xmlFree(href);
    return channel_id;
}

void parse_entries(xmlChar *channel_id, xmlNode *node) {
    for (xmlNode *cur = node->children; cur; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE &&
            xmlStrcmp(cur->name, (const xmlChar *)"entry") == 0) {

            VideoInfo video = {NULL, NULL, NULL, NULL};

            for (xmlNode *c = cur->children; c; c = c->next) {
                if (c->type != XML_ELEMENT_NODE) continue;

                if (xmlStrcmp(c->name, (const xmlChar *)"title") == 0) {
                    xmlChar *t = xmlNodeGetContent(c);
                    if (t) { video.title = strdup((char*)t); xmlFree(t); }
                }
                else if (xmlStrcmp(c->name, (const xmlChar *)"link") == 0) {
                    xmlChar *href = xmlGetProp(c, (const xmlChar *)"href");
                    if (href) { video.video_url = strdup((char*)href); xmlFree(href); }
                }
                else if (xmlStrcmp(c->name, (const xmlChar *)"published") == 0) {
                    xmlChar *p = xmlNodeGetContent(c);
                    if (p) { video.published_at = strdup((char*)p); xmlFree(p); }
                }
                else if (xmlStrcmp(c->name, (const xmlChar *)"yt:videoId") == 0 || xmlStrcmp(c->name, (const xmlChar *)"id") == 0) {
                    xmlChar *v = xmlNodeGetContent(c);
                    if (v) { video.video_id = strdup((char*)v); xmlFree(v); }
                }
            }
            int new_video_id = insert_video_and_create_status((char*)channel_id, video.video_id, video.title, video.published_at, video.video_url);
            if (new_video_id > 0) {
                log_info("Video başarıyla eklendi ve kullanıcı durumları oluşturuldu (Video ID: %d)", new_video_id);
            }
            free(video.video_id);
            free(video.title);
            free(video.published_at);
            free(video.video_url);
        }
    }
}

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(ptr == NULL) {
        log_error("Yetersiz bellek!");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

// RSS feed'i liste olarak parse eden fonksiyon
RSSFeedResult* parse_rss_to_list(const char* url) {
    char* rss_data = fetch_rss(url);
    if (!rss_data) {
        return NULL;
    }

    xmlDocPtr doc = xmlReadMemory(rss_data, strlen(rss_data), "noname.xml", NULL, 0);
    if (doc == NULL) {
        log_error("XML parse hatası!");
        free(rss_data);
        return NULL;
    }

    xmlNode *root = xmlDocGetRootElement(doc);
    if (root == NULL) {
        log_error("Boş XML!");
        xmlFreeDoc(doc);
        free(rss_data);
        return NULL;
    }

    RSSFeedResult *result = (RSSFeedResult*)malloc(sizeof(RSSFeedResult));
    if (!result) {
        log_error("Bellek tahsisi hatası");
        xmlFreeDoc(doc);
        free(rss_data);
        return NULL;
    }

    memset(result, 0, sizeof(RSSFeedResult));
    result->video_list.videos = NULL;
    result->video_list.count = 0;

    parse_channel_info_to_struct(root, &result->channel);
    
    int video_count = count_videos(root);
    
    if (video_count > 0) {
        result->video_list.videos = (RSSVideo*)malloc(sizeof(RSSVideo) * video_count);
        if (result->video_list.videos) {
            result->video_list.count = video_count;
            parse_entries_to_list(root, &result->video_list);
        }
    }
    insert_channel(result->channel.channel_id, result->channel.title, result->channel.link);

    xmlFreeDoc(doc);
    xmlCleanupParser();
    free(rss_data);
    
    return result;
}

// Kanal bilgilerini struct'a parse eden yardımcı fonksiyon
static void parse_channel_info_to_struct(xmlNode *node, RSSChannel *channel) {
    for (xmlNode *cur = node->children; cur; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE) {
            if (xmlStrcmp(cur->name, (const xmlChar *)"title") == 0) {
                xmlChar *title = xmlNodeGetContent(cur);
                if (title) {
                    strncpy(channel->title, (char*)title, sizeof(channel->title) - 1);
                    channel->title[sizeof(channel->title) - 1] = '\0';
                    xmlFree(title);
                }
            }
            else if (xmlStrcmp(cur->name, (const xmlChar *)"link") == 0) {
                xmlChar *rel = xmlGetProp(cur, (const xmlChar *)"rel");
                if (rel && xmlStrcmp(rel, (const xmlChar *)"alternate") == 0) {
                    xmlChar *href = xmlGetProp(cur, (const xmlChar *)"href");
                    if (href) {
                        strncpy(channel->link, (char*)href, sizeof(channel->link) - 1);
                        channel->link[sizeof(channel->link) - 1] = '\0';
                        xmlFree(href);
                    }
                }
                if (rel) xmlFree(rel);
            }
            else if (xmlStrcmp(cur->name, (const xmlChar *)"channelId") == 0 ||
                     xmlStrcmp(cur->name, (const xmlChar *)"yt:channelId") == 0) {
                xmlChar *channel_id = xmlNodeGetContent(cur);
                if (channel_id) {
                    snprintf(channel->channel_id, sizeof(channel->channel_id), "UC%s", (char*)channel_id);
                    xmlFree(channel_id);
                }
            }
        }
    }
}

// Video sayısını sayan yardımcı fonksiyon
static int count_videos(xmlNode *node) {
    int count = 0;
    for (xmlNode *cur = node->children; cur; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE &&
            xmlStrcmp(cur->name, (const xmlChar *)"entry") == 0) {
            count++;
        }
    }
    return count;
}

// Video listesini parse eden fonksiyon
static void parse_entries_to_list(xmlNode *node, RSSVideoList *video_list) {
    int index = 0;
    
    for (xmlNode *cur = node->children; cur; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE &&
            xmlStrcmp(cur->name, (const xmlChar *)"entry") == 0 &&
            index < video_list->count) {

            RSSVideo *video = &video_list->videos[index];
            memset(video, 0, sizeof(RSSVideo));

            for (xmlNode *c = cur->children; c; c = c->next) {
                if (c->type != XML_ELEMENT_NODE) continue;

                if (xmlStrcmp(c->name, (const xmlChar *)"title") == 0) {
                    xmlChar *title = xmlNodeGetContent(c);
                    if (title) {
                        strncpy(video->title, (char*)title, sizeof(video->title) - 1);
                        video->title[sizeof(video->title) - 1] = '\0';
                        xmlFree(title);
                    }
                }
                else if (xmlStrcmp(c->name, (const xmlChar *)"link") == 0) {
                    xmlChar *href = xmlGetProp(c, (const xmlChar *)"href");
                    if (href) {
                        strncpy(video->video_url, (char*)href, sizeof(video->video_url) - 1);
                        video->video_url[sizeof(video->video_url) - 1] = '\0';
                        xmlFree(href);
                    }
                }
                else if (xmlStrcmp(c->name, (const xmlChar *)"published") == 0) {
                    xmlChar *published = xmlNodeGetContent(c);
                    if (published) {
                        strncpy(video->published_at, (char*)published, sizeof(video->published_at) - 1);
                        video->published_at[sizeof(video->published_at) - 1] = '\0';
                        xmlFree(published);
                    }
                }
                else if (xmlStrcmp(c->name, (const xmlChar *)"yt:videoId") == 0 || 
                         xmlStrcmp(c->name, (const xmlChar *)"id") == 0) {
                    xmlChar *video_id = xmlNodeGetContent(c);
                    if (video_id) {
                        strncpy(video->video_id, (char*)video_id, sizeof(video->video_id) - 1);
                        video->video_id[sizeof(video->video_id) - 1] = '\0';
                        xmlFree(video_id);
                    }
                }
            }
            
            index++;
        }
    }
    
    video_list->count = index;
}

void free_rss_feed_result(RSSFeedResult *result) {
    if (result) {
        if (result->video_list.videos) {
            free(result->video_list.videos);
        }
        free(result);
        log_info("RSS feed sonucu belleği serbest bırakıldı.");
    }
}

void print_rss_feed_result(RSSFeedResult *result) {
    if (!result) {
        log_error("Geçersiz RSS feed sonucu!");
        return;
    }

    for (int i = 0; i < result->video_list.count; i++) {
        RSSVideo *video = &result->video_list.videos[i];
        int new_video_id = insert_video_and_create_status(result->channel.channel_id, video->video_id, video->title, 
                    video->published_at, video->video_url);
        if (new_video_id > 0) {
            log_info("Video eklendi ve kullanıcı durumları oluşturuldu (Video ID: %d)", new_video_id);
        }
    }

    log_info("Kanal ve video bilgileri veritabanına eklendi.");
}
