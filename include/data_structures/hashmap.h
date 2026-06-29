#ifndef HASHMAP_H_
#define HASHMAP_H_
#include <stdint.h>

#define HASHMAP_ARR_CAPACITY 128

struct hashmap_node
{
    struct hashmap_node* next;
    struct hashmap_node* prev;
    char* key;
    uint64_t val;
};

typedef struct hashmap_node* hashmap[HASHMAP_ARR_CAPACITY];

uint32_t hashstr(const char* str);
void hashmap_set_val(hashmap map, const char* key, uint64_t val);
//If "key" is not present in the map, then returns false.
bool hashmap_get_val(hashmap map, const char* key, uint64_t* outVal);
void hashmap_delete_key(hashmap map, const char* key);
void hashmap_delete(hashmap map);

#ifdef HASHMAP_IMPL
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//FNV-1a algorithm
uint32_t hashstr(const char* str)
{
    uint32_t hash = 2166136261u; // FNV offset basis

    while (*str)
    {
        hash ^= (uint32_t)(unsigned char)(*str++);
        hash *= 16777619; // FNV prime
    }

    return hash;
}

void hashmap_set_val(hashmap map, const char* key, uint64_t val)
{
    if (!key) {printf("hashmap_set_val ERROR: key is NULL\n"); return;}

    uint32_t index = hashstr(key) % HASHMAP_ARR_CAPACITY;

    struct hashmap_node* node = map[index];

    if (node)
    {
        //there is node with this hash
        while(strcmp(node->key, key) != 0)
        {
            if (!node->next)
            {
                //alloc new node
                struct hashmap_node* newNode = malloc(sizeof(struct hashmap_node));
                newNode->next = NULL;
                newNode->prev = node;
                newNode->key = strdup(key);
                newNode->val = val;

                node->next = newNode;
                return;
            }

            node = node->next;
        }

        //found node, update value
        node->val = val;
        return;
    }

    //alloc new node at head
    map[index] = malloc(sizeof(struct hashmap_node));
    map[index]->next = NULL;
    map[index]->prev = NULL;
    map[index]->key = strdup(key);
    map[index]->val = val;
}

bool hashmap_get_val(hashmap map, const char* key, uint64_t* outVal)
{
    if (!key) {printf("hashmap_get_val ERROR: key is NULL\n"); return false;}
    if (!outVal) {printf("hashmap_get_val ERROR: outVal is NULL\n"); return false;}

    uint32_t index = hashstr(key) % HASHMAP_ARR_CAPACITY;

    struct hashmap_node* node = map[index];

    if (!node)
        {return false;}

    while (strcmp(node->key, key) != 0)
    {
        node = node->next;
        if (!node)
            {return false;}
    }

    *outVal = node->val;
    return true;
}

void hashmap_delete_key(hashmap map, const char* key)
{
    if (!key) {printf("hashmap_delete_key ERROR: key is NULL\n"); return;}

    uint32_t index = hashstr(key) % HASHMAP_ARR_CAPACITY;

    struct hashmap_node* node = map[index];
    if (!node)
        return; //No node has this key

    while(strcmp(node->key, key) != 0)
    {
        node = node->next;
        if (!node)
            return; //No more nodes with this key
    }

    //found node
    if (node->prev == NULL)
    {
        //first node
        map[index] = node->next;
    }

    else
    {
        //there is a prev
        node->prev->next = node->next;
    }

    if (node->next != NULL)
    {
        //there is a next
        node->next->prev = node->prev;
    }

    //free node
    free(node->key);
    free(node);
}

void hashmap_delete(hashmap map)
{
    for (size_t i = 0; i < HASHMAP_ARR_CAPACITY; i++)
    {
        if (map[i] == NULL) continue;

        //free list
        struct hashmap_node* node = map[i];
        map[i] = NULL;

        while (node)
        {
            struct hashmap_node* next = node->next;
            free(node->key);
            free(node);
            node = next;
        }
    }
}

#endif

#endif //Include guard
