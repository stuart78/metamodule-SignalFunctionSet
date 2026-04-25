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
	// FugueX (expander), Beat, and Note are not ported to MetaModule.
}
