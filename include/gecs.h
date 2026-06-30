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
#include <stdint.h>

#define GECS_INVALID_ID 0
#define GECS_INVALID_GEN 0
#define GECS_INVALID_COMPONENT_TYPE_ID 0
#define GECS_INVALID_SYSTEM_ID 0
#define GECS_ENTITY_NAME_MAX_LENGTH 24

typedef struct
{
    size_t id;
    size_t gen;
} ID;

typedef size_t ComponentTypeID;
typedef size_t SystemID;

//@brief Initializes GECS, which is mandatory to use the library.
void GECS_Init();

//@brief Register a System Archetype in the system.
//The order in which the ComponentTypeIDs are specified is crucial to the component data access in the callback function,
//since it matches exactly with the indices.
//@param callback The function called upon system execution, entity having that exact component set.
//@param componentCount The count of components specified in the variadic part of the arguments.
//The variadic field expects the ComponentTypeIDs of the System's Archetype, in order.
//@return The SystemID used to identify the newly created System's Archetype in the system.
SystemID GECS_RegisterSystem(void (*callback)(ID, void**), int componentCount, ...);

//@brief Execute the callback defined by the system registration.
//@param systemID The idendificator for the system to execute.
void GECS_ExecuteSystem(SystemID systemID);

//@brief Register a component type in the system.
//To attach any component to an entity, it must be registered through this function.
//@param size The size in bytes of the singular component.
//@return The unique id assigned to the newly registered component type.
ComponentTypeID GECS_RegisterComponent(size_t size);

//@brief Creates an entity in the system.
//More than 1 entity can have the same name at the same time.
//@param name The name of the entity.
//@return The newly created entity on success. GECS_INVALID_ID on failure.
ID GECS_CreateEntity(const char* name);

//@brief Deletes an existing entity and its associated components.
//@param entity The target's entity ID.
void GECS_DeleteEntity(ID entity);

//@brief Checks if an entity exists.
//@param entity The target's entity ID.
//@return True if the entity exists, false otherwise.
bool GECS_DoesEntityExist(ID entity);

//@brief Attach a registered component to an existing entity.
//You cannot attach the same component type to the same entity more than once.
//@param componentData An allcated buffer long as the component type's size.
void GECS_AttachComponent(ID entity, ComponentTypeID componentTypeID, void* componentData);
//@brief Detach a registered component from an existing entity.
void GECS_DetachComponent(ID entity, ComponentTypeID componentTypeID);

//@brief Retrieves the component object from a specified existing entity.
//@return The retrieved component data on success. NULL if the entity doesn't have the component, useful to check existence.
void* GECS_GetComponent(ID entity, ComponentTypeID componentTypeID);

//@brief Retrieves a specified entity's name.
//@param entity The target entity's ID.
//@return The entity's null terminated name.
const char* GECS_GetEntityName(ID entity);

//@brief Cleans up GECS, do it as soon as you're done with the library.
void GECS_CleanUp();

#endif
