#include "def.h"
#include "hash.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAP_KEY_MAXLEN 100

static unsigned long
hash_string(const char *str)
{
    int c;
    unsigned long hash = 5381;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

hash_t *
hash_new(int size, callback_t free_value)
{
    hash_t *hash = xcalloc(1, sizeof(hash_t));

    size = size > 0 ? size : 4;

    hash->bucket_count = size;
    hash->buckets = xcalloc(size, sizeof(bucket_t));
    hash->free_value = free_value;

    return hash;
}

static pair_t *
get_pair(bucket_t *bucket, const char *key, unsigned int *idx)
{
    *idx = 0;
    unsigned int count = bucket->count;
    pair_t *pair = NULL;

    if (count < 1)
        return NULL;

    pair = bucket->pairs;

    while (*idx < count)
    {
        if (pair->key != NULL &&
            strncmp(pair->key, key, MAP_KEY_MAXLEN) == 0)
            return pair;

        pair++; (*idx)++;
    }

    return NULL;
}

static void
bucket_destroy(bucket_t *bucket, callback_t free_func)
{
    unsigned int i = 0;
    unsigned int count = bucket->count;
    pair_t *pair = NULL;

    if (count < 1)
        return;

    pair = bucket->pairs;

    while (i < count)
    {
        free((void *)pair->key);

        if (free_func != NULL && pair->data != NULL)
            free_func(pair->data);

        pair++; i++;
    }

    free(bucket->pairs);
}

void
hash_destroy(hash_t *hash)
{
    unsigned int i = 0, count = 0;
    bucket_t *bucket = NULL;

    if (hash == NULL)
        return;

    bucket = hash->buckets;
    count = hash->bucket_count;

    while (i < count)
    {
        bucket_destroy(bucket, hash->free_value);

        bucket++; i++;
    }

    free(hash->buckets);
    free(hash);
    hash = NULL;
}

unsigned int
hash_count(hash_t *hash)
{
    if (hash == NULL)
        return 0;

    return hash->count;
}

static bucket_t *
get_bucket(hash_t *hash, const char *key)
{
    unsigned long keyhash = hash_string(key);

    return &(hash->buckets[keyhash % hash->bucket_count]);
}

int
hash_add(hash_t *hash, const char *key, void *data)
{
    size_t keylen, len;
    unsigned int bucket_count, idx;
    char *key_cpy = NULL;

    bucket_t *bucket = NULL;
    pair_t *pair = NULL;

    if (hash == NULL || key == NULL)
        return 0;

    bucket = get_bucket(hash, key);
    pair = get_pair(bucket, key, &idx);

    /* there is already a matching pair in the current bucket */
    if (pair != NULL)
        return 0;

    bucket_count = bucket->count;

    /* the bucket is empty */
    if (bucket_count < 1)
    {
        bucket->pairs = xcalloc(1, sizeof(pair_t));

        pair = bucket->pairs;
    }
    /* reallocate the existing bucket */
    else
    {
        bucket->pairs = realloc(bucket->pairs, sizeof(pair_t) * (bucket_count + 1));
        pair = &(bucket->pairs[bucket_count]);
    }

    /* determine maximum key length */
    keylen = strlen(key);
    len = keylen > MAP_KEY_MAXLEN ? MAP_KEY_MAXLEN : keylen;

    /* copy and assign key */
    key_cpy = xcalloc(len+1, sizeof(char));
    strncpy(key_cpy, key, len);

    pair->key = key_cpy;
    pair->data = data;

    bucket->count++;
    hash->count++;

    return 1;
}

void *
hash_get(hash_t *hash, const char* key)
{
    unsigned int idx = 0;
    bucket_t *bucket = NULL;
    pair_t *pair = NULL;

    if (hash == NULL || key == NULL)
        return NULL;

    bucket = get_bucket(hash, key);
    pair = get_pair(bucket, key, &idx);

    if (pair == NULL)
        return NULL;

    return pair->data;
}

int
hash_remove(hash_t *hash, const char *key)
{
    unsigned int i = 0, j = 0, idx = 0;
    bucket_t *bucket = NULL;
    pair_t *pair = NULL, *new_pairs = NULL;

    if (hash == NULL || key == NULL)
        return 0;

    bucket = get_bucket(hash, key);

    if (bucket == NULL)
        return 0;

    pair = get_pair(bucket, key, &idx);

    if (pair == NULL)
        return 0;

    bucket->count--;
    new_pairs = bucket->count
        ? xcalloc(bucket->count, sizeof(pair_t))
        : NULL;

    for (i = 0; i<bucket->count+1; i++)
    {
        if (i == idx)
            continue;

        pair = &bucket->pairs[i];

        j = i > idx ? i-1 : i;

        new_pairs[j].key = pair->key;
        new_pairs[j].data = pair->data;
    }

    free(bucket->pairs);
    bucket->pairs = new_pairs;

    return 1;
}

static void
hash_iter_init(hash_iter_t *iter, hash_t *hash)
{
    iter->_hash = hash;
    iter->_pair = 0;
    iter->_bucket = 0;

    while (hash->bucket_count > iter->_bucket &&
            hash->buckets[iter->_bucket].count == 0)
    {
        iter->_bucket += 1;
    }
}

hash_iter_t *
hash_iter_start(hash_t *hash)
{
    hash_iter_t *iter = xcalloc(1, sizeof(hash_iter_t));

    hash_iter_init(iter, hash);

    return iter;
}

void
hash_iter_rewind(hash_iter_t *iter)
{
    hash_iter_init(iter, iter->_hash);
}

int
hash_iter(hash_iter_t *iter, const char **key, void **data)
{
    if (iter == NULL || iter->_hash == NULL)
        return 0;

    const hash_t *hash = iter->_hash;

    if (hash->bucket_count <= iter->_bucket)
        return 0;

    const bucket_t *bucket = hash->buckets + iter->_bucket;

    if (bucket == NULL ||
            (bucket->count <= iter->_pair && hash->bucket_count <= iter->_bucket))
        return 0;

    *key = bucket->pairs[iter->_pair].key;
    *data = bucket->pairs[iter->_pair].data;

    /* proceed iterator */
    if ((iter->_pair + 1) >= bucket->count)
    {
        do
        {
            iter->_bucket += 1;
        }
        while (hash->bucket_count > iter->_bucket &&
                hash->buckets[iter->_bucket].count == 0);

        iter->_pair = 0;
    }
    else
        iter->_pair += 1;

    return 1;
}

void
hash_foreach(hash_t *hash, void (*func)(void *))
{
    unsigned int i = 0, j = 0, pairs = 0;
    unsigned int bucket_count = hash->bucket_count;

    bucket_t *bucket = hash->buckets;
    pair_t *pair = NULL;

    while (i < bucket_count)
    {
        j = 0;
        pairs = bucket->count;
        pair = bucket->pairs;

        while (j < pairs)
        {
            if (pair->data != NULL)
                func(pair->data);

            pair++; j++;
        }

        bucket++; i++;
    }
}

/* vim: set et sw=4 sts=4 tw=80: */
