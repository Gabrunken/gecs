#ifndef DYARRAY_H_
#define DYARRAY_H_

#include <stddef.h>

typedef struct
{
    void* buf;
    size_t bufCapacity; //Allocated memory for buf
    size_t elementSize; //Size in bytes of each element
    size_t elementCount; //Number of stored elements
} dyarray;

//Element size is the size in bytes of each element stored in the array (must be at least 1).
//Initial element capacity indicates the initial per-element capacity allocated for the array (must be at least 1).
//Returns false on failure.
bool DYArrayCreate(dyarray* arr, size_t elementSize, size_t initialElementCapacity);
void DYArrayFree(dyarray* arr);

//Copies the element for N bytes, where N is the singular element size.
//Returns false on failure.
bool DYArrayAddElement(dyarray* arr, void* element);

//Returns NULL on failure (example: the index is out of bounds).
void* DYArrayGetElement(dyarray* arr, size_t idx);

//Removes an element by swapping it with the last one, and then removing the last element,
//to achieve constant time complexity for the operation.
//This means that the array will NOT be ordered after.
//Returns false on failure.
bool DYArrayRemoveElementSP(dyarray* arr, size_t idx);

#ifdef DYARRAY_IMPL

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DYARRAY_EXPECT(condition, ...)\
do\
{\
    if (!condition)\
    {\
        printf("\033[31m" "ASSERTION FAILED at %s -- line %d\n", __FILE__, __LINE__);\
        __VA_OPT__(printf("Message: " "\033[39m", __VA_ARGS__);)\
        exit(1);\
    }\
} while (0)

#define DYARRAY_REALLOC_FACTOR 2

bool DYArrayCreate(dyarray* arr, size_t elementSize, size_t initialElementCapacity)
{
    if (!arr) {printf("DYArrayCreate ERROR: arr is NULL.\n"); return false;}
    if (!elementSize) {printf("DYArrayCreate ERROR: elementSize is 0.\n"); return false;}
    if (!initialElementCapacity) {printf("DYArrayCreate ERROR: initialElementCapacity is 0.\n"); return false;}

    arr->buf = malloc(elementSize * initialElementCapacity);
    if (!arr->buf) {printf("DYArrayCreate ERROR: malloc failed.\n"); return false;}

    arr->elementSize = elementSize;
    arr->elementCount = 0;
    arr->bufCapacity = initialElementCapacity * elementSize;

    return true;
}

void DYArrayFree(dyarray *arr)
{
    if (!arr) {printf("DYArrayFree ERROR: arr is NULL.\n"); return;}
    if (!arr->buf) {printf("DYArrayFree ERROR: arr.buf is NULL.\n"); return;}

    free(arr->buf);
    arr->buf = NULL;
    arr->bufCapacity = 0;
    arr->elementCount = 0;
    arr->elementSize = 0;
}

bool DYArrayAddElement(dyarray* arr, void* element)
{
    if (!arr) {printf("DYArrayAddElement ERROR: arr is NULL.\n"); return false;}
    if (!arr->buf) {printf("DYArrayAddElement ERROR: arr.buf is NULL.\n"); return false;}
    if (arr->elementSize == 0) {printf("DYArrayAddElement ERROR: arr.elementSize is 0.\n"); return false;}
    if (!element) {printf("DYArrayAddElement ERROR: element is NULL.\n"); return false;}

    //Realloc if necessary
    if (arr->elementCount * arr->elementSize >= arr->bufCapacity)
    {
        void* ptr = realloc(arr->buf, arr->bufCapacity * DYARRAY_REALLOC_FACTOR);
        if (!ptr) {printf("DYArrayAddElement ERROR: realloc failed.\n"); return false;}
        arr->buf = ptr;
        arr->bufCapacity *= DYARRAY_REALLOC_FACTOR;
    }

    memcpy(((char*)arr->buf) + arr->elementCount * arr->elementSize, element, arr->elementSize);
    arr->elementCount++;
    return true;
}

bool DYArrayRemoveElementSP(dyarray *arr, size_t idx)
{
    if (!arr) {printf("DYArrayAddElement ERROR: arr is NULL.\n"); return false;}
    if (!arr->buf) {printf("DYArrayAddElement ERROR: arr.buf is NULL.\n"); return false;}
    if (arr->elementSize == 0) {printf("DYArrayAddElement ERROR: arr.elementSize is 0.\n"); return false;}

    //Check if idx is out of bounds
    if (idx >= arr->elementCount) {printf("DYArrayAddElement ERROR: index is out of bounds\n"); return false;}

    memcpy(
        ((char*)arr->buf) + idx * arr->elementSize,
        ((char*)arr->buf) + arr->elementSize * (arr->elementCount - 1),
        arr->elementSize);

    arr->elementCount--;
    return true;
}

void* DYArrayGetElement(dyarray* arr, size_t idx)
{
    if (!arr) {printf("DYArrayGetElement ERROR: arr is NULL.\n"); return NULL;}
    if (!arr->buf) {printf("DYArrayGetElement ERROR: arr.buf is NULL.\n"); return NULL;}
    if (arr->elementSize == 0) {printf("DYArrayGetElement ERROR: arr.elementSize is 0.\n"); return NULL;}

    if (idx >= arr->elementCount) {printf("DYArrayAddElement ERROR: index is out of bounds\n"); return NULL;}

    return ((char*)arr->buf) + idx * arr->elementSize;
}

#endif //Impl

#endif //Include guard
