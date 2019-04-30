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

    node->count = 0;
    node->length = 0;

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
    for (size_t i = 0; i < node->data->count; i++)
    {
        //逐个free数据块
        // free buffers one by one
        free(node->data->data[i]);
    }
    free(node->data->url);
    free(node->data);
    free(node);
}

void dataproc(uv_work_t *req)
{
    spider_t *cspider = ((data_t *)req->data)->cspider;
    data_t *text = (data_t *)req->data;
    // Put all buffer's data into a string
    char *get = (char *)malloc(sizeof(char) * text->length + 1);
    assert(get);

    int currentCount = 0;
    for (size_t i = 0; i < text->count; i++)
    {
        memcpy(get + currentCount, text->data[i], text->each[i]);
        currentCount += text->each[i];
    }
    *(get + currentCount) = '\0';
    // get data
    (cspider->process)(cspider, get, text->url, cspider->process_user_data);
    free(get);
}

void datasave(uv_work_t *req, int status)
{
    UNUSED(status);
    spider_t *cspider = ((data_t *)req->data)->cspider;
    // log
    logger(0, "%s save finish.\n", ((data_t *)req->data)->url, cspider);

    uv_rwlock_wrlock(cspider->lock);
    cspider->pipeline_thread--;
    data_queue_t *q = removeData(cspider->data_queue_doing, req->data);
    assert(q);

    logger(q != NULL, "removeData error in %s.\n", "dataProcess.c", cspider);
    freeData(q);
    uv_rwlock_wrunlock(cspider->lock);
}

/*
  provide two functions to users.
  saveString : save the string.
  addUrl : add the url back to the download task queue.
*/

void saveString(spider_t *cspider, void *data, int flag)
{
    assert(flag == LOCK || flag == NO_LOCK);
    if (flag == LOCK)
    {
        uv_rwlock_wrlock(cspider->save_lock);
        (cspider->save)(data, cspider->save_user_data);
        uv_rwlock_wrunlock(cspider->save_lock);
    }
    else
    {
        (cspider->save)(data, cspider->save_user_data);
    }
}

void saveStrings(spider_t *cspider, void **datas, size_t size, int flag)
{
    assert(flag == LOCK || flag == NO_LOCK);
    if (flag == LOCK)
    {
        // need to lock
        uv_rwlock_wrlock(cspider->save_lock);
        for (size_t i = 0; i < size; i++)
        {
            (cspider->save)(datas[i], cspider->save_user_data);
        }
        uv_rwlock_wrunlock(cspider->save_lock);
    }
    else
    {
        // no need to lock
        for (size_t i = 0; i < size; i++)
        {
            (cspider->save)(datas[i], cspider->save_user_data);
        }
    }
}

void addUrl(spider_t *cspider, char *url)
{
    if (!bloom_check(cspider->bloom, url))
    {
        // no exits
        bloom_add(cspider->bloom, url);
        size_t len = strlen(url) + 1;
        char *reUrl = (char *)malloc(sizeof(char) * len);
        assert(reUrl);

        memcpy(reUrl, url, len);
        uv_rwlock_wrlock(cspider->lock);
        createTask(cspider->task_queue, reUrl);
        uv_rwlock_wrunlock(cspider->lock);
    }
}

void addUrls(spider_t *cspider, char **urls, size_t size)
{
    char **reUrls = (char **)malloc(size * sizeof(char *));
    for (size_t i = 0; i < size; i++)
    {
        if (!bloom_check(cspider->bloom, urls[i]))
        {
            // no exits
            bloom_add(cspider->bloom, urls[i]);
            size_t len = strlen(urls[i]);
            reUrls[i] = (char *)malloc(sizeof(char) * (len + 1));
            assert(reUrls[i]);

            memcpy(reUrls[i], urls[i], len + 1);
        }
        else
        {
            reUrls[i] = NULL;
        }
    }
    //  uv_rwlock_wrlock(cspider->lock);
    for (size_t i = 0; i < size; i++)
    {
        if (reUrls[i] != NULL)
            createTask(cspider->task_queue, reUrls[i]);
    }
    //  uv_rwlock_wrunlock(cspider->lock);
}

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
    spider_t *cspider = (spider_t *)handle->data;
    uv_rwlock_wrlock(cspider->lock);
    if (!isTaskQueueEmpty(cspider->task_queue))
    {
        //if there is task unhandled yet, start work thread
        if (cspider->download_thread <= cspider->download_thread_max)
        {
            //when thread's number reach the max limit
            spider_task_queue_t *rem =
                removeTask(cspider->task_queue, cspider->task_queue->next->task);
            assert(rem);

            uv_work_t *req = (uv_work_t *)malloc(sizeof(uv_work_t));
            assert(req);

            req->data = rem->task;
            // Point to the worker
            spider_task_t *ptask = (spider_task_t *)rem->task;
            ptask->worker = req;
            ptask->cspider = cspider;
            addTask(cspider->task_queue_doing, rem);
            uv_queue_work(cspider->loop, req, download, work_done);

            // add thread's number
            cspider->download_thread++;
        }
    }

    if (!isDataQueueEmpty(cspider->data_queue))
    {
        // if there is data required to be processed,
        // start thread
        if (cspider->pipeline_thread <= cspider->pipeline_thread_max)
        {
            data_queue_t *rem =
                removeData(cspider->data_queue, cspider->data_queue->next->data);
            assert(rem);

            uv_work_t *req = (uv_work_t *)malloc(sizeof(uv_work_t));
            assert(req);

            req->data = rem->data;
            // points to working handle
            data_t *pdata = (data_t *)rem->data;
            pdata->worker = req;
            pdata->cspider = cspider;

            addData(cspider->data_queue_doing, rem);
            uv_queue_work(cspider->loop, req, dataproc, datasave);
            cspider->pipeline_thread++;
        }
    }

    if (!isTaskQueueEmpty(cspider->task_queue_doing) ||
        !isTaskQueueEmpty(cspider->task_queue) ||
        !isDataQueueEmpty(cspider->data_queue) ||
        !isDataQueueEmpty(cspider->data_queue_doing))
    {
        uv_rwlock_wrunlock(cspider->lock);
    }
    else
    {
        uv_rwlock_wrunlock(cspider->lock);
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
size_t save_data(void *ptr, size_t size, size_t nmemb, void *ss)
{
    spider_task_t *save = (spider_task_t *)ss;
    size_t count = save->data->count;
    size_t all = size * nmemb;

    char *buf = (char *)malloc(all);
    if (buf == NULL)
        return (size_t)-1;
    save->data->data[count] = buf; // "char != 1" only appears in IBM machines.

    if (ptr == NULL)
        return (size_t)-1;
    strncpy(save->data->data[count], (char *)ptr, all);

    save->data->each[count] = all;
    save->data->count = count + 1;
    save->data->length += all;

    return all;
}

/**
  use curl to download
  @req : the worker
**/
void download(uv_work_t *req)
{
    CURL *curl;
    CURLcode res;

    spider_task_t *task = (spider_task_t *)req->data;
    spider_t *cspider = task->cspider;
    site_t *site = (site_t *)cspider->site;
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
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, req->data);
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
    spider_t *cspider = ((spider_task_t *)req->data)->cspider;
    /*打印到日志
    print log
   */
    logger(0, "%s download finish.\n", ((spider_task_t *)req->data)->url, cspider);
    /*
    when finish download data,
    first, remove task from task_queue_doing
    second, add rawText to data_queue
    finally, free the task.
   */
    uv_rwlock_wrlock(cspider->lock);
    cspider->download_thread--;
    spider_task_queue_t *q = removeTask(cspider->task_queue_doing, req->data);
    assert(q);

    data_queue_t *queue =
        (data_queue_t *)malloc(sizeof(data_queue_t));
    assert(queue);

    queue->data = q->task->data;
    addData(cspider->data_queue, queue);
    freeTask(q);
    uv_rwlock_wrunlock(cspider->lock);
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

    task->url = url;
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
  init_cspider : init the cspider

  return the cspider_t which is ready
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
    spider->save = NULL;
    spider->process_user_data = NULL;
    spider->save_user_data = NULL;
    spider->loop = uv_default_loop();

    spider->idler = (uv_idle_t *)malloc(sizeof(uv_idle_t));

    spider->lock = (uv_rwlock_t *)malloc(sizeof(uv_rwlock_t));
    uv_rwlock_init(spider->lock);
    spider->save_lock = (uv_rwlock_t *)malloc(sizeof(uv_rwlock_t));
    uv_rwlock_init(spider->save_lock);

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

void spider_setopt_url(spider_t *cspider, char *url)
{
    assert(cspider);
    assert(url);
    if (!bloom_check(cspider->bloom, url))
    {
        // url no exits
        bloom_add(cspider->bloom, url);
        size_t len = strlen(url);
        char *reUrl = (char *)malloc(sizeof(char) * (len + 1));
        assert(reUrl);
        strncpy(reUrl, url, len + 1);
        createTask(cspider->task_queue, reUrl);
    }
}

void spider_setopt_baseurl(spider_t *cspider, char *url)
{
    assert(cspider);
    assert(url);
    cspider->site->base_url = url;
}

void spider_setopt_cookie(spider_t *cspider, char *cookie)
{
    assert(cspider);
    assert(cookie);
    ((site_t *)cspider->site)->cookie = cookie;
}

void spider_setopt_useragent(spider_t *cspider, char *agent)
{
    assert(cspider);
    assert(agent);
    ((site_t *)cspider->site)->user_agent = agent;
}

void spider_setopt_proxy(spider_t *cspider, char *proxy)
{
    assert(cspider);
    assert(proxy);
    ((site_t *)cspider->site)->proxy = proxy;
}

void spider_setopt_timeout(spider_t *cspider, long timeout)
{
    assert(cspider);
    ((site_t *)cspider->site)->timeout = timeout;
}

void spider_setopt_logfile(spider_t *cspider, FILE *log)
{
    assert(cspider);
    assert(log);
    cspider->log = log;
    cspider->log_lock = (uv_rwlock_t *)malloc(sizeof(uv_rwlock_t));
    uv_rwlock_init(cspider->log_lock);
}

void spider_setopt_process(spider_t *cspider,
                           void (*process)(spider_t *, char *, char *, void *),
                           void *user_data)
{
    assert(cspider);
    assert(process);
    cspider->process = process;
    cspider->process_user_data = user_data;
}

void spider_setopt_save(spider_t *cspider, void (*save)(void *, void *),
                        void *user_data)
{
    assert(cspider);
    assert(save);
    cspider->save = save;
    cspider->save_user_data = user_data;
}

void spider_setopt_threadnum(spider_t *cspider, int flag, int number)
{
    assert(cspider);
    assert(flag == DOWNLOAD || flag == SAVE);
    assert(number > 0);
    if (flag == DOWNLOAD)
    {
        cspider->download_thread_max = number;
    }
    else
    {
        cspider->pipeline_thread_max = number;
    }
}

int spider_run(spider_t *cspider)
{
    if (cspider->process == NULL)
    {
        printf("warn : need to init process function(use cs_setopt_process)\n");
        return 0;
    }
    if (cspider->save == NULL)
    {
        printf("warn : need to init data persistence function(use cs_setopt_save)\n");
        return 0;
    }
    uv_idle_init(cspider->loop, cspider->idler);
    uv_idle_start(cspider->idler, watcher);

    return uv_run(cspider->loop, UV_RUN_DEFAULT);
}

void logger(int flag, const char *str1, const char *str2, spider_t *cspider)
{
    if (!flag)
    {
        // false
        if (cspider->log != NULL)
        {
            uv_rwlock_wrlock(cspider->log_lock);
            fprintf(cspider->log, "%s %s", str1, str2);
            uv_rwlock_wrunlock(cspider->log_lock);
        }
    }
}
