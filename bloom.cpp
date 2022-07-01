#include "spider.h"

#define SETBIT(a, n) (a[n / CHAR_BIT] |= (1 << (n % CHAR_BIT)))
#define GETBIT(a, n) (a[n / CHAR_BIT] & (1 << (n % CHAR_BIT)))
/**
   create_bloom : create Bloom_t struct
   @size : size of bit array
   @nfuncs : size of hash function array

   return new Bloom_t
**/
bloom_t *create_bloom(size_t size, size_t nfuncs, ...)
{
    bloom_t *bloom;
    va_list l;
    bloom = (bloom_t *)malloc(sizeof(bloom_t));
    if (!bloom)
    {
        return NULL;
    }
    bloom->a = (unsigned char *)calloc((size + CHAR_BIT - 1) / CHAR_BIT, sizeof(char));
    if (!bloom->a)
    {
        free(bloom);
        return NULL;
    }
    bloom->funcs = (hashfunc_t *)malloc(nfuncs * sizeof(hashfunc_t));
    if (!bloom->funcs)
    {
        free(bloom->a);
        free(bloom);
        return NULL;
    }

    va_start(l, nfuncs);
    for (size_t n = 0; n < nfuncs; ++n)
    {
        bloom->funcs[n] = va_arg(l, hashfunc_t);
    }
    va_end(l);

    bloom->nfuncs = nfuncs;
    bloom->asize = size;

    return bloom;
}

/**
   bloom_destroy : destroy the bloom
   @bloom : ready to be destroyed
**/
int bloom_destroy(bloom_t *bloom)
{
    free(bloom->a);
    free(bloom->funcs);
    free(bloom);

    return 0;
}

/**
   bloom_add : add string to filter
   @bloom : the bloom
   @s : ready to add
**/
int bloom_add(bloom_t *bloom, const char *s)
{
    size_t n;

    for (n = 0; n < bloom->nfuncs; ++n)
    {
        SETBIT(bloom->a, bloom->funcs[n](s) % bloom->asize);
    }

    return 0;
}

/**
   bloom_check : check if string exits
   @bloom : the bloom
   @s : ready
**/
int bloom_check(bloom_t *bloom, const char *s)
{
    size_t n;

    for (n = 0; n < bloom->nfuncs; ++n)
    {
        if (!(GETBIT(bloom->a, bloom->funcs[n](s) % bloom->asize)))
            return 0;
    }

    return 1;
}

/**
  init Bloom_t
**/
bloom_t *bloom_new() { return create_bloom(2500000, 2, sax_hash, sdbm_hash); }

/**
hash functions
**/

unsigned int sax_hash(char *key)
{
    unsigned int h = 0;

    while (*key)
        h ^= (h << 5) + (h >> 2) + (unsigned char)*key++;

    return h;
}

unsigned int sdbm_hash(char *key)
{
    unsigned int h = 0;
    while (*key)
        h = (unsigned char)*key++ + (h << 6) + (h << 16) - h;
    return h;
}
