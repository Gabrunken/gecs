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
#define GECS_INVALID_GEN 0
#define GECS_INVALID_COMPONENT_TYPE_ID 0
#define GECS_ENTITY_NAME_MAX_LENGTH 24

typedef struct
{
    size_t id;
    size_t gen;
} ID;

typedef size_t ComponentTypeID;

void GECS_Init();

//Register a component type in the system.
//To attach any component to an entity, it must before registered through this function.
//"size" is the size in bytes of the singular component.
ComponentTypeID GECS_RegisterComponent(size_t size);

//Creates an entity in the system.
//Returns GECS_INVALID_ID on failure.
//More than 1 entity can have the same name.
ID GECS_CreateEntity(const char* name);

//Deletes an existing entity and its associated components.
void GECS_DeleteEntity(ID entity);

//Attach a registered component to an existing entity.
//You cannot attach the same component type to the same entity more than once.
//"componentData" must be an allcated buffer of the component type's size.
void GECS_AttachComponent(ID entity, ComponentTypeID componentTypeID, void* componentData);
//Detach a registered component from an existing entity.
void GECS_DetachComponent(ID entity, ComponentTypeID componentTypeID);

//Retrieves the component object from a specified existing entity.
//Returns NULL if the entity doesn't have the component, useful to check existence.
void* GECS_GetComponent(ID entity, ComponentTypeID componentTypeID);

struct SparseSet* GECS_GetComponentSparseSet(ComponentTypeID componentTypeID);

void GECS_CleanUp();

#endif
