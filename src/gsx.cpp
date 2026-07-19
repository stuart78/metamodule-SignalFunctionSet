#include "plugin.hpp"
#include "sfs_lut.hpp"


struct Gsx : Module {
	enum ParamId {
		PARAMFREQUENCY_PARAM,
		PARAMSTREAMS_PARAM,
		PARAMSHAPE_PARAM,
		PARAMRANGE_PARAM,
		PARAMDURATION_PARAM,
		PARAMDELAY_PARAM,
		PARAMDENSITY_PARAM,
		PARAMVARIATION_PARAM,
		PARAMSPREAD_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		INFREQUENCY_INPUT,
		INSTREAMS_INPUT,
		INSHAPE_INPUT,
		INRANGE_INPUT,
		INDURATION_INPUT,
		INDELAY_INPUT,
		INDENSITY_INPUT,
		INVARIATION_INPUT,
		INSPREAD_INPUT,
		INVCA_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUTLEFT_OUTPUT,
		OUTRIGHT_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	// Grain structure to track individual grain state
	struct Grain {
		bool active = false;           // Is this grain currently playing?
		float envelopePhase = 0.f;     // Envelope phase (0-1 over grain lifetime)
		float wavePhase = 0.f;         // Waveform phase (0-1, wraps for oscillation)
		float frequency = 440.f;       // Grain frequency in Hz
		float invDuration = 50.f;      // 1/duration (precomputed to avoid per-sample divide)
		// Precomputed equal-power pan gains. Computing sqrt() in the per-sample
		// loop (once was here) is ~20% of total CPU; pan never changes during
		// a grain's lifetime, so we compute it once at trigger time.
		float leftGain = 0.7071f;
		float rightGain = 0.7071f;
		// Precomputed wave phase increment per sample (= frequency * sampleTime).
		float waveIncrement = 0.f;
		// Precomputed envelope phase increment per sample (= sampleTime / duration).
		float envelopeIncrement = 0.f;

		void reset() {
			active = false;
			envelopePhase = 0.f;
			wavePhase = 0.f;
		}

		void trigger(float freq, float dur, float panPos, float sampleTime) {
			active = true;
			envelopePhase = 0.f;
			wavePhase = 0.f;
			frequency = freq;
			invDuration = 1.f / dur;
			waveIncrement = freq * sampleTime;
			envelopeIncrement = sampleTime * invDuration;
			float p = clamp(panPos, 0.f, 1.f);
			// Equal-power panning, computed once at trigger.
			leftGain = std::sqrt(1.f - p);
			rightGain = std::sqrt(p);
		}
	};

	// Hann window envelope function. Uses precomputed LUT instead of cos().
	float hannWindow(float phase) {
		return sfs_lut::hann(phase);
	}

	// Generate waveform sample at given phase with shape morphing
	// phase: 0-1 normalized phase
	// shape: 0-1 (0=sine, 0.33=tri, 0.66=saw, 1=square)
	float generateGrainWave(float phase, float shape) {
		// Sine wave (LUT instead of std::sin)
		float sine = sfs_lut::sine(phase);

		// Triangle wave: starts at 0, goes to +1 at 0.25, 0 at 0.5, -1 at 0.75, 0 at 1.0
		float triangle;
		if (phase < 0.25f) {
			triangle = 4.f * phase;
		} else if (phase < 0.75f) {
			triangle = 2.f - 4.f * phase;
		} else {
			triangle = 4.f * phase - 4.f;
		}

		// Sawtooth wave: ramps from 0 to +1 at 0.5, jumps to -1, ramps to 0 at 1.0
		float sawtooth;
		if (phase < 0.5f) {
			sawtooth = 2.f * phase;
		} else {
			sawtooth = 2.f * phase - 2.f;
		}

		// Square wave
		float square = (phase < 0.5f) ? 1.f : -1.f;

		// Morph between waveforms
		if (shape <= 0.33f) {
			// Sine to Triangle (0.0 to 0.33)
			float mix = shape * 3.f;
			return sine * (1.f - mix) + triangle * mix;
		}
		else if (shape <= 0.66f) {
			// Triangle to Sawtooth (0.33 to 0.66)
			float mix = (shape - 0.33f) * 3.f;
			return triangle * (1.f - mix) + sawtooth * mix;
		}
		else {
			// Sawtooth to Square (0.66 to 1.0)
			float mix = (shape - 0.66f) * 3.f;
			return sawtooth * (1.f - mix) + square * mix;
		}
	}

	// Stream management.
	// MetaModule has a far smaller CPU budget than a desktop, and 20×20=400
	// concurrent grains is overkill for the platform. Halving each dimension
	// quarters the worst-case work per sample while still supporting dense
	// textures (8 streams × 8 = 64 simultaneous grains).
#ifdef METAMODULE
	static constexpr int MAX_STREAMS = 8;
	static constexpr int GRAINS_PER_STREAM = 8;
#else
	static constexpr int MAX_STREAMS = 20;
	static constexpr int GRAINS_PER_STREAM = 20;
#endif

	struct Stream {
		Grain grains[GRAINS_PER_STREAM];
		float nextGrainTime = 0.f; // Time in seconds until next grain trigger
		float phaseAccumulator = 0.f; // For tracking fractional samples
	};

	Stream streams[MAX_STREAMS];

	Gsx() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(PARAMFREQUENCY_PARAM, std::log2(50.f), std::log2(2000.f), std::log2(130.81f), "Frequency", " Hz", 2.f, 1.f);
		configParam(PARAMSTREAMS_PARAM, 1.f, (float)MAX_STREAMS, (float)std::min(10, MAX_STREAMS), "Streams", " streams");
		configParam(PARAMSHAPE_PARAM, 0.f, 1.f, 0.f, "Shape");
		configParam(PARAMRANGE_PARAM, 0.f, 500.f, 100.f, "Range", " Hz");
		configParam(PARAMDURATION_PARAM, 1.f, 100.f, 20.f, "Duration", " ms");
		configParam(PARAMDELAY_PARAM, 0.1f, 200.f, 0.1f, "Delay", " ms");
		configParam(PARAMDENSITY_PARAM, 1.f, 1000.f, 100.f, "Density", " grains/sec");
		configParam(PARAMVARIATION_PARAM, 0.f, 1.f, 0.5f, "Variation", "%", 0.f, 100.f);
		configParam(PARAMSPREAD_PARAM, 0.f, 1.f, 0.5f, "Spread", "%", 0.f, 100.f);
		configInput(INFREQUENCY_INPUT, "Frequency CV");
		configInput(INSTREAMS_INPUT, "Streams CV");
		configInput(INSHAPE_INPUT, "Shape CV");
		configInput(INRANGE_INPUT, "Range CV");
		configInput(INDURATION_INPUT, "Duration CV");
		configInput(INDELAY_INPUT, "Delay CV");
		configInput(INDENSITY_INPUT, "Density CV");
		configInput(INVARIATION_INPUT, "Variation CV");
		configInput(INSPREAD_INPUT, "Spread CV");
		configInput(INVCA_INPUT, "VCA CV");
		configOutput(OUTLEFT_OUTPUT, "Left");
		configOutput(OUTRIGHT_OUTPUT, "Right");
	}

	void process(const ProcessArgs& args) override {
		// Read parameters with CV inputs (LUT-based pow2 — was 2 std::pow per sample)
		float centerFreq = sfs_lut::pow2(params[PARAMFREQUENCY_PARAM].getValue());
		if (inputs[INFREQUENCY_INPUT].isConnected()) {
			centerFreq *= sfs_lut::pow2(inputs[INFREQUENCY_INPUT].getVoltage());
		}
		centerFreq = clamp(centerFreq, 50.f, 2000.f);

		int numStreams = (int)std::round(params[PARAMSTREAMS_PARAM].getValue());
		if (inputs[INSTREAMS_INPUT].isConnected()) {
			numStreams = (int)std::round(clamp(params[PARAMSTREAMS_PARAM].getValue() +
				inputs[INSTREAMS_INPUT].getVoltage() * 2.f, 1.f, (float)MAX_STREAMS));
		}
		numStreams = clamp(numStreams, 1, MAX_STREAMS);

		float shape = params[PARAMSHAPE_PARAM].getValue();
		if (inputs[INSHAPE_INPUT].isConnected()) {
			shape = clamp(shape + inputs[INSHAPE_INPUT].getVoltage() / 5.f, 0.f, 1.f);
		}

		float range = params[PARAMRANGE_PARAM].getValue();
		if (inputs[INRANGE_INPUT].isConnected()) {
			range = clamp(range + inputs[INRANGE_INPUT].getVoltage() * 100.f, 0.f, 500.f);
		}

		float duration = params[PARAMDURATION_PARAM].getValue() / 1000.f; // Convert ms to seconds
		if (inputs[INDURATION_INPUT].isConnected()) {
			duration = clamp((params[PARAMDURATION_PARAM].getValue() +
				inputs[INDURATION_INPUT].getVoltage() * 20.f) / 1000.f, 0.001f, 0.1f);
		}

		float density = params[PARAMDENSITY_PARAM].getValue();
		if (inputs[INDENSITY_INPUT].isConnected()) {
			density = clamp(params[PARAMDENSITY_PARAM].getValue() +
				inputs[INDENSITY_INPUT].getVoltage() * 200.f, 1.f, 1000.f);
		}

		// Always use Density as primary timing control (grains/sec -> seconds between grains)
		float delay = 1.f / density;

		// Delay parameter adds additional spacing (when > 0.1ms)
		float delayOffset = params[PARAMDELAY_PARAM].getValue() / 1000.f; // Convert ms to seconds
		if (inputs[INDELAY_INPUT].isConnected()) {
			delayOffset = clamp((params[PARAMDELAY_PARAM].getValue() +
				inputs[INDELAY_INPUT].getVoltage() * 40.f) / 1000.f, 0.0001f, 0.2f);
		}

		// Add delay offset if specified (allows manual override when Delay knob is turned up)
		if (delayOffset > 0.0002f) {  // Greater than 0.2ms = using Delay control
			delay = delayOffset;
		}

		float variation = params[PARAMVARIATION_PARAM].getValue();
		if (inputs[INVARIATION_INPUT].isConnected()) {
			variation = clamp(variation + inputs[INVARIATION_INPUT].getVoltage() / 5.f, 0.f, 1.f);
		}

		float spread = params[PARAMSPREAD_PARAM].getValue();
		if (inputs[INSPREAD_INPUT].isConnected()) {
			spread = clamp(spread + inputs[INSPREAD_INPUT].getVoltage() / 5.f, 0.f, 1.f);
		}

		// Read VCA input (0-5V = 0-1 gain, linear VCA)
		float vcaGain = 1.f;
		if (inputs[INVCA_INPUT].isConnected()) {
			vcaGain = clamp(inputs[INVCA_INPUT].getVoltage() / 5.f, 0.f, 1.f);
		}

		// Initialize output accumulators
		float leftOut = 0.f;
		float rightOut = 0.f;
		int activeGrainCount = 0;

		// Process each active stream
		for (int s = 0; s < numStreams; s++) {
			Stream& stream = streams[s];

			// Decrement grain timer
			stream.nextGrainTime -= args.sampleTime;

			// Trigger new grain if timer expired
			if (stream.nextGrainTime <= 0.f) {
				// Find available grain slot
				for (int g = 0; g < GRAINS_PER_STREAM; g++) {
					if (!stream.grains[g].active) {
						// Calculate grain frequency with variation
						// Range defines frequency bandwidth, Variation controls randomness amount
						float grainFreq = centerFreq;
						if (variation > 0.01f && range > 0.f) {
							// Use linear variation for Range (for predictable control)
							// But square variation for tighter control at very low values
							float variationScale = (variation < 0.3f) ? variation * variation / 0.3f : variation;
							float freqOffset = (random::uniform() - 0.5f) * 2.f * range * variationScale;
							grainFreq += freqOffset;
						}
						grainFreq = clamp(grainFreq, 20.f, 20000.f);

						// Calculate grain duration with variation
						// Quasi-synchronous mode benefits from consistent grain duration
						float grainDur = duration;
						if (variation > 0.01f) {
							// Reduced duration variation for better quasi-synchronous behavior
							float variationScale = variation * variation;
							float durVariation = (random::uniform() - 0.5f) * 2.f * variationScale * 0.3f;
							grainDur *= (1.f + durVariation);
						}
						grainDur = clamp(grainDur, 0.001f, 0.2f);

						// Calculate pan position with spread
						// Each grain gets random pan position across the stereo field
						float panPos = 0.5f; // Center by default
						if (spread > 0.01f) {
							// Random pan position with dramatic stereo spread
							// Push distribution toward extremes (hard left/right) at high spread values
							float randomPan = random::uniform(); // 0 to 1

							// Apply square root curve to bias toward extremes
							// Normalize to -1 to 1, apply sqrt, scale back
							float offset = randomPan - 0.5f; // -0.5 to 0.5
							float sign = (offset >= 0.f) ? 1.f : -1.f;
							float normalized = std::abs(offset) * 2.f; // 0 to 1
							float pushed = std::sqrt(normalized) * 0.5f * sign; // -0.5 to 0.5, biased toward extremes

							panPos = 0.5f + pushed * spread;
							panPos = clamp(panPos, 0.f, 1.f);
						}

						// Trigger the grain (precomputes pan gains, wave/env increments)
						stream.grains[g].trigger(grainFreq, grainDur, panPos, args.sampleTime);
						break;
					}
				}

				// Schedule next grain with delay and variation
				// Quasi-synchronous mode requires tighter timing control
				float nextDelay = delay;
				if (variation > 0.01f && nextDelay > 0.f) {
					// Use exponential scaling for timing variation too
					float variationScale = variation * variation;
					float delayVariation = (random::uniform() - 0.5f) * 2.f * variationScale;
					nextDelay *= (1.f + delayVariation);
				}
				stream.nextGrainTime = std::max(0.001f, nextDelay);
			}

			// Process all active grains in this stream.
			// Hot loop — every operation here runs ~48,000 times/sec per active grain.
			for (int g = 0; g < GRAINS_PER_STREAM; g++) {
				Grain& grain = stream.grains[g];
				if (!grain.active) continue;

				activeGrainCount++;

				// Generate grain sample (LUT-based sine + cheap waveform morph)
				float grainSample = generateGrainWave(grain.wavePhase, shape);

				// Apply envelope (LUT-based Hann)
				grainSample *= hannWindow(grain.envelopePhase);

				// Use precomputed pan gains (no per-sample sqrt)
				leftOut += grainSample * grain.leftGain;
				rightOut += grainSample * grain.rightGain;

				// Advance waveform phase using precomputed increment
				grain.wavePhase += grain.waveIncrement;
				if (grain.wavePhase >= 1.f) grain.wavePhase -= (int)grain.wavePhase;

				// Advance envelope phase using precomputed increment
				grain.envelopePhase += grain.envelopeIncrement;

				// Check if grain is finished based on envelope
				if (grain.envelopePhase >= 1.f) {
					grain.reset();
				}
			}
		}

		// Output with intelligent gain scaling based on active grain count
		// More grains = lower gain to prevent clipping
		// Fewer grains = higher gain to maintain presence
		float gain = 1.0f;
		if (activeGrainCount > 0) {
			// Scale from 1.0 (1 grain) to 0.15 (100+ grains) using logarithmic curve
			gain = clamp(1.0f / std::sqrt((float)activeGrainCount * 0.5f), 0.15f, 1.0f);
		}

		// Apply VCA gain to final output
		gain *= vcaGain;

		outputs[OUTLEFT_OUTPUT].setVoltage(clamp(leftOut * gain, -10.f, 10.f));
		outputs[OUTRIGHT_OUTPUT].setVoltage(clamp(rightOut * gain, -10.f, 10.f));
	}
};


struct GsxWidget : ModuleWidget {
	GsxWidget(Gsx* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/gsx.svg")));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.16, 28.79)), module, Gsx::PARAMFREQUENCY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(30.48, 28.79)), module, Gsx::PARAMSTREAMS_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(50.8, 28.79)), module, Gsx::PARAMSHAPE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.16, 59.27)), module, Gsx::PARAMRANGE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(30.48, 59.27)), module, Gsx::PARAMDURATION_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(50.8, 59.27)), module, Gsx::PARAMDELAY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.16, 89.75)), module, Gsx::PARAMDENSITY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(30.48, 89.75)), module, Gsx::PARAMVARIATION_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(50.8, 89.75)), module, Gsx::PARAMSPREAD_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.16, 41.49)), module, Gsx::INFREQUENCY_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.48, 41.49)), module, Gsx::INSTREAMS_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(50.8, 41.49)), module, Gsx::INSHAPE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.16, 71.97)), module, Gsx::INRANGE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.48, 71.97)), module, Gsx::INDURATION_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(50.8, 71.97)), module, Gsx::INDELAY_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.16, 102.45)), module, Gsx::INDENSITY_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.48, 102.45)), module, Gsx::INVARIATION_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(50.8, 102.45)), module, Gsx::INSPREAD_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.16, 120.23)), module, Gsx::INVCA_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(40.64, 120.23)), module, Gsx::OUTLEFT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(50.8, 120.23)), module, Gsx::OUTRIGHT_OUTPUT));
	}
};


Model* modelGsx = createModel<Gsx, GsxWidget>("gsx");