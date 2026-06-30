#include <vadefs.h>
#define DYARRAY_IMPL
#include "data_structures/dyarray.h"
#include <gecs.h>
#include <stdlib.h>
#define SPARSE_SET_IMPL
#include <data_structures/sparse_set.h>
#define HASHMAP_IMPL
#include <data_structures/hashmap.h>
#include <stdarg.h>

#define GECS_INITIAL_ENTITY_ALLOC_SIZE 10'000
#define GECS_MAX_SYSTEM_COMPONENTS 8

//Contains a SparseSet for each component type registered.
//Any component cannot be removed once registered.
static dyarray _registeredComponents;
static struct SparseSet _entities;

//Generational IDs
static size_t* _currentIDsGeneration;
static size_t _currentIDsGenerationLen;
//Refactor to use dyarray (this is used like a stack so the swap and pop removal works)
static size_t* _currentIDsFreeList;
static size_t _freeIDCount;

typedef struct
{
	//In order!
	void (*callback)(ID, void**);
	ComponentTypeID components[GECS_MAX_SYSTEM_COMPONENTS];
	uint8_t componentCount;
} _SystemInfo;

dyarray _registeredSystems; //SystemID(s) will be used as indices (starting from 1) for this array containing _SystemInfo(s).

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

	SparseSetCreate(&_entities, 0, sizeof(char) * GECS_ENTITY_NAME_MAX_LENGTH + 1 /*name + null terminator*/);

	_currentIDsGenerationLen = GECS_INITIAL_ENTITY_ALLOC_SIZE;
	_currentIDsGeneration = calloc(_currentIDsGenerationLen, sizeof(size_t));
	EXPECT(_currentIDsGeneration);
	_currentIDsFreeList = calloc(_currentIDsGenerationLen, sizeof(size_t));
	EXPECT(_currentIDsFreeList);

	EXPECT(DYArrayCreate(&_registeredComponents, sizeof(struct SparseSet), 10));
	EXPECT(DYArrayCreate(&_registeredSystems, sizeof(_SystemInfo), 10));

	_initialized = true;
}

SystemID GECS_RegisterSystem(void (*callback)(ID, void**), int componentCount, ...)
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
		componentTypeID = va_arg(args, ComponentTypeID);
		if (componentTypeID > _registeredComponents.elementCount)
		{
			printf("GECS_RegisterSystem ERROR: componentTypeID %zu is not a valid component type.\n", componentTypeID);
			va_end(args);
			return GECS_INVALID_SYSTEM_ID;
		}

		info.components[i] = componentTypeID;
	}

	DYArrayAddElement(&_registeredSystems, &info);

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

	_SystemInfo* info = DYArrayGetElement(&_registeredSystems, systemID - 1);
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

		info->callback((ID){smallestSetElementID, _currentIDsGeneration[smallestSetElementID - 1]}, components);
	}
}

struct SparseSet* _GECS_GetComponentSparseSet(ComponentTypeID componentTypeID)
{
	GECS_EXPECT(_initialized);

	if (componentTypeID == GECS_INVALID_COMPONENT_TYPE_ID)
	{
		printf("GECS_GetComponentSparseSet ERROR: componentTypeID is invalid.\n");
		return NULL;
	}

	if (componentTypeID > _registeredComponents.elementCount)
	{
		printf("GECS_GetComponentSparseSet ERROR: componeneTypeID is not a registered component.\n");
		return NULL;
	}

	return DYArrayGetElement(&_registeredComponents, componentTypeID - 1);
}

void* GECS_GetComponent(ID entity, ComponentTypeID componentTypeID)
{
	GECS_EXPECT(_initialized);

	if (entity.id == GECS_INVALID_ID || entity.gen == GECS_INVALID_GEN)
	{
		printf("GECS_GetComponent ERROR: entity %zu of generation %zu is invalid.\n", entity.id, entity.gen);
		return NULL;
	}

	if (componentTypeID == GECS_INVALID_COMPONENT_TYPE_ID)
	{
		printf("GECS_GetComponent ERROR: componentTypeID is invalid (%zu).\n", componentTypeID);
		return NULL;
	}

	if (componentTypeID > _registeredComponents.elementCount)
	{
		printf("GECS_GetComponent ERROR: componentTypeID is not a registered component (%zu).\n", componentTypeID);
		return NULL;
	}

	if (!SparseSetHasElement(&_entities, entity.id))
	{
		printf("GECS_GetComponent ERROR: entity %zu of generation %zu does not exist.\n", entity.id, entity.gen);
		return NULL;
	}

	//Check if the entity is not valid anymore
	if (_currentIDsGeneration[entity.id - 1] != entity.gen)
	{
		printf("GECS_GetComponent ERROR: entity %zu of generation %zu does not exist anymore.\n", entity.id, entity.gen);
		return NULL;
	}

	struct SparseSet* set = DYArrayGetElement(&_registeredComponents, componentTypeID - 1);
	EXPECT(set);

	//Returns NULL if the element is not present in the set (it is not an error, it is expected)
	return SparseSetGetElement(set, entity.id);
}

const char* GECS_GetEntityName(ID entity)
{
	GECS_EXPECT(_initialized);

	if (entity.id == GECS_INVALID_ID || entity.gen == GECS_INVALID_GEN)
	{
		printf("GECS_GetEntityName ERROR: entity %zu of generation %zu is invalid.\n", entity.id, entity.gen);
		return NULL;
	}

	if (_currentIDsGeneration[entity.id - 1] != entity.gen)
	{
		printf("GECS_GetEntityName ERROR: entity %zu of generation %zu does not exist anymore.\n", entity.id, entity.gen);
		return NULL;
	}

	return SparseSetGetElement(&_entities, entity.id); //Returns NULL if there is no entity with that id.
}

ComponentTypeID GECS_RegisterComponent(size_t size)
{
	GECS_EXPECT(_initialized);

	if (!size)
	{
		printf("GECS_RegisterComponent ERROR: size is 0.\n");
		return GECS_INVALID_COMPONENT_TYPE_ID;
	}

	struct SparseSet set = {0};
	SparseSetCreate(&set, 0, size);
	DYArrayAddElement(&_registeredComponents, &set);

	//The first ComponentTypeID starts from 1.
	return _registeredComponents.elementCount;
}

//Also takes responsibility to increase generations.
static ID _GECS_GetNewID()
{
	GECS_EXPECT(_initialized);

	ID id = {0};

	size_t entityCount = SparseSetGetElementCount(&_entities);

	//Check if theres something in the free list
	if (_freeIDCount > 0)
	{
		//Take last element
		_freeIDCount--;
		size_t freeIDIdx = _currentIDsFreeList[_freeIDCount];
		_currentIDsGeneration[freeIDIdx]++;
		id.id = freeIDIdx + 1;
		id.gen = _currentIDsGeneration[freeIDIdx];
		return id;
	}

	//Resize it if necessary
	if (_currentIDsGenerationLen <= entityCount)
	{
		_currentIDsGenerationLen++; //Hard fix if the len was initially 0
		_currentIDsGenerationLen *= 2;
		_currentIDsGeneration = realloc(_currentIDsGeneration, _currentIDsGenerationLen * sizeof(size_t));
		EXPECT(_currentIDsGeneration);
		_currentIDsFreeList = realloc(_currentIDsFreeList, _currentIDsGenerationLen * sizeof(size_t));
		EXPECT(_currentIDsFreeList);
	}

	//Brand new ID
	id.id = entityCount + 1;
	id.gen = 1;
	_currentIDsGeneration[entityCount] = id.gen;

	return id;
}

ID GECS_CreateEntity(const char *name)
{
	GECS_EXPECT(_initialized);

	if (!name)
	{
		printf("GECS_CreateEntity ERROR: 'name' is NULL.\n");
		return (ID){GECS_INVALID_ID, GECS_INVALID_GEN};
	}

	size_t nameLen = strlen(name);
	if (nameLen == 0)
	{
		printf("GECS_CreateEntity ERROR: 'name' is empty.\n");
		return (ID){GECS_INVALID_ID, GECS_INVALID_GEN};
	}

	if (nameLen > GECS_ENTITY_NAME_MAX_LENGTH)
	{
		printf("GECS_CreateEntity ERROR: 'name' is too long, max is %d.\n", GECS_ENTITY_NAME_MAX_LENGTH);
		return (ID){GECS_INVALID_ID, GECS_INVALID_GEN};
	}

	ID id = _GECS_GetNewID(); //Automatically increases entityCount on success.
	if (!id.id)
	{
		printf("GECS_CreateEntity ERROR: failed to create new entity of name %s.\n", name);
		return id;
	}

	char safeName[GECS_ENTITY_NAME_MAX_LENGTH + 1] = {0};
	strncpy(safeName, name, GECS_ENTITY_NAME_MAX_LENGTH);

	SparseSetAddElement(&_entities, id.id, safeName);

	return id;
}

void GECS_DeleteEntity(ID entity)
{
	GECS_EXPECT(_initialized);

	if (entity.id == GECS_INVALID_ID || entity.gen == GECS_INVALID_GEN)
	{
		printf("GECS_DeleteEntity ERROR: entity %zu of generation %zu is invalid.\n", entity.id, entity.gen);
		return;
	}

	if (!SparseSetHasElement(&_entities, entity.id))
	{
		printf("GECS_DeleteEntity ERROR: entity %zu of generation %zu does not exist.\n", entity.id, entity.gen);
		return;
	}

	//Here the element of ID "entity" exists, let's check if the generation is the right one.
	//An out of bounds check is not necessary since SparseSetHasElement will early return before accessing a non allocated ID.
	if (_currentIDsGeneration[entity.id - 1] != entity.gen)
	{
		printf("GECS_DeleteEntity ERROR: entity %zu of generation %zu is not valid anymore.\n", entity.id, entity.gen);
		return;
	}

	//Add the removed id to the free list
	//A size check !should! not be necessary since when generating a new id
	//this array gets resized to fulfill any bounds.
	_currentIDsFreeList[_freeIDCount] = entity.id - 1;
	_freeIDCount++;

	SparseSetRemoveElement(&_entities, entity.id);

	//Remove any components it had.
	//Iterate for each element registered in the system and check if it contains this entity's ID.
	for (size_t i = 0; i < _registeredComponents.elementCount; i++)
	{
		struct SparseSet* set = DYArrayGetElement(&_registeredComponents, i);
		GECS_EXPECT(set);

		if (SparseSetHasElement(set, entity.id))
		{
			SparseSetRemoveElement(set, entity.id);
		}
	}
}

bool GECS_DoesEntityExist(ID entity)
{
	GECS_EXPECT(_initialized);

	if (entity.id == GECS_INVALID_ID || entity.gen == GECS_INVALID_GEN)
		return false;

	if (!SparseSetHasElement(&_entities, entity.id))
		return false;

	if (_currentIDsGeneration[entity.id - 1] != entity.gen)
		return false;

	return true;
}

void GECS_AttachComponent(ID entity, ComponentTypeID componentTypeID, void* componentData)
{
	GECS_EXPECT(_initialized);

	if (entity.id == GECS_INVALID_ID || entity.gen == GECS_INVALID_GEN)
	{
		printf("GECS_AttachComponent ERROR: entity %zu of generation %zu is invalid.\n", entity.id, entity.gen);
		return;
	}

	if (componentTypeID == GECS_INVALID_COMPONENT_TYPE_ID)
	{
		printf("GECS_AttachComponent ERROR: componentTypeID is invalid.\n");
		return;
	}

	if (!componentData)
	{
		printf("GECS_AttachComponent ERROR: componentData is NULL.\n");
		return;
	}

	//Check if entity exists
	if (!SparseSetHasElement(&_entities, entity.id))
	{
		printf("GECS_AttachComponent ERROR: entity %zu does not exist.\n", entity.id);
		return;
	}

	if (_currentIDsGeneration[entity.id - 1] != entity.gen)
	{
		printf("GECS_AttachComponent ERROR: entity %zu of generation %zu is not valid anymore.\n", entity.id, entity.gen);
		return;
	}

	if (componentTypeID > _registeredComponents.elementCount)
	{
		printf("GECS_AttachComponent ERROR: there is no registered component with type ID %zu.\n", componentTypeID);
		return;
	}

	struct SparseSet* componentSet = DYArrayGetElement(&_registeredComponents, componentTypeID - 1);

	//Check if the entity already has that component type
	if (SparseSetHasElement(componentSet, entity.id))
	{
		printf("GECS_AttachComponent ERROR: entity %zu already has component type of ID %zu.\n", entity.id, componentTypeID);
		return;
	}

	SparseSetAddElement(componentSet, entity.id, componentData);
}

void GECS_DetachComponent(ID entity, ComponentTypeID componentTypeID)
{
	GECS_EXPECT(_initialized);

	if (entity.id == GECS_INVALID_ID || entity.gen == GECS_INVALID_GEN)
	{
		printf("GECS_DetachComponent ERROR: entity %zu of generation %zu is invalid.\n", entity.id, entity.gen);
		return;
	}

	if (componentTypeID == GECS_INVALID_COMPONENT_TYPE_ID)
	{
		printf("GECS_DetachComponent ERROR: componentTypeID is invalid.\n");
		return;
	}

	if (componentTypeID > _registeredComponents.elementCount)
	{
		printf("GECS_DetachComponent ERROR: there is no registered component with type ID %zu.\n", componentTypeID);
		return;
	}

	//Check if entity exists
	if (!SparseSetHasElement(&_entities, entity.id))
	{
		printf("GECS_DetachComponent ERROR: entity %zu does not exist.\n", entity.id);
		return;
	}

	//Check entity generation
	if (_currentIDsGeneration[entity.id - 1] != entity.gen)
	{
		printf("GECS_DetachComponent ERROR: entity %zu of generation %zu is not valid anymore.\n", entity.id, entity.gen);
		return;
	}

	struct SparseSet* componentSet = DYArrayGetElement(&_registeredComponents, componentTypeID - 1);

	//Check if the entity has that component
	if (!SparseSetHasElement(componentSet, entity.id))
	{
		printf("GECS_DetachComponent ERROR: entity %zu does not have component type ID %zu.\n", entity.id, componentTypeID);
		return;
	}

	SparseSetRemoveElement(componentSet, entity.id);
}

void GECS_CleanUp()
{
	GECS_EXPECT(_initialized);

	//Free sparse sets
	for (size_t i = 0; i < _registeredComponents.elementCount; i++)
	{
		struct SparseSet* set = DYArrayGetElement(&_registeredComponents, i);
		EXPECT(set);
		SparseSetFree(set);
	}

	DYArrayFree(&_registeredComponents);
	DYArrayFree(&_registeredSystems);

	free(_currentIDsFreeList);
	free(_currentIDsGeneration);

	_currentIDsGenerationLen = 0;
	_freeIDCount = 0;

	SparseSetFree(&_entities);

	_initialized = false;
}
