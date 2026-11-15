#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "audio_capture.h"
#include <iostream>
#include <cmath>
#include <chrono>
#include <mutex>
#include <atomic>
#include <thread>

struct AudioCapture::Impl {
    ma_device device{};
    std::vector<float> buffer;
    std::mutex mtx;
    std::atomic<bool> recording{false};
};

// miniaudio calls this on its own audio thread whenever a new chunk of
// samples is ready - we just append to a buffer under a lock.
static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput,
                           ma_uint32 frameCount) {
    (void)pOutput;
    auto* self = reinterpret_cast<AudioCapture::Impl*>(pDevice->pUserData);
    if (!self->recording.load()) return;

    const float* in = reinterpret_cast<const float*>(pInput);
    std::lock_guard<std::mutex> lock(self->mtx);
    self->buffer.insert(self->buffer.end(), in, in + frameCount);
}

AudioCapture::AudioCapture() : impl_(new Impl()) {}
AudioCapture::~AudioCapture() {
    ma_device_uninit(&impl_->device);
    delete impl_;
}

bool AudioCapture::init() {
    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format   = ma_format_f32;
    cfg.capture.channels = 1;
    cfg.sampleRate       = kSampleRate;
    cfg.dataCallback     = data_callback;
    cfg.pUserData        = impl_;

    if (ma_device_init(nullptr, &cfg, &impl_->device) != MA_SUCCESS) {
        std::cerr << "[AudioCapture] Failed to init capture device\n";
        return false;
    }
    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        std::cerr << "[AudioCapture] Failed to start capture device\n";
        return false;
    }
    std::cout << "[AudioCapture] Microphone ready (" << kSampleRate << "Hz mono)\n";
    return true;
}

void AudioCapture::startRecording() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->buffer.clear();
    impl_->recording.store(true);
}

std::vector<float> AudioCapture::stopRecording() {
    impl_->recording.store(false);
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->buffer; // copy out
}

std::vector<float> AudioCapture::recordUntilSilence(
    int max_record_ms, int silence_ms, float silence_rms_threshold) {

    using namespace std::chrono;

    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->buffer.clear();
    }
    impl_->recording.store(true);

    const int chunk_samples = kSampleRate / 20; // check every 50ms
    auto start = steady_clock::now();
    int silence_accum_ms = 0;
    size_t last_checked = 0;
    bool heard_speech = false;

    while (true) {
        std::this_thread::sleep_for(milliseconds(50));

        size_t size_now;
        {
            std::lock_guard<std::mutex> lock(impl_->mtx);
            size_now = impl_->buffer.size();
        }

        // Compute RMS over the newest chunk only
        if (size_now > last_checked) {
            std::lock_guard<std::mutex> lock(impl_->mtx);
            size_t from = last_checked;
            double sumsq = 0.0;
            size_t n = size_now - from;
            for (size_t i = from; i < size_now; ++i) sumsq += impl_->buffer[i] * impl_->buffer[i];
            float rms = n > 0 ? std::sqrt(sumsq / n) : 0.0f;
            last_checked = size_now;

            if (rms > silence_rms_threshold) {
                heard_speech = true;
                silence_accum_ms = 0;
            } else {
                silence_accum_ms += 50;
            }
        } else {
            silence_accum_ms += 50;
        }

        auto elapsed_ms = duration_cast<milliseconds>(steady_clock::now() - start).count();

        // Only end on silence once we've actually heard speech, otherwise
        // an idle mic would "finish" instantly with nothing recorded.
        if ((heard_speech && silence_accum_ms >= silence_ms) || elapsed_ms >= max_record_ms) {
            break;
        }
    }

    impl_->recording.store(false);

    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->buffer; // copy out
}
