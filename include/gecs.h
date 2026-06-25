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

typedef size_t ID;

void GECS_RegisterComponent(const char* name, size_t size);
ID GECS_CreateEntity();
void GECS_DeleteEntity(ID entity);
void GECS_CreateComponent(ID entity, const char* componentName);
void GECS_DeleteComponent(ID entity, const char* componentName);

void GECS_CleanUp();

#endif
