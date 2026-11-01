/*
 * catalog.c: see catalog.h. The order is the one the launcher lists locked achievements in.
 */
#include "catalog.h"

#include <string.h>

#define S(en, es) {L##en, L##es}

const ach_def ach_catalog[] = {
    {"chapter-01", ACH_CAT_STORY, 10, 0, 0, S("The First Step", "El primer paso"),
     S("Complete chapter 1 on any difficulty.", "Completa el capítulo 1 en cualquier dificultad.")},
    {"story-ninja-dog", ACH_CAT_STORY, 10, 0, 0, S("Faithful Dog", "Perro fiel"),
     S("Complete the story on Ninja Dog.", "Completa la historia en Ninja Dog.")},
    {"story-normal", ACH_CAT_STORY, 25, 0, 0, S("Way of the Ninja", "Camino del ninja"),
     S("Complete the story on Normal.", "Completa la historia en Normal.")},
    {"story-hard", ACH_CAT_STORY, 50, 0, 0, S("Tempered Steel", "Acero templado"),
     S("Complete the story on Hard.", "Completa la historia en Hard.")},
    {"story-very-hard", ACH_CAT_STORY, 75, 0, 0, S("No Quarter", "Sin tregua"),
     S("Complete the story on Very Hard.", "Completa la historia en Very Hard.")},
    {"story-master-ninja", ACH_CAT_STORY, 100, 0, 0, S("Master Ninja", "Maestro Ninja"),
     S("Complete the story on Master Ninja.", "Completa la historia en Master Ninja.")},
    {"rank-master-ninja", ACH_CAT_RANK, 15, 0, 0, S("Rank: Master Ninja", "Calificación: Master Ninja"),
     S("End a chapter with the Master Ninja rank.", "Termina un capítulo con el rango Master Ninja.")},
    {"rank-sixteen-seals", ACH_CAT_RANK, 100, 0, 16, S("Sixteen Seals", "Dieciséis sellos"),
     S("Get the Master Ninja rank in all 16 chapters on one difficulty.",
       "Consigue el rango Master Ninja en los 16 capítulos de una misma dificultad.")},
    {"true-ultimate", ACH_CAT_CHALLENGE, 250, 1, 0,
     S("The True Ultimate Master Ninja", "El verdadero Ultimate Master Ninja"),
     S("Complete the story on Master Ninja without healing items, ninpo, a hit taken, or life and ki upgrades.",
       "Completa la historia en Master Ninja sin objetos de curación, ninpo, golpes recibidos ni mejoras de vida y ki.")},
    {"mission-eternal-legend", ACH_CAT_MISSION, 75, 0, 0, S("Eternal Legend", "Leyenda eterna"),
     S("Clear the Eternal Legend mission.", "Supera la misión Eternal Legend.")},
    {"mission-eternal-legend-mn", ACH_CAT_MISSION, 150, 0, 0, S("Legend Without End", "Leyenda sin final"),
     S("Clear Eternal Legend on Master Ninja.", "Supera Eternal Legend en Master Ninja.")},
    {"missions-all-mn", ACH_CAT_MISSION, 150, 0, 46, S("Every Trial", "Todas las pruebas"),
     S("Clear all 46 missions on Master Ninja.", "Supera las 46 misiones en Master Ninja.")},
    {"dragon-sword-500", ACH_CAT_COUNTER, 25, 0, 500, S("Disciple of the Dragon", "Discípulo del dragón"),
     S("Defeat 500 enemies with the Dragon Sword.", "Derrota a 500 enemigos con la Espada del Dragón.")},
    {"fire-ninpo-100", ACH_CAT_COUNTER, 25, 0, 100, S("Rain of Fire", "Lluvia de fuego"),
     S("Cast fire ninpo 100 times: The Art of the Fire Wheels or of the Inferno.",
       "Lanza 100 ninpo de fuego: The Art of the Fire Wheels o The Art of the Inferno.")},
    {"golden-scarabs", ACH_CAT_COLLECTION, 50, 0, 50, S("Golden Scarabs", "Escarabajos dorados"),
     S("Hand all 50 Golden Scarabs to Muramasa.", "Entrega los 50 Escarabajos Dorados a Muramasa.")},
    {"dragon-sword-only", ACH_CAT_CHALLENGE, 100, 1, 0, S("Only the Sword", "Solo la espada"),
     S("Complete the story using only the Dragon Sword.", "Completa la historia usando solo la Espada del Dragón.")},
    {"deaths-100", ACH_CAT_CHALLENGE, 5, 0, 100, S("Always Back Up", "Siempre en pie"),
     S("Die 100 times. What matters is getting back up.", "Muere 100 veces. Lo que importa es levantarse.")},
    {"port-first-session", ACH_CAT_PORT, 5, 0, 0, S("First Session", "Primera sesión"),
     S("Play your first session in the native port.", "Juega tu primera partida en el port nativo.")},
    {"port-100-hours", ACH_CAT_PORT, 25, 0, 360000, S("A Hundred Hours", "Cien horas"),
     S("Play 100 hours in the port.", "Juega 100 horas en el port.")},
    {"port-screenshot", ACH_CAT_PORT, 5, 0, 0, S("Photographer", "Fotógrafo"),
     S("Save a screenshot with F12.", "Guarda una captura con F12.")},
};

const int ach_catalog_count = (int)(sizeof ach_catalog / sizeof ach_catalog[0]);

/* Designed and agreed, but not shown nor detected yet: what they need is in
 * docs/target/achievements-design.md. Moving one back into ach_catalog makes it live. */
const ach_def ach_parked[] = {
    {"mission-stadium-mn", ACH_CAT_MISSION, 100, 0, 0, S("The Arena", "La arena"),
     S("Clear the stadium challenge on Master Ninja.", "Supera el desafío del estadio en Master Ninja.")},
    {"mission-stadium-untouched", ACH_CAT_CHALLENGE, 150, 0, 0, S("Untouchable", "Intocable"),
     S("Clear the stadium on Master Ninja without healing or taking a hit.",
       "Supera el estadio en Master Ninja sin curarte ni recibir un golpe.")},
    {"izuna-1000", ACH_CAT_COUNTER, 50, 0, 1000, S("A Thousand Falls", "Mil caídas"),
     S("Finish 1,000 enemies with the Izuna Drop.", "Remata 1.000 enemigos con el Izuna Drop.")},
    {"flying-swallow-500", ACH_CAT_COUNTER, 20, 0, 500, S("Flight of the Swallow", "Vuelo de la golondrina"),
     S("Use the Flying Swallow 500 times.", "Usa el Flying Swallow 500 veces.")},
    {"all-weapons", ACH_CAT_COLLECTION, 30, 0, 0, S("Full Arsenal", "Arsenal completo"),
     S("Obtain every weapon.", "Consigue todas las armas.")},
    {"weapons-max", ACH_CAT_COLLECTION, 50, 0, 0, S("Master Forge", "Forja maestra"),
     S("Raise every weapon to its top level.", "Lleva todas las armas a su nivel máximo.")},
    {"boss-no-damage", ACH_CAT_CHALLENGE, 30, 0, 0, S("Not a Scratch", "Sin un rasguño"),
     S("Defeat a boss without taking damage.", "Derrota a un jefe sin recibir daño.")},
};

const int ach_parked_count = (int)(sizeof ach_parked / sizeof ach_parked[0]);

const ach_def *ach_find(const char *id) {
    int i;
    for (i = 0; id && i < ach_catalog_count; i++)
        if (!strcmp(ach_catalog[i].id, id))
            return &ach_catalog[i];
    return NULL;
}

const wchar_t *ach_category_glyph(ach_category c) {
    static const wchar_t *const k[ACH_CAT_COUNT] = {L"龍", L"極", L"試", L"数", L"蒐", L"挑", L"灯"};
    return (unsigned)c < ACH_CAT_COUNT ? k[c] : L"?";
}

const wchar_t *ach_category_name(ach_category c, int lang) {
    static const wchar_t *const k[ACH_CAT_COUNT][ACH_LANGS] = {
        {L"STORY", L"HISTORIA"},     {L"RANK", L"RANGO"},         {L"MISSIONS", L"MISIONES"},
        {L"COUNTER", L"CONTADOR"},   {L"COLLECTION", L"COLECCIÓN"}, {L"CHALLENGE", L"RETO"},
        {L"PORT", L"PORT"},
    };
    return (unsigned)c < ACH_CAT_COUNT && (unsigned)lang < ACH_LANGS ? k[c][lang] : L"";
}

int ach_total_points(void) {
    int i, n = 0;
    for (i = 0; i < ach_catalog_count; i++)
        n += ach_catalog[i].points;
    return n;
}
