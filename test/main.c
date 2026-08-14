#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "gecs.h"

// --- STRUTTURE COMPONENTI ---
typedef struct {
    float x;
    float y;
} Position;

typedef struct {
    int hp;
} Health;

typedef struct {
    int timeToLive;
} Bomb;

// --- VARIABILI GLOBALI ---
ComponentTypeID COMP_POSITION;
ComponentTypeID COMP_HEALTH;
ComponentTypeID COMP_BOMB;

SystemID SYS_COMBAT;
SystemID SYS_MOVEMENT;
SystemID SYS_BOMB;

// --- SISTEMA 1: COMBATTIMENTO E MORTE ---
void CombatSystem(EntityID entity, void** components)
{
    Health* health = (Health*)components[0];

    // Un colpo potentissimo da 20 HP
    health->hp -= 20;
    printf("[SYS_COMBAT] Entita' %zu colpita. HP rimasti: %d\n", entity.id, health->hp);

    if (health->hp <= 0)
    {
        // L'utente chiama solo questa. L'engine fa il resto:
        // 1. Accoda la distruzione fisica.
        // 2. Disattiva logicamente l'entità per il resto del frame tramite il tuo buffer interno.
        printf("  -> [! FATALITY !] L'entita' %zu e' morta! Chiamo GECS_DeleteEntity.\n", entity.id);
        GECS_DeleteEntity(entity);
    }
    else if (health->hp == 20)
    {
        printf("  -> [! MUTAZIONE !] L'entita' %zu e' a 20 HP. Ordino l'aggiunta del componente BOMB!\n", entity.id);
        Bomb newBomb = { 1 };
        GECS_AttachComponent(entity, COMP_BOMB, &newBomb);
    }
}

// --- SISTEMA 2: MOVIMENTO ---
void MovementSystem(EntityID entity, void** components)
{
    Position* pos = (Position*)components[0];
    pos->x += 1.0f;
    printf("[SYS_MOVEMENT] Entita' %zu si e' mossa a X: %.1f\n", entity.id, pos->x);
}

// --- SISTEMA 3: BOMBA ---
void BombSystem(EntityID entity, void** components)
{
    Bomb* bomb = (Bomb*)components[0];
    bomb->timeToLive -= 1;

    printf("[SYS_BOMB] Entita' %zu... Tic Toc... Manca %d\n", entity.id, bomb->timeToLive);

    if (bomb->timeToLive <= 0)
    {
        printf("  -> [! CATACLISMA !] La bomba %zu e' esplosa! Chiamo GECS_ClearECS()!\n", entity.id);
        GECS_ClearECS();
    }
}


// --- MAIN ---
int main()
{
    printf("--- INIZIALIZZAZIONE GECS ---\n");
    GECS_Init();

    COMP_POSITION = GECS_RegisterComponent(sizeof(Position), "Position", 2, 4, "x", 4, "y");
    COMP_HEALTH   = GECS_RegisterComponent(sizeof(Health), "Health", 1, 1, "hp");
    COMP_BOMB     = GECS_RegisterComponent(sizeof(Bomb), "Bomb", 1, 1, "timeToLive");

    SYS_COMBAT   = GECS_RegisterSystem(CombatSystem, 1, COMP_HEALTH);
    SYS_MOVEMENT = GECS_RegisterSystem(MovementSystem, 1, COMP_POSITION);
    SYS_BOMB     = GECS_RegisterSystem(BombSystem, 1, COMP_BOMB);

    EntityID orco = GECS_CreateEntity("Orco");
    Position posOrco = { 0.0f, 0.0f };
    Health hpOrco = { 40 }; // Test in 2 frame

    GECS_AttachComponent(orco, COMP_POSITION, &posOrco);
    GECS_AttachComponent(orco, COMP_HEALTH, &hpOrco);

    printf("\n--- INIZIO GAME LOOP ---\n");

    for (int frame = 1; frame <= 3; frame++)
    {
        printf("\n=== FRAME %d ===\n", frame);

        GECS_ExecuteSystem(SYS_COMBAT);
        GECS_ExecuteSystem(SYS_MOVEMENT);
        GECS_ExecuteSystem(SYS_BOMB);

        printf(">>> Chiamo ProcessFrameEnd() per svuotare il Command Buffer e ripulire la RAM...\n");
        GECS_ProcessFrameEnd();

        if (!GECS_DoesEntityExist(orco)) {
            printf(">>> L'engine conferma: l'entita' non esiste piu' fisicamente. Loop terminato in sicurezza.\n");
            break;
        }
    }

    printf("\n--- CLEANUP GECS ---\n");
    GECS_CleanUp();

    return 0;
}
