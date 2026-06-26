#include <gecs.h>
#include <stdlib.h>
#define SPARSE_SET_IMPL
#include <sparse_set.h>
#define HASHMAP_IMPL
#include <hashmap.h>

#define GECS_ENTITY_NAME_MAX_LENGTH 24
#define GECS_COMPONENT_NAME_MAX_LENGTH 24
#define GECS_INVALID_ID 0
#define GECS_MAX_ENTITIES 1'000'000

static hashmap componentMap;
static size_t componentNum;
static char** componentNames;

static ID nextID = 1;

static struct SparseSet entities;

static bool initialized;

#define GECS_EXPECT(condition, ...)\
do\
{\
    if (!condition)\
    {\
        printf("\033[31m" "ASSERTION FAILED at %s -- line %d\n", __FILE__, __LINE__);\
        __VA_OPT__(printf("Message: " "\033[39m", __VA_ARGS__);)\
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

bool GECS_DoesEntityHaveComponent(ID entity, const char* componentTypeName)
{
	GECS_EXPECT(initialized && entity && componentTypeName);

	struct SparseSet* set = GECS_GetComponentSparseSet(componentTypeName);
	if (!set)
	{
		printf("_GECS_DoesEntityHaveComponent ERROR: component type of name '%s' does not exist.\n", componentTypeName);
		return false;
	}

	return SparseSetHasElement(set, entity);
}

void GECS_Init()
{
	GECS_EXPECT(!initialized);

	SparseSetCreate(&entities, 0, sizeof(char) * GECS_ENTITY_NAME_MAX_LENGTH + 1 /*name + null terminator*/);

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

//Very rudimental system, for now it works, consider reusing deleted IDs on an improved version.
static ID _GECS_GetNewID()
{
	GECS_EXPECT(initialized);

	if (nextID > GECS_MAX_ENTITIES)
	{
		printf("_GECS_GetNewID ERROR: Max entities reached (max: %d).\n", GECS_MAX_ENTITIES);
		return GECS_INVALID_ID;
	}

	return nextID++;
}

ID GECS_CreateEntity(const char *name)
{
	GECS_EXPECT(initialized);
	GECS_EXPECT(name);

	size_t nameLen = strlen(name);
	if (nameLen == 0)
	{
		printf("GECS_CreateEntity ERROR: 'name' is empty.\n");
		return GECS_INVALID_ID;
	}

	if (nameLen > GECS_ENTITY_NAME_MAX_LENGTH)
	{
		printf("GECS_CreateEntity ERROR: 'name' is too long, max is %d.\n", GECS_ENTITY_NAME_MAX_LENGTH);
		return GECS_INVALID_ID;
	}

	ID id = _GECS_GetNewID();
	if (!id)
	{
		printf("GECS_CreateEntity ERROR: failed to create new entity of name %s.\n", name);
		return id;
	}

	char safeName[GECS_ENTITY_NAME_MAX_LENGTH + 1] = {0};
	strncpy(safeName, name, GECS_ENTITY_NAME_MAX_LENGTH);

	SparseSetAddElement(&entities, id, safeName);

	return id;
}

void GECS_DeleteEntity(ID entity)
{
	GECS_EXPECT(initialized);
	GECS_EXPECT(entity);

	if (!SparseSetHasElement(&entities, entity))
	{
		printf("GECS_DeleteEntity ERROR: entity %zu does not exist.\n", entity);
		return;
	}

	SparseSetRemoveElement(&entities, entity);

	//Remove any components it had
	for (size_t i = 0; i < componentNum; i++)
	{
		struct SparseSet* set = GECS_GetComponentSparseSet(componentNames[i]);
		GECS_EXPECT(set);
		if (SparseSetHasElement(set, entity))
		{
			SparseSetRemoveElement(set, entity);
		}
	}
}

void GECS_CreateComponent(ID entity, const char* componentTypeName, void* componentData)
{
	GECS_EXPECT(initialized);
	GECS_EXPECT(entity && componentTypeName);

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
	if (!SparseSetHasElement(&entities, entity))
	{
		printf("GECS_CreateComponent ERROR: entity %zu does not exist.\n", entity);
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
	if (SparseSetHasElement(componentDataSet, entity))
	{
		printf("GECS_CreateComponent ERROR: entity %zu already has component type '%s'.\n", entity, componentTypeName);
		return;
	}

	SparseSetAddElement(componentDataSet, entity, componentData);
}

void GECS_DeleteComponent(ID entity, const char *componentTypeName)
{
	GECS_EXPECT(initialized);
	GECS_EXPECT(entity && componentTypeName);

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
	if (!SparseSetHasElement(&entities, entity))
	{
		printf("GECS_DeleteComponent ERROR: entity %zu does not exist.\n", entity);
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
	if (!SparseSetHasElement(componentDataSet, entity))
	{
		printf("GECS_DeleteComponent ERROR: entity %zu does not have component type '%s'.\n", entity, componentTypeName);
		return;
	}

	SparseSetRemoveElement(componentDataSet, entity);
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

	free(componentNames);

	componentNames = NULL;
	componentNum = 0;
	nextID = 0;

	hashmap_delete(componentMap);
	SparseSetFree(&entities);

	initialized = false;
}
