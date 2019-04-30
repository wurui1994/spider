#ifndef SPIDER_H
#define SPIDER_H

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <locale.h>

#include <uv.h>

#include <pcre.h>

#include <curl/curl.h>

#include <libxml/xpath.h>
#include <libxml/HTMLparser.h>

/*
the max number of the buffers
*/
#define BUFFER_MAX_NUMBER 1024
#define DOWNLOAD 1
#define SAVE 0
#define LOCK 1
#define NO_LOCK 0
#define REGEX_ALL 0
#define REGEX_NO_ALL 1

#define FileTypeErr 0
#define FileTypeHTML 1
#define FileTypeCSS 2
#define FileTypeJSON 3

#define MaxPageQueueNum 32
#define LogMaxPageQueueNum 5

#define UNUSED(x) (void)(x)

typedef unsigned char byte;

// typedef
typedef struct site site_t;
typedef struct spider spider_t;
typedef struct data_struct data_t;
typedef struct page_queue_struct page_queue_t;
typedef struct data_queue_struct data_queue_t;
typedef struct task_struct spider_task_t;
typedef struct task_queue_struct spider_task_queue_t;

typedef struct xpath_result_struct xpath_result_t;

typedef unsigned int (*hashfunc_t)(char *);
typedef struct bloom_struct bloom_t;

// struct

/*
  raw data, such as html and json which we get.
*/
struct data_struct
{
  char *data[BUFFER_MAX_NUMBER];  /* Array of buffer */
  size_t each[BUFFER_MAX_NUMBER]; /* each buffer's size */
  size_t count;                   /* buffer's number */
  size_t length;                  /* the sum of all buffer's size */
  char *url;                      /* the url where it is downloaded */
  uv_work_t *worker;              /* Point to the worker */
  spider_t *cspider;              /* the Main cspider struct */
};

typedef struct page_struct
{
  void *data;
  size_t capacity;
  size_t used;
  char file_type;
} page_t;

/*
  page carrier'a queue
*/
struct page_queue_struct
{
  page_t *pages;
  size_t capacity;
  size_t usage;
};

/*
 data queue
*/
struct data_queue_struct
{
  data_t *data;
  struct data_queue_struct *next; /* next node */
  struct data_queue_struct *prev; /* previous node */
};

struct task_struct
{
  char *url;                // the url which task need to deal with
  struct data_struct *data; // Point to the struct which save the data we download.
  uv_work_t *worker;        // Point to the worker
  struct spider *cspider;   // Point to the Main cspider struct
};

/*
  task queue
*/
struct task_queue_struct
{
  spider_task_t *task;            //
  struct task_queue_struct *next; // next node
  struct task_queue_struct *prev; // previous node
};

struct site
{
  char *base_url;
  char *user_agent; //user agent
  char *proxy;      // proxy address
  char *cookie;     //cookie string
  long timeout;     // timeout (ms)
};

struct bloom_struct
{
  size_t asize;      // bloom filter's bit array's size
  unsigned char *a;  // bit array
  size_t nfuncs;     // hash function's number
  hashfunc_t *funcs; // array of hash function
};

struct spider
{
  uv_loop_t *loop;
  uv_idle_t *idler;

  //task queue
  struct task_queue_struct *task_queue_doing;
  struct task_queue_struct *task_queue;

  //data queue
  struct data_queue_struct *data_queue;
  struct data_queue_struct *data_queue_doing;

  //
  void *process_user_data;
  void *save_user_data;

  //Max thread number
  int download_thread_max;
  int pipeline_thread_max;

  //current thread number
  int download_thread;
  int pipeline_thread;

  //lock
  uv_rwlock_t *lock;

  //data persistence lock
  uv_rwlock_t *save_lock;

  //include useragent, cookie, timeout, proxy
  site_t *site;

  //log file
  FILE *log;
  uv_rwlock_t *log_lock;

  //bloom filter
  struct bloom_struct *bloom;

  // custom function
  void (*process)(struct spider *cspider, char *d, char *url, void *user_data);
  void (*save)(void *data, void *user_data);
};

struct xpath_result_struct
{
  char **urls;
  size_t size;
};

// regex
int regexAll(const char *regex, char *str, char **res, int num, int flag);
int match(char *regex, char *str);

// xpath
xpath_result_t xpath(char *xml, char *path);

// log
void logger(int flag, const char *str1, const char *str2, spider_t *spider);

// hash functions
unsigned int sax_hash(char *key);
unsigned int sdbm_hash(char *key);

char *joinUrl(char *, char *);
void joinUrls(char *, char **, size_t);

// data
int isDataQueueEmpty(data_queue_t *head);
data_queue_t *initDataQueue(void);
data_t *createData(void);
void addData(data_queue_t *head, data_queue_t *queue);
data_queue_t *removeData(data_queue_t *head, data_t *data);
void freeData(data_queue_t *node);

void dataproc(uv_work_t *req);
void datasave(uv_work_t *req, int status);

// task
int isTaskQueueEmpty(spider_task_queue_t *head);
spider_task_queue_t *initTaskQueue(void);
void createTask(spider_task_queue_t *head, char *url);
spider_task_queue_t *removeTask(spider_task_queue_t *head, spider_task_t *task);
void addTask(spider_task_queue_t *head, spider_task_queue_t *task);
void freeTask(spider_task_queue_t *node);

// bloom filter
bloom_t *bloom_new(void);
int bloom_check(bloom_t *bloom, char *s);
int bloom_add(bloom_t *bloom, char *s);
int bloom_destroy(bloom_t *bloom);

//
size_t save_data(void *ptr, size_t size, size_t nmemb, void *save);
void download(uv_work_t *req);
void work_done(uv_work_t *req, int status);
void watcher(uv_idle_t *handle);

// spider
void spider_setopt_url(spider_t *spider, char *url);
void spider_setopt_baseurl(spider_t *spider, char *url);
void spider_setopt_cookie(spider_t *spider, char *cookie);
void spider_setopt_useragent(spider_t *cspider, char *agent);
void spider_setopt_proxy(spider_t *spider, char *proxy);
void spider_setopt_timeout(spider_t *spider, long timeout);
void spider_setopt_logfile(spider_t *spider, FILE *log);
void spider_setopt_process(spider_t *spider, void (*process)(spider_t *, char *, char *, void *), void *user_data);
void spider_setopt_save(spider_t *cspider, void (*save)(void *, void *), void *user_data);
void spider_setopt_threadnum(spider_t *spider, int flag, int number);

spider_t *spider_new(void);
int spider_run(spider_t *spider);

// user interface
void saveString(struct spider *spider, void *data, int flag);
void saveStrings(struct spider *spider, void **datas, size_t size, int flag);
void addUrl(struct spider *spider, char *url);
void addUrls(struct spider *spider, char **urls, size_t size);
void freeString(char *str);
void freeStrings(char **strs, size_t size);

#endif
