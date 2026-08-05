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
#include <dyarray.h>
#include <sparse_set.h>

#define GECS_INVALID_ID 0
#define GECS_INVALID_GEN 0
#define GECS_INVALID_COMPONENT_TYPE_ID 0
#define GECS_INVALID_SYSTEM_ID 0

#define GECS_ENTITY_NAME_MAX_LENGTH 23
#define GECS_MAX_SYSTEM_COMPONENTS 8
#define GECS_MAX_COMPONENT_NAME_LENGTH 23
#define GECS_MAX_COMPONENT_FIELD_NAME_LENGTH 23
#define GECS_MAX_REGISTERED_COMPONENTS 128 /* Should never exceed 255, i'm using a uint8_t */
#define GECS_MAX_COMPONENT_FIELDS 64

typedef struct
{
    size_t id;
    size_t gen;
} EntityID;

typedef uint8_t ComponentTypeID;
typedef size_t SystemID; /* Big enough for storing as many systems as needed, i do no max checkings, so if you fuck up you fuck up. Congrats. */

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

typedef struct
{
    char name[GECS_ENTITY_NAME_MAX_LENGTH + 1];
    /*
     * The components this entity has (existence, not value).
     * These are NOT ordered, this is a dense, not ordered flat array.
     */
    ComponentTypeID componentsPresence[GECS_MAX_REGISTERED_COMPONENTS];
    uint8_t componentIDToPresenceIdx[GECS_MAX_REGISTERED_COMPONENTS];
    uint8_t componentCount;
} EntityInfo;

typedef struct
{
    dyarray IDsGeneration;
    dyarray IDsFreeList;
    struct SparseSet entitySparseSet;
    dyarray componentSparseSets; //Contains only SparseSets, not _RegisteredComponent data
    dyarray entityActivationState;
    dyarray entityComponentsActivationState; //Contains sparsesets, containing bools themselves
} GECSSnapshot;

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
 * @brief Saves the current Entity-Component instance's state into a file.
 * @param snapshot The loaded snapshot to save.
 * @param filePath a file path to the desired file destination.
 * @return True on success, False on failure.
 */
bool GECS_SaveSnapshotInDisk(const GECSSnapshot* snapshot, const char* filePath);

/*
 * @brief Loads the snapshot file into a GECSSnapshot struct.
 * @param filePath The snapshot's file path.
 * If the function fails to load the snapshot correctly, it will
 * return an invalid snapshot. Check with GECS_IsSnaphotValid.
 * A valid snapshot must be freed after use with GECS_FreeSnapshot.
 * @return The loaded snapshot.
 */
GECSSnapshot GECS_MakeSnapshotFromDisk(const char* filePath);

/*
 * @brief Directly saves the current state of the ECS in a file.
 * @param filePath The destination file path.
 * This function is faster than calling MakeSnapshot and SaveSnapshotInDisk
 * since it does not do an intermediate buffer allocation nor copy.
 * Altho if you needed the buffer for later use, you can still call MakeSnapshot manually.
 * @return True on success, False on failure.
 */
bool GECS_MakeAndSaveSnapshotInDisk(const char* filePath);

/*
 * @brief Directly creates and loads a snapshot into the ECS from a file.
 * @param filePath The source file path.
 * This function is faster than calling MakeSnapshot and LoadSnapshotFromDisk
 * since it does not do an intermediate buffer allocation nor copy.
 * Altho if you needed the buffer for later use, you can still call MakeSnapshot manually.
 * @return True on success, False on failure.
 */
bool GECS_MakeAndLoadSnapshotFromDisk(const char* filePath);

/*
 * @brief Makes a snapshot of the current state of GECS.
 * This function will save the binary data of each entity and component associated,
 * altho it does NOT save component nor system registration.
 * A valid snapshot must be freed after use with GECS_FreeSnapshot.
 * @return The allocated snapshot.
 */
GECSSnapshot GECS_MakeSnapshot();

/*
 * @brief Load into GECS the passed snapshot.
 * IMPORTANT: snapshots may not be compatible with different instances of GECS;
 * To successfully load a snapshot you must first assure that your ComponentTypes
 * match exactly the ones used in the passed snapshot, otherwise it will cause Undefined Behaviour.
 */
void GECS_LoadSnapshot(const GECSSnapshot* snapshot);

/*
 * @brief Frees the memory allocated by the passed snapshot.
 */
void GECS_FreeSnapshot(GECSSnapshot* snapshot);

/*
 * @brief Check if the passed snapshot is valid.
 * @return True if the snapshot is valid, False otherwise.
 */
bool GECS_IsSnapshotValid(const GECSSnapshot* snapshot);

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
 * @brief Deactivate this entity, disabling any system from interacting with it.
 * @param entity The entity ID to deactivate.
 */
void GECS_DeactivateEntity(EntityID entity);

/*
 * @brief Activate this entity, re-enabling the interaction with any system.
 * @param entity The entity ID to activate.
 */
void GECS_ActivateEntity(EntityID entity);

/*
 * @brief Checks if an entity is active.
 * @param entity The target entity.
 * @return True is the entity is active, False otherwise.
 */
bool GECS_IsEntityActive(EntityID entity);

/*
 * @brief Deactivate this entity's specified component, disabling any system from interacting with it.
 * @param entity The entity ID to deactivate.
 * @param componentTypeID The target entity's component id.
 */
void GECS_DeactivateEntityComponent(EntityID entity, ComponentTypeID componentTypeID);

/*
 * @brief Activate this entity's specified component, re-enabling the interaction with any system.
 * @param entity The entity ID to activate.
 * @param componentTypeID The target entity's component id.
 */
void GECS_ActivateEntityComponent(EntityID entity, ComponentTypeID componentTypeID);

/*
 * @brief Checks if an entity's specified component is active.
 * @param entity The target entity.
 * @param componentTypeID The target entity's component id.
 * @return True is the entity is active, False otherwise.
 */
bool GECS_IsEntityComponentActive(EntityID entity, ComponentTypeID componentTypeID);

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
//Not sure if i want this, might as well use GetComponent.
//bool GECS_DoesEntityHaveComponent(EntityID entity, ComponentTypeID componentTypeID);

void GECS_ClearECS();

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
