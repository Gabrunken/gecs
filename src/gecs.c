#include <string.h>
#define DYARRAY_IMPL
#include <dyarray.h>
#include <gecs.h>
#include <stdlib.h>
#define SPARSE_SET_IMPL
#include <sparse_set.h>
#define HASHMAP_IMPL
#include <hashmap.h>
#include <stdarg.h>

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

void GECS_Init()
{
	GECS_EXPECT(!_initialized);

	SparseSetCreate(&_entities, 0, sizeof(EntityInfo));

	DyArrayCreate(&_currentIDsGeneration, sizeof(size_t), 100);
	DyArrayCreate(&_currentIDsFreeList, sizeof(size_t), 100);
	GECS_EXPECT(_currentIDsGeneration.buf && _currentIDsFreeList.buf);

	GECS_EXPECT(DyArrayCreate(&_registeredComponents, sizeof(_RegisteredComponent), 10));
	GECS_EXPECT(DyArrayCreate(&_registeredSystems, sizeof(_SystemInfo), 10));

	_initialized = true;
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

		//Find matching components
		size_t smallestSetElementID = SparseSetGetIDFromPhysicalIndex(smallestSet, i);
		for (uint8_t j = 0; j < info->componentCount; j++)
		{
			if (!SparseSetHasElement(componentSets[j], smallestSetElementID)) {archetypeFound = false; break;}
			components[j] = SparseSetGetElement(componentSets[j], smallestSetElementID);
		}

		if (!archetypeFound) continue;

		size_t gen = *(size_t*)DyArrayGetElement(&_currentIDsGeneration, smallestSetElementID - 1);
		info->callback((EntityID){smallestSetElementID, gen}, components);
	}
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

		id.id = freeIDIdx + 1;
		id.gen = currentGen;

		DyArrayRemoveElementSP(&_currentIDsFreeList, _currentIDsFreeList.elementCount - 1);
		return id;
	}

	//Brand new EntityID
	id.id = entityCount + 1;
	id.gen = 1;

	DyArrayAddElement(&_currentIDsGeneration, &id.gen);

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

void GECS_DeleteEntity(EntityID entity)
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
		struct SparseSet* componentSet = &componentInfo->set;

		SparseSetRemoveElement(componentSet, entity.id);
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

void GECS_AttachComponent(EntityID entity, ComponentTypeID componentTypeID, void* componentData)
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
}

void GECS_DetachComponent(EntityID entity, ComponentTypeID componentTypeID)
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

	GECSSnapshot snapshot = {0};

	DyArrayClone(&_currentIDsGeneration, &snapshot.IDsGeneration);
	DyArrayClone(&_currentIDsFreeList, &snapshot.IDsFreeList);
	SparseSetClone(&_entities, &snapshot.entitySparseSet);

	DyArrayClone(&_registeredComponents, &snapshot.componentSparseSets);
	for (size_t i = 0; i < _registeredComponents.elementCount; i++) {
		struct SparseSet* componentSet = DyArrayGetElement(&_registeredComponents, i);
		struct SparseSet newSet = {0};
		SparseSetClone(componentSet, &newSet);

		DyArraySetElement(&snapshot.componentSparseSets, i, &newSet);
	}

	return snapshot;
}

void GECS_LoadSnapshot(const GECSSnapshot* snapshot) {
	GECS_EXPECT(_initialized);

	if (!GECS_IsSnapshotValid(snapshot)) {
		printf("GECS_LoadSnapshot ERROR: snapshot is not valid.\n");
		return;
	}

	//First free our current structures
	DyArrayFree(&_currentIDsGeneration);
	DyArrayFree(&_currentIDsFreeList);
	SparseSetFree(&_entities);

	for (size_t i = 0; i < _registeredComponents.elementCount; i++) {
		struct SparseSet* set = DyArrayGetElement(&_registeredComponents, i);
		SparseSetFree(set);
	}

	DyArrayFree(&_registeredComponents);

	//Now let's clone the ones in the snapshot
	DyArrayClone((void*)&snapshot->IDsGeneration, &_currentIDsGeneration);
	DyArrayClone((void*)&snapshot->IDsFreeList, &_currentIDsFreeList);
	SparseSetClone((void*)&snapshot->entitySparseSet, &_entities);

	DyArrayClone((void*)&snapshot->componentSparseSets, &_registeredComponents);
	for (size_t i = 0; i < snapshot->componentSparseSets.elementCount; i++) {
		struct SparseSet* ogSet = DyArrayGetElement((void*)&snapshot->componentSparseSets, i);
		struct SparseSet* newSet = DyArrayGetElement(&_registeredComponents, i);
		SparseSetClone(ogSet, newSet);
	}
}

bool GECS_SaveSnapshotInDisk(const GECSSnapshot* snapshot, const char* filePath);
GECSSnapshot GECS_MakeSnapshotFromFisk(const char* filePath);

bool GECS_MakeAndSaveSnapshotInDisk(const char* filePath);
bool GECS_MakeAndLoadSnapshotFromDisk(const char* filePath);

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
}

bool GECS_IsSnapshotValid(const GECSSnapshot* snapshot) {
	if (!snapshot) return false;

	//I should check everything... fuck no.
	if (snapshot->IDsGeneration.buf && snapshot->IDsFreeList.buf && snapshot->componentSparseSets.buf && snapshot->entitySparseSet.data)
		return true;

	return false;
}
