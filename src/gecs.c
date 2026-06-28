#include <gecs.h>
#include <stdlib.h>
#define SPARSE_SET_IMPL
#include <data_structures/sparse_set.h>
#define HASHMAP_IMPL
#include <data_structures/hashmap.h>

#define GECS_INITIAL_ENTITY_ALLOC_SIZE 10'000

static hashmap componentMap;
static size_t componentNum;
static char** componentNames;

static size_t* currentIDsGeneration;
static size_t currentIDsGenerationLen;
static size_t* currentIDsFreeList;
static size_t freeIDCount;

static struct SparseSet entities;

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

//Returns NULL if component type does not exist
struct SparseSet* GECS_GetComponentSparseSet(const char* name)
{
	GECS_EXPECT(initialized && name);

	struct SparseSet* set = NULL;
	hashmap_get_val(componentMap, name, (void*)&set);
	return set;
}

void* GECS_GetComponent(ID entity, const char* componentTypeName)
{
	GECS_EXPECT(initialized && entity.id && componentTypeName);

	if (!SparseSetHasElement(&entities, entity.id)) return NULL;

	//Check if the entity is not valid anymore
	if (currentIDsGeneration[entity.id - 1] != entity.gen) return NULL;

	struct SparseSet* set = GECS_GetComponentSparseSet(componentTypeName);
	if (!set)
	{
		printf("GECS_GetComponent ERROR: component type of name '%s' does not exist.\n", componentTypeName);
		return NULL;
	}

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

	initialized = true;
}

void GECS_RegisterComponent(const char *name, size_t size)
{
	GECS_EXPECT(initialized);
	GECS_EXPECT(name && size);

	if (GECS_GetComponentSparseSet(name))
	{
		printf("GECS_RegisterComponent ERROR: component type of name '%s' already exists.\n", name);
		return;
	}

	size_t nameLen = strlen(name);
	if (nameLen > GECS_COMPONENT_NAME_MAX_LENGTH)
	{
		printf("GECS_RegisterComponent ERROR: name '%s' is too long (max: %d).\n", name, GECS_COMPONENT_NAME_MAX_LENGTH);
		return;
	}

	struct SparseSet* set = malloc(sizeof(struct SparseSet));
	GECS_EXPECT(set);

	SparseSetCreate(set, 0, size);
	hashmap_set_val(componentMap, name, (uint64_t)set);

	componentNames = realloc(componentNames, sizeof(void*) * (++componentNum));
	GECS_EXPECT(componentNames);
	componentNames[componentNum - 1] = calloc(1, GECS_COMPONENT_NAME_MAX_LENGTH + 1);
	strcpy(componentNames[componentNum - 1], name);
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
	GECS_EXPECT(name);

	size_t nameLen = strlen(name);
	if (nameLen == 0)
	{
		printf("GECS_CreateEntity ERROR: 'name' is empty.\n");
		return (ID){GECS_INVALID_ID, 0};
	}

	if (nameLen > GECS_ENTITY_NAME_MAX_LENGTH)
	{
		printf("GECS_CreateEntity ERROR: 'name' is too long, max is %d.\n", GECS_ENTITY_NAME_MAX_LENGTH);
		return (ID){GECS_INVALID_ID, 0};
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
	GECS_EXPECT(entity.id);

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

	//Remove any components it had
	for (size_t i = 0; i < componentNum; i++)
	{
		struct SparseSet* set = GECS_GetComponentSparseSet(componentNames[i]);
		GECS_EXPECT(set);
		if (SparseSetHasElement(set, entity.id))
		{
			SparseSetRemoveElement(set, entity.id);
		}
	}
}

void GECS_CreateComponent(ID entity, const char* componentTypeName, void* componentData)
{
	GECS_EXPECT(initialized);
	GECS_EXPECT(entity.id && componentTypeName);

	size_t componentNameLen = strlen(componentTypeName);
	if (componentNameLen > GECS_COMPONENT_NAME_MAX_LENGTH)
	{
		printf("GECS_CreateComponent ERROR: the component type name is too long (max: %d).\n", GECS_COMPONENT_NAME_MAX_LENGTH);
		return;
	}

	if (componentNameLen == 0)
	{
		printf("GECS_CreateComponent ERROR: the component type name is empty.\n");
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

	//Check if component type exists
	struct SparseSet* componentDataSet = GECS_GetComponentSparseSet(componentTypeName);
	if (!componentDataSet)
	{
		printf("GECS_CreateComponent ERROR: component type of name '%s' does not exist.\n", componentTypeName);
		return;
	}

	//Check if the entity already has that component type
	if (SparseSetHasElement(componentDataSet, entity.id))
	{
		printf("GECS_CreateComponent ERROR: entity %zu already has component type '%s'.\n", entity.id, componentTypeName);
		return;
	}

	SparseSetAddElement(componentDataSet, entity.id, componentData);
}

void GECS_DeleteComponent(ID entity, const char *componentTypeName)
{
	GECS_EXPECT(initialized);
	GECS_EXPECT(entity.id && componentTypeName);

	size_t componentNameLen = strlen(componentTypeName);
	if (componentNameLen > GECS_COMPONENT_NAME_MAX_LENGTH)
	{
		printf("GECS_DeleteComponent ERROR: the component type name is too long (max: %d).\n", GECS_COMPONENT_NAME_MAX_LENGTH);
		return;
	}

	if (componentNameLen == 0)
	{
		printf("GECS_DeleteComponent ERROR: the component type name is empty.\n");
		return;
	}

	//Check if entity exists
	if (!SparseSetHasElement(&entities, entity.id))
	{
		printf("GECS_DeleteComponent ERROR: entity %zu does not exist.\n", entity.id);
		return;
	}

	if (currentIDsGeneration[entity.id - 1] != entity.gen)
	{
		printf("GECS_DeleteComponent ERROR: entity %zu of generation %zu is not valid anymore.\n", entity.id, entity.gen);
		return;
	}

	//Check if the component exists
	struct SparseSet* componentDataSet = GECS_GetComponentSparseSet(componentTypeName);
	if (!componentDataSet)
	{
		printf("GECS_DeleteComponent ERROR: the component type '%s' does not exist.\n", componentTypeName);
		return;
	}

	//Check if the entity has that component
	if (!SparseSetHasElement(componentDataSet, entity.id))
	{
		printf("GECS_DeleteComponent ERROR: entity %zu does not have component type '%s'.\n", entity.id, componentTypeName);
		return;
	}

	SparseSetRemoveElement(componentDataSet, entity.id);
}

void GECS_CleanUp()
{
	GECS_EXPECT(initialized);

	//Free sparse sets
	for (size_t i = 0; i < componentNum; i++)
	{
		struct SparseSet* set;
		GECS_EXPECT(hashmap_get_val(componentMap, componentNames[i], (uint64_t*)&set));
		SparseSetFree(set);
		free(set);

		free(componentNames[i]);
	}

	if (componentNames)
		free(componentNames);
	free(currentIDsFreeList);
	free(currentIDsGeneration);

	currentIDsGenerationLen = 0;
	freeIDCount = 0;

	componentNames = NULL;
	componentNum = 0;

	hashmap_delete(componentMap);
	SparseSetFree(&entities);

	initialized = false;
}
