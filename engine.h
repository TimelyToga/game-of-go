#ifndef ENGINE_H
#define ENGINE_H

#include <stdint.h>
#include "raylib.h"

typedef struct
{
    int windowWidth;
    int windowHeight;
    uint64_t simulationStep;
} State;

State *createState(void);

void init(State *state);
void doSimulation(State *state);
void draw(State *state);
#endif