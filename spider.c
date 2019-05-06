#include "spider.h"

int isDataQueueEmpty(data_queue_t *head)
{
    return (head->next == head);
}

data_queue_t *initDataQueue()
{
    data_queue_t *queue = (data_queue_t *)malloc(sizeof(data_queue_t));
    assert(queue);

    queue->next = queue;
    queue->prev = queue;
    queue->data = NULL;
    return queue;
}

data_t *createData()
{
    data_t *node = (data_t *)malloc(sizeof(data_t));
    assert(node);

    node->buffer_size = 0;

    return node;
}

void addData(data_queue_t *head, data_queue_t *queue)
{
    queue->next = head;
    queue->prev = head->prev;
    queue->next->prev = queue;
    queue->prev->next = queue;
    return;
}

data_queue_t *removeData(data_queue_t *head, data_t *data)
{
    data_queue_t *p = head->next;
    data_queue_t *res = NULL;

    while (p != head)
    {
        if (p->data == data)
        {
            res = p;
            p->prev->next = p->next;
            p->next->prev = p->prev;
            break;
        }
        else
        {
            p = p->next;
        }
    }

    return res;
}

void freeData(data_queue_t *node)
{
    free(node->data->worker);
    free(node->data->buffer);
    free(node->data->url);
    free(node->data);
    free(node);
}

void process_data(uv_work_t *work)
{
    spider_t *spider = ((data_t *)work->data)->spider;
    data_t *text = (data_t *)work->data;
    
    (spider->process)(spider, text->buffer, text->buffer_size, text->url, spider->process_user_data);
}

void datasave(uv_work_t *req, int status)
{
    UNUSED(status);
    spider_t *spider = ((data_t *)req->data)->spider;
    // log
    logger(0, "%s save finish.\n", ((data_t *)req->data)->url, spider);

    uv_rwlock_wrlock(spider->lock);
    spider->pipeline_thread--;
    data_queue_t *q = removeData(spider->data_queue_doing, req->data);
    assert(q);

    logger(q != NULL, "removeData error in %s.\n", "dataProcess.c", spider);
    freeData(q);
    uv_rwlock_wrunlock(spider->lock);
}

/*
  provide two functions to users.
  saveString : save the string.
  addUrl : add the url back to the download task queue.
*/


void freeString(char *str)
{
    free(str);
}

void freeStrings(char **strs, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        free(strs[i]);
    }
}

/**
  watcher : watch the work queues, to find out if there is work to do
  @handle : the uv_idle_t
**/
void watcher(uv_idle_t *handle)
{
    spider_t *spider = (spider_t *)handle->data;
    uv_rwlock_wrlock(spider->lock);

    if (!isTaskQueueEmpty(spider->task_queue))
    {
        //if there is task unhandled yet, start work thread
        if (spider->download_thread <= spider->download_thread_max)
        {
            //when thread's number reach the max limit
            spider_task_queue_t *rem =
                removeTask(spider->task_queue, spider->task_queue->next->task);
            assert(rem);

            uv_work_t *work = (uv_work_t *)malloc(sizeof(uv_work_t));
            assert(work);

            work->data = rem->task;
            // Point to the worker
            spider_task_t *ptask = (spider_task_t *)rem->task;
            ptask->worker = work;
            ptask->spider = spider;
            addTask(spider->task_queue_doing, rem);
            uv_queue_work(spider->loop, work, download, work_done);

            // add thread's number
            spider->download_thread++;
        }
    }

    if (!isDataQueueEmpty(spider->data_queue))
    {
        // if there is data required to be processed,
        // start thread
        if (spider->pipeline_thread <= spider->pipeline_thread_max)
        {
            data_queue_t *rem =
                removeData(spider->data_queue, spider->data_queue->next->data);
            assert(rem);

            uv_work_t *req = (uv_work_t *)malloc(sizeof(uv_work_t));
            assert(req);

            req->data = rem->data;
            // points to working handle
            data_t *pdata = (data_t *)rem->data;
            pdata->worker = req;
            pdata->spider = spider;

            addData(spider->data_queue_doing, rem);
            uv_queue_work(spider->loop, req, process_data, datasave);
            spider->pipeline_thread++;
        }
    }

    if (!isTaskQueueEmpty(spider->task_queue_doing) ||
        !isTaskQueueEmpty(spider->task_queue) ||
        !isDataQueueEmpty(spider->data_queue) ||
        !isDataQueueEmpty(spider->data_queue_doing))
    {
        uv_rwlock_wrunlock(spider->lock);
    }
    else
    {
        uv_rwlock_wrunlock(spider->lock);
        uv_idle_stop(handle);
    }
}

#include "spider.h"

/**
  execute after curl get data.
  @ptr : point to string which curl get
  @size :
  @nmemb : @size * @nmemb equal the size of string
  @ss : input pointer
**/
size_t save_data(void *contents, size_t size, size_t nmemb, void *ss)
{
    spider_task_t *save = (spider_task_t *)ss;
    size_t all = size * nmemb;
	size_t realsize = size * nmemb;
	if (save->data->buffer_size)
	{
		void* ptr = realloc(save->data->buffer, save->data->buffer_size + realsize);
		if (ptr)
		{
			save->data->buffer = ptr;
		}
		else
		{
			puts("realloc fail.");
			exit(-1);
		}
	}
	else
	{
		save->data->buffer = malloc(realsize);
	}
	void* ptr = save->data->buffer + save->data->buffer_size;
	if (ptr)
	{
		memcpy(ptr, contents, realsize);
	}
	save->data->buffer_size += realsize;
    return realsize;
}

/**
  use curl to download
  @req : the worker
**/
void download(uv_work_t *work)
{
    CURL *curl;
    CURLcode res;

    spider_task_t *task = (spider_task_t *)work->data;
    spider_t *spider = task->spider;
    site_t *site = (site_t *)spider->site;
    curl = curl_easy_init();
    assert(curl);

    if (curl)
    {
        if (site->user_agent != NULL)
        {
            curl_easy_setopt(curl, CURLOPT_USERAGENT, site->user_agent);
        }
        if (site->proxy != NULL)
        {
            curl_easy_setopt(curl, CURLOPT_PROXY, site->proxy);
        }
        if (site->timeout != 0)
        {
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, site->timeout);
        }
        if (site->cookie != NULL)
        {
            curl_easy_setopt(curl, CURLOPT_COOKIE, site->cookie);
        }
        /*支持重定向*/
        /*support redirection*/
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);

        curl_easy_setopt(curl, CURLOPT_URL, task->url);
        // put url to cs_rawText_t
        task->data->url = task->url;

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, save_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, work->data);
        res = curl_easy_perform(curl);

        curl_easy_cleanup(curl);
    }
}

/**
  work_done : it will be called after work thread finish
  @req : the worker
**/
void work_done(uv_work_t *req, int status)
{
    UNUSED(status);
    spider_t *spider = ((spider_task_t *)req->data)->spider;
    /*打印到日志
    print log
   */
    logger(0, "%s download finish.\n", ((spider_task_t *)req->data)->url, spider);
    /*
    when finish download data,
    first, remove task from task_queue_doing
    second, add rawText to data_queue
    finally, free the task.
   */
    uv_rwlock_wrlock(spider->lock);
    spider->download_thread--;
    spider_task_queue_t *q = removeTask(spider->task_queue_doing, req->data);
    assert(q);

    data_queue_t *queue =
        (data_queue_t *)malloc(sizeof(data_queue_t));
    assert(queue);

    queue->data = q->task->data;
    addData(spider->data_queue, queue);
	//freeTask(q);
    uv_rwlock_wrunlock(spider->lock);
	
    return;
}

/**
isTaskQueueEmpty : is task queue empty?
@head : task queue ready to test
return 1 for empty, 0 for no empty
**/
int isTaskQueueEmpty(spider_task_queue_t *head)
{
    return (head->next == head);
}
/**

initTaskQueue : return the initializing queue

return the new task queue.
**/
spider_task_queue_t *initTaskQueue()
{
    spider_task_queue_t *new_queue = (spider_task_queue_t *)malloc(sizeof(spider_task_queue_t));
    assert(new_queue);

    new_queue->task = NULL;
    new_queue->next = new_queue;
    new_queue->prev = new_queue;
    return new_queue;
}
/**
createTask : add a task to task queue
@head : the task queue ready to be inserted
@url : new task's url
**/
void createTask(spider_task_queue_t *head, char *url)
{
    spider_task_t *task = (spider_task_t *)malloc(sizeof(spider_task_t));
    assert(task);

    strcpy(task->url,url);
    /*需要先新建一个存放数据的地方*/
    task->data = createData();
    spider_task_queue_t *queue = (spider_task_queue_t *)malloc(sizeof(spider_task_queue_t));
    assert(queue);

    queue->task = task;
    queue->next = head;
    queue->prev = head->prev;
    queue->next->prev = queue;
    queue->prev->next = queue;
    return;
}
/**

removeTask : remove the task from the task queue
@head : the task queue ready to be removed from
@task : removed task

if @task exists in @head,
return @task
else
return NULL

**/
spider_task_queue_t *removeTask(spider_task_queue_t *head, spider_task_t *task)
{
    spider_task_queue_t *p = head->next;
    spider_task_queue_t *res = NULL;

    while (p != head)
    {
        if (p->task == task)
        {
            res = p;
            p->prev->next = p->next;
            p->next->prev = p->prev;
            break;
        }
        else
        {
            p = p->next;
        }
    }
    return res;
}
/**

  addTask : add a task into queue
  @head : the task queue ready to be inserted
  @task : the task ready to insert into @head

**/
void addTask(spider_task_queue_t *head, spider_task_queue_t *task)
{
    task->next = head;
    task->prev = head->prev;
    task->next->prev = task;
    task->prev->next = task;
    return;
}
/**
freeTask : free the task queue
@node : the task ready to be freed

first, free the uv_work_t
second, free cs_task_t
finally, frr cs_task_queue

**/
void freeTask(spider_task_queue_t *node)
{
    free(node->task->worker);
    free(node->task);
    free(node);
}

#include "spider.h"

/**
  init_spider : init the spider

  return the spider_t which is ready
**/

spider_t *spider_new()
{
    spider_t *spider = (spider_t *)malloc(sizeof(spider_t));
    assert(spider);

    // init task queue
    spider->task_queue = initTaskQueue();
    assert(spider->task_queue);

    spider->task_queue_doing = initTaskQueue();
    assert(spider->task_queue_doing);

    // init data queue
    spider->data_queue = initDataQueue();
    assert(spider->data_queue);

    spider->data_queue_doing = initDataQueue();
    assert(spider->data_queue_doing);

    spider->download_thread_max = 16;
    spider->pipeline_thread_max = 16;
    spider->download_thread = 1;
    spider->pipeline_thread = 1;
    spider->process = NULL;
    spider->process_user_data = NULL;
    spider->loop = uv_default_loop();

    spider->idler = (uv_idle_t *)malloc(sizeof(uv_idle_t));

    spider->lock = (uv_rwlock_t *)malloc(sizeof(uv_rwlock_t));
    uv_rwlock_init(spider->lock);

    spider->idler->data = spider;
    spider->site = (site_t *)malloc(sizeof(site_t));
    spider->site->user_agent = NULL;
    spider->site->proxy = NULL;
    spider->site->cookie = NULL;
    spider->site->timeout = 0;
    spider->log = NULL;
    spider->bloom = bloom_new();
    return spider;
}

void spider_setopt_url(spider_t *spider, char *url)
{
    assert(spider);
    assert(url);
    if (!bloom_check(spider->bloom, url))
    {
        // url no exits
        bloom_add(spider->bloom, url);
        size_t len = strlen(url);
        char *reUrl = (char *)malloc(sizeof(char) * (len + 1));
        assert(reUrl);
        strncpy(reUrl, url, len + 1);
        createTask(spider->task_queue, reUrl);
    }
}

void spider_setopt_baseurl(spider_t *spider, char *url)
{
    assert(spider);
    assert(url);
    spider->site->base_url = url;
}

void spider_setopt_cookie(spider_t *spider, char *cookie)
{
    assert(spider);
    assert(cookie);
    ((site_t *)spider->site)->cookie = cookie;
}

void spider_setopt_useragent(spider_t *spider, char *agent)
{
    assert(spider);
    assert(agent);
    ((site_t *)spider->site)->user_agent = agent;
}

void spider_setopt_proxy(spider_t *spider, char *proxy)
{
    assert(spider);
    assert(proxy);
    ((site_t *)spider->site)->proxy = proxy;
}

void spider_setopt_timeout(spider_t *spider, long timeout)
{
    assert(spider);
    ((site_t *)spider->site)->timeout = timeout;
}

void spider_setopt_logfile(spider_t *spider, FILE *log)
{
    assert(spider);
    assert(log);
    spider->log = log;
    spider->log_lock = (uv_rwlock_t *)malloc(sizeof(uv_rwlock_t));
    uv_rwlock_init(spider->log_lock);
}

void spider_setopt_process(spider_t *spider,
                           void (*process)(spider_t *, char *, size_t buffer_size, char *, void *),
                           void *user_data)
{
    assert(spider);
    assert(process);
    spider->process = process;
    spider->process_user_data = user_data;
}

void spider_setopt_threadnum(spider_t *spider, int flag, int number)
{
    assert(spider);
    assert(flag == DOWNLOAD || flag == SAVE);
    assert(number > 0);
    if (flag == DOWNLOAD)
    {
        spider->download_thread_max = number;
    }
    else
    {
        spider->pipeline_thread_max = number;
    }
}

int spider_run(spider_t *spider)
{
    if (spider->process == NULL)
    {
        printf("warn : need to init process function(use spider_setopt_process)\n");
        return 0;
    }

    uv_idle_init(spider->loop, spider->idler);
    uv_idle_start(spider->idler, watcher);

    return uv_run(spider->loop, UV_RUN_DEFAULT);
}

void logger(int flag, const char *str1, const char *str2, spider_t *spider)
{
    if (!flag)
    {
        // false
        if (spider->log != NULL)
        {
            // uv_rwlock_wrlock(spider->log_lock);
            // fprintf(spider->log, "%s %s", str1, str2);
            // uv_rwlock_wrunlock(spider->log_lock);
        }
    }
}
