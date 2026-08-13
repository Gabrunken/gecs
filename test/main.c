#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "gecs.h" // Assumo che il tuo header si chiami così

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

// --- VARIABILI GLOBALI PER GLI ID ---
ComponentTypeID COMP_POSITION;
ComponentTypeID COMP_HEALTH;
ComponentTypeID COMP_BOMB;

SystemID SYS_COMBAT;
SystemID SYS_MOVEMENT;
SystemID SYS_BOMB;

// --- SISTEMA 1: COMBATTIMENTO E MORTE ---
// Testa GECS_DeleteEntity differito e GECS_AttachComponent differito
void CombatSystem(EntityID entity, void** components)
{
    Health* health = (Health*)components[0];

    // Il veleno toglie 10 hp a frame
    health->hp -= 10;
    printf("[SYS_COMBAT] Entita' %zu colpita. HP rimasti: %d\n", entity.id, health->hp);

    if (health->hp <= 0)
    {
        printf("  -> [! ATTENZIONE !] L'entita' %zu e' morta! Chiamo GECS_DeleteEntity...\n", entity.id);
        GECS_DeleteEntity(entity);
    }
    else if (health->hp == 50)
    {
        // A metà vita, l'entità impazzisce e diventa una bomba
        printf("  -> [! MUTAZIONE !] L'entita' %zu è a 50 HP. Aggiungo componente BOMB!\n", entity.id);
        Bomb newBomb = { 3 }; // 3 frame prima di esplodere
        GECS_AttachComponent(entity, COMP_BOMB, &newBomb);
    }
}

// --- SISTEMA 2: MOVIMENTO ---
// Serve a dimostrare che, nello stesso frame, un'entità appena "uccisa" dal CombatSystem
// esiste ancora finché non chiamiamo ProcessFrameEnd.
void MovementSystem(EntityID entity, void** components)
{
    Position* pos = (Position*)components[0];
    pos->x += 1.0f;
    printf("[SYS_MOVEMENT] Entita' %zu si e' mossa a X: %.1f\n", entity.id, pos->x);
}

// --- SISTEMA 3: IL CATACLISMA ---
// Testa l'annullamento della coda tramite ClearECS
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

    // 1. Registrazione Componenti
    // (Uso dei tipi fittizi "4" per float e "1" per int, adatta secondo la tua vera implementazione!)
    COMP_POSITION = GECS_RegisterComponent(sizeof(Position), "Position", 2, 4, "x", 4, "y");
    COMP_HEALTH   = GECS_RegisterComponent(sizeof(Health), "Health", 1, 1, "hp");
    COMP_BOMB     = GECS_RegisterComponent(sizeof(Bomb), "Bomb", 1, 1, "timeToLive");

    // 2. Registrazione Sistemi (l'ordine conta per l'esecuzione!)
    SYS_COMBAT   = GECS_RegisterSystem(CombatSystem, 1, COMP_HEALTH);
    SYS_MOVEMENT = GECS_RegisterSystem(MovementSystem, 1, COMP_POSITION);
    SYS_BOMB     = GECS_RegisterSystem(BombSystem, 1, COMP_BOMB);

    // 3. Creazione Entità di Test
    EntityID orco = GECS_CreateEntity("Orco");
    Position posOrco = { 0.0f, 0.0f };
    Health hpOrco = { 70 }; // Partirà da 70, scenderà a 50 (diventa bomba), poi a 0 (muore)

    // Attacchiamo i componenti iniziali "istantaneamente" (il lock dei sistemi è off qui)
    GECS_AttachComponent(orco, COMP_POSITION, &posOrco);
    GECS_AttachComponent(orco, COMP_HEALTH, &hpOrco);

    printf("\n--- INIZIO GAME LOOP ---\n");

    // Simuliamo 6 frame di gioco
    for (int frame = 1; frame <= 6; frame++)
    {
        printf("\n=== FRAME %d ===\n", frame);

        // -- ESECUZIONE SISTEMI --
        // Attiva qui il tuo blocco di mutazione strutturale se lo hai implementato in ExecuteSystem!
        GECS_ExecuteSystem(SYS_COMBAT);
        GECS_ExecuteSystem(SYS_MOVEMENT);
        GECS_ExecuteSystem(SYS_BOMB);

        // -- FINE FRAME --
        printf(">>> Chiamo ProcessFrameEnd() per svuotare il Command Buffer...\n");
        GECS_ProcessFrameEnd();

        // Verifica manuale per vedere se l'Orco esiste ancora nel frame successivo al ClearECS
        if (!GECS_DoesEntityExist(orco)) {
            printf(">>> L'engine conferma: l'entita' Orco non esiste piu' fisicamente.\n");
            // Se l'engine si è pulito per il ClearECS, usciamo dal loop.
            if (frame >= 4) {
                printf(">>> Il cataclisma ha distrutto tutto, esco dal loop in anticipo!\n");
                break;
            }
        }
    }

    printf("\n--- CLEANUP GECS ---\n");
    GECS_CleanUp();
    printf("Test superato senza crash. Sei un chad.\n");

    return 0;
}
