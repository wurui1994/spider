#include "spider.h"

char *joinUrl(char *baseuri, char *reluri)
{
    int size = strlen(baseuri) + strlen(reluri) + 1;
    char *res = (char*)malloc(size);
    memset(res,0,size);
    if (strncmp(reluri,"http",4) == 0)
    {
        strcpy(res,reluri);
    }
    else
    {
        strcpy(res,baseuri);
        strcat(res,reluri);
    }
    return res;
}

void rstrip(char *string)
{
    int l;
    if (!string)
        return;
    l = strlen(string) - 1;
    while (isspace(string[l]) && l >= 0)
        string[l--] = 0;
}

void lstrip(char *string)
{
    int i, l;
    if (!string)
        return;
    l = strlen(string);
    while (isspace(string[(i = 0)]))
        while (i++ < l)
            string[i - 1] = string[i];
}

void joinUrls(char *baseuri, char **uris, int size)
{
    int i;
    char *parsed = NULL;

    for (i = 0; i < size; i++)
    {
        lstrip(uris[i]);
        rstrip(uris[i]);
        parsed = joinUrl(baseuri, uris[i]);
        if (parsed == NULL)
        {
            continue;
        }
        free(uris[i]);
        uris[i] = NULL;
//        puts(parsed);
        uris[i] = parsed;
        parsed = NULL;
    }
}

void process(spider_t *cspider, char *d, char *url, void *user_data)
{
    xpath_result_t result = xpath(d, "//a/@href");
    char **get = result.urls;
    int size = result.size;
    //  int size = regexAll("http:\\/\\/(.*?)\\.html", d, get, 3, REGEX_ALL);
    if(cspider->site->base_url)
        url = cspider->site->base_url;

    joinUrls(url, get, size);
    addUrls(cspider, get, size);
    saveStrings(cspider, (void **)get, size, LOCK);
    freeStrings(get, size);
    free(get);
}

void save(void *str, void *user_data)
{
    char *get = (char *)str;
    FILE *file = (FILE *)user_data;
    fprintf(file, "%s saved\n", get);
    return;
}

int main()
{
    char *agent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.10; rv:42.0) "
                  "Gecko/20100101 Firefox/42.0";
    setlocale(LC_ALL, "");

    spider_t *spider = spider_new();

    spider_setopt_url(spider, "www.nvshens.com");
    spider_setopt_baseurl(spider, "www.nvshens.com");
    spider_setopt_useragent(spider, agent);

    //指向自定义的解析函数，和数据持久化函数
    spider_setopt_process(spider, process, NULL);
    spider_setopt_save(spider, save, stdout);

    //设置抓取线程数量，和数据持久化的线程数量
    spider_setopt_threadnum(spider, DOWNLOAD, 2);
    spider_setopt_threadnum(spider, SAVE, 2);
//    spider_setopt_logfile(spider, stdout);

    return spider_run(spider);
}
