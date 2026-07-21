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

#define GECS_ENTITY_NAME_MAX_LENGTH 23
#define GECS_MAX_SYSTEM_COMPONENTS 8
#define GECS_MAX_COMPONENT_NAME_LENGTH 23
#define GECS_MAX_COMPONENT_FIELD_NAME_LENGTH 23
#define GECS_MAX_REGISTERED_COMPONENTS 128
#define GECS_MAX_COMPONENT_FIELDS 64

typedef struct
{
    size_t id;
    size_t gen;
} EntityID;

typedef size_t ComponentTypeID;
typedef size_t SystemID;

typedef struct
{
	char name[GECS_MAX_COMPONENT_FIELD_NAME_LENGTH + 1];
	uint32_t type;
} ComponentFieldInfo;

typedef struct
{
	ComponentFieldInfo componentFieldsInfo[GECS_MAX_COMPONENT_FIELDS];
	char name[GECS_MAX_COMPONENT_NAME_LENGTH + 1];
	uint32_t fieldCount;
} ComponentTypeInfo;

//typedef uint64_t ComponentsPresence[(GECS_MAX_REGISTERED_COMPONENTS + 63) / 64 /* manual ceil */];

typedef struct
{
    char name[GECS_ENTITY_NAME_MAX_LENGTH + 1];
    //ComponentsPresence components; It is best to do whole IDs instead of bitfield
} EntityInfo;

/*
 * @brief Initializes GECS, which is mandatory to use the library.
 */
void GECS_Init();

/*
 * @brief Register a System Archetype in the system.
 * The order in which the ComponentTypeIDs are specified is crucial to the component data access in the callback function,
 * since it matches exactly with the indices.
 * @param callback The function called upon system execution, entity having that exact component set.
 * @param componentCount The count of components specified in the variadic part of the arguments.
 * The variadic field expects the ComponentTypeIDs of the System's Archetype, in order.
 * @return The SystemID used to identify the newly created System's Archetype in the system.
 */
SystemID GECS_RegisterSystem(void (*callback)(EntityID, void**), int componentCount, ...);

/*
 * @brief Execute the callback defined by the system registration.
 * @param systemID The idendificator for the system to execute.
 */
void GECS_ExecuteSystem(SystemID systemID);

/*
 * @brief Register a component type in the system.
 * To attach any component to an entity, it must be registered through this function.
 * @param size The size in bytes of the singular component.
 * @param name The name of this component type.
 * @param fieldCount The number of elements (fields) this component consists of.
 * The variadic field is used to describe the elements this component has, coupled with the
 * previous argument "fieldCount", to provide introspection information for the system.
 * This field is made of "fieldType" and "fieldName" pairs, so for each field in the component,
 * insert the type and name in this order. The fields must be EXACTLY ordered and layed out as
 * you would use them in memory, with the fieldType also indicating the byte size of that field.
 * NOTE: the field types must be defined by the user, since it might have its own custom types,
 * and not basic ones such as simple primitives (int, bool, float). For that, do your own enum.
 * @return The unique id assigned to the newly registered component type.
 */
ComponentTypeID GECS_RegisterComponent(size_t size, const char* name, uint32_t fieldCount, ...);

/*
 * @brief Get a component type meta data.
 * @param componentTypeID The id of the requested component type.
 * @return A pointer to a read-only struct that contains the meta data for this
 * component type.
 */
const ComponentTypeInfo* GECS_GetComponentTypeInfo(ComponentTypeID componentTypeID);

/*
 * @brief Creates an entity in the system.
 * More than 1 entity can have the same name at the same time.
 * @param name The name of the entity.
 * @return The newly created entity on success. GECS_INVALID_ID on failure.
 */
EntityID GECS_CreateEntity(const char* name);

/*
 * @brief Deletes an existing entity and its associated components.
 * @param entity The target's entity ID.
 */
void GECS_DeleteEntity(EntityID entity);

/*
 * @brief Checks if an entity exists.
 * @param entity The target's entity ID.
 * @return True if the entity exists, false otherwise.
 */
bool GECS_DoesEntityExist(EntityID entity);

/*
 * @brief Attach a registered component to an existing entity.
 * You cannot attach the same component type to the same entity more than once.
 * @param componentData An allcated buffer long as the component type's size.
 */
void GECS_AttachComponent(EntityID entity, ComponentTypeID componentTypeID, void* componentData);

/*
 * @brief Detach a registered component from an existing entity.
 */
void GECS_DetachComponent(EntityID entity, ComponentTypeID componentTypeID);

/*
 * @brief Retrieves the component object from a specified existing entity.
 * Is it useful to check if an entity has a component.
 * @return The retrieved component data on success. NULL if the entity doesn't have the component.
 */
void* GECS_GetComponent(EntityID entity, ComponentTypeID componentTypeID);

/*
 * @brief Fast way to know if an entity has a component
 */
//bool GECS_DoesEntityHaveComponent(EntityID entity, ComponentTypeID componentTypeID);

/*
 * @brief Retrieves a specified entity's info.
 * @param entity The target entity's ID.
 * @return The entity's read-only info struct pointer.
 */
const EntityInfo* GECS_GetEntityInfo(EntityID entity);

/*
 * @brief Cleans up GECS, do it as soon as you're done with the library.
 */
void GECS_CleanUp();

#endif
