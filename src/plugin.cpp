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
	// FugueX (expander), Beat, and Note are not ported to MetaModule.
}
