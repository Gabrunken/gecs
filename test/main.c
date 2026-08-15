#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include "gecs.h"

#define MAX_ENTITIES 1000000 // 1 MILIONE di entità
#define TEST_FRAMES 100      // Quanti frame simulare

// --- COMPONENTI ---
typedef struct { float x, y; } Position;
typedef struct { float dx, dy; } Velocity;
typedef struct { int framesLeft; } LifeTime;

ComponentTypeID COMP_POS;
ComponentTypeID COMP_VEL;
ComponentTypeID COMP_LIFE;

// --- SISTEMI ---
// Sistema 1: Pura matematica intensiva per stressare la Cache e la CPU
void MovementSystem(EntityID entity, void** components)
{
    Position* pos = (Position*)components[0];
    Velocity* vel = (Velocity*)components[1];

    pos->x += vel->dx;
    pos->y += vel->dy;
}

// Sistema 2: Logica condizionale e stress del Command Buffer
void LifeSystem(EntityID entity, void** components)
{
    LifeTime* life = (LifeTime*)components[0];
    life->framesLeft--;

    if (life->framesLeft <= 0)
    {
        // Boom. Accodiamo la distruzione.
        // L'entità viene disattivata logicamente all'istante (niente zombie).
        GECS_DeleteEntity(entity);

        EntityID id = GECS_CreateEntity("New Entity");
        GECS_DeleteEntity(id);
    }
}

int main()
{
    printf("--- GECS MEGA STRESS TEST ---\n");
    printf("Inizializzazione Engine...\n");
    GECS_Init();

    // 1. Registrazione (uso '4' per indicare ipotetici float/int a 4 byte)
    COMP_POS  = GECS_RegisterComponent(sizeof(Position), "Position", 2, 4, "x", 4, "y");
    COMP_VEL  = GECS_RegisterComponent(sizeof(Velocity), "Velocity", 2, 4, "dx", 4, "dy");
    COMP_LIFE = GECS_RegisterComponent(sizeof(LifeTime), "LifeTime", 1, 4, "framesLeft");

    SystemID SYS_MOVE = GECS_RegisterSystem(MovementSystem, 2, COMP_POS, COMP_VEL);
    SystemID SYS_LIFE = GECS_RegisterSystem(LifeSystem, 1, COMP_LIFE);

    // 2. Spawn di Massa
    printf("Allocazione di %d Entita' in corso (Attendi)...\n", MAX_ENTITIES);

    srand(1337); // Seed fisso per avere risultati deterministici tra run diverse
    for (int i = 0; i < MAX_ENTITIES; i++)
    {
        EntityID e = GECS_CreateEntity("Particella");

        Position p = { (float)(rand() % 800), (float)(rand() % 600) };
        Velocity v = { (float)(rand() % 10) / 10.0f, (float)(rand() % 10) / 10.0f };
        LifeTime l = { (rand() % 50) + 10 }; // Vivono tra 10 e 60 frame!

        // Attacchiamo subito prima del loop, niente command buffer qui.
        GECS_AttachComponent(e, COMP_POS, &p);
        GECS_AttachComponent(e, COMP_VEL, &v);
        GECS_AttachComponent(e, COMP_LIFE, &l);
    }
    printf("Spawn completato. Inizio simulazione.\n\n");

    // 3. Loop di Simulazione
    double totalSystemsTime = 0.0;
    double totalFlushTime = 0.0;

    for (int frame = 1; frame <= TEST_FRAMES; frame++)
    {
        clock_t startFrame = clock();

        // -- FASE 1: ESECUZIONE SISTEMI --
        GECS_ExecuteSystem(SYS_MOVE);
        GECS_ExecuteSystem(SYS_LIFE);

        clock_t endSystems = clock();

        // -- FASE 2: RISOLUZIONE COMMAND BUFFER --
        GECS_ProcessFrameEnd();

        clock_t endFlush = clock();

        // Calcolo tempi in millisecondi
        double systemsMs = ((double)(endSystems - startFrame) / CLOCKS_PER_SEC) * 1000.0;
        double flushMs   = ((double)(endFlush - endSystems) / CLOCKS_PER_SEC) * 1000.0;

        totalSystemsTime += systemsMs;
        totalFlushTime += flushMs;

        // Stampiamo solo ogni 10 frame per non intasare la console e rallentare il test
        if (frame % 10 == 0) {
            printf("[Frame %03d] Sistemi: %.2f ms | CommandBuffer Flush: %.2f ms | Totale: %.2f ms\n",
                   frame, systemsMs, flushMs, systemsMs + flushMs);
        }
    }

    printf("\n--- RISULTATI BENCHMARK (%d Frames) ---\n", TEST_FRAMES);
    printf("Tempo MEDIO Sistemi: %.2f ms a frame\n", totalSystemsTime / TEST_FRAMES);
    printf("Tempo MEDIO Flush:   %.2f ms a frame\n", totalFlushTime / TEST_FRAMES);

    double avgTotalMs = (totalSystemsTime + totalFlushTime) / TEST_FRAMES;
    printf("Tempo MEDIO Totale:  %.2f ms (Stima: %.0f FPS)\n", avgTotalMs, 1000.0 / avgTotalMs);

    GECS_CleanUp();
    return 0;
}
