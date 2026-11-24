#pragma once

// IPDE audio: a small software mixer. On Windows it plays through waveOut (winmm);
// elsewhere it is silent (or writes to a WAV file when IPDE_AUDIO_DUMP is set, for tests).

#include <memory>
#include <string>
#include <vector>

namespace ipde::audio {

constexpr int SAMPLE_RATE = 44100;

// Mono samples in the range -1..1.
using Sound = std::shared_ptr<const std::vector<float>>;

bool init();
void shutdown();
// Starts playing a sound (several sounds can play at the same time).
void play(const Sound &p_sound, float p_volume = 1.0f, float p_pitch = 1.0f, bool p_loop = false);
void stop_all();
void set_master_volume(float p_volume);

// Loads a WAV file (PCM 8/16 bit or float 32, any channel count; it is mixed to mono and resampled).
Sound load_wav(const std::string &p_path);

// Built-in synthesized effects.
namespace sfx {
Sound coin();
Sound jump();
Sound hurt();
Sound goal();
Sound bounce();
Sound click();
} // namespace sfx

} // namespace ipde::audio
