// gcc -Os *.c -lxml2 -luv -lpcre -lcurl -o spider

#include "spider.h"

void process(spider_t *spider, char *buffer, size_t buffer_size, char *url, void *user_data)
{
	int opts = HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR |
			   HTML_PARSE_NOWARNING | HTML_PARSE_NONET;
	htmlDocPtr doc = htmlReadMemory(buffer, buffer_size, url, NULL, opts);
	if (!doc)
		return 0;
	xmlChar *xpath = (xmlChar *)"//a/@href";
	xmlXPathContextPtr context = xmlXPathNewContext(doc);
	xmlXPathObjectPtr result = xmlXPathEvalExpression(xpath, context);
	xmlXPathFreeContext(context);
	if (!result)
		return 0;
	xmlNodeSetPtr nodeset = result->nodesetval;
	if (xmlXPathNodeSetIsEmpty(nodeset))
	{
		xmlXPathFreeObject(result);
		return 0;
	}
	size_t count = 0;
	for (int i = 0; i < nodeset->nodeNr; i++)
	{
		double r = rand();
		int x = r * nodeset->nodeNr / RAND_MAX;
		const xmlNode *node = nodeset->nodeTab[x]->xmlChildrenNode;
		xmlChar *href = xmlNodeListGetString(doc, node, 1);
		if (1)
		{
			xmlChar *orig = href;
			href = xmlBuildURI(href, (xmlChar *)url);
			xmlFree(orig);
		}
		char *link = (char *)href;
		if (!link)
			continue;
		if (!strncmp(link, "http://", 7) || !strncmp(link, "https://", 8))
		{
			if (!bloom_check(spider->bloom, link))
			{
				// no exist
				bloom_add(spider->bloom, link);
				size_t len = strlen(link) + 1;
			
				uv_rwlock_wrlock(spider->lock);
				createTask(spider->task_queue, link);
				uv_rwlock_wrunlock(spider->lock);
				puts(link);
			}
		}
		xmlFree(link);
	}
	xmlXPathFreeObject(result);
	xmlFreeDoc(doc);
}

int main()
{
	char *agent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.10; rv:42.0) "
				  "Gecko/20100101 Firefox/42.0";
	setlocale(LC_ALL, "");

	spider_t *spider = spider_new();

	spider_setopt_url(spider, "https://www.nvshens.com");
	spider_setopt_baseurl(spider, "www.nvshens.com");
	spider_setopt_useragent(spider, agent);

	//指向自定义的解析函数
	spider_setopt_process(spider, process, NULL);

	//设置抓取线程数量，和数据持久化的线程数量
	spider_setopt_threadnum(spider, DOWNLOAD, 8);
	spider_setopt_threadnum(spider, SAVE, 8);
	// spider_setopt_logfile(spider, stdout);

	return spider_run(spider);
}
