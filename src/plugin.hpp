#pragma once
#include <rack.hpp>


using namespace rack;

// Declare the Plugin, defined in plugin.cpp
extern Plugin* pluginInstance;

// Declare each Model, defined in each module source file
extern Model* modelDrift;
extern Model* modelGsx;
extern Model* modelFugue;
extern Model* modelPhase;
extern Model* modelOvertone;
extern Model* modelIntone;
extern Model* modelTine;
extern Model* modelMeter;
extern Model* modelSwell;
extern Model* modelShift;
extern Model* modelVac;
extern Model* modelBand;
extern Model* modelMetaFugue;
extern Model* modelMuse;
extern Model* modelCycle;
extern Model* modelChance;
extern Model* modelOperator;
extern Model* modelOpEnv;
extern Model* modelBeat;
extern Model* modelNote;
extern Model* modelArrange;
extern Model* modelRecord;
extern Model* modelPlay;
// MetaModule doesn't support expanders — modelFugueX intentionally omitted.
