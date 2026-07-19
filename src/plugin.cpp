#include "plugin.hpp"


Plugin* pluginInstance;


void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelDrift);
	p->addModel(modelGsx);
	p->addModel(modelFugue);
	p->addModel(modelPhase);
	p->addModel(modelOvertone);
	p->addModel(modelIntone);
	p->addModel(modelTine);
	p->addModel(modelMeter);
	p->addModel(modelSwell);
	p->addModel(modelShift);
	p->addModel(modelVac);
	p->addModel(modelBand);
	p->addModel(modelMetaFugue);
	p->addModel(modelMuse);
	p->addModel(modelCycle);
	p->addModel(modelChance);
	p->addModel(modelOperator);
	p->addModel(modelOpEnv);
	p->addModel(modelBeat);
	p->addModel(modelNote);
	p->addModel(modelArrange);
	p->addModel(modelRecord);
	p->addModel(modelPlay);
	// FugueX and MeterX (expanders) are not ported to MetaModule.
	// Wave/Ratio/Gravity remain WIP/hidden on VCV and are not shipped here.
}
