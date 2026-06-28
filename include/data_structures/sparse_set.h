#ifndef SPARSE_SET_H_
#define SPARSE_SET_H_

#include <stdint.h>
#include <stddef.h>

struct SparseSet
{
    void* data;
    size_t valueSize;
    size_t dataLen;
    size_t dataArrLen;
    size_t* logicalToPhysical;
    size_t logicalToPhysicalArrLen;
    size_t* physicalToLogical;
};

//Data must be an element of size "valueSize"
void SparseSetAddElement(struct SparseSet* set, size_t id, void* data);
void SparseSetRemoveElement(struct SparseSet* set, size_t id);
//Returns NULL if the element is not present in the set
void* SparseSetGetElement(struct SparseSet* set, size_t id);
void SparseSetCreate(struct SparseSet* set, size_t initialElementCount, size_t elementSize);
void SparseSetFree(struct SparseSet* set);
int SparseSetHasElement(struct SparseSet* set, size_t id);
//Get the number of stored elements in the set
size_t SparseSetGetElementCount(struct SparseSet* set);
//Get the singular element size in bytes
size_t SparseSetGetElementSize(struct SparseSet* set);
size_t SparseSetGetPhysicalIndexFromID(struct SparseSet* set, size_t id);
size_t SparseSetGetIDFromPhysicalIndex(struct SparseSet* set, size_t physicalIdx);
void* SparseSetGetDataBuffer(struct SparseSet* set);

#ifdef SPARSE_SET_IMPL

#include <string.h>
#include <stdio.h>
#ifdef EXPECT
#error EXPECT macro already defined
#endif
#include <stdlib.h>
#define EXPECT(condition, ...)\
do\
{\
    if (!condition)\
    {\
        printf("\033[31m" "ASSERTION FAILED at %s -- line %d\n", __FILE__, __LINE__);\
        __VA_OPT__(printf("Message: " "\033[39m", __VA_ARGS__);)\
        exit(1);\
    }\
} while (0)

void SparseSetCreate(struct SparseSet* set, size_t initialElementCount, size_t elementSize)
{
    EXPECT(set, "SparseSetCreate: set is NULL");
    EXPECT(elementSize, "SparseSetCreate: elementSize is 0");

    set->valueSize = elementSize;
    set->dataLen = 0;
    if (initialElementCount == 0) initialElementCount++;

    set->data = malloc(initialElementCount * elementSize);
    EXPECT(set->data, "malloc failed");
    set->dataArrLen = initialElementCount * elementSize;

    set->logicalToPhysical = malloc((initialElementCount + 1000) * sizeof(size_t)); /*Minimum floor for allocation*/
    EXPECT(set->logicalToPhysical, "malloc failed");
    set->logicalToPhysicalArrLen = (initialElementCount + 1000) * sizeof(size_t);

    set->physicalToLogical = malloc(initialElementCount * sizeof(size_t));
    EXPECT(set->physicalToLogical, "malloc failed");
}

void SparseSetFree(struct SparseSet* set)
{
    EXPECT(set, "SparseSetFree: set is NULL");

    set->valueSize = 0;
    set->dataLen = 0;
    set->logicalToPhysicalArrLen = 0;
    free(set->data); set->data = NULL;
    free(set->logicalToPhysical); set->logicalToPhysical = NULL;
    free(set->physicalToLogical); set->physicalToLogical = NULL;
}

int SparseSetHasElement(struct SparseSet* set, size_t id)
{
    EXPECT(set, "SparseSetHasElement: set is NULL");

    if (set->dataLen == 0) return 0;
    if (id >= set->logicalToPhysicalArrLen / sizeof(size_t)) return 0;

    size_t physicalID = set->logicalToPhysical[id];
    if (physicalID >= set->dataLen) return 0;

    return set->physicalToLogical[physicalID] == id;
}

void SparseSetAddElement(struct SparseSet* set, size_t id, void* val)
{
    EXPECT(set, "SparseSetAddElement: set is NULL");

    if (SparseSetHasElement(set, id)) return;

    if (set->dataLen >= set->dataArrLen / set->valueSize)
    {
        void* ptr = realloc(set->data, set->dataArrLen * 3);
        EXPECT(ptr, "realloc failed");
        set->data = ptr;
        set->dataArrLen *= 3;

        ptr = realloc(set->physicalToLogical, (set->dataArrLen / set->valueSize) * sizeof(size_t));
        EXPECT(ptr, "realloc failed");
        set->physicalToLogical = ptr;
    }

    if (id >= set->logicalToPhysicalArrLen / sizeof(size_t))
    {
        void* ptr = realloc(set->logicalToPhysical, set->logicalToPhysicalArrLen + sizeof(size_t) * id);
        EXPECT(ptr, "realloc failed");
        set->logicalToPhysical = ptr;
        set->logicalToPhysicalArrLen += sizeof(size_t) * id;
    }

    memcpy((char*)set->data + (set->dataLen * set->valueSize), val, set->valueSize);

    set->logicalToPhysical[id] = set->dataLen;
    set->physicalToLogical[set->dataLen] = id;

    set->dataLen++;
}

void SparseSetRemoveElement(struct SparseSet* set, size_t id)
{
    EXPECT(set, "SparseSetRemoveElement: set is NULL");

    if (!SparseSetHasElement(set, id)) return;

    if (id >= set->logicalToPhysicalArrLen / sizeof(size_t))
    {
        printf("SparseSetRemoveElement Error: id is greater than array length\n");
        return;
    }

    if (set->dataLen == 0)
    {
        printf("SparseSetRemoveElement Error: the set has no elements\n");
        return;
    }

    memcpy((char*)set->data + set->logicalToPhysical[id] * set->valueSize, (char*)set->data + (set->dataLen - 1)  * set->valueSize, set->valueSize);

    set->logicalToPhysical[set->physicalToLogical[set->dataLen - 1]] = set->logicalToPhysical[id]; //Swap logical index

    set->physicalToLogical[set->logicalToPhysical[id]] = set->physicalToLogical[set->dataLen - 1]; //Swap back index

    set->dataLen--;
}

void* SparseSetGetElement(struct SparseSet* set, size_t id)
{
    EXPECT(set, "SparseSetGetElement: set is NULL");

    if (id >= set->logicalToPhysicalArrLen / sizeof(size_t))
    {
        printf("SparseSetGetElement Error: id is greater than array length\n");
        return NULL;
    }

    if (set->dataLen == 0)
    {
        printf("SparseSetGetElement Error: the set has no elements\n");
        return NULL;
    }

    if (set->logicalToPhysical[id] >= set->dataLen)
    {
        printf("SparseSetGetElement Error: the id is not valid\n");
        return NULL;
    }

    if (!SparseSetHasElement(set, id)) return NULL;

    return (char*)set->data + set->logicalToPhysical[id] * set->valueSize;
}

size_t SparseSetGetElementCount(struct SparseSet* set)
{
    EXPECT(set, "SparseSetGetElementCount: set is NULL");
    return set->dataLen;
}

size_t SparseSetGetElementSize(struct SparseSet* set)
{
    EXPECT(set, "SparseSetGetElementSize: set is NULL");
    size_t SparseSetGetElementSize(struct SparseSet* set);
    return set->valueSize;
}

size_t SparseSetGetPhysicalIndexFromID(struct SparseSet* set, size_t id)
{
    EXPECT(set, "SparseSetGetPhysicalIndexFromID: set is NULL");

    if (id >= set->logicalToPhysicalArrLen / sizeof(size_t))
    {
        printf("SparseSetGetPhysicalIndexFromID: id is out of bounds\n");
        return 0;
    }

    return set->logicalToPhysical[id];
}

size_t SparseSetGetIDFromPhysicalIndex(struct SparseSet* set, size_t physicalIdx)
{
    EXPECT(set, "SparseSetGetIDFromPhysicalIndex: set is NULL");
    if (physicalIdx >= set->dataLen)
    {
        printf("SparseSetGetIDFromPhysicalIndex: physicalIdx is out of bounds\n");
        return 0;
    }

    return set->physicalToLogical[physicalIdx];
}

void* SparseSetGetDataBuffer(struct SparseSet* set)
{
    EXPECT(set, "SparseSetGetDataBuffer: set is NULL");
    return set->data;
}

#endif //SPARSE_SET_IMPL

#endif //INCLUDE GUARD
