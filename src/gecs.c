#include <gecs.h>
#include <stdlib.h>
#define SPARSE_SET_IMPL
#include <sparse_set.h>
#define HASHMAP_IMPL
#include <hashmap.h>

static size_t componentNum;
static char** componentNames;
static hashmap componentMap;

#define GECS_EXPECT(condition, ...)\
do\
{\
    if (!condition)\
    {\
        printf("\033[31m" "ASSERTION FAILED at %s -- line %d\n", __FILE__, __LINE__);\
        __VA_OPT__(printf("Message: " "\033[39m", __VA_ARGS__);)\
    }\
} while (0)

void GECS_RegisterComponent(const char *name, size_t size)
{
	GECS_EXPECT(name && size);

	{
		uint64_t val;
		if (hashmap_get_val(componentMap, name, &val))
		{
			printf("GECS_RegisterComponent WARNING: a component of name %s already exists.\n", name);
			return;
		}
	}

	struct SparseSet* set = malloc(sizeof(struct SparseSet));
	GECS_EXPECT(set);

	SparseSetCreate(set, 0, size);
	hashmap_set_val(componentMap, name, (uint64_t)set);

	if (!componentNames)
	{
		componentNames = malloc(sizeof(char*));
	}

	else
	{
		componentNames = realloc(componentNames, sizeof(char*) * componentNum + 1);
	}

	GECS_EXPECT(componentNames);

	size_t nameLen = strlen(name);
	componentNames[componentNum] = malloc(nameLen + 1);
	strncpy(componentNames[componentNum], name, nameLen);
	componentNames[componentNum][nameLen] = 0;

	componentNum++;
}

void GECS_CleanUp()
{
	//Free sparse sets
	for (size_t i = 0; i < componentNum; i++)
	{
		struct SparseSet* set;
		GECS_EXPECT(hashmap_get_val(componentMap, componentNames[i], (uint64_t*)&set));
		free(set);

		free(componentNames[i]);
	}

	free(componentNames);
	hashmap_delete(componentMap);
}
