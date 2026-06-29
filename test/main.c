#include <stdio.h>
#include <gecs.h>
#include <stdint.h>

struct Position
{
	float x, y;
};

int main()
{
	GECS_Init();

	GECS_CleanUp();

	printf("All done.\n");
    return 0;
}
