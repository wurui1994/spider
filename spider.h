#ifndef SPIDER_H
#define SPIDER_H

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <locale.h>

#include <curl/curl.h>

#include <libxml/uri.h>
#include <libxml/xpath.h>
#include <libxml/HTMLparser.h>

#include <string>
#include <vector>
#include <iostream>
#include <memory>
#include <chrono>
#include <future>
#include <deque>
#include <algorithm>
#include <condition_variable>

#define UNUSED(x) (void)(x)

#define _DEBUG 0

#define USE_CURL_MULTI 0 // just a test

// typedef
typedef struct site site_t;

typedef unsigned int (*hashfunc_t)(const char *);
typedef struct bloom_struct bloom_t;

struct site
{
	std::string base_url;
	std::string user_agent; // user agent
	std::string proxy;		// proxy address
	std::string cookie;		// cookie string
	long timeout;			// timeout (ms)
};

struct bloom_struct
{
	size_t asize;	   // bloom filter's bit array's size
	unsigned char *a;  // bit array
	size_t nfuncs;	   // hash function's number
	hashfunc_t *funcs; // array of hash function
};

// hash functions
unsigned int sax_hash(char *key);
unsigned int sdbm_hash(char *key);

// bloom filter
bloom_t *bloom_new(void);
int bloom_check(bloom_t *bloom, const char *s);
int bloom_add(bloom_t *bloom, const char *s);
int bloom_destroy(bloom_t *bloom);

namespace StringUtil
{
	void putsString(const std::string &s);
	bool startWith(const std::string &s, const std::string &head);
	bool endWith(const std::string &s, const std::string &tail);
	bool contains(const std::string &s, const std::string &sub);
}

struct Request
{
	std::vector<unsigned char> buffer;
	std::string url; /* the url where it is downloaded */
};

typedef struct _Node
{
	std::shared_ptr<_Node> next; /* next node */
	std::shared_ptr<_Node> prev; /* previous node */
	std::shared_ptr<Request> m_data;
} Node;

class Timer
{
public:
	Timer();
	~Timer();

private:
	std::chrono::steady_clock::time_point st;
	std::chrono::steady_clock::time_point et;
};

class Downloader
{
public:
	Downloader();
	void setSite(site_t site);
	void setTask(std::shared_ptr<Request> task);
	void runTask();
	CURL *curlHandle();
	std::shared_ptr<Request> task();
	~Downloader();

protected:
	static size_t writeCallback(void *contents, size_t size, size_t nmemb, void *ss);

private:
	CURL *curl;
	CURLcode res;
	site_t m_site;
	std::shared_ptr<Request> m_task;
};

#if USE_CURL_MULTI
class Spider;

class Curl_Multi
{
public:
	Curl_Multi();
	~Curl_Multi();
	void setSpider(Spider *spider);
	void RunTask();
	void addCurlHandle(CURL *handle);
	void check_multi_info();
	void setRunning(bool isRunning);
	int handleCount();

private:
	int msgs_left;
	int still_running;
	bool m_isRunning;
	CURLM *curl_handle;
	Spider *m_spider;
};
#endif

class Spider
{
public:
	Spider();
	~Spider();
	int RunLoop();
	void Stop();

public:
	void setUrl(const char *url);
	void setBaseUrl(const char *url);
	void setCookie(const char *cookie);
	void setUserAgent(const char *agent);
	void setProxy(const char *proxy);
	void setTimeout(long timeout);

	std::string &baseUrl();

	void download(std::shared_ptr<Request> task);

	struct bloom_struct *bloom();

	void createTask(const char *url);

	void process(std::shared_ptr<Request> text);

	std::shared_ptr<Request> findTaskByCurlHandle(CURL *handle);

protected:
	void processTask();

private:
	// task queue
	std::deque<std::shared_ptr<Request>> task_queue;

	// include useragent, cookie, timeout, proxy
	site_t site;

	// bloom filter
	struct bloom_struct *m_bloom;

	std::vector<std::string> urls;

	bool m_stop;

	Downloader m_downloader;

	int url_count = 0;

	std::vector<std::shared_ptr<Downloader>> m_downloaders;
#if USE_CURL_MULTI
	std::vector<std::shared_ptr<Curl_Multi>> m_curl_multi;
#endif
	std::mutex mutex;
	std::condition_variable m_loop_cv;
};

#endif
