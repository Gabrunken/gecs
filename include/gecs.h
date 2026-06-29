#ifndef GECS_H_
#define GECS_H_

/*
        Written by Gabro
This is a small ECS library, made to target ease-of-use and
out-of-the-box functionality without too much headaches.
It is pure ECS, so components are POD (Plain Old Data) and entities
are logical IDs which you'll use to access the various components the entity
is composed of.
*/

#include <stddef.h>

#define GECS_INVALID_ID 0
#define GECS_ENTITY_NAME_MAX_LENGTH 24
#define GECS_COMPONENT_NAME_MAX_LENGTH 24

typedef struct
{
    size_t id;
    size_t gen;
} ID;

void GECS_Init();

void GECS_RegisterComponent(const char* name, size_t size);

//Returns GECS_INVALID_ID (aka 0) on failure
ID GECS_CreateEntity(const char* name);

//Deletes an entity and its associated components.
void GECS_DeleteEntity(ID entity);
void GECS_CreateComponent(ID entity, const char* componentTypeName, void* componentData);
void GECS_DeleteComponent(ID entity, const char* componentTypeName);

//Returns NULL if the entity doesn't have the component, useful to check existence.
void* GECS_GetComponent(ID entity, const char* componentTypeName);

struct SparseSet* GECS_GetComponentSparseSet(const char* name);

void GECS_CleanUp();

#endif
