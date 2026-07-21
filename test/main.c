#include <stdio.h>
#include <gecs.h>
#include <stdint.h>

struct Position
{
	float x, y;
};

typedef enum
{
	BOOL,
	FLOAT
} FieldType;

int main()
{
	GECS_Init();

	ComponentTypeID id = GECS_RegisterComponent(sizeof(struct Position), "Position", 2, FLOAT, "x", FLOAT, "y");

	const ComponentTypeInfo* info = GECS_GetComponentTypeInfo(id);

	printf("Component Type Info:\n\tname %s\n\tfield count %d\n\t", info->name, info->fieldCount);
	for (uint32_t i = 0; i < info->fieldCount; i++)
	{
		printf("%s %d\n\t", info->componentFieldsInfo[i].name, info->componentFieldsInfo->type);
	}

	puts("");

	GECS_CleanUp();

	printf("All done.\n");
    return 0;
}
