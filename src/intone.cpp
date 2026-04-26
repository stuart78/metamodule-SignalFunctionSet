#include "plugin.hpp"
#include "sfs_lut.hpp"
#include <cmath>


static const int NUM_FORMANTS = 5;
// Overlapping FOFs per formant. On MetaModule the per-sample CPU cost of
// dozens of simultaneous damped sinusoids is significant, so we halve the
// pool. With 5 formants * 4 grains = 20 max active grains it can still
// produce smooth attacks for typical f0 rates.
#ifdef METAMODULE
static const int GRAINS_PER_FORMANT = 4;
#else
static const int GRAINS_PER_FORMANT = 8;
#endif
static const int DISPLAY_BINS = 256;       // spectrum display resolution

// Vowel preset table: F1-F5 frequencies in Hz for /a/, /e/, /i/, /o/, /u/
// (adult male voice averages)
static const float VOWEL_FREQS[5][NUM_FORMANTS] = {
	{ 730.f, 1090.f, 2440.f, 3400.f, 4400.f }, // /a/
	{ 530.f, 1840.f, 2480.f, 3400.f, 4400.f }, // /e/
	{ 270.f, 2290.f, 3010.f, 3400.f, 4400.f }, // /i/
	{ 570.f,  840.f, 2410.f, 3400.f, 4400.f }, // /o/
	{ 300.f,  870.f, 2240.f, 3400.f, 4400.f }, // /u/
};

// Default per-formant amplitudes (formants higher up are usually quieter)
static const float DEFAULT_AMPS[NUM_FORMANTS] = { 1.0f, 0.8f, 0.5f, 0.3f, 0.2f };


// One FOF grain — a damped sinusoid triggered at a moment in time.
//
// Per-sample math is the hot path: with 5 formants × N grains × 48kHz,
// we go through this `tick()` millions of times per second. The original
// implementation called std::sin / std::cos / std::exp per grain per
// sample. We now use:
//   - phase accumulators (no sin/cos with growing argument)
//   - a sine LUT for the oscillator and the cos attack window
//   - a precomputed dampPerSample = exp(-alpha) (constant for grain lifetime)
//   - the built-in Hann LUT for the half-cosine attack ramp
struct FOFGrain {
	bool active = false;
	int k = 0;              // samples since trigger
	int attackSamples = 0;  // attack window length (samples)
	float invAttackSamples = 0.f; // 1 / attackSamples, for fast normalization
	float oscPhase = 0.f;   // sine oscillator phase, 0..1, increments by oscInc/sample
	float oscInc = 0.f;     // = omega / (2π) — frequency in cycles/sample
	float amplitude = 0.f;
	float envelope = 1.f;   // current decay envelope (running e^(-αk))
	float dampPerSample = 1.f; // = exp(-alpha), precomputed once at trigger

	void trigger(float w, float a, float b, float amp) {
		k = 0;
		oscPhase = 0.f;
		// Phase increment per sample = ω / (2π) = freq * sampleTime
		oscInc = w * (1.f / (2.f * (float)M_PI));
		amplitude = amp;
		envelope = 1.f;
		// exp(-alpha) is invariant for the grain's lifetime — compute it once.
		dampPerSample = std::exp(-a);
		// attackSamples = π/β (when β > 0); cap to reasonable max
		attackSamples = (b > 0.f) ? (int)(M_PI / b) : 64;
		if (attackSamples > 2048) attackSamples = 2048;
		if (attackSamples < 4) attackSamples = 4;
		invAttackSamples = 1.f / (float)attackSamples;
		active = true;
	}

	float tick() {
		if (!active) return 0.f;

		// Sinusoid (LUT, no per-sample std::sin)
		float s = sfs_lut::sine(oscPhase);

		// Attack window via Hann LUT.
		// Original was 0.5 * (1 - cos(β*k)) which equals hann(k/attackSamples)
		// because attackSamples = π/β (so β*k goes from 0 to π over attack).
		// Hann LUT covers 0..1 with full period; we want first half of cosine
		// (0 to π), so map k/attackSamples to phase in [0, 0.5] of the Hann LUT.
		float env;
		if (k < attackSamples) {
			env = sfs_lut::hann(0.5f * (float)k * invAttackSamples);
		} else {
			env = 1.f;
		}
		float out = amplitude * envelope * env * s;

		// Advance phase + envelope
		oscPhase += oscInc;
		if (oscPhase >= 1.f) oscPhase -= (int)oscPhase;
		k++;
		envelope *= dampPerSample;  // no per-sample std::exp

		// Deactivate when envelope is very small
		if (envelope < 1e-5f && k > attackSamples) {
			active = false;
		}

		return out;
	}
};


// One formant cell: a ring buffer of grains
struct FormantCell {
	FOFGrain grains[GRAINS_PER_FORMANT];
	int nextSlot = 0;

	void trigger(float omega, float alpha, float beta, float amp) {
		grains[nextSlot].trigger(omega, alpha, beta, amp);
		nextSlot = (nextSlot + 1) % GRAINS_PER_FORMANT;
	}

	float tick() {
		float sum = 0.f;
		for (int i = 0; i < GRAINS_PER_FORMANT; i++) {
			if (grains[i].active) {
				sum += grains[i].tick();
			}
		}
		return sum;
	}
};


// Simple 2-pole resonant bandpass filter (RBJ biquad).
// Used in Audio mode to formant-filter the incoming signal.
struct Biquad {
	float b0 = 1.f, b1 = 0.f, b2 = 0.f;
	float a1 = 0.f, a2 = 0.f;
	float x1 = 0.f, x2 = 0.f, y1 = 0.f, y2 = 0.f;

	void setBPF(float freqHz, float bwHz, float sampleRate) {
		float w0 = 2.f * (float)M_PI * freqHz / sampleRate;
		float Q = freqHz / bwHz;
		if (Q < 0.1f) Q = 0.1f;
		float sw = std::sin(w0);
		float cw = std::cos(w0);
		float alpha = sw / (2.f * Q);
		float a0 = 1.f + alpha;
		b0 = alpha / a0;
		b1 = 0.f;
		b2 = -alpha / a0;
		a1 = -2.f * cw / a0;
		a2 = (1.f - alpha) / a0;
	}

	float process(float x) {
		float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
		x2 = x1; x1 = x;
		y2 = y1; y1 = y;
		return y;
	}

	void reset() {
		x1 = x2 = y1 = y2 = 0.f;
	}
};


// Forward declaration for display
struct Intone;

struct IntoneDisplay : Widget {
	Intone* module = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override;

	void draw(const DrawArgs& args) override {
		Widget::draw(args);
	}
};


// Custom horizontal vowel morph slider — reuses Fugue's slider SVG assets
struct VowelMorphSlider : app::SvgSlider {
	VowelMorphSlider() {
		horizontal = true;
		setBackgroundSvg(Svg::load(asset::plugin(pluginInstance, "res/slider-horiz-bg.svg")));
		setHandleSvg(Svg::load(asset::plugin(pluginInstance, "res/slider-horiz-handle.svg")));
		setHandlePosCentered(
			math::Vec(8.1f, 8.12f),
			math::Vec(89.34f, 8.12f)
		);
	}
};


struct Intone : Module {
	enum ParamId {
		F1_FREQ_PARAM, F2_FREQ_PARAM, F3_FREQ_PARAM, F4_FREQ_PARAM, F5_FREQ_PARAM,
		F1_BW_PARAM,   F2_BW_PARAM,   F3_BW_PARAM,   F4_BW_PARAM,   F5_BW_PARAM,
		F1_AMP_PARAM,  F2_AMP_PARAM,  F3_AMP_PARAM,  F4_AMP_PARAM,  F5_AMP_PARAM,
		VOWEL_PARAM,
		SKIRT_PARAM,
		MODE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		VOCT_INPUT,
		EXC_INPUT,
		F1_FREQ_CV, F2_FREQ_CV, F3_FREQ_CV, F4_FREQ_CV, F5_FREQ_CV,
		F1_BW_CV,   F2_BW_CV,   F3_BW_CV,   F4_BW_CV,   F5_BW_CV,
		VOWEL_CV,
		SKIRT_CV,
		INPUTS_LEN
	};
	enum OutputId {
		AUDIO_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	// DSP state
	FormantCell formants[NUM_FORMANTS];
	Biquad bpfs[NUM_FORMANTS];
	float phase = 0.f;
	dsp::SchmittTrigger excTrigger;

	// Cached current parameters (used by display)
	float currentFormantFreq[NUM_FORMANTS] = {};
	float currentFormantBW[NUM_FORMANTS] = {};
	float currentFormantAmp[NUM_FORMANTS] = {};
	bool displayDirty = true;

	Intone() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		// Per-formant frequency offset (±1 octave)
		for (int i = 0; i < NUM_FORMANTS; i++) {
			configParam(F1_FREQ_PARAM + i, -1.f, 1.f, 0.f,
				string::f("Formant %d Frequency Offset", i + 1), " oct");
		}
		// Per-formant bandwidth
		for (int i = 0; i < NUM_FORMANTS; i++) {
			configParam(F1_BW_PARAM + i, 30.f, 500.f, 80.f,
				string::f("Formant %d Bandwidth", i + 1), " Hz");
		}
		// Per-formant amplitude
		for (int i = 0; i < NUM_FORMANTS; i++) {
			configParam(F1_AMP_PARAM + i, 0.f, 1.f, DEFAULT_AMPS[i],
				string::f("Formant %d Amplitude", i + 1));
		}

		configParam(VOWEL_PARAM, 0.f, 1.f, 0.f, "Vowel Morph");
		configParam(SKIRT_PARAM, 0.f, 1.f, 0.5f, "Skirt Width");
		configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Input Mode", {"Audio", "Trigger"});

		configInput(VOCT_INPUT, "V/Oct");
		configInput(EXC_INPUT, "Excitation");
		for (int i = 0; i < NUM_FORMANTS; i++) {
			configInput(F1_FREQ_CV + i, string::f("Formant %d Frequency CV", i + 1));
		}
		for (int i = 0; i < NUM_FORMANTS; i++) {
			configInput(F1_BW_CV + i, string::f("Formant %d Bandwidth CV", i + 1));
		}
		configInput(VOWEL_CV, "Vowel Morph CV");
		configInput(SKIRT_CV, "Skirt Width CV");

		configOutput(AUDIO_OUTPUT, "Audio");
	}

	// Linear interpolation between vowel formant frequencies
	float vowelFreq(int formantIdx, float vowelPos) {
		// vowelPos in 0-1, 5 vowels evenly spaced
		float scaled = vowelPos * 4.f; // 0..4
		int idx = (int)scaled;
		if (idx >= 4) {
			idx = 4;
			return VOWEL_FREQS[4][formantIdx];
		}
		float frac = scaled - (float)idx;
		return VOWEL_FREQS[idx][formantIdx] * (1.f - frac) +
		       VOWEL_FREQS[idx + 1][formantIdx] * frac;
	}

	void process(const ProcessArgs& args) override {
		// Read mode
		bool modeSwitchTrigger = params[MODE_PARAM].getValue() > 0.5f;
		bool excPatched = inputs[EXC_INPUT].isConnected();
		// Three effective modes:
		//   Default (no patch): FOF synth, V/Oct = pitch
		//   Audio mode (patched + switch up): BPF bank, V/Oct = formant transpose
		//   Trigger mode (patched + switch down): FOF triggered externally, V/Oct = formant transpose
		bool audioMode = excPatched && !modeSwitchTrigger;
		bool triggerMode = excPatched && modeSwitchTrigger;
		bool defaultMode = !excPatched;

		// V/Oct
		float voct = inputs[VOCT_INPUT].isConnected() ? inputs[VOCT_INPUT].getVoltage() : 0.f;

		// Vowel morph position
		float vowelPos = params[VOWEL_PARAM].getValue();
		if (inputs[VOWEL_CV].isConnected())
			vowelPos += inputs[VOWEL_CV].getVoltage() / 10.f;
		vowelPos = clamp(vowelPos, 0.f, 1.f);

		// Skirt width: 0 = soft (slow attack), 1 = hard (fast attack)
		// Maps to beta value — larger beta = faster attack = wider skirts
		float skirt = params[SKIRT_PARAM].getValue();
		if (inputs[SKIRT_CV].isConnected())
			skirt += inputs[SKIRT_CV].getVoltage() / 5.f;
		skirt = clamp(skirt, 0.f, 1.f);
		// Map skirt 0-1 to beta range: 0.005 to 0.05 (radians per sample)
		float beta = 0.005f + skirt * 0.045f;

		// Compute fundamental frequency and formant transpose ratio.
		// Only default mode uses V/Oct as pitch — the other two modes use
		// V/Oct as a formant transposition multiplier.
		float f0;
		float formantTransposeRatio = 1.f;
		if (defaultMode) {
			f0 = dsp::FREQ_C4 * std::pow(2.f, voct);
			f0 = clamp(f0, 8.f, 4000.f);
		} else {
			f0 = dsp::FREQ_C4; // not used in audio/trigger modes
			formantTransposeRatio = std::pow(2.f, voct);
		}

		// Compute per-formant parameters
		float formantFreqs[NUM_FORMANTS];
		float formantBWs[NUM_FORMANTS];
		float formantAmps[NUM_FORMANTS];

		for (int i = 0; i < NUM_FORMANTS; i++) {
			// Base frequency from vowel morph
			float baseFreq = vowelFreq(i, vowelPos);
			// Manual offset (±1 octave)
			float offset = params[F1_FREQ_PARAM + i].getValue();
			if (inputs[F1_FREQ_CV + i].isConnected())
				offset += inputs[F1_FREQ_CV + i].getVoltage() / 5.f;
			offset = clamp(offset, -1.f, 1.f);
			float freq = baseFreq * std::pow(2.f, offset) * formantTransposeRatio;
			freq = clamp(freq, 30.f, 8000.f);
			formantFreqs[i] = freq;

			// Bandwidth
			float bw = params[F1_BW_PARAM + i].getValue();
			if (inputs[F1_BW_CV + i].isConnected())
				bw += inputs[F1_BW_CV + i].getVoltage() * 50.f;
			bw = clamp(bw, 30.f, 500.f);
			formantBWs[i] = bw;

			// Amplitude
			formantAmps[i] = params[F1_AMP_PARAM + i].getValue();
		}

		// Update display cache + dirty flag
		bool changed = false;
		for (int i = 0; i < NUM_FORMANTS; i++) {
			if (std::fabs(currentFormantFreq[i] - formantFreqs[i]) > 0.5f ||
			    std::fabs(currentFormantBW[i] - formantBWs[i]) > 0.5f ||
			    std::fabs(currentFormantAmp[i] - formantAmps[i]) > 0.001f) {
				changed = true;
			}
			currentFormantFreq[i] = formantFreqs[i];
			currentFormantBW[i] = formantBWs[i];
			currentFormantAmp[i] = formantAmps[i];
		}
		if (changed) displayDirty = true;

		float out = 0.f;

		if (audioMode) {
			// --- Audio mode: parallel resonant BPF bank ---
			// Update filter coefficients when params change (cheap to do per-sample,
			// but only worth it on change)
			if (changed) {
				for (int i = 0; i < NUM_FORMANTS; i++) {
					bpfs[i].setBPF(formantFreqs[i], formantBWs[i], args.sampleRate);
				}
			}
			float in = inputs[EXC_INPUT].getVoltage() / 5.f;
			for (int i = 0; i < NUM_FORMANTS; i++) {
				out += bpfs[i].process(in) * formantAmps[i];
			}
			// Scale: the BPF gain is roughly unity at peak, so output stays
			// close to input level. Bring back to ±5V.
			out *= 5.f;
		} else {
			// --- Default mode or Trigger mode: FOF synthesis ---
			bool fireGrains = false;

			if (triggerMode) {
				// Rising edge on EXC input fires fresh FOFs
				if (excTrigger.process(inputs[EXC_INPUT].getVoltage(), 0.1f, 1.f)) {
					fireGrains = true;
				}
			} else {
				// Default mode: internal F0 oscillator drives re-trigger
				phase += f0 * args.sampleTime;
				if (phase >= 1.f) {
					phase -= 1.f;
					fireGrains = true;
				}
			}

			if (fireGrains) {
				for (int i = 0; i < NUM_FORMANTS; i++) {
					float omega = 2.f * (float)M_PI * formantFreqs[i] * args.sampleTime;
					float alpha = (float)M_PI * formantBWs[i] * args.sampleTime;
					float amp = formantAmps[i];
					if (std::fabs(amp) > 0.0001f) {
						formants[i].trigger(omega, alpha, beta, amp);
					}
				}
			}

			// Sum all formant cells
			for (int i = 0; i < NUM_FORMANTS; i++) {
				out += formants[i].tick();
			}
			// Scale to ±5V (FOF outputs are typically small)
			out *= 2.5f;
		}

		outputs[AUDIO_OUTPUT].setVoltage(clamp(out, -10.f, 10.f));
	}
};


// Spectrum display drawing implementation
void IntoneDisplay::drawLayer(const DrawArgs& args, int layer) {
	if (layer != 1 || !module) {
		Widget::drawLayer(args, layer);
		return;
	}

	float w = box.size.x;
	float h = box.size.y;

	// Logarithmic frequency axis: 50 Hz to 5000 Hz
	const float minFreq = 50.f;
	const float maxFreq = 5000.f;
	const float logMin = std::log10(minFreq);
	const float logMax = std::log10(maxFreq);
	const float logRange = logMax - logMin;

	auto freqToX = [&](float f) -> float {
		float lf = std::log10(clamp(f, minFreq, maxFreq));
		return (lf - logMin) / logRange * w;
	};

	// Per-formant colors
	NVGcolor formantColors[NUM_FORMANTS] = {
		nvgRGBA(100, 180, 255, 80),  // F1 blue
		nvgRGBA(100, 255, 180, 80),  // F2 cyan-green
		nvgRGBA(255, 220, 100, 80),  // F3 yellow
		nvgRGBA(255, 140, 100, 80),  // F4 orange
		nvgRGBA(255, 100, 180, 80),  // F5 pink
	};

	// Draw each formant as a Lorentzian bell curve
	// amp / (1 + ((f - f_center) / (bandwidth/2))²)
	for (int i = 0; i < NUM_FORMANTS; i++) {
		float fc = module->currentFormantFreq[i];
		float bw = module->currentFormantBW[i];
		float amp = module->currentFormantAmp[i];

		if (amp < 0.001f) continue;

		nvgBeginPath(args.vg);
		bool first = true;
		for (int b = 0; b < DISPLAY_BINS; b++) {
			float t = (float)b / (float)(DISPLAY_BINS - 1);
			float lf = logMin + t * logRange;
			float f = std::pow(10.f, lf);
			float dist = (f - fc) / (bw * 0.5f);
			float val = amp / (1.f + dist * dist);
			float px = t * w;
			float py = h - 2.f - val * (h - 4.f);
			if (first) {
				nvgMoveTo(args.vg, px, py);
				first = false;
			} else {
				nvgLineTo(args.vg, px, py);
			}
		}
		// Close path along bottom for fill
		nvgLineTo(args.vg, w, h - 2.f);
		nvgLineTo(args.vg, 0.f, h - 2.f);
		nvgClosePath(args.vg);
		nvgFillColor(args.vg, formantColors[i]);
		nvgFill(args.vg);
	}

	// Draw composite envelope (sum of all formants) as bright line
	nvgBeginPath(args.vg);
	for (int b = 0; b < DISPLAY_BINS; b++) {
		float t = (float)b / (float)(DISPLAY_BINS - 1);
		float lf = logMin + t * logRange;
		float f = std::pow(10.f, lf);
		float total = 0.f;
		for (int i = 0; i < NUM_FORMANTS; i++) {
			float fc = module->currentFormantFreq[i];
			float bw = module->currentFormantBW[i];
			float amp = module->currentFormantAmp[i];
			float dist = (f - fc) / (bw * 0.5f);
			total += amp / (1.f + dist * dist);
		}
		// Cap at 1.0 for display
		if (total > 1.f) total = 1.f;
		float px = t * w;
		float py = h - 2.f - total * (h - 4.f);
		if (b == 0) nvgMoveTo(args.vg, px, py);
		else nvgLineTo(args.vg, px, py);
	}
	nvgStrokeColor(args.vg, nvgRGBA(220, 220, 255, 220));
	nvgStrokeWidth(args.vg, 1.2f);
	nvgStroke(args.vg);

	Widget::drawLayer(args, layer);
}


struct IntoneWidget : ModuleWidget {
	IntoneWidget(Intone* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/intone.svg")));

		// Screws
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Spectrum display
		IntoneDisplay* display = new IntoneDisplay();
		display->module = module;
		display->box.pos = mm2px(Vec(5.8f, 14.f));
		display->box.size = mm2px(Vec(69.68f, 24.f));
		addChild(display);

		// Formant grid: 5 columns at X = 13.55, 27.10, 40.64, 54.18, 67.73
		float colX[NUM_FORMANTS] = { 13.55f, 27.10f, 40.64f, 54.18f, 67.73f };

		// Freq trimpots Y=48
		for (int i = 0; i < NUM_FORMANTS; i++) {
			addParam(createParamCentered<Trimpot>(
				mm2px(Vec(colX[i], 48.f)), module, Intone::F1_FREQ_PARAM + i));
		}
		// BW trimpots Y=60
		for (int i = 0; i < NUM_FORMANTS; i++) {
			addParam(createParamCentered<Trimpot>(
				mm2px(Vec(colX[i], 60.f)), module, Intone::F1_BW_PARAM + i));
		}
		// Amp trimpots Y=72
		for (int i = 0; i < NUM_FORMANTS; i++) {
			addParam(createParamCentered<Trimpot>(
				mm2px(Vec(colX[i], 72.f)), module, Intone::F1_AMP_PARAM + i));
		}
		// Freq CV jacks Y=84
		for (int i = 0; i < NUM_FORMANTS; i++) {
			addInput(createInputCentered<PJ301MPort>(
				mm2px(Vec(colX[i], 84.f)), module, Intone::F1_FREQ_CV + i));
		}
		// BW CV jacks Y=94
		for (int i = 0; i < NUM_FORMANTS; i++) {
			addInput(createInputCentered<PJ301MPort>(
				mm2px(Vec(colX[i], 94.f)), module, Intone::F1_BW_CV + i));
		}

		// Vowel morph slider Y=106 (centered horizontally)
		addParam(createParamCentered<VowelMorphSlider>(
			mm2px(Vec(40.64f, 106.f)), module, Intone::VOWEL_PARAM));

		// Bottom row Y=118
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.62f, 118.f)), module, Intone::VOWEL_CV));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(17.78f, 118.f)), module, Intone::SKIRT_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(27.94f, 118.f)), module, Intone::SKIRT_CV));
		addParam(createParamCentered<CKSS>(mm2px(Vec(38.10f, 118.f)), module, Intone::MODE_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(48.26f, 118.f)), module, Intone::VOCT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(58.42f, 118.f)), module, Intone::EXC_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(73.66f, 118.f)), module, Intone::AUDIO_OUTPUT));
	}
};


Model* modelIntone = createModel<Intone, IntoneWidget>("Intone");
