#pragma once
#include <vector>
#include <cstdint>
#include <string>

// Records mono 16kHz float32 PCM from the default input device - the format
// whisper.cpp wants directly, no resampling needed downstream.
class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();

    bool init();

    // Blocks until the player stops talking (silence for `silence_ms`)
    // or `max_record_ms` is hit. Returns the recorded samples.
    std::vector<float> recordUntilSilence(
        int max_record_ms = 15000,
        int silence_ms = 900,
        float silence_rms_threshold = 0.01f
    );

    // Push-to-talk style control: call startRecording() the moment the key
    // goes down, keep polling elsewhere, then call stopRecording() the
    // moment the key comes back up to get the captured samples.
    void startRecording();
    std::vector<float> stopRecording();

    static constexpr int kSampleRate = 16000;

    // Public only so the free-function miniaudio callback in audio_capture.cpp
    // can reference this type. It's still opaque to anyone including this
    // header - the actual definition lives entirely in the .cpp file.
    struct Impl;

private:
    Impl* impl_;
};
