#include "spider.h"

namespace StringUtil
{
	void putsString(const std::string &s)
	{
		std::cout << s << std::endl;
	}

	bool startWith(const std::string &s, const std::string &head)
	{
		return s.compare(0, head.size(), head);
	}

	bool endWith(const std::string &s, const std::string &tail)
	{
		return s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
	}

	bool contains(const std::string &s, const std::string &sub)
	{
		return s.find(sub) != std::string::npos;
	}
}

Timer::Timer()
{
	st = std::chrono::steady_clock::now();
}

Timer::~Timer()
{
	et = std::chrono::steady_clock::now();
	std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(et - st);
	double cost_time = time_span.count();
	printf("%f\n", cost_time);
}

Downloader::Downloader()
{
#if USE_MUTEX_LOCK
	m_isRunning = false;
#endif
	curl = curl_easy_init();
}

void Downloader::setSite(site_t site)
{
	m_site = site;

	if (!m_site.user_agent.empty())
		curl_easy_setopt(curl, CURLOPT_USERAGENT, m_site.user_agent.c_str());

	if (!m_site.proxy.empty())
		curl_easy_setopt(curl, CURLOPT_PROXY, m_site.proxy.c_str());

	if (m_site.timeout != 0)
		curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, m_site.timeout);

	if (!m_site.cookie.empty())
		curl_easy_setopt(curl, CURLOPT_COOKIE, m_site.cookie.c_str());

	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
}

size_t Downloader::writeCallback(void *contents, size_t size, size_t nmemb, void *ss)
{
	Request *save = (Request *)ss;
	size_t realsize = size * nmemb;
	unsigned char *data = (unsigned char *)contents;
	save->buffer.insert(save->buffer.end(), data, data + realsize);
	return realsize;
}

void Downloader::setTask(std::shared_ptr<Request> task)
{
	m_task = task;
#if _DEBUG
	StringUtil::putsString(task->url);
	Timer curlTimer;
#endif

#if USE_MUTEX_LOCK
	m_isRunning = true;
	std::lock_guard<std::mutex> lock(m_mutex);
#endif

	/*支持重定向*/
	/*support redirection*/
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);

	curl_easy_setopt(curl, CURLOPT_URL, task->url.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, task.get());
}

void Downloader::runTask()
{
	res = curl_easy_perform(curl);
	if (res != CURLE_OK)
	{
		std::cout << "curl return error code = " << res << "\t" << this << std::endl;
	}
	int res_status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res_status);
	if (res_status != 200)
	{
		printf("HTTP %d: %s\n", (int)res_status, m_task->url.c_str());
	}
}

CURL *Downloader::curlHandle()
{
	return curl;
}

std::shared_ptr<Request> Downloader::task()
{
	return m_task;
}

Downloader::~Downloader()
{
	if (curl)
		curl_easy_cleanup(curl);
}

#if USE_CURL_MULTI
Curl_Multi::Curl_Multi()
{
	const int max_con = 32;
	curl_handle = curl_multi_init();
	curl_multi_setopt(curl_handle, CURLMOPT_MAX_TOTAL_CONNECTIONS, max_con);
	curl_multi_setopt(curl_handle, CURLMOPT_MAX_HOST_CONNECTIONS, 6L);

	/* enables http/2 if available */
#ifdef CURLPIPE_MULTIPLEX
	curl_multi_setopt(curl_handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
#endif

	/* Limit the amount of simultaneous connections curl should allow: */
	curl_multi_setopt(curl_handle, CURLMOPT_MAXCONNECTS, (long)max_con);
}

Curl_Multi::~Curl_Multi()
{
	curl_multi_cleanup(curl_handle);
}

void Curl_Multi::RunTask()
{
	m_isRunning = true;
	std::thread task(
		[&]()
		{
			check_multi_info();
		});
	task.detach();
}

void Curl_Multi::setSpider(Spider *spider)
{
	m_spider = spider;
}

void Curl_Multi::addCurlHandle(CURL *handle)
{
	curl_multi_add_handle(curl_handle, handle);
}

int is_html(char *ctype)
{
	return ctype != NULL && strlen(ctype) > 10 && strstr(ctype, "text/html");
}

void Curl_Multi::check_multi_info(void)
{
	int complete = 0;
	while (m_isRunning)
	{
		int numfds;
		curl_multi_wait(curl_handle, NULL, 0, 1000, &numfds);
		curl_multi_perform(curl_handle, &still_running);

		/* See how the transfers went */
		CURLMsg *m = NULL;
		while ((m = curl_multi_info_read(curl_handle, &msgs_left)))
		{
			printf("still_running=%d\n", still_running);
			if (m->msg != CURLMSG_DONE)
				continue;
			CURL *handle = m->easy_handle;
			char *url;
			curl_easy_getinfo(handle, CURLINFO_EFFECTIVE_URL, &url);
			if (m->data.result != CURLE_OK)
			{
				printf("[%d] Connection failure: %s %d\n", complete, url, m->data.result);
				continue;
			}
			long res_status;
			curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &res_status);
			if (res_status != 200)
			{
				printf("[%d] HTTP %d: %s\n", complete, (int)res_status, url);
				continue;
			}
			char *ctype;
			curl_easy_getinfo(handle, CURLINFO_CONTENT_TYPE, &ctype);
			// printf("[%d] HTTP 200 (%s): %s\n", complete, ctype, url);
			if (is_html(ctype))
			{
				auto task = m_spider->findTaskByCurlHandle(handle);
				m_spider->process(task);
			}

			curl_multi_remove_handle(curl_handle, handle);
		}
		std::this_thread::sleep_for(std::chrono::microseconds(1000));
	}
}

void Curl_Multi::setRunning(bool isRunning)
{
	m_isRunning = isRunning;
}

int Curl_Multi::handleCount()
{
	return still_running;
}
#endif

void Spider::createTask(const char *url)
{
	std::shared_ptr<Request> task = std::make_shared<Request>();
	task->url = url;
	// buffer不清除 行为未定义 不如size的值
	task->buffer.clear();
	task_queue.push_back(task);
}

std::vector<std::string>
htmlParser(std::vector<unsigned char> const &buffer, std::string const &url, std::string const &xpath)
{
	std::vector<std::string> urls;

	if (buffer.empty())
		return urls;

	int opts = HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR |
			   HTML_PARSE_NOWARNING | HTML_PARSE_NONET;
	htmlDocPtr doc = htmlReadMemory((const char *)buffer.data(), buffer.size(), url.c_str(), NULL, opts);
	if (!doc)
		return std::move(urls);
	xmlXPathContextPtr context = xmlXPathNewContext(doc);
	xmlXPathObjectPtr result = xmlXPathEvalExpression((xmlChar *)xpath.c_str(), context);
	xmlXPathFreeContext(context);
	if (!result)
		return std::move(urls);
	xmlNodeSetPtr nodeset = result->nodesetval;
	if (xmlXPathNodeSetIsEmpty(nodeset))
	{
		xmlXPathFreeObject(result);
		return std::move(urls);
	}
	for (int i = 0; i < nodeset->nodeNr; i++)
	{
		const xmlNode *node = nodeset->nodeTab[i]->xmlChildrenNode;
		xmlChar *href = xmlNodeListGetString(doc, node, 1);
		href = xmlBuildURI(href, (xmlChar *)url.c_str());
		char *link = (char *)href;
		if (!link)
			continue;

		urls.push_back(link);

		xmlFree(link);
	}
	xmlXPathFreeObject(result);
	xmlFreeDoc(doc);
	return std::move(urls);
}

void Spider::process(std::shared_ptr<Request> text)
{
	urls.push_back(text->url);
	printf("%d \n", ++url_count);

	std::vector<std::string> urls = htmlParser(text->buffer, text->url, "//a/@href");
	printf("urls count = %ld \n", urls.size());
	for (std::string const &url : urls)
	{
		const char *link = url.c_str();
		if (StringUtil::startWith(link, "http://") || StringUtil::startWith(link, "https://"))
		{
			if (StringUtil::contains(link, this->baseUrl()) && !bloom_check(this->bloom(), link))
			{
				// no exist
				bloom_add(this->bloom(), link);
				size_t len = strlen(link) + 1;
				this->createTask(link);
			}
		}
	}
	m_loop_cv.notify_one();
}

std::shared_ptr<Request> Spider::findTaskByCurlHandle(CURL *handle)
{
	std::shared_ptr<Downloader> downloader;
	for (std::shared_ptr<Downloader> const &d : m_downloaders)
	{
		if (d && d->curlHandle() == handle)
		{
			return d->task();
		}
	}
	return nullptr;
}

Spider::Spider()
{
	m_bloom = bloom_new();
	site.timeout = 0; // ms
	m_stop = false;
	task_queue.clear();
}

Spider::~Spider()
{
}

struct bloom_struct *Spider::bloom()
{
	return m_bloom;
}

int Spider::RunLoop()
{
	processTask();
	while (!m_stop)
	{
		std::unique_lock<std::mutex> lock(mutex);
		m_loop_cv.wait(lock, [this]
					   { return m_stop || !task_queue.empty(); });
		if (task_queue.empty())
			return 0;
		processTask();
	}
	return 0;
}

void Spider::Stop()
{
#if USE_CURL_MULTI
	for (std::shared_ptr<Curl_Multi> const &d : m_curl_multi)
	{
		d->setRunning(false);
	}
#endif
	m_stop = true;
	task_queue.clear();
}

void Spider::processTask()
{

	// task_queue back maybe empty nullptr, crash when push_back [thread race]
	std::shared_ptr<Request> task = task_queue.front();
	task_queue.pop_front();
	if (task)
		download(task);

#ifndef NDEBUG
	if (urls.size() >= 128)
	{
		for (const std::string &s : urls)
		{
			StringUtil::putsString(s);
		}
		this->Stop();
	}
#endif
}

std::string &Spider::baseUrl()
{
	return site.base_url;
}

void Spider::download(std::shared_ptr<Request> task)
{
#if USE_CURL_MULTI
	std::shared_ptr<Downloader> downloader = std::make_shared<Downloader>();
	downloader->setSite(site);
	downloader->setTask(task);
	m_downloaders.push_back(downloader);

	std::shared_ptr<Curl_Multi> curl_multi;
	bool isAdded = false;
	for (std::shared_ptr<Curl_Multi> const &d : m_curl_multi)
	{
		if (d && d->handleCount() < 16)
		{
			curl_multi = d;
			isAdded = true;
			break;
		}
	}

	if (curl_multi)
	{
		curl_multi->addCurlHandle(downloader->curlHandle());
	}
	else
	{
		curl_multi = std::make_shared<Curl_Multi>();
		curl_multi->setSpider(this);
		curl_multi->addCurlHandle(downloader->curlHandle());
		curl_multi->RunTask();
		m_curl_multi.push_back(curl_multi);
	}

#else
	std::thread worker(
		[=]()
		{
			Downloader downloader;
			downloader.setSite(site);
			downloader.setTask(task);
			downloader.runTask();
			process(task);
		});
	worker.detach();
#endif
}

void Spider::setUrl(const char *url)
{
	if (!bloom_check(bloom(), url))
	{
		// url no exits
		bloom_add(bloom(), url);
		createTask(url);
	}
}

void Spider::setBaseUrl(const char *url)
{
	site.base_url = url;
}

void Spider::setCookie(const char *cookie)
{
	site.cookie = cookie;
}

void Spider::setUserAgent(const char *agent)
{
	site.user_agent = agent;
}

void Spider::setProxy(const char *proxy)
{
	site.proxy = proxy;
}

void Spider::setTimeout(long timeout)
{
	site.timeout = timeout;
}
