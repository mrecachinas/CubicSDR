#include <SoapySDR/Device.hpp>
#include <SoapySDR/Registry.hpp>
#include <SoapySDR/Formats.hpp>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <random>

class SoapyTestSource : public SoapySDR::Device {
public:
    SoapyTestSource(const SoapySDR::Kwargs &args) {
        if (args.count("tone_freq")) toneFreq_ = std::stod(args.at("tone_freq"));
        if (args.count("sample_rate")) sampleRate_ = std::stod(args.at("sample_rate"));
        if (args.count("noise_floor")) noiseFloor_ = std::stod(args.at("noise_floor"));
    }

    // Identification
    std::string getDriverKey() const override { return "testsource"; }
    std::string getHardwareKey() const override { return "TestSource"; }

    // Channels
    size_t getNumChannels(const int) const override { return 1; }
    bool getFullDuplex(const int, const size_t) const override { return false; }

    // Stream formats
    std::vector<std::string> getStreamFormats(const int, const size_t) const override {
        return { SOAPY_SDR_CF32 };
    }
    std::string getNativeStreamFormat(const int, const size_t, double &fullScale) const override {
        fullScale = 1.0;
        return SOAPY_SDR_CF32;
    }

    // Sample rate
    void setSampleRate(const int, const size_t, const double rate) override { sampleRate_ = rate; }
    double getSampleRate(const int, const size_t) const override { return sampleRate_; }
    std::vector<double> listSampleRates(const int, const size_t) const override {
        return { 1e6, 2e6, 2.5e6, 3.2e6, 5e6, 10e6 };
    }
    SoapySDR::RangeList getSampleRateRange(const int, const size_t) const override {
        return { SoapySDR::Range(1e6, 20e6) };
    }

    // Frequency
    void setFrequency(const int, const size_t, const double freq, const SoapySDR::Kwargs &) override {
        centerFreq_ = freq;
    }
    double getFrequency(const int, const size_t) const override { return centerFreq_; }
    std::vector<std::string> listFrequencies(const int, const size_t) const override { return { "RF" }; }
    SoapySDR::RangeList getFrequencyRange(const int, const size_t, const std::string &) const override {
        return { SoapySDR::Range(1e6, 6e9) };
    }

    // Gain
    void setGain(const int, const size_t, const double gain) override { gain_ = gain; }
    double getGain(const int, const size_t) const override { return gain_; }
    SoapySDR::Range getGainRange(const int, const size_t) const override {
        return SoapySDR::Range(0.0, 50.0);
    }

    // Bandwidth
    void setBandwidth(const int, const size_t, const double bw) override { bandwidth_ = bw; }
    double getBandwidth(const int, const size_t) const override { return bandwidth_; }
    SoapySDR::RangeList getBandwidthRange(const int, const size_t) const override {
        return { SoapySDR::Range(1e6, 20e6) };
    }

    // Antenna
    std::vector<std::string> listAntennas(const int, const size_t) const override { return { "RX" }; }
    void setAntenna(const int, const size_t, const std::string &) override {}
    std::string getAntenna(const int, const size_t) const override { return "RX"; }

    // Streaming
    SoapySDR::Stream *setupStream(const int direction, const std::string &format,
                                   const std::vector<size_t> &, const SoapySDR::Kwargs &) override {
        if (direction != SOAPY_SDR_RX) throw std::runtime_error("TX not supported");
        if (format != SOAPY_SDR_CF32) throw std::runtime_error("Only CF32 supported");
        return reinterpret_cast<SoapySDR::Stream *>(this);
    }
    void closeStream(SoapySDR::Stream *) override {}
    int activateStream(SoapySDR::Stream *, const int, const long long, const size_t) override {
        phase_ = 0.0;
        return 0;
    }
    int deactivateStream(SoapySDR::Stream *, const int, const long long) override { return 0; }

    int readStream(SoapySDR::Stream *, void * const *buffs, const size_t numElems,
                   int &flags, long long &timeNs, const long timeoutUs) override {

        // Pace output to match sample rate
        auto now = std::chrono::high_resolution_clock::now();
        if (lastRead_.time_since_epoch().count() != 0) {
            double elapsedUs = std::chrono::duration<double, std::micro>(now - lastRead_).count();
            double expectedUs = (numElems / sampleRate_) * 1e6;
            if (elapsedUs < expectedUs) {
                std::this_thread::sleep_for(
                    std::chrono::microseconds(static_cast<long long>(expectedUs - elapsedUs)));
            }
        }
        lastRead_ = std::chrono::high_resolution_clock::now();

        auto *out = static_cast<float *>(buffs[0]);
        double phaseInc = 2.0 * M_PI * toneFreq_ / sampleRate_;
        double amplitude = std::pow(10.0, gain_ / 20.0) * 0.5;

        std::uniform_real_distribution<float> noiseDist(-noiseFloor_, noiseFloor_);

        for (size_t i = 0; i < numElems; i++) {
            out[2 * i]     = static_cast<float>(amplitude * std::cos(phase_)) + noiseDist(rng_);
            out[2 * i + 1] = static_cast<float>(amplitude * std::sin(phase_)) + noiseDist(rng_);
            phase_ += phaseInc;
        }
        // Keep phase bounded
        if (phase_ > 2.0 * M_PI * 1e6) phase_ -= 2.0 * M_PI * 1e6;

        flags = 0;
        timeNs = 0;
        return static_cast<int>(numElems);
    }

    size_t getStreamMTU(SoapySDR::Stream *) const override { return 16384; }

private:
    double sampleRate_ = 2.5e6;
    double centerFreq_ = 100e6;
    double toneFreq_ = 100e3;   // Tone offset from center (Hz)
    double gain_ = 20.0;
    double bandwidth_ = 2.5e6;
    double noiseFloor_ = 0.01f;
    double phase_ = 0.0;
    std::mt19937 rng_{42};
    std::chrono::high_resolution_clock::time_point lastRead_{};
};

// Registration
static SoapySDR::KwargsList findTestSource(const SoapySDR::Kwargs &args) {
    SoapySDR::KwargsList results;
    SoapySDR::Kwargs devInfo;
    devInfo["driver"] = "testsource";
    devInfo["label"] = "Test Signal Source (Simulated)";
    devInfo["tone_freq"] = "100000";
    devInfo["noise_floor"] = "0.01";
    results.push_back(devInfo);
    return results;
}

static SoapySDR::Device *makeTestSource(const SoapySDR::Kwargs &args) {
    return new SoapyTestSource(args);
}

static SoapySDR::Registry registerTestSource("testsource", &findTestSource, &makeTestSource, SOAPY_SDR_ABI_VERSION);
