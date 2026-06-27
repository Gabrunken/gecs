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

typedef size_t ID;

void GECS_Init();

void GECS_RegisterComponent(const char* name, size_t size);
//Returns GECS_INVALID_ID (aka 0) on failure
ID GECS_CreateEntity(const char* name);
void GECS_DeleteEntity(ID entity);
void GECS_CreateComponent(ID entity, const char* componentTypeName, void* componentData);
void GECS_DeleteComponent(ID entity, const char* componentTypeName);

struct SparseSet* GECS_GetComponentSparseSet(const char* name);
bool GECS_DoesEntityHaveComponent(ID entity, const char* componentTypeName);

void GECS_CleanUp();

#endif
