#include <stdio.h>
#include <string.h>
#define DYARRAY_IMPL
#include <dyarray.h>
#include <stdlib.h>
#define SPARSE_SET_IMPL
#include <sparse_set.h>
#define HASHMAP_IMPL
#include <hashmap.h>
#include <stdarg.h>

#include <gecs.h> //Put it last to avoid reincluding single-headers.

#define GECS_INITIAL_ENTITY_ALLOC_SIZE 10'000

typedef struct
{
	struct SparseSet set;
	ComponentTypeInfo info;
} _RegisteredComponent;

typedef struct
{
	//In order!
	void (*callback)(EntityID, void**);
	ComponentTypeID components[GECS_MAX_SYSTEM_COMPONENTS];
	uint8_t componentCount;
} _SystemInfo;

/*
 * Vector containing "_SystemInfo" struct for each registered system.
 */
static dyarray _registeredSystems; //SystemID(s) will be used as indices (starting from 1) for this array containing _SystemInfo(s).

/*
 * Contains a "_RegisteredComponent" struct for each registered component type.
 * Any component cannot be removed once registered.
 */
static dyarray _registeredComponents;

/*
 * A SparseSet containing "EntityInfo" structs for each entity created.
 */
static struct SparseSet _entities;

//Generational IDs
static dyarray _currentIDsGeneration;
static dyarray _currentIDsFreeList;

//Entity and components activation state
/*
 * This one simply contains bools, and its indexed by using entity IDs - 1.
 * Everytime an entity is created, this dyarray adds an element to itself only
 * if the id is a new id and not picked from the freelist, otherwise it resets the
 * freelist's picked id to be in an active state.
 */
static dyarray _entityActivationState;

/*
 * This one contains a SparseSet for each component type, like _registeredComponents,
 * only that the value inside the SparseSets are bools.
 * SparseSets use standard ID indexing with entity IDs.
 */
static dyarray _entityComponentsActivationState;

//Entity and components living state
/*
 * This one simply contains bools, and its indexed by using entity IDs - 1.
 * Everytime an entity is created, this dyarray adds an element to itself only
 * if the id is a new id and not picked from the freelist, otherwise it resets the
 * freelist's picked id to be in an active state.
 */
static dyarray _entityLivingState;

/*
 * This one contains a SparseSet for each component type, like _registeredComponents,
 * only that the value inside the SparseSets are bools.
 * SparseSets use standard ID indexing with entity IDs.
 */
static dyarray _entityComponentsLivingState;

static dyarray _commandQueue;

static bool _initialized;

#define GECS_EXPECT(condition, ...)\
do\
{\
    if (!condition)\
    {\
        printf("\033[31m" "ASSERTION FAILED at %s -- line %d\n", __FILE__, __LINE__);\
        __VA_OPT__(printf("Message: " "\033[39m", __VA_ARGS__);)\
        exit(1);\
    }\
} while (0)

struct SparseSet* _GECS_GetComponentSparseSet(ComponentTypeID componentTypeID);

typedef enum
{
    GECS_CMD_DELETE_ENTITY,
    GECS_CMD_ATTACH_COMPONENT, //Needed in case of SparseSet reallocation and pointer invalidation.
    GECS_CMD_DETACH_COMPONENT,
    GECS_CMD_CLEAR_ECS, //All these functions below and this included will invalidate any command after them in the cmd buffer.
    GECS_CMD_LOAD_SNAPSHOT,
    GECS_CMD_MAKE_AND_LOAD_SNAPSHOT,
} GECSCommandType;

typedef struct
{
    GECSCommandType type;

    EntityID targetEntity;
    ComponentTypeID targetComponent;

    union {
        void* componentDataBuffer; // Usato per ATTACH
        struct {
        	GECSSnapshot* snapshotToLoad; // Usato per LOAD
         	const char* filePath; //Usato per MAKE and LOAD
        };
    } payload;

} GECSCommand; //A Big Ass struct to hold a possible command in the cmd buffer.

void GECS_Init()
{
	GECS_EXPECT(!_initialized);

	SparseSetCreate(&_entities, 0, sizeof(EntityInfo));

	DyArrayCreate(&_currentIDsGeneration, sizeof(size_t), 100);
	DyArrayCreate(&_currentIDsFreeList, sizeof(size_t), 100);
	GECS_EXPECT(_currentIDsGeneration.buf && _currentIDsFreeList.buf);

	GECS_EXPECT(DyArrayCreate(&_registeredComponents, sizeof(_RegisteredComponent), 10));
	GECS_EXPECT(DyArrayCreate(&_registeredSystems, sizeof(_SystemInfo), 10));
	GECS_EXPECT(DyArrayCreate(&_entityActivationState, sizeof(bool), 100));
	GECS_EXPECT(DyArrayCreate(&_entityComponentsActivationState, sizeof(struct SparseSet), 10));
	GECS_EXPECT(DyArrayCreate(&_entityLivingState, sizeof(bool), 100));
	GECS_EXPECT(DyArrayCreate(&_entityComponentsLivingState, sizeof(struct SparseSet), 10));
	GECS_EXPECT(DyArrayCreate(&_commandQueue, sizeof(GECSCommand), 100));

	_initialized = true;
}

void GECS_DeleteEntity(EntityID entity)
{
	GECSCommand cmd = {0};
	cmd.type = GECS_CMD_DELETE_ENTITY;
	cmd.targetEntity = entity;

	DyArrayAddElement(&_commandQueue, &cmd);
}

void GECS_AttachComponent(EntityID entity, ComponentTypeID componentTypeID, void* componentData)
{
	GECSCommand cmd = {0};
	cmd.type = GECS_CMD_ATTACH_COMPONENT;
	cmd.targetEntity = entity;
	cmd.targetComponent = componentTypeID;
	const ComponentTypeInfo* componentInfo = GECS_GetComponentTypeInfo(componentTypeID);
	cmd.payload.componentDataBuffer = malloc(componentInfo->componentSize);
	if (!cmd.payload.componentDataBuffer) {
		printf("GECS_AttachComponent ERROR: malloc failed.\n");
		return;
	}

	memcpy(cmd.payload.componentDataBuffer, componentData, componentInfo->componentSize);

	DyArrayAddElement(&_commandQueue, &cmd);
}

void GECS_DetachComponent(EntityID entity, ComponentTypeID componentTypeID)
{
	GECSCommand cmd = {0};
	cmd.type = GECS_CMD_DETACH_COMPONENT;
	cmd.targetEntity = entity;
	cmd.targetComponent = componentTypeID;

	DyArrayAddElement(&_commandQueue, &cmd);
}

void GECS_LoadSnapshot(const GECSSnapshot* snapshot)
{
	GECSCommand cmd = {0};
	cmd.type = GECS_CMD_LOAD_SNAPSHOT;
	cmd.payload.snapshotToLoad = (GECSSnapshot*)snapshot;

	DyArrayAddElement(&_commandQueue, &cmd);
}

bool GECS_MakeAndLoadSnapshotFromDisk(const char* filePath)
{
	GECSCommand cmd = {0};
	cmd.type = GECS_CMD_MAKE_AND_LOAD_SNAPSHOT;
	cmd.payload.filePath = _strdup(filePath);

	DyArrayAddElement(&_commandQueue, &cmd);
}

void GECS_ClearECS()
{
	GECSCommand cmd = {0};
	cmd.type = GECS_CMD_CLEAR_ECS;

	DyArrayAddElement(&_commandQueue, &cmd);
}

SystemID GECS_RegisterSystem(void (*callback)(EntityID, void**), int componentCount, ...)
{
	GECS_EXPECT(_initialized);
	if (!callback)
	{
		printf("GECS_RegisterSystem ERROR: callback is NULL.\n");
		return GECS_INVALID_SYSTEM_ID;
	}

	if (componentCount == 0)
	{
		printf("GECS_RegisterSystem ERROR: componentCount is 0.\n");
		return GECS_INVALID_SYSTEM_ID;
	}

	if (componentCount > GECS_MAX_SYSTEM_COMPONENTS)
	{
		printf("GECS_RegisterSystem ERROR: componentCount is above the maximum allowed (%d).\n", GECS_MAX_SYSTEM_COMPONENTS);
		return GECS_INVALID_SYSTEM_ID;
	}

	_SystemInfo info = {0};
	info.componentCount = componentCount;
	info.callback = callback;

	va_list args;
	va_start(args, componentCount);

	ComponentTypeID componentTypeID;
	for (int i = 0; i < componentCount; i++)
	{
		int notSafeID = va_arg(args, int); //Variadic arguments are always promoted to int, even if smaller
		//Check if the user fucked up, by providing an invalid id
		if (!(notSafeID > 0 && notSafeID <= _registeredComponents.elementCount)){
			printf("GECS_RegisterSystem ERROR: the input ID %d is not a valid component type.\n", notSafeID);
			va_end(args);
			return GECS_INVALID_SYSTEM_ID;
		}

		componentTypeID = notSafeID; //Now safe lol

		info.components[i] = componentTypeID;
	}

	DyArrayAddElement(&_registeredSystems, &info);

	va_end(args);

	return _registeredSystems.elementCount;
}

SystemID GECS_vRegisterSystem(void (*callback)(EntityID, void**), int componentCount, va_list args)
{
	GECS_EXPECT(_initialized);
	if (!callback)
	{
		printf("GECS_vRegisterSystem ERROR: callback is NULL.\n");
		return GECS_INVALID_SYSTEM_ID;
	}

	if (componentCount == 0)
	{
		printf("GECS_vRegisterSystem ERROR: componentCount is 0.\n");
		return GECS_INVALID_SYSTEM_ID;
	}

	if (componentCount > GECS_MAX_SYSTEM_COMPONENTS)
	{
		printf("GECS_vRegisterSystem ERROR: componentCount is above the maximum allowed (%d).\n", GECS_MAX_SYSTEM_COMPONENTS);
		return GECS_INVALID_SYSTEM_ID;
	}

	_SystemInfo info = {0};
	info.componentCount = componentCount;
	info.callback = callback;

	ComponentTypeID componentTypeID;
	for (int i = 0; i < componentCount; i++)
	{
		int notSafeID = va_arg(args, int); //Variadic arguments are always promoted to int, even if smaller
		//Check if the user fucked up, by providing an invalid id
		if (!(notSafeID > 0 && notSafeID <= _registeredComponents.elementCount)){
			printf("GECS_vRegisterSystem ERROR: the input ID %d is not a valid component type.\n", notSafeID);
			return GECS_INVALID_SYSTEM_ID;
		}

		componentTypeID = notSafeID; //Now safe lol

		info.components[i] = componentTypeID;
	}

	DyArrayAddElement(&_registeredSystems, &info);

	return _registeredSystems.elementCount;
}

void GECS_ExecuteSystem(SystemID systemID)
{
	GECS_EXPECT(_initialized);

	if (systemID == GECS_INVALID_SYSTEM_ID)
	{
		printf("GECS_ExecuteSystem ERROR: systemID is invalid.\n");
		return;
	}

	if (systemID > _registeredSystems.elementCount)
	{
		printf("GECS_ExecuteSystem ERROR: systemID is not a registered system.\n");
		return;
	}

	_SystemInfo* info = DyArrayGetElement(&_registeredSystems, systemID - 1);
	struct SparseSet* componentSets[GECS_MAX_SYSTEM_COMPONENTS] = {0};
	struct SparseSet* smallestSet;

	//Iterate through all system associated components and fetch the smallest set.
	for (uint8_t i = 0; i < info->componentCount; i++)
	{
		componentSets[i] = _GECS_GetComponentSparseSet(info->components[i]);
		if (i == 0) {smallestSet = componentSets[i]; continue;}

		if (SparseSetGetElementCount(componentSets[i]) < SparseSetGetElementCount(smallestSet))
		{
			smallestSet = componentSets[i];
		}
	}

	//Iterate for each element on that set.
	size_t smallestSetLength = SparseSetGetElementCount(smallestSet);
	void* components[GECS_MAX_SYSTEM_COMPONENTS] = {0};
	for (size_t i = 0; i < smallestSetLength; i++)
	{
		bool archetypeFound = true;

		//Find matching components.
		size_t smallestSetElementID = SparseSetGetIDFromPhysicalIndex(smallestSet, i); //This is the entity ID of this smallest set's component parent entity.
		//Check if the whole entity is active or not
		/* Check manually since the public function also requires a gen, and we don't need it, we know the entity exists. */
		bool* isEntityActive = DyArrayGetElement(&_entityActivationState, smallestSetElementID - 1);
		if(*isEntityActive == false) {continue;}
		bool* isEntityAlive = DyArrayGetElement(&_entityLivingState, smallestSetElementID - 1);
		if(*isEntityAlive == false) {continue;}

		for (uint8_t j = 0; j < info->componentCount; j++)
		{
			//Check if this entity has this component.
			if (!SparseSetHasElement(componentSets[j], smallestSetElementID)) {archetypeFound = false; break;}
			//Check if the component is active
			struct SparseSet* componentActiveStateSet = DyArrayGetElement(&_entityComponentsActivationState, info->components[j] - 1);
			bool* isComponentActive = SparseSetGetElement(componentActiveStateSet, smallestSetElementID);
			if(*isComponentActive == false) {archetypeFound = false; break;}

			struct SparseSet* componentLivingStateSet = DyArrayGetElement(&_entityComponentsLivingState, info->components[j] - 1);
			bool* isComponentAlive = SparseSetGetElement(componentLivingStateSet, smallestSetElementID);
			if(*isComponentAlive == false) {archetypeFound = false; break;}

			components[j] = SparseSetGetElement(componentSets[j], smallestSetElementID);
		}

		//The needed component set for this system has not been found in this entity.
		if (!archetypeFound) continue;

		size_t gen = *(size_t*)DyArrayGetElement(&_currentIDsGeneration, smallestSetElementID - 1);
		//Call the user-defined system's routine.
		info->callback((EntityID){smallestSetElementID, gen}, components);
	}
}

void GECS_DeactivateEntity(EntityID entity)
{
	GECS_EXPECT(_initialized);

	if (!GECS_DoesEntityExist(entity)) {
		printf("GECS_DeactivateEntity ERROR: passed entity does not exist.\n");
		return;
	}

	bool* activationState = DyArrayGetElement(&_entityActivationState, entity.id - 1);
	*activationState = false;
}

void GECS_ActivateEntity(EntityID entity)
{
	GECS_EXPECT(_initialized);

	if (!GECS_DoesEntityExist(entity)) {
		printf("GECS_ActivateEntity ERROR: passed entity does not exist.\n");
		return;
	}

	bool* activationState = DyArrayGetElement(&_entityActivationState, entity.id - 1);
	*activationState = true;
}

bool GECS_IsEntityActive(EntityID entity)
{
	GECS_EXPECT(_initialized);

	if (!GECS_DoesEntityExist(entity)) {
		printf("GECS_IsEntityActive ERROR: passed entity does not exist.\n");
		return false;
	}

	bool* activationState = DyArrayGetElement(&_entityActivationState, entity.id - 1);
	return *activationState;
}

void GECS_DeactivateEntityComponent(EntityID entity, ComponentTypeID componentTypeID)
{
	GECS_EXPECT(_initialized);

	if (!GECS_DoesEntityExist(entity)) {
		printf("GECS_DeactivateEntityComponent ERROR: entity does not exist.\n");
		return;
	}

	struct SparseSet* activeStateSet = DyArrayGetElement(&_entityComponentsActivationState, componentTypeID - 1);
	bool* activeState = SparseSetGetElement(activeStateSet, entity.id);
	if (!activeState) {
		printf("GECS_DeactivateEntityComponent ERROR: entity of id %zu and generation %zu does not have any component of type id %d.\n",
			entity.id,
			entity.gen,
			componentTypeID
		);
	}

	*activeState = false;
}

void GECS_ActivateEntityComponent(EntityID entity, ComponentTypeID componentTypeID)
{
	GECS_EXPECT(_initialized);

	if (!GECS_DoesEntityExist(entity)) {
		printf("GECS_ActivateEntityComponent ERROR: entity does not exist.\n");
		return;
	}

	struct SparseSet* activeStateSet = DyArrayGetElement(&_entityComponentsActivationState, componentTypeID - 1);
	bool* activeState = SparseSetGetElement(activeStateSet, entity.id);
	if (!activeState) {
		printf("GECS_ActivateEntityComponent ERROR: entity of id %zu and generation %zu does not have any component of type id %d.\n",
			entity.id,
			entity.gen,
			componentTypeID
		);
	}

	*activeState = true;
}

bool GECS_IsEntityComponentActive(EntityID entity, ComponentTypeID componentTypeID)
{
	GECS_EXPECT(_initialized);

	if (!GECS_DoesEntityExist(entity)) {
		printf("GECS_ActivateEntityComponent ERROR: entity does not exist.\n");
		return false;
	}

	struct SparseSet* activeStateSet = DyArrayGetElement(&_entityComponentsActivationState, componentTypeID - 1);
	bool* activeState = SparseSetGetElement(activeStateSet, entity.id);
	if (!activeState) {
		printf("GECS_ActivateEntityComponent ERROR: entity of id %zu and generation %zu does not have any component of type id %d.\n",
			entity.id,
			entity.gen,
			componentTypeID
		);
	}

	return *activeState;
}

bool _GECS_DoesComponentTypeExist(ComponentTypeID componentTypeID)
{
	GECS_EXPECT(_initialized);

	if (componentTypeID == GECS_INVALID_COMPONENT_TYPE_ID || componentTypeID > _registeredComponents.elementCount){
		return false;
	}

	return true;
}

struct SparseSet* _GECS_GetComponentSparseSet(ComponentTypeID componentTypeID)
{
	GECS_EXPECT(_initialized);

	if (!_GECS_DoesComponentTypeExist(componentTypeID)){
		printf("_GECS_GetComponentSparseSet ERROR: componentTypeID %d does not exist.\n", componentTypeID);
		return NULL;
	}

	_RegisteredComponent* componentInfo = DyArrayGetElement(&_registeredComponents, componentTypeID - 1);
	return &componentInfo->set;
}

void* GECS_GetComponent(EntityID entity, ComponentTypeID componentTypeID)
{
	GECS_EXPECT(_initialized);

	if (!GECS_DoesEntityExist(entity)){
		printf("GECS_GetComponent ERROR: entity of id %zu and generation %zu does not exist.\n", entity.id, entity.gen);
		return NULL;
	}

	struct SparseSet* set = _GECS_GetComponentSparseSet(componentTypeID);
	if (!set){
		printf("GECS_GetComponent ERROR: componentTypeID %d is not valid.\n", componentTypeID);
		return NULL;
	}

	//Returns NULL if the element is not present in the set (it is not an error, it is expected)
	return SparseSetGetElement(set, entity.id);
}

const EntityInfo* GECS_GetEntityInfo(EntityID entity)
{
	GECS_EXPECT(_initialized);

	if (!GECS_DoesEntityExist(entity)){
		printf("GECS_GetEntityInfo ERROR: entity does not exist.\n");
		return NULL;
	}

	return SparseSetGetElement(&_entities, entity.id); //Returns NULL if there is no entity with that id.
}

ComponentTypeID GECS_RegisterComponent(size_t size, const char* name, uint32_t fieldCount, ...)
{
	GECS_EXPECT(_initialized);

	if (_registeredComponents.elementCount == GECS_MAX_REGISTERED_COMPONENTS){
		printf("GECS_RegisterComponent ERROR: max registered components reached (%d).\n", GECS_MAX_REGISTERED_COMPONENTS);
		return GECS_INVALID_COMPONENT_TYPE_ID;
	}

	if (!size){
		printf("GECS_RegisterComponent ERROR: size is 0.\n");
		return GECS_INVALID_COMPONENT_TYPE_ID;
	}

	if (!name){
		printf("GECS_RegisterComponent ERROR: name is NULL.\n");
		return GECS_INVALID_COMPONENT_TYPE_ID;
	}

	if (fieldCount > GECS_MAX_COMPONENT_FIELDS){
		printf("GECS_RegisterComponent ERROR: fieldCount is above the limit (%d).\n", GECS_MAX_COMPONENT_FIELDS);
		return GECS_INVALID_COMPONENT_TYPE_ID;
	}

	_RegisteredComponent componentInfo = {0};
	componentInfo.info.fieldCount = fieldCount;
	strncpy(componentInfo.info.name, name, GECS_MAX_COMPONENT_NAME_LENGTH);
	componentInfo.info.componentSize = size;

	va_list args;
	va_start(args, fieldCount);

	//Iterate for each field
	for (uint32_t i = 0; i < fieldCount; i++)
	{
		uint32_t fieldType = va_arg(args, uint32_t);
		const char* fieldName = va_arg(args, char*);
		if (!fieldName){
			printf("GECS_RegisterComponent ERROR: a variadic argument is NULL.\n");
			va_end(args);
			return GECS_INVALID_COMPONENT_TYPE_ID;
		}

		componentInfo.info.componentFieldsInfo[i].type = fieldType;

		strncpy(componentInfo.info.componentFieldsInfo[i].name, fieldName, GECS_MAX_COMPONENT_FIELD_NAME_LENGTH);
	}

	va_end(args);

	struct SparseSet set = {0};
	SparseSetCreate(&set, 0, size);
	componentInfo.set = set;

	DyArrayAddElement(&_registeredComponents, &componentInfo);

	struct SparseSet componentActivationStateSet;
	SparseSetCreate(&componentActivationStateSet, sizeof(bool), 100);
	DyArrayAddElement(&_entityComponentsActivationState, &componentActivationStateSet);

	struct SparseSet componentLivingStateSet;
	SparseSetCreate(&componentLivingStateSet, sizeof(bool), 100);
	DyArrayAddElement(&_entityComponentsLivingState, &componentLivingStateSet);

	//The first ComponentTypeID starts from 1.
	return _registeredComponents.elementCount;
}

ComponentTypeID GECS_vRegisterComponent(size_t size, const char* name, uint32_t fieldCount, va_list args)
{
	GECS_EXPECT(_initialized);

	if (_registeredComponents.elementCount == GECS_MAX_REGISTERED_COMPONENTS){
		printf("GECS_vRegisterComponent ERROR: max registered components reached (%d).\n", GECS_MAX_REGISTERED_COMPONENTS);
		return GECS_INVALID_COMPONENT_TYPE_ID;
	}

	if (!size){
		printf("GECS_vRegisterComponent ERROR: size is 0.\n");
		return GECS_INVALID_COMPONENT_TYPE_ID;
	}

	if (!name){
		printf("GECS_vRegisterComponent ERROR: name is NULL.\n");
		return GECS_INVALID_COMPONENT_TYPE_ID;
	}

	if (fieldCount > GECS_MAX_COMPONENT_FIELDS){
		printf("GECS_vRegisterComponent ERROR: fieldCount is above the limit (%d).\n", GECS_MAX_COMPONENT_FIELDS);
		return GECS_INVALID_COMPONENT_TYPE_ID;
	}

	_RegisteredComponent componentInfo = {0};
	componentInfo.info.fieldCount = fieldCount;
	strncpy(componentInfo.info.name, name, GECS_MAX_COMPONENT_NAME_LENGTH);
	componentInfo.info.componentSize = size;

	//Iterate for each field
	for (uint32_t i = 0; i < fieldCount; i++)
	{
		uint32_t fieldType = va_arg(args, uint32_t);
		const char* fieldName = va_arg(args, char*);
		if (!fieldName){
			printf("GECS_vRegisterComponent ERROR: a variadic argument is NULL.\n");
			return GECS_INVALID_COMPONENT_TYPE_ID;
		}

		componentInfo.info.componentFieldsInfo[i].type = fieldType;

		strncpy(componentInfo.info.componentFieldsInfo[i].name, fieldName, GECS_MAX_COMPONENT_FIELD_NAME_LENGTH);
	}

	struct SparseSet set = {0};
	SparseSetCreate(&set, 0, size);
	componentInfo.set = set;

	DyArrayAddElement(&_registeredComponents, &componentInfo);

	struct SparseSet componentActivationStateSet;
	SparseSetCreate(&componentActivationStateSet, sizeof(bool), 100);
	DyArrayAddElement(&_entityComponentsActivationState, &componentActivationStateSet);

	struct SparseSet componentLivingStateSet;
	SparseSetCreate(&componentLivingStateSet, sizeof(bool), 100);
	DyArrayAddElement(&_entityComponentsLivingState, &componentLivingStateSet);

	//The first ComponentTypeID starts from 1.
	return _registeredComponents.elementCount;
}

const ComponentTypeInfo* GECS_GetComponentTypeInfo(ComponentTypeID componentTypeID)
{
	GECS_EXPECT(_initialized);

	if (!_GECS_DoesComponentTypeExist(componentTypeID)){
		printf("GECS_GetComponentTypeInfo ERROR: componentTypeID %d does not exist.\n", componentTypeID);
		return NULL;
	}

	_RegisteredComponent* componentInfo = DyArrayGetElement(&_registeredComponents, componentTypeID - 1);
	return &componentInfo->info;
}

//Also takes responsibility to increase generations.
static EntityID _GECS_GetNewID()
{
	GECS_EXPECT(_initialized);

	EntityID id = {0};

	size_t entityCount = SparseSetGetElementCount(&_entities);

	//Check if theres something in the free list
	if (_currentIDsFreeList.elementCount > 0)
	{
		//Take last element
		size_t freeIDIdx = *(size_t*)DyArrayGetElement(&_currentIDsFreeList, _currentIDsFreeList.elementCount - 1);

		size_t currentGen = *(size_t*)DyArrayGetElement(&_currentIDsGeneration, freeIDIdx);
		currentGen++;

		DyArraySetElement(&_currentIDsGeneration, freeIDIdx, &currentGen);
		bool* activationState = DyArrayGetElement(&_entityActivationState, freeIDIdx /*Correct indexing since it represents entity ID - 1*/);
		*activationState = true; //Reactivate this entity in case it had been deactivated before deletion.
		bool* livingState = DyArrayGetElement(&_entityLivingState, freeIDIdx /*Correct indexing since it represents entity ID - 1*/);
		*livingState = true; //Reactivate this entity living state in case it had been deactivated before deletion.

		id.id = freeIDIdx + 1;
		id.gen = currentGen;

		DyArrayRemoveElementSP(&_currentIDsFreeList, _currentIDsFreeList.elementCount - 1);
		return id;
	}

	//Brand new EntityID
	id.id = entityCount + 1;
	id.gen = 1;

	DyArrayAddElement(&_currentIDsGeneration, &id.gen);
	DyArrayAddElement(&_entityActivationState, &(bool){true});
	DyArrayAddElement(&_entityLivingState, &(bool){true});

	return id;
}

EntityID GECS_CreateEntity(const char *name)
{
	GECS_EXPECT(_initialized);

	if (!name)
	{
		printf("GECS_CreateEntity ERROR: 'name' is NULL.\n");
		return (EntityID){GECS_INVALID_ID, GECS_INVALID_GEN};
	}

	size_t nameLen = strlen(name);
	if (nameLen == 0)
	{
		printf("GECS_CreateEntity ERROR: 'name' is empty.\n");
		return (EntityID){GECS_INVALID_ID, GECS_INVALID_GEN};
	}

	if (nameLen > GECS_ENTITY_NAME_MAX_LENGTH)
	{
		printf("GECS_CreateEntity ERROR: 'name' is too long, max is %d.\n", GECS_ENTITY_NAME_MAX_LENGTH);
		return (EntityID){GECS_INVALID_ID, GECS_INVALID_GEN};
	}

	EntityID id = _GECS_GetNewID(); //Automatically increases entityCount on success.
	if (!id.id)
	{
		printf("GECS_CreateEntity ERROR: failed to create new entity of name %s.\n", name);
		return id;
	}

	EntityInfo entity = {0};
	memset(entity.componentIDToPresenceIdx, 255, GECS_MAX_REGISTERED_COMPONENTS * sizeof(uint8_t));

	strncpy(entity.name, name, GECS_ENTITY_NAME_MAX_LENGTH);

	SparseSetAddElement(&_entities, id.id, &entity);

	return id;
}

void _GECS_DeleteEntity_Instant(EntityID entity)
{
	GECS_EXPECT(_initialized);

	if (!GECS_DoesEntityExist(entity)){
		printf("GECS_DeleteEntity ERROR: entity of id %zu and generation %zu does not exist.\n", entity.id, entity.gen);
		return;
	}

	//First remove any components it had, by checking its metadata
	EntityInfo* entityInfo = SparseSetGetElement(&_entities, entity.id);
	for (uint8_t i = 0; i < entityInfo->componentCount; i++)
	{
		ComponentTypeID target = entityInfo->componentsPresence[i];

		_RegisteredComponent* componentInfo = DyArrayGetElement(&_registeredComponents, target - 1);
		GECS_DetachComponent(entity, target); //Call the standard function since it does a proper cleanup instead of doing it manually.
	}

	//Add the removed id to the free list
	size_t freeIDIdx = entity.id - 1;
	DyArrayAddElement(&_currentIDsFreeList, &freeIDIdx);

	SparseSetRemoveElement(&_entities, entity.id);
}

bool GECS_DoesEntityExist(EntityID entity)
{
	GECS_EXPECT(_initialized);

	if (entity.id == GECS_INVALID_ID || entity.gen == GECS_INVALID_GEN)
		return false;

	if (!SparseSetHasElement(&_entities, entity.id))
		return false;

	size_t gen = *(size_t*)DyArrayGetElement(&_currentIDsGeneration, entity.id - 1);
	if (gen != entity.gen)
		return false;

	return true;
}

void _GECS_AttachComponent_Instant(EntityID entity, ComponentTypeID componentTypeID, void* componentData)
{
	GECS_EXPECT(_initialized);

	if (!componentData)
	{
		printf("GECS_AttachComponent ERROR: componentData is NULL.\n");
		return;
	}

	if (!GECS_DoesEntityExist(entity)){
		printf("GECS_AttachComponent ERROR: entity of id %zu and generation %zu does not exist.\n", entity.id, entity.gen);
		return;
	}

	struct SparseSet* componentSet = _GECS_GetComponentSparseSet(componentTypeID);
	if (!componentSet){
		printf("GECS_AttachComponent ERROR: componentTypeID %d is not valid.\n", componentTypeID);
		return;
	}

	//Check if the entity already has that component type
	if (SparseSetHasElement(componentSet, entity.id))
	{
		printf("GECS_AttachComponent ERROR: entity %zu already has component type of id %d.\n", entity.id, componentTypeID);
		return;
	}

	SparseSetAddElement(componentSet, entity.id, componentData);

	EntityInfo* entityInfo = SparseSetGetElement(&_entities, entity.id);
	entityInfo->componentsPresence[entityInfo->componentCount] = componentTypeID;
	entityInfo->componentIDToPresenceIdx[componentTypeID - 1] = entityInfo->componentCount;
	entityInfo->componentCount++;

	struct SparseSet* componentActiveStateSet = DyArrayGetElement(&_entityComponentsActivationState, componentTypeID - 1);
	SparseSetAddElement(componentActiveStateSet, entity.id, &(bool){true});
	struct SparseSet* componentLivingStateSet = DyArrayGetElement(&_entityComponentsLivingState, componentTypeID - 1);
	SparseSetAddElement(componentLivingStateSet, entity.id, &(bool){true});
}

void _GECS_DetachComponent_Instant(EntityID entity, ComponentTypeID componentTypeID)
{
	GECS_EXPECT(_initialized);

	if (!GECS_DoesEntityExist(entity)){
		printf("GECS_DetachComponent ERROR: entity of id %zu and generation %zu does not exist.\n", entity.id, entity.gen);
		return;
	}

	struct SparseSet* componentSet = _GECS_GetComponentSparseSet(componentTypeID);
	if (!componentSet){
		printf("GECS_DetachComponent ERROR: componentTypeID %d is not valid.\n", componentTypeID);
		return;
	}

	//Check if the entity has that component
	if (!SparseSetHasElement(componentSet, entity.id))
	{
		printf("GECS_DetachComponent ERROR: entity %zu does not have component type id %d.\n", entity.id, componentTypeID);
		return;
	}

	SparseSetRemoveElement(componentSet, entity.id);

	EntityInfo* entityInfo = SparseSetGetElement(&_entities, entity.id);
	//Swap and pop
	//I could check if the index is 255 (no component), but i do this check above
	uint8_t targetIdx = entityInfo->componentIDToPresenceIdx[componentTypeID - 1];
	entityInfo->componentsPresence[targetIdx] = entityInfo->componentsPresence[entityInfo->componentCount - 1];
	entityInfo->componentIDToPresenceIdx[componentTypeID - 1] = 255; /* Invalid index, since in the system there can only be no more than 255 components */
	entityInfo->componentCount--;

	struct SparseSet* componentActiveStateSet = DyArrayGetElement(&_entityComponentsActivationState, componentTypeID - 1);
	SparseSetRemoveElement(componentActiveStateSet, entity.id);
	struct SparseSet* componentLivingStateSet = DyArrayGetElement(&_entityComponentsLivingState, componentTypeID - 1);
	SparseSetRemoveElement(componentLivingStateSet, entity.id);
}

void GECS_CleanUp()
{
	GECS_EXPECT(_initialized);

	//Free sparse sets
	for (size_t i = 0; i < _registeredComponents.elementCount; i++)
	{
		_RegisteredComponent* componentInfo = DyArrayGetElement(&_registeredComponents, i);
		SparseSetFree(&componentInfo->set);
	}

	DyArrayFree(&_registeredComponents);
	DyArrayFree(&_registeredSystems);

	DyArrayFree(&_currentIDsGeneration);
	DyArrayFree(&_currentIDsFreeList);

	SparseSetFree(&_entities);

	DyArrayFree(&_entityActivationState);

	for (size_t i = 0; i < _entityComponentsActivationState.elementCount; i++) {
		struct SparseSet* set = DyArrayGetElement(&_entityComponentsActivationState, i);
		SparseSetFree(set);
	}

	DyArrayFree(&_entityComponentsActivationState);

	DyArrayFree(&_entityLivingState);

	for (size_t i = 0; i < _entityComponentsLivingState.elementCount; i++) {
		struct SparseSet* set = DyArrayGetElement(&_entityComponentsLivingState, i);
		SparseSetFree(set);
	}

	DyArrayFree(&_entityComponentsLivingState);

	DyArrayFree(&_commandQueue);

	_initialized = false;
}

/*
 * ===========================================================================
 * ================================ SNAPSHOTS ================================
 * ===========================================================================
 */

GECSSnapshot GECS_MakeSnapshot() {
	GECS_EXPECT(_initialized);

	/*
	 * Copy the current state of components and entities in a buffer.
	 * The useful data to load is the whole entity sparse set and
	 * each _RegisteredComponent present in the system.
	 * Also copy the free list and gen list
	 */

	//Always take GECSSnapshot struct formatting as reference

	GECSSnapshot snapshot = {0};

	/* =====_currentIDsGeneration -> snapshot===== */
	DyArrayClone(&_currentIDsGeneration, &snapshot.IDsGeneration);
	/* ========== */

	/* =====_currentIDsFreeList -> snapshot===== */
	DyArrayClone(&_currentIDsFreeList, &snapshot.IDsFreeList);
	/* ========== */

	/* =====_entities -> snapshot===== */
	SparseSetClone(&_entities, &snapshot.entitySparseSet);
	/* ========== */

	/* =====_registeredComponents -> snapshot===== */
	DyArrayCreate(&snapshot.componentSparseSets, sizeof(struct SparseSet), 10); //I create it manually because i don't need the ComponentTypeInfo
	for (size_t i = 0; i < _registeredComponents.elementCount; i++) {
		_RegisteredComponent* registeredComponent = DyArrayGetElement(&_registeredComponents, i);

		DyArrayAddElement(&snapshot.componentSparseSets, &registeredComponent->set); //Do a temporary shallow copy
		struct SparseSet* setToClone = DyArrayGetElement(&snapshot.componentSparseSets, i);

		SparseSetClone(&registeredComponent->set, setToClone); //Do a deep copy
	}
	/* ========== */

	/* =====_entityActivationState -> snapshot===== */
	DyArrayClone(&_entityActivationState, &snapshot.entityActivationState);
	/* ========== */

	/* =====_entityComponentsActivationState -> snapshot===== */
	DyArrayCreate(&snapshot.entityComponentsActivationState, sizeof(dyarray), 10);
	for (size_t i = 0; i < _entityComponentsActivationState.elementCount; i++) {
		struct SparseSet* originalSet = DyArrayGetElement(&_entityComponentsActivationState, i);
		struct SparseSet newSet = {0};
		SparseSetClone(originalSet, &newSet);

		DyArrayAddElement(&snapshot.entityComponentsActivationState, &newSet);
	}
	/* ========== */

	return snapshot;
}

void _GECS_LoadSnapshot_Instant(const GECSSnapshot* snapshot) {
	GECS_EXPECT(_initialized);

	if (!GECS_IsSnapshotValid(snapshot)) {
		printf("GECS_LoadSnapshot ERROR: snapshot is not valid.\n");
		return;
	}

	//First free our current structures

	/* ===== free _currentIDsGeneration ===== */
	DyArrayFree(&_currentIDsGeneration);
	/* ========== */

	/* ===== free _currentIDsFreeList ===== */
	DyArrayFree(&_currentIDsFreeList);
	/* ========== */

	/* ===== free _entities ===== */
	SparseSetFree(&_entities);
	/* ========== */

	/* ===== free _registeredComponents ===== */
	for (size_t i = 0; i < _registeredComponents.elementCount; i++) {
		_RegisteredComponent* registeredComponent = DyArrayGetElement(&_registeredComponents, i);
		SparseSetFree(&registeredComponent->set);
	}

	/*
	 * I COULD technically not free this,
	 * since component types must be compatible,
	 * i expect the ones in the snapshot to be the same as this one,
	 * not needing to reallocate it.
	 */
	//DyArrayFree(&_registeredComponents);
	/*
	 * I won't free it, also because i'm not serializing the registered component's metadata,
	 * so if i WANT to free it, i must serialize the whole RegisteredComponent struct, and read it.
	 */
	/* ========== */

	/* ===== free _entityActivationState ===== */
	DyArrayFree(&_entityActivationState);
	/* ========== */

	/* ===== free _entityComponentsActivationState ===== */
	for (size_t i = 0; i < _entityComponentsActivationState.elementCount; i++) {
		struct SparseSet* set = DyArrayGetElement(&_entityComponentsActivationState, i);
		SparseSetFree(set);
	}

	DyArrayFree(&_entityComponentsActivationState);
	/* ========== */

	//Now let's clone the ones in the snapshot to load them in the current system.
	/* ===== snapshot -> _currentIDsGeneration ===== */
	DyArrayClone((void*)&snapshot->IDsGeneration, &_currentIDsGeneration);
	/* ========== */

	/* ===== snapshot -> _currentIDsFreeList ===== */
	DyArrayClone((void*)&snapshot->IDsFreeList, &_currentIDsFreeList);
	/* ========== */

	/* ===== snapshot -> _entities ===== */
	SparseSetClone((void*)&snapshot->entitySparseSet, &_entities);
	/* ========== */

	/* ===== snapshot -> _registeredComponents ===== */
	for (size_t i = 0; i < snapshot->componentSparseSets.elementCount; i++) {
		_RegisteredComponent* registeredComponent = DyArrayGetElement(&_registeredComponents, i);
		struct SparseSet* setToClone = DyArrayGetElement((void*)&snapshot->componentSparseSets, i);
		SparseSetClone(setToClone, &registeredComponent->set);
	}
	/* ========== */

	/* ===== snapshot -> _entityActivationState ===== */
	DyArrayClone((void*)&snapshot->entityActivationState, &_entityActivationState);
	/* ========== */

	/* ===== snapshot -> _entityComponentsActivationState ===== */
	DyArrayCreate(&_entityComponentsActivationState, sizeof(dyarray), 10);
	for (size_t i = 0; i < snapshot->entityComponentsActivationState.elementCount; i++) {
		struct SparseSet* originalSet = DyArrayGetElement((void*)&snapshot->entityComponentsActivationState, i);
		struct SparseSet newSet = {0};
		SparseSetClone(originalSet, &newSet);

		DyArrayAddElement(&_entityComponentsActivationState, &newSet);
	}
	/* ========== */
}

bool GECS_SaveSnapshotInDisk(const GECSSnapshot* snapshot, const char* filePath) {
	GECS_EXPECT(_initialized);

	if (!GECS_IsSnapshotValid(snapshot)) {
		printf("GECS_SaveSnapshotInDisk ERROR: snapshot is not valid.\n");
		return false;
	}

	if (!filePath || strlen(filePath) == 0) {
		printf("GECS_SaveSnapshotInDisk ERROR: filePath is not valid.\n");
		return false;
	}

	FILE* file = fopen(filePath, "wb");
	if (!file) {
		printf("GECS_SaveSnapshotInDisk ERROR: could not open nor create file at path %s.\n", filePath);
		return false;
	}

	/*
	 * Formatted like this:
	 */

	typedef struct
	{
	    dyarray IDsGeneration;
	    dyarray IDsFreeList;
	    struct SparseSet entitySparseSet;
	    dyarray componentSparseSets; //Contains only SparseSets, not _RegisteredComponent data
		dyarray entityActivationState;
	    dyarray entityComponentsActivationState; //Contains dyarrays, containing bools themselves
	} GECSSnapshot;

	DyArraySerialize((void*)&snapshot->IDsGeneration, file);
	DyArraySerialize((void*)&snapshot->IDsFreeList, file);
	SparseSetSerialize((void*)&snapshot->entitySparseSet, file);

	//Do dyarray manually
	fwrite(&snapshot->componentSparseSets.bufCapacity, 1, sizeof(size_t), file);
	fwrite(&snapshot->componentSparseSets.elementSize, 1, sizeof(size_t), file);
	fwrite(&snapshot->componentSparseSets.elementCount, 1, sizeof(size_t), file);

	for (size_t i = 0; i < snapshot->componentSparseSets.elementCount; i++) {
		struct SparseSet* setToSerialize = DyArrayGetElement((void*)&snapshot->componentSparseSets, i);
		SparseSetSerialize(setToSerialize, file);
	}

	DyArraySerialize((void*)&snapshot->entityActivationState, file);

	fwrite(&snapshot->entityComponentsActivationState.bufCapacity, 1, sizeof(size_t), file);
	fwrite(&snapshot->entityComponentsActivationState.elementSize, 1, sizeof(size_t), file);
	fwrite(&snapshot->entityComponentsActivationState.elementCount, 1, sizeof(size_t), file);

	for (size_t i = 0; i < snapshot->entityComponentsActivationState.elementCount; i++) {
		struct SparseSet* setToSerialize = DyArrayGetElement((void*)&snapshot->entityComponentsActivationState, i);
		SparseSetSerialize(setToSerialize, file);
	}

	fclose(file);

	return true;
}

GECSSnapshot GECS_MakeSnapshotFromDisk(const char* filePath) {
	GECS_EXPECT(_initialized);

	GECSSnapshot snapshotToMake = {0};

	if (!filePath || strlen(filePath) == 0) {
		printf("GECS_MakeSnapshotFromDisk ERROR: filePath is not valid.\n");
		return snapshotToMake;
	}

	FILE* file = fopen(filePath, "rb");
	if (!file) {
		printf("GECS_MakeSnapshotFromDisk ERROR: could not open file at path %s.\n", filePath);
		return snapshotToMake;
	}

	//Deserialize file and put inside snapshot.
	//A snapshot file is formatted like this:
	typedef struct
	{
	    dyarray IDsGeneration;
	    dyarray IDsFreeList;
	    struct SparseSet entitySparseSet;
	    dyarray componentSparseSets; //Contains only SparseSets, not _RegisteredComponent data
		dyarray entityActivationState;
	    dyarray entityComponentsActivationState; //Contains dyarrays, containing bools themselves
	} GECSSnapshot;

	DyArrayDeserialize(file, &snapshotToMake.IDsGeneration);
	DyArrayDeserialize(file, &snapshotToMake.IDsFreeList);
	SparseSetDeserialize(file, &snapshotToMake.entitySparseSet);

	//Do dyarray manually
	fread(&snapshotToMake.componentSparseSets.bufCapacity, 1, sizeof(size_t), file);
	fread(&snapshotToMake.componentSparseSets.elementSize, 1, sizeof(size_t), file);
	fread(&snapshotToMake.componentSparseSets.elementCount, 1, sizeof(size_t), file);

	snapshotToMake.componentSparseSets.buf = malloc(snapshotToMake.componentSparseSets.bufCapacity);

	for (size_t i = 0; i < snapshotToMake.componentSparseSets.elementCount; i++) {
		struct SparseSet* set = DyArrayGetElement(&snapshotToMake.componentSparseSets, i);
		SparseSetDeserialize(file, set);
	}

	DyArrayDeserialize(file, &snapshotToMake.entityActivationState);

	fread(&snapshotToMake.entityComponentsActivationState.bufCapacity, 1, sizeof(size_t), file);
	fread(&snapshotToMake.entityComponentsActivationState.elementSize, 1, sizeof(size_t), file);
	fread(&snapshotToMake.entityComponentsActivationState.elementCount, 1, sizeof(size_t), file);

	snapshotToMake.entityComponentsActivationState.buf = malloc(snapshotToMake.entityComponentsActivationState.bufCapacity);

	for (size_t i = 0; i < snapshotToMake.entityComponentsActivationState.elementCount; i++) {
		struct SparseSet* setToDeserialize = DyArrayGetElement((void*)&snapshotToMake.entityComponentsActivationState, i);
		SparseSetDeserialize(file, setToDeserialize);
	}

	fclose(file);

	return snapshotToMake;
}

bool GECS_MakeAndSaveSnapshotInDisk(const char* filePath) {
	GECS_EXPECT(_initialized);

	if (!filePath || strlen(filePath) == 0) {
		printf("GECS_MakeAndSaveSnapshotInDisk ERROR: filePath is not valid.\n");
		return false;
	}

	FILE* file = fopen(filePath, "wb");
	if (!file) {
		printf("GECS_MakeAndSaveSnapshotInDisk ERROR: could not open file at path %s.\n", filePath);
		return false;
	}

	DyArraySerialize((void*)&_currentIDsGeneration, file);
	DyArraySerialize((void*)&_currentIDsFreeList, file);
	SparseSetSerialize((void*)&_entities, file);

	//Do dyarray manually
	fwrite(&_registeredComponents.bufCapacity, 1, sizeof(size_t), file);
	fwrite(&_registeredComponents.elementSize, 1, sizeof(size_t), file);
	fwrite(&_registeredComponents.elementCount, 1, sizeof(size_t), file);

	for (size_t i = 0; i < _registeredComponents.elementCount; i++) {
		struct SparseSet* setToSerialize = DyArrayGetElement((void*)&_registeredComponents, i);
		SparseSetSerialize(setToSerialize, file);
	}

	DyArraySerialize((void*)&_entityActivationState, file);

	fwrite(&_entityComponentsActivationState.bufCapacity, 1, sizeof(size_t), file);
	fwrite(&_entityComponentsActivationState.elementSize, 1, sizeof(size_t), file);
	fwrite(&_entityComponentsActivationState.elementCount, 1, sizeof(size_t), file);

	for (size_t i = 0; i < _entityComponentsActivationState.elementCount; i++) {
		struct SparseSet* setToSerialize = DyArrayGetElement((void*)&_entityComponentsActivationState, i);
		SparseSetSerialize(setToSerialize, file);
	}

	fclose(file);
	return true;
}

bool _GECS_MakeAndLoadSnapshotFromDisk_Instant(const char* filePath) {
	GECS_EXPECT(_initialized);

	if (!filePath || strlen(filePath) == 0) {
		printf("GECS_MakeAndLoadSnapshotFromDisk ERROR: filePath is not valid.\n");
		return false;
	}

	FILE* file = fopen(filePath, "rb");
	if (!file) {
		printf("GECS_MakeAndLoadSnapshotFromDisk ERROR: could not open file at path %s.\n", filePath);
		return false;
	}

	DyArrayFree(&_currentIDsGeneration);
	DyArrayFree(&_currentIDsFreeList);
	SparseSetFree(&_entities);
	DyArrayFree(&_registeredComponents);

	DyArrayDeserialize(file, &_currentIDsGeneration);
	DyArrayDeserialize(file, &_currentIDsFreeList);
	SparseSetDeserialize(file, &_entities);

	//Do dyarray manually
	fread(&_registeredComponents.bufCapacity, 1, sizeof(size_t), file);
	fread(&_registeredComponents.elementSize, 1, sizeof(size_t), file);
	fread(&_registeredComponents.elementCount, 1, sizeof(size_t), file);

	_registeredComponents.buf = malloc(_registeredComponents.bufCapacity);

	for (size_t i = 0; i < _registeredComponents.elementCount; i++) {
		struct SparseSet* set = DyArrayGetElement(&_registeredComponents, i);
		SparseSetDeserialize(file, set);
	}

	DyArrayDeserialize(file, &_entityActivationState);

	fread(&_entityComponentsActivationState.bufCapacity, 1, sizeof(size_t), file);
	fread(&_entityComponentsActivationState.elementSize, 1, sizeof(size_t), file);
	fread(&_entityComponentsActivationState.elementCount, 1, sizeof(size_t), file);

	_entityComponentsActivationState.buf = malloc(_entityComponentsActivationState.bufCapacity);

	for (size_t i = 0; i < _entityComponentsActivationState.elementCount; i++) {
		struct SparseSet* setToDeserialize = DyArrayGetElement((void*)&_entityComponentsActivationState, i);
		SparseSetDeserialize(file, setToDeserialize);
	}

	fclose(file);

	return true;
}

void GECS_FreeSnapshot(GECSSnapshot* snapshot) {
	GECS_EXPECT(_initialized);

	//Also accepts NULL
	if (!GECS_IsSnapshotValid(snapshot)) {
		printf("GECS_FreeSnapshot ERROR: snapshot is not valid.");
		return;
	}

	DyArrayFree(&snapshot->IDsGeneration);
	DyArrayFree(&snapshot->IDsFreeList);
	SparseSetFree(&snapshot->entitySparseSet);

	for (size_t i = 0; i < snapshot->componentSparseSets.elementCount; i++) {
		struct SparseSet* set = DyArrayGetElement(&snapshot->componentSparseSets, i);
		SparseSetFree(set);
	}

	DyArrayFree(&snapshot->componentSparseSets);

	DyArrayFree(&snapshot->entityActivationState);

	for (size_t i = 0; i < snapshot->entityComponentsActivationState.elementCount; i++) {
		struct SparseSet* setToFree = DyArrayGetElement(&snapshot->entityComponentsActivationState, i);
		SparseSetFree(setToFree);
	}

	DyArrayFree(&snapshot->entityComponentsActivationState);
}

bool GECS_IsSnapshotValid(const GECSSnapshot* snapshot) {
	if (!snapshot) return false;

	//I should check everything... fuck no.
	if (snapshot->IDsGeneration.buf && snapshot->IDsFreeList.buf && snapshot->componentSparseSets.buf && snapshot->entitySparseSet.data)
		return true;

	return false;
}

void _GECS_ClearECS_Instant()
{
	GECS_EXPECT(_initialized);

	/*
	 * Clear everything but not freeing the allocations, i'd have to re-create everything, for now i'll leave it like this
     * i'll fix it in the future.
	 */

	DyArrayClear(&_currentIDsGeneration);
	DyArrayClear(&_currentIDsFreeList);

	SparseSetClear(&_entities);

	for (size_t i = 0; i < _registeredComponents.elementCount; i++) {
		_RegisteredComponent* registeredComponent = DyArrayGetElement(&_registeredComponents, i);
		SparseSetClear(&registeredComponent->set);
	}

	DyArrayClear(&_entityActivationState);

	for (size_t i = 0; i < _entityComponentsActivationState.elementCount; i++) {
		struct SparseSet* set = DyArrayGetElement(&_entityComponentsActivationState, i);
		SparseSetClear(set);
	}
}

void GECS_ProcessFrameEnd()
{
	GECS_EXPECT(_initialized);

	bool sceneObliterated = false;
	for (size_t i = 0; i < _commandQueue.elementCount; i++)
	{
		GECSCommand* cmd = DyArrayGetElement(&_commandQueue, i);
		if (!cmd) continue;

		if (sceneObliterated)
		{
			//Do not execute any other commands, simply iterate the last ones and free any buffer that remained allocated.
			if (cmd->type == GECS_CMD_ATTACH_COMPONENT) {
				free(cmd->payload.componentDataBuffer);
			}

			else if (cmd->type == GECS_CMD_MAKE_AND_LOAD_SNAPSHOT) {
				free((void*)cmd->payload.filePath);
			}

			continue;
		}

		switch (cmd->type)
		{
			case GECS_CMD_DELETE_ENTITY:
				_GECS_DeleteEntity_Instant(cmd->targetEntity);
				break;
			case GECS_CMD_ATTACH_COMPONENT:
				_GECS_AttachComponent_Instant(cmd->targetEntity, cmd->targetComponent, cmd->payload.componentDataBuffer);
				free(cmd->payload.componentDataBuffer);
				break;
			case GECS_CMD_DETACH_COMPONENT:
				_GECS_DetachComponent_Instant(cmd->targetEntity, cmd->targetComponent);
				break;

			//These commands need to clear the command queue since they'll be completely obliterating any entity that
			//had put the later commands in the queue, so basically invalidating they're effectiveness.
			case GECS_CMD_CLEAR_ECS:
				_GECS_ClearECS_Instant();
				sceneObliterated = true;
				break;
			case GECS_CMD_LOAD_SNAPSHOT:
				//Remember that the snapshot is a pointer passed by the user, and we did not make a copy of the whole snapshot.
				//This means that if the user allocated that snapshot on a System's routine, this shit will crash.
				//Document on how to use snapshots and tell to make global or external variables.
				_GECS_LoadSnapshot_Instant(cmd->payload.snapshotToLoad);
				sceneObliterated = true;
				break;
			case GECS_CMD_MAKE_AND_LOAD_SNAPSHOT:
				_GECS_MakeAndLoadSnapshotFromDisk_Instant(cmd->payload.filePath);
				free((void*)cmd->payload.filePath); //It was duped.
				sceneObliterated = true;
				break;
			default:
				break;
		}
	}

	DyArrayClear(&_commandQueue);
}
