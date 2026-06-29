#define DYARRAY_IMPL
#include "data_structures/dyarray.h"
#include <gecs.h>
#include <stdlib.h>
#define SPARSE_SET_IMPL
#include <data_structures/sparse_set.h>
#define HASHMAP_IMPL
#include <data_structures/hashmap.h>

#define GECS_INITIAL_ENTITY_ALLOC_SIZE 10'000

//static hashmap componentMap;
//static size_t componentNum;
//static char** componentNames;

//Contains a SparseSet for each component type registered.
//Any component cannot be removed once registered.
static dyarray registeredComponents;
static struct SparseSet entities;

//Generational IDs
static size_t* currentIDsGeneration;
static size_t currentIDsGenerationLen;
static size_t* currentIDsFreeList;
static size_t freeIDCount;

static bool initialized;

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

//
struct SparseSet* GECS_GetComponentSparseSet(ComponentTypeID componentTypeID)
{
	GECS_EXPECT(initialized);

	if (componentTypeID == GECS_INVALID_COMPONENT_TYPE_ID)
	{
		printf("GECS_GetComponentSparseSet ERROR: componentTypeID is invalid.\n");
		return NULL;
	}

	if (componentTypeID > registeredComponents.elementCount)
	{
		printf("GECS_GetComponentSparseSet ERROR: componeneTypeID is not a registered component.\n");
		return NULL;
	}

	return DYArrayGetElement(&registeredComponents, componentTypeID - 1);
}

void* GECS_GetComponent(ID entity, ComponentTypeID componentTypeID)
{
	GECS_EXPECT(initialized);

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

	if (componentTypeID > registeredComponents.elementCount)
	{
		printf("GECS_GetComponent ERROR: componentTypeID is not a registered component (%zu).\n", componentTypeID);
		return NULL;
	}

	if (!SparseSetHasElement(&entities, entity.id))
	{
		printf("GECS_GetComponent ERROR: entity %zu of generation %zu does not exist.\n", entity.id, entity.gen);
		return NULL;
	}

	//Check if the entity is not valid anymore
	if (currentIDsGeneration[entity.id - 1] != entity.gen)
	{
		printf("GECS_GetComponent ERROR: entity %zu of generation %zu does not exist anymore.\n", entity.id, entity.gen);
		return NULL;
	}

	struct SparseSet* set = DYArrayGetElement(&registeredComponents, componentTypeID - 1);
	EXPECT(set);

	//Returns NULL if the element is not present in the set (it is not an error, it is expected)
	return SparseSetGetElement(set, entity.id);
}

void GECS_Init()
{
	GECS_EXPECT(!initialized);

	SparseSetCreate(&entities, 0, sizeof(char) * GECS_ENTITY_NAME_MAX_LENGTH + 1 /*name + null terminator*/);

	currentIDsGenerationLen = GECS_INITIAL_ENTITY_ALLOC_SIZE;
	currentIDsGeneration = calloc(currentIDsGenerationLen, sizeof(size_t));
	EXPECT(currentIDsGeneration);
	currentIDsFreeList = calloc(currentIDsGenerationLen, sizeof(size_t));
	EXPECT(currentIDsFreeList);

	EXPECT(DYArrayCreate(&registeredComponents, sizeof(struct SparseSet), 10));

	initialized = true;
}

ComponentTypeID GECS_RegisterComponent(size_t size)
{
	GECS_EXPECT(initialized);

	if (!size)
	{
		printf("GECS_RegisterComponent ERROR: size is 0.\n");
		return GECS_INVALID_COMPONENT_TYPE_ID;
	}

	struct SparseSet set = {0};
	SparseSetCreate(&set, 0, size);
	DYArrayAddElement(&registeredComponents, &set);

	//The first ComponentTypeID starts from 1.
	return registeredComponents.elementCount;
}

//Also takes responsibility to increase generations.
static ID _GECS_GetNewID()
{
	GECS_EXPECT(initialized);

	ID id = {0};

	size_t entityCount = SparseSetGetElementCount(&entities);

	//Check if theres something in the free list
	if (freeIDCount > 0)
	{
		//Take last element
		freeIDCount--;
		size_t freeIDIdx = currentIDsFreeList[freeIDCount];
		currentIDsGeneration[freeIDIdx]++;
		id.id = freeIDIdx + 1;
		id.gen = currentIDsGeneration[freeIDIdx];
		return id;
	}

	//Resize it if necessary
	if (currentIDsGenerationLen <= entityCount)
	{
		currentIDsGenerationLen++; //Hard fix if the len was initially 0
		currentIDsGenerationLen *= 2;
		currentIDsGeneration = realloc(currentIDsGeneration, currentIDsGenerationLen * sizeof(size_t));
		EXPECT(currentIDsGeneration);
		currentIDsFreeList = realloc(currentIDsFreeList, currentIDsGenerationLen * sizeof(size_t));
		EXPECT(currentIDsFreeList);
	}

	//Brand new ID
	id.id = entityCount + 1;
	id.gen = 1;
	currentIDsGeneration[entityCount] = id.gen;

	return id;
}

ID GECS_CreateEntity(const char *name)
{
	GECS_EXPECT(initialized);

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

	SparseSetAddElement(&entities, id.id, safeName);

	return id;
}

void GECS_DeleteEntity(ID entity)
{
	GECS_EXPECT(initialized);

	if (entity.id == GECS_INVALID_ID || entity.gen == GECS_INVALID_GEN)
	{
		printf("GECS_DeleteEntity ERROR: entity %zu of generation %zu is invalid.\n", entity.id, entity.gen);
		return;
	}

	if (!SparseSetHasElement(&entities, entity.id))
	{
		printf("GECS_DeleteEntity ERROR: entity %zu of generation %zu does not exist.\n", entity.id, entity.gen);
		return;
	}

	//Here the element of ID "entity" exists, let's check if the generation is the right one.
	//An out of bounds check is not necessary since SparseSetHasElement will early return before accessing a non allocated ID.
	if (currentIDsGeneration[entity.id - 1] != entity.gen)
	{
		printf("GECS_DeleteEntity ERROR: entity %zu of generation %zu is not valid anymore.\n", entity.id, entity.gen);
		return;
	}

	//Add the removed id to the free list
	//A size check !should! not be necessary since when generating a new id
	//this array gets resized to fulfill any bounds.
	currentIDsFreeList[freeIDCount] = entity.id - 1;
	freeIDCount++;

	SparseSetRemoveElement(&entities, entity.id);

	//Remove any components it had.
	//Iterate for each element registered in the system and check if it contains this entity's ID.
	for (size_t i = 0; i < registeredComponents.elementCount; i++)
	{
		struct SparseSet* set = DYArrayGetElement(&registeredComponents, i);
		GECS_EXPECT(set);

		if (SparseSetHasElement(set, entity.id))
		{
			SparseSetRemoveElement(set, entity.id);
		}
	}
}

void GECS_AttachComponent(ID entity, ComponentTypeID componentTypeID, void* componentData)
{
	GECS_EXPECT(initialized);

	if (entity.id == GECS_INVALID_ID || entity.gen == GECS_INVALID_GEN)
	{
		printf("GECS_CreateComponent ERROR: entity %zu of generation %zu is invalid.\n", entity.id, entity.gen);
		return;
	}

	if (componentTypeID == GECS_INVALID_COMPONENT_TYPE_ID)
	{
		printf("GECS_CreateComponent ERROR: componentTypeID is invalid.\n");
		return;
	}

	if (!componentData)
	{
		printf("GECS_CreateComponent ERROR: componentData is NULL.\n");
		return;
	}

	//Check if entity exists
	if (!SparseSetHasElement(&entities, entity.id))
	{
		printf("GECS_CreateComponent ERROR: entity %zu does not exist.\n", entity.id);
		return;
	}

	if (currentIDsGeneration[entity.id - 1] != entity.gen)
	{
		printf("GECS_CreateComponent ERROR: entity %zu of generation %zu is not valid anymore.\n", entity.id, entity.gen);
		return;
	}

	if (componentTypeID > registeredComponents.elementCount)
	{
		printf("GECS_CreateComponent ERROR: there is no registered component with type ID %zu.\n", componentTypeID);
		return;
	}

	struct SparseSet* componentSet = DYArrayGetElement(&registeredComponents, componentTypeID - 1);

	//Check if the entity already has that component type
	if (SparseSetHasElement(componentSet, entity.id))
	{
		printf("GECS_CreateComponent ERROR: entity %zu already has component type of ID %zu.\n", entity.id, componentTypeID);
		return;
	}

	SparseSetAddElement(componentSet, entity.id, componentData);
}

void GECS_DetachComponent(ID entity, ComponentTypeID componentTypeID)
{
	GECS_EXPECT(initialized);

	if (entity.id == GECS_INVALID_ID || entity.gen == GECS_INVALID_GEN)
	{
		printf("GECS_DeleteComponent ERROR: entity %zu of generation %zu is invalid.\n", entity.id, entity.gen);
		return;
	}

	if (componentTypeID == GECS_INVALID_COMPONENT_TYPE_ID)
	{
		printf("GECS_DeleteComponent ERROR: componentTypeID is invalid.\n");
		return;
	}

	if (componentTypeID > registeredComponents.elementCount)
	{
		printf("GECS_DeleteComponent ERROR: there is no registered component with type ID %zu.\n", componentTypeID);
		return;
	}

	//Check if entity exists
	if (!SparseSetHasElement(&entities, entity.id))
	{
		printf("GECS_DeleteComponent ERROR: entity %zu does not exist.\n", entity.id);
		return;
	}

	//Check entity generation
	if (currentIDsGeneration[entity.id - 1] != entity.gen)
	{
		printf("GECS_DeleteComponent ERROR: entity %zu of generation %zu is not valid anymore.\n", entity.id, entity.gen);
		return;
	}

	struct SparseSet* componentSet = DYArrayGetElement(&registeredComponents, componentTypeID - 1);

	//Check if the entity has that component
	if (!SparseSetHasElement(componentSet, entity.id))
	{
		printf("GECS_DeleteComponent ERROR: entity %zu does not have component type ID %zu.\n", entity.id, componentTypeID);
		return;
	}

	SparseSetRemoveElement(componentSet, entity.id);
}

void GECS_CleanUp()
{
	GECS_EXPECT(initialized);

	//Free sparse sets
	for (size_t i = 0; i < registeredComponents.elementCount; i++)
	{
		struct SparseSet* set = DYArrayGetElement(&registeredComponents, i);
		EXPECT(set);
		SparseSetFree(set);
	}

	DYArrayFree(&registeredComponents);

	free(currentIDsFreeList);
	free(currentIDsGeneration);

	currentIDsGenerationLen = 0;
	freeIDCount = 0;

	SparseSetFree(&entities);

	initialized = false;
}
