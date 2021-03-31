/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
WhooshGeneratorAudioProcessor::WhooshGeneratorAudioProcessor(): out_state_(
	                                                                std::make_unique<AudioProcessorValueTreeState>(
		                                                                *this, nullptr, "OUT PARAMETERS",
		                                                                create_out_parameters())), in_state_(
	                                                                std::make_unique<AudioProcessorValueTreeState>(
		                                                                *this, nullptr, "IN PARAMETERS",
		                                                                create_in_parameters()))
#ifndef JucePlugin_PreferredChannelConfigurations
                                                                , AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
	                                                                .withInput("Input", juce::AudioChannelSet::stereo(),
	                                                                           true)
#endif
	                                                                .withOutput(
		                                                                "Output", juce::AudioChannelSet::stereo(), true)
	                                                                // .withInput("Sidechain",
	                                                                //            juce::AudioChannelSet::stereo())
#endif
                                                                )
#endif
{
}

WhooshGeneratorAudioProcessor::~WhooshGeneratorAudioProcessor()
{
}

//==============================================================================
const juce::String WhooshGeneratorAudioProcessor::getName() const
{
	return JucePlugin_Name;
}

bool WhooshGeneratorAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
	return false;
#endif
}

bool WhooshGeneratorAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
	return false;
#endif
}

bool WhooshGeneratorAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
	return false;
#endif
}

double WhooshGeneratorAudioProcessor::getTailLengthSeconds() const
{
	return 0.0;
}

int WhooshGeneratorAudioProcessor::getNumPrograms()
{
	return 1; // NB: some hosts don't cope very well if you tell them there are 0 programs,
	// so this should be at least 1, even if you're not really implementing programs.
}

int WhooshGeneratorAudioProcessor::getCurrentProgram()
{
	return 0;
}

void WhooshGeneratorAudioProcessor::setCurrentProgram(int index)
{
}

const juce::String WhooshGeneratorAudioProcessor::getProgramName(int index)
{
	return {};
}

void WhooshGeneratorAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
}

//==============================================================================
void WhooshGeneratorAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
	sample_rate = sampleRate;
	audioSource.prepareToPlay(samplesPerBlock, sampleRate);
	for (std::list<fx_chain_element*>::value_type element : fx_chain)
	{
		element->prepareToPlay(sampleRate, samplesPerBlock);
	}
}

void WhooshGeneratorAudioProcessor::releaseResources()
{
	audioSource.releaseResources();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool WhooshGeneratorAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
#else
	// This is the place where you check if the layout is supported.
	// In this template code we only support mono or stereo.
	// Some plugin hosts, such as certain GarageBand versions, will only
	// load plugins that support stereo bus layouts.
	if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
		&& layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
		return false;

	// This checks if the input layout matches the output layout
#if ! JucePlugin_IsSynth
	if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
		return false;
#endif

	return true;
#endif
}

#endif


void WhooshGeneratorAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
	ScopedNoDenormals noDenormals;

	const int total_num_input_channels = getTotalNumInputChannels();
	const int total_num_output_channels = getTotalNumOutputChannels();

	for (int channel = total_num_input_channels;
	     channel < total_num_output_channels;
	     ++channel)
	{
		buffer.clear(channel, 0, buffer.getNumSamples());
	}

	const int input_buses_count = getBusCount(true);

	AudioBuffer<float> mainInputOutput = getBusBuffer(buffer, true, 0); 
	// AudioBuffer<float> sideChainInput = getBusBuffer(buffer, true, 1);


	auto selectedBuffer = mainInputOutput;
	auto bufferToFill = AudioSourceChannelInfo(selectedBuffer);

	for (auto channel = 0; channel < total_num_output_channels; ++channel)
	{
		const auto actual_input_channel = 0;
		const auto* inputBuffer = bufferToFill.buffer->getReadPointer(
			actual_input_channel, bufferToFill.startSample);
		auto* outputBuffer = bufferToFill.buffer->getWritePointer(channel, bufferToFill.startSample);


		for (auto sample = 0; sample < bufferToFill.numSamples; ++sample)
		{
			//Volume
			samples_squares_sum += inputBuffer[sample] * inputBuffer[sample];

			(last_rms_value >= threshold_value)
				? outputBuffer[sample] = inputBuffer[sample]
				: outputBuffer[sample] = 0;
		}
	}

	if (block_index >= rms_blocks_length)
	{
		temp_previous_value = last_rms_value;

		new_rms_value = sqrt(samples_squares_sum / bufferToFill.numSamples);

		new_rms_value = (last_rms_value < threshold_value) ? 0. : new_rms_value;

		samples_squares_sum = 0.0;
		block_index = 0;

		const float variation = (new_rms_value - last_rms_value) * variation_speed;
		last_rms_value = last_rms_value + variation;
	}
	else
	{
		block_index++;
	}

	sample_index += bufferToFill.buffer->getNumSamples();
	for (std::list<fx_chain_element>::value_type* element : fx_chain)
	{
		element->getNextAudioBlock(bufferToFill);
	}
	audioSource.getNextAudioBlock(bufferToFill);
}

//==============================================================================
bool WhooshGeneratorAudioProcessor::hasEditor() const
{
	return true; // (change this to false if you choose to not supply an editor)
}

void WhooshGeneratorAudioProcessor::add_element_to_fx_chain(fx_chain_element* element)
{
	fx_chain.push_back(element);
	element->prepareToPlay(sample_rate, getBlockSize());
}

void WhooshGeneratorAudioProcessor::remove_element_to_fx_chain(fx_chain_element* element)
{
	fx_chain.erase(std::find(fx_chain.begin(), fx_chain.end(), element));
}

juce::AudioProcessorEditor* WhooshGeneratorAudioProcessor::createEditor()
{
	return new WhooshGeneratorAudioProcessorEditor(*this);
}

//==============================================================================
void WhooshGeneratorAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
	// You should use this method to store your parameters in the memory block.
	// You could do that either as raw data, or use the XML or ValueTree classes
	// as intermediaries to make it easy to save and load complex data.
}

void WhooshGeneratorAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
	// You should use this method to restore your parameters from this memory block,
	// whose contents will have been created by the getStateInformation() call.
}

my_audio_source& WhooshGeneratorAudioProcessor::getAudioSource()
{
	return audioSource;
}

float WhooshGeneratorAudioProcessor::get_last_rms_value_in_db()
{
	return Decibels::gainToDecibels(last_rms_value);
}


AudioProcessorValueTreeState* WhooshGeneratorAudioProcessor::get_out_state()
{
	return out_state_.get();
}

AudioProcessorValueTreeState* WhooshGeneratorAudioProcessor::get_in_state()
{
	return in_state_.get();
}


//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new WhooshGeneratorAudioProcessor();
}

AudioProcessorValueTreeState::ParameterLayout WhooshGeneratorAudioProcessor::create_out_parameters() const
{
	std::vector<std::unique_ptr<RangedAudioParameter>> parameters;

	NormalisableRange<float> frequency_range = util::log_range(50, 20000);

	parameters.push_back(std::make_unique<AudioParameterFloat>("volume", "VOLUME", 0.0f, 1.0f, 0.01f));
	parameters.push_back(std::make_unique<AudioParameterFloat>("frequency", "FREQUENCY", frequency_range, 0.,
	                                                           "FREQUENCY", AudioProcessorParameter::genericParameter));


	return {parameters.begin(), parameters.end()};
}

AudioProcessorValueTreeState::ParameterLayout WhooshGeneratorAudioProcessor::create_in_parameters() const
{
	std::vector<std::unique_ptr<RangedAudioParameter>> parameters;

	NormalisableRange<float> frequency_range = util::log_range(1,(float)SpectrumAnalyserComponent::fft_size/2);

	parameters.push_back(std::make_unique<AudioParameterFloat>("threshold", "THRESHOLD", 0.0f, .5f, 0.001f));
	parameters.push_back(std::make_unique<AudioParameterFloat>("rms_length", "RMS LENGTH", 0.0f, 10.0f, 0.01f));
	parameters.push_back(std::make_unique<AudioParameterFloat>("min_frequency", "MIN FREQUENCY", frequency_range, 0.,
	                                                           "MIN FREQUENCY", AudioProcessorParameter::genericParameter));
	parameters.push_back(std::make_unique<AudioParameterFloat>("max_frequency", "MAX FREQUENCY", frequency_range, SpectrumAnalyserComponent::fft_size,
	                                                           "MAX FREQUENCY", AudioProcessorParameter::genericParameter));
	parameters.push_back(std::make_unique<AudioParameterFloat>("fft_speed", "FFT SPEED", 0.0f, 1.0f, 0.01f));
	parameters.push_back(std::make_unique<AudioParameterFloat>("volume_speed", "VOLUME SPEED", 0.0f, 1.0f, 0.01f));


	return {parameters.begin(), parameters.end()};
}
