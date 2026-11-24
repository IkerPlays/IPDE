#include "core/audio.h"

#include "core/fs.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#else
#include <chrono>
#endif

namespace ipde::audio {

namespace {

struct Voice {
	Sound sound;
	double position = 0.0;
	float volume = 1.0f;
	float pitch = 1.0f;
	bool loop = false;
};

std::mutex g_mutex;
std::vector<Voice> g_voices;
std::atomic<float> g_master{ 0.8f };
std::atomic<bool> g_running{ false };
std::thread *g_thread = nullptr; // Never destroyed while running, so exit() can't abort.

// Mixes the active voices into p_out (p_count mono samples).
void mix(float *p_out, int p_count) {
	std::fill(p_out, p_out + p_count, 0.0f);
	std::lock_guard<std::mutex> lock(g_mutex);
	for (Voice &v : g_voices) {
		const std::vector<float> &data = *v.sound;
		const double len = (double)data.size();
		for (int i = 0; i < p_count && v.position < len - 1; i++) {
			const size_t i0 = (size_t)v.position;
			const float f = (float)(v.position - (double)i0);
			p_out[i] += (data[i0] * (1.0f - f) + data[i0 + 1] * f) * v.volume;
			v.position += v.pitch;
			if (v.loop && v.position >= len - 1) {
				v.position -= len - 1;
			}
		}
	}
	g_voices.erase(std::remove_if(g_voices.begin(), g_voices.end(), [](const Voice &v) { return v.position >= (double)v.sound->size() - 1; }), g_voices.end());
	const float master = g_master.load();
	for (int i = 0; i < p_count; i++) {
		// Soft clipping keeps loud mixes from cracking.
		const float x = p_out[i] * master;
		p_out[i] = x / (1.0f + std::fabs(x) * 0.25f);
	}
}

void to_pcm16(const float *p_in, int16_t *r_out, int p_count) {
	for (int i = 0; i < p_count; i++) {
		r_out[i] = (int16_t)std::lround(std::clamp(p_in[i], -1.0f, 1.0f) * 32767.0f);
	}
}

#ifdef _WIN32

constexpr int BUFFER_SAMPLES = 1024;
constexpr int BUFFER_COUNT = 4;

void audio_thread() {
	HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	WAVEFORMATEX fmt = {};
	fmt.wFormatTag = WAVE_FORMAT_PCM;
	fmt.nChannels = 1;
	fmt.nSamplesPerSec = SAMPLE_RATE;
	fmt.wBitsPerSample = 16;
	fmt.nBlockAlign = 2;
	fmt.nAvgBytesPerSec = SAMPLE_RATE * 2;
	HWAVEOUT device = nullptr;
	if (waveOutOpen(&device, WAVE_MAPPER, &fmt, (DWORD_PTR)event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
		CloseHandle(event);
		g_running = false;
		return;
	}
	std::vector<int16_t> memory((size_t)BUFFER_SAMPLES * BUFFER_COUNT);
	std::vector<float> scratch(BUFFER_SAMPLES);
	WAVEHDR headers[BUFFER_COUNT] = {};
	for (int i = 0; i < BUFFER_COUNT; i++) {
		headers[i].lpData = (LPSTR)(memory.data() + (size_t)i * BUFFER_SAMPLES);
		headers[i].dwBufferLength = BUFFER_SAMPLES * 2;
		waveOutPrepareHeader(device, &headers[i], sizeof(WAVEHDR));
		headers[i].dwFlags |= WHDR_DONE; // Ready to be filled.
	}
	while (g_running) {
		for (int i = 0; i < BUFFER_COUNT; i++) {
			if (headers[i].dwFlags & WHDR_DONE) {
				mix(scratch.data(), BUFFER_SAMPLES);
				to_pcm16(scratch.data(), (int16_t *)headers[i].lpData, BUFFER_SAMPLES);
				headers[i].dwFlags &= ~WHDR_DONE;
				waveOutWrite(device, &headers[i], sizeof(WAVEHDR));
			}
		}
		WaitForSingleObject(event, 50);
	}
	waveOutReset(device);
	for (int i = 0; i < BUFFER_COUNT; i++) {
		waveOutUnprepareHeader(device, &headers[i], sizeof(WAVEHDR));
	}
	waveOutClose(device);
	CloseHandle(event);
}

#else

std::string g_dump_path;
std::vector<int16_t> g_dump;

// No audio device: optionally record the mix to a WAV file (for tests).
void audio_thread() {
	const int chunk = SAMPLE_RATE / 50;
	std::vector<float> scratch(chunk);
	std::vector<int16_t> pcm(chunk);
	while (g_running) {
		mix(scratch.data(), chunk);
		if (!g_dump_path.empty()) {
			to_pcm16(scratch.data(), pcm.data(), chunk);
			g_dump.insert(g_dump.end(), pcm.begin(), pcm.end());
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}

void write_dump() {
	if (g_dump_path.empty()) {
		return;
	}
	std::string out = "RIFF";
	auto u32 = [&](uint32_t v) { out.append((const char *)&v, 4); };
	auto u16 = [&](uint16_t v) { out.append((const char *)&v, 2); };
	const uint32_t bytes = (uint32_t)(g_dump.size() * 2);
	u32(36 + bytes);
	out += "WAVEfmt ";
	u32(16);
	u16(1);
	u16(1);
	u32(SAMPLE_RATE);
	u32(SAMPLE_RATE * 2);
	u16(2);
	u16(16);
	out += "data";
	u32(bytes);
	out.append((const char *)g_dump.data(), bytes);
	fs::write_file(g_dump_path, out);
}

#endif

} // namespace

bool init() {
	if (g_thread) {
		return g_running.load();
	}
#ifndef _WIN32
	const char *dump = getenv("IPDE_AUDIO_DUMP");
	g_dump_path = dump ? dump : "";
#endif
	g_running = true;
	g_thread = new std::thread(audio_thread);
	return true;
}

void shutdown() {
	if (!g_thread) {
		return;
	}
	g_running = false;
	g_thread->join();
	delete g_thread;
	g_thread = nullptr;
#ifndef _WIN32
	write_dump();
#endif
	std::lock_guard<std::mutex> lock(g_mutex);
	g_voices.clear();
}

void play(const Sound &p_sound, float p_volume, float p_pitch, bool p_loop) {
	if (!p_sound || p_sound->size() < 2 || !g_running) {
		return;
	}
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_voices.size() >= 32) {
		auto it = std::find_if(g_voices.begin(), g_voices.end(), [](const Voice &o) { return !o.loop; });
		if (it != g_voices.end()) {
			g_voices.erase(it);
		}
	}
	Voice v;
	v.sound = p_sound;
	v.volume = p_volume;
	v.pitch = std::max(0.05f, p_pitch);
	v.loop = p_loop;
	g_voices.push_back(v);
}

void stop_all() {
	std::lock_guard<std::mutex> lock(g_mutex);
	g_voices.clear();
}

void set_master_volume(float p_volume) {
	g_master = std::clamp(p_volume, 0.0f, 1.0f);
}

/* WAV loading */

Sound load_wav(const std::string &p_path) {
	std::string data;
	if (!fs::read_file(p_path, data) || data.size() < 12 || data.compare(0, 4, "RIFF") != 0 || data.compare(8, 4, "WAVE") != 0) {
		return nullptr;
	}
	auto rd16 = [&](size_t o) { uint16_t v; memcpy(&v, data.data() + o, 2); return v; };
	auto rd32 = [&](size_t o) { uint32_t v; memcpy(&v, data.data() + o, 4); return v; };
	int format = 0, channels = 0, bits = 0;
	uint32_t rate = 0;
	size_t pcm_offset = 0, pcm_size = 0;
	size_t pos = 12;
	while (pos + 8 <= data.size()) {
		const std::string id = data.substr(pos, 4);
		const uint32_t size = rd32(pos + 4);
		const size_t body = pos + 8;
		if (id == "fmt " && body + 16 <= data.size()) {
			format = rd16(body);
			channels = rd16(body + 2);
			rate = rd32(body + 4);
			bits = rd16(body + 14);
			if (format == 0xFFFE && size >= 26 && body + 26 <= data.size()) {
				format = rd16(body + 24); // WAVE_FORMAT_EXTENSIBLE: real format in the sub-format GUID.
			}
		} else if (id == "data") {
			pcm_offset = body;
			pcm_size = std::min<size_t>(size, data.size() - body);
		}
		pos = body + size + (size & 1);
	}
	if (channels <= 0 || rate == 0 || pcm_offset == 0) {
		return nullptr;
	}
	const int bytes_per_sample = bits / 8;
	if (!((format == 1 && (bits == 8 || bits == 16 || bits == 24)) || (format == 3 && bits == 32))) {
		return nullptr;
	}
	const size_t frames = pcm_size / ((size_t)bytes_per_sample * channels);
	std::vector<float> mono(frames);
	const unsigned char *p = (const unsigned char *)data.data() + pcm_offset;
	for (size_t f = 0; f < frames; f++) {
		float sum = 0.0f;
		for (int c = 0; c < channels; c++) {
			const unsigned char *s = p + (f * channels + c) * bytes_per_sample;
			float v = 0.0f;
			if (format == 3) {
				memcpy(&v, s, 4);
			} else if (bits == 8) {
				v = (s[0] - 128) / 128.0f;
			} else if (bits == 16) {
				int16_t i16;
				memcpy(&i16, s, 2);
				v = i16 / 32768.0f;
			} else {
				const int32_t i24 = (int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24) >> 8;
				v = i24 / 8388608.0f;
			}
			sum += v;
		}
		mono[f] = sum / (float)channels;
	}
	if (rate == (uint32_t)SAMPLE_RATE) {
		return std::make_shared<const std::vector<float>>(std::move(mono));
	}
	// Linear resampling to the mixer rate.
	const double ratio = (double)rate / SAMPLE_RATE;
	const size_t out_count = (size_t)((double)frames / ratio);
	std::vector<float> out(out_count);
	for (size_t i = 0; i < out_count; i++) {
		const double src = i * ratio;
		const size_t i0 = std::min((size_t)src, frames - 1);
		const size_t i1 = std::min(i0 + 1, frames - 1);
		const float t = (float)(src - (double)i0);
		out[i] = mono[i0] * (1.0f - t) + mono[i1] * t;
	}
	return std::make_shared<const std::vector<float>>(std::move(out));
}

/* Synthesized effects */

namespace sfx {

namespace {

constexpr double TAU = 6.283185307179586;

float square(double p_phase) {
	return std::fmod(p_phase, 1.0) < 0.5 ? 1.0f : -1.0f;
}

float triangle(double p_phase) {
	const double f = std::fmod(p_phase, 1.0);
	return (float)(f < 0.5 ? f * 4.0 - 1.0 : 3.0 - f * 4.0);
}

// Short fade in/out so effects never click.
float edge(size_t p_i, size_t p_n) {
	const float a = std::min(1.0f, p_i / 80.0f);
	const float b = std::min(1.0f, (p_n - p_i) / 200.0f);
	return a * b;
}

Sound make(std::vector<float> &&p_samples) {
	return std::make_shared<const std::vector<float>>(std::move(p_samples));
}

} // namespace

Sound coin() {
	static Sound s = [] {
		const size_t n = (size_t)(SAMPLE_RATE * 0.32);
		std::vector<float> v(n);
		double phase = 0;
		for (size_t i = 0; i < n; i++) {
			const double t = (double)i / SAMPLE_RATE;
			const double f = t < 0.07 ? 988.0 : 1319.0;
			phase += f / SAMPLE_RATE;
			const float env = t < 0.07 ? 1.0f : (float)std::exp(-(t - 0.07) * 12.0);
			v[i] = square(phase) * 0.22f * env * edge(i, n);
		}
		return make(std::move(v));
	}();
	return s;
}

Sound jump() {
	static Sound s = [] {
		const size_t n = (size_t)(SAMPLE_RATE * 0.2);
		std::vector<float> v(n);
		double phase = 0;
		for (size_t i = 0; i < n; i++) {
			const double t = (double)i / n;
			phase += (280.0 + 520.0 * t * t) / SAMPLE_RATE;
			v[i] = square(phase) * 0.16f * (float)(1.0 - t * 0.7) * edge(i, n);
		}
		return make(std::move(v));
	}();
	return s;
}

Sound hurt() {
	static Sound s = [] {
		const size_t n = (size_t)(SAMPLE_RATE * 0.4);
		std::vector<float> v(n);
		double phase = 0;
		uint32_t seed = 12345;
		for (size_t i = 0; i < n; i++) {
			const double t = (double)i / n;
			phase += (420.0 * (1.0 - t) + 60.0) / SAMPLE_RATE;
			seed = seed * 1664525u + 1013904223u;
			const float noise = ((seed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
			v[i] = (square(phase) * 0.6f + noise * 0.4f) * 0.25f * (float)(1.0 - t) * edge(i, n);
		}
		return make(std::move(v));
	}();
	return s;
}

Sound goal() {
	static Sound s = [] {
		const double notes[] = { 523.25, 659.25, 783.99, 1046.5 };
		const double step = 0.11;
		const size_t n = (size_t)(SAMPLE_RATE * (step * 3 + 0.7));
		std::vector<float> v(n);
		double phase = 0;
		for (size_t i = 0; i < n; i++) {
			const double t = (double)i / SAMPLE_RATE;
			const int k = std::min(3, (int)(t / step));
			phase += notes[k] / SAMPLE_RATE;
			const double local = t - k * step;
			const float env = k < 3 ? 1.0f : (float)std::exp(-local * 4.0);
			v[i] = (triangle(phase) * 0.7f + square(phase) * 0.15f) * 0.3f * env * edge(i, n);
		}
		return make(std::move(v));
	}();
	return s;
}

Sound bounce() {
	static Sound s = [] {
		const size_t n = (size_t)(SAMPLE_RATE * 0.28);
		std::vector<float> v(n);
		double phase = 0;
		for (size_t i = 0; i < n; i++) {
			const double t = (double)i / n;
			phase += (180.0 + 900.0 * t) / SAMPLE_RATE;
			v[i] = (float)std::sin(phase * TAU) * 0.35f * (float)(1.0 - t) * edge(i, n);
		}
		return make(std::move(v));
	}();
	return s;
}

Sound click() {
	static Sound s = [] {
		const size_t n = (size_t)(SAMPLE_RATE * 0.03);
		std::vector<float> v(n);
		for (size_t i = 0; i < n; i++) {
			const double t = (double)i / SAMPLE_RATE;
			v[i] = (float)std::sin(t * 1800.0 * TAU) * 0.2f * (float)std::exp(-t * 150.0) * edge(i, n);
		}
		return make(std::move(v));
	}();
	return s;
}

} // namespace sfx

} // namespace ipde::audio
