#include <cassert>
#ifndef _WIN32
#include <dlfcn.h>
#endif
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "coreinfo.h"
#include "data.h"
#include "emulator.h"
#include "libretro.h"

#include <SDL2/SDL.h>
#include <GL/gl.h>

#ifndef _WIN32
#define GETSYM dlsym
#else
#define GETSYM GetProcAddress
#endif

using namespace std;

namespace Retro {

static Emulator* s_loadedEmulator = nullptr;
static SDL_Window *window = NULL;
static SDL_GLContext gl_context = NULL;
static struct retro_hw_render_callback hw_render;

static map<string, const char*> s_envVariables = {
	{ "genesis_plus_gx_bram", "per game" },
	{ "genesis_plus_gx_render", "single field" },
	{ "genesis_plus_gx_blargg_ntsc_filter", "disabled" },
	{"citra_use_cpu_jit", "enabled"},
	{"citra_cpu_scale", "100%"},
	{"citra_use_shader_jit", "enabled"},
	{"citra_use_hw_shaders", "enabled"},
	{"citra_use_hw_shader_cache", "enabled"},
	{"citra_use_acc_geo_shaders", "enabled"},
	{"citra_use_acc_mul", "Eenabled"},
	{"citra_texture_filter", "Bicubic"},
	{"citra_texture_sampling", "Linear"},
	{"citra_custom_textures", "enabled"},
	{"citra_dump_textures", "enabled"},
	{"citra_resolution_factor","1x"},
	{"citra_layout_option", "Default Top-Bottom Screen"},
	{"citra_swap_screen", "Top"},
	{"citra_swap_screen_mode", "Toggle"},
	{"citra_analog_function","C-Stick"},
	{"citra_deadzone", "15"},
	{"citra_mouse_touchscreen", "enabled"},
	{"citra_touch_touchscreen", "enabled"},
	{"citra_render_touchscreen", "disabled"},
	{"citra_use_virtual_sd", "enabled"},
	{"citra_use_libretro_save_path", "LibRetro Default"},
	{"citra_is_new_3ds", "New 3DS"},
	{"citra_region_value","Auto"},
	{"citra_language", "English"},
	{"citra_use_gdbstub", "disabled"},
};

static void (*retro_init)(void);
static void (*retro_deinit)(void);
static unsigned (*retro_api_version)(void);
static void (*retro_get_system_info)(struct retro_system_info* info);
static void (*retro_get_system_av_info)(struct retro_system_av_info* info);
static void (*retro_reset)(void);
static void (*retro_run)(void);
static size_t (*retro_serialize_size)(void);
static bool (*retro_serialize)(void* data, size_t size);
static bool (*retro_unserialize)(const void* data, size_t size);
static bool (*retro_load_game)(const struct retro_game_info* game);
static void (*retro_unload_game)(void);
static void* (*retro_get_memory_data)(unsigned id);
static size_t (*retro_get_memory_size)(unsigned id);
static void (*retro_cheat_reset)(void);
static void (*retro_cheat_set)(unsigned index, bool enabled, const char* code);
static void (*retro_set_environment)(retro_environment_t);
static void (*retro_set_video_refresh)(retro_video_refresh_t);
static void (*retro_set_audio_sample)(retro_audio_sample_t);
static void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
static void (*retro_set_input_poll)(retro_input_poll_t);
static void (*retro_set_input_state)(retro_input_state_t);

Emulator::Emulator() {
}

Emulator::~Emulator() {
	if (m_corePath) {
		free(m_corePath);
	}
	if (m_romLoaded) {
		unloadRom();
	}
	if (m_coreHandle) {
		unloadCore();
	}
}

bool Emulator::isLoaded() {
	return s_loadedEmulator;
}

bool Emulator::loadRom(const string& romPath) {
	if (m_romLoaded) {
		unloadRom();
	}

	auto core = coreForRom(romPath);
	if (core.size() == 0) {
		return false;
	}

	if (m_coreHandle && m_core != core) {
		unloadCore();
	}
	if (!m_coreHandle) {
		string lib = libForCore(core) + "_libretro.";
#ifdef __APPLE__
		lib += "dylib";
#elif defined(_WIN32)
		lib += "dll";
#else
		lib += "so";
#endif
		if (!loadCore(corePath() + "/" + lib)) {
			return false;
		}
		m_core = core;
	}

	retro_game_info gameInfo;
	ifstream in(romPath, ios::binary | ios::ate);
	if (in.fail()) {
		return false;
	}
	ostringstream out;
	gameInfo.size = in.tellg();
	if (in.fail()) {
		return false;
	}
	char* romData = new char[gameInfo.size];
	gameInfo.path = romPath.c_str();
	gameInfo.data = romData;
	in.seekg(0, ios::beg);
	in.read(romData, gameInfo.size);
	if (in.fail()) {
		delete[] romData;
		return false;
	}
	in.close();

	auto res = retro_load_game(&gameInfo);
	delete[] romData;
	if (!res) {
		return false;
	}
	retro_get_system_av_info(&m_avInfo);
	fixScreenSize(romPath);

	m_romLoaded = true;
	m_romPath = romPath;
	return true;
}

void Emulator::run() {
	assert(s_loadedEmulator == this);
	m_audioData.clear();

	// SDL_Event e;
    //     while (SDL_PollEvent(&e)) {
    //         if (e.type == SDL_QUIT)
    //             goto exit_loop;
	// }

	retro_run();

	SDL_Delay(16);

	// exit_loop: {
	// 	if (hw_render.context_destroy)
	// 		hw_render.context_destroy();
	
	// 	SDL_GL_DeleteContext(gl_context);
	// 	SDL_DestroyWindow(window);
	// 	SDL_Quit();
	// 	return;
	// }
}

void Emulator::reset() {
	assert(s_loadedEmulator == this);

	memset(m_buttonMask, 0, sizeof(m_buttonMask));

	retro_system_info systemInfo;
	retro_get_system_info(&systemInfo);
	if (!strcmp(systemInfo.library_name, "Stella")) {
		// Stella does not properly clear everything when reseting or loading a savestate
		string romPath = m_romPath;

#ifdef _WIN32
		FreeLibrary(m_coreHandle);
#else
		dlclose(m_coreHandle);
#endif
		m_coreHandle = nullptr;
		s_loadedEmulator = nullptr;
		m_romLoaded = false;
		loadRom(m_romPath);
		if (m_addressSpace) {
			m_addressSpace->reset();
			m_addressSpace->addBlock(Retro::ramBase(m_core), retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM), retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
		}
	}

	retro_reset();
}

void Emulator::unloadCore() {
	if (!m_coreHandle) {
		return;
	}
	if (m_romLoaded) {
		unloadRom();
	}
	retro_deinit();
#ifdef _WIN32
	FreeLibrary(m_coreHandle);
#else
	dlclose(m_coreHandle);
#endif
	m_coreHandle = nullptr;
	s_loadedEmulator = nullptr;
}

void Emulator::unloadRom() {
	if (!m_romLoaded) {
		return;
	}
	retro_unload_game();
	m_romLoaded = false;
	m_romPath.clear();
	m_addressSpace = nullptr;
	m_map.clear();
}

bool Emulator::serialize(void* data, size_t size) {
	assert(s_loadedEmulator == this);
	return retro_serialize(data, size);
}

bool Emulator::unserialize(const void* data, size_t size) {
	assert(s_loadedEmulator == this);
	try {
		retro_system_info systemInfo;
		retro_get_system_info(&systemInfo);
		if (!strcmp(systemInfo.library_name, "Stella")) {
			reset();
		}

		return retro_unserialize(data, size);
	} catch (...) {
		return false;
	}
}

size_t Emulator::serializeSize() {
	assert(s_loadedEmulator == this);
	return retro_serialize_size();
}

void Emulator::clearCheats() {
	assert(s_loadedEmulator == this);
	retro_cheat_reset();
}

void Emulator::setCheat(unsigned index, bool enabled, const char* code) {
	assert(s_loadedEmulator == this);
	retro_cheat_set(index, enabled, code);
}

bool Emulator::loadCore(const string& corePath) {
	if (s_loadedEmulator) {
		return false;
	}

#ifdef _WIN32
	m_coreHandle = LoadLibrary(corePath.c_str());
#else
	m_coreHandle = dlopen(corePath.c_str(), RTLD_LAZY);
#endif
	if (!m_coreHandle) {
		return false;
	}

	retro_init = reinterpret_cast<void (*)()>(GETSYM(m_coreHandle, "retro_init"));
	retro_deinit = reinterpret_cast<void (*)()>(GETSYM(m_coreHandle, "retro_deinit"));
	retro_api_version = reinterpret_cast<unsigned int (*)()>(GETSYM(m_coreHandle, "retro_api_version"));
	retro_get_system_info = reinterpret_cast<void (*)(struct retro_system_info*)>(GETSYM(m_coreHandle, "retro_get_system_info"));
	retro_get_system_av_info = reinterpret_cast<void (*)(struct retro_system_av_info*)>(GETSYM(m_coreHandle, "retro_get_system_av_info"));
	retro_reset = reinterpret_cast<void (*)()>(GETSYM(m_coreHandle, "retro_reset"));
	retro_run = reinterpret_cast<void (*)()>(GETSYM(m_coreHandle, "retro_run"));
	retro_serialize_size = reinterpret_cast<size_t (*)()>(GETSYM(m_coreHandle, "retro_serialize_size"));
	retro_serialize = reinterpret_cast<bool (*)(void*, size_t)>(GETSYM(m_coreHandle, "retro_serialize"));
	retro_unserialize = reinterpret_cast<bool (*)(const void*, size_t)>(GETSYM(m_coreHandle, "retro_unserialize"));
	retro_load_game = reinterpret_cast<bool (*)(const struct retro_game_info*)>(GETSYM(m_coreHandle, "retro_load_game"));
	retro_unload_game = reinterpret_cast<void (*)()>(GETSYM(m_coreHandle, "retro_unload_game"));
	retro_get_memory_data = reinterpret_cast<void* (*) (unsigned int)>(GETSYM(m_coreHandle, "retro_get_memory_data"));
	retro_get_memory_size = reinterpret_cast<size_t (*)(unsigned int)>(GETSYM(m_coreHandle, "retro_get_memory_size"));
	retro_cheat_reset = reinterpret_cast<void (*)()>(GETSYM(m_coreHandle, "retro_cheat_reset"));
	retro_cheat_set = reinterpret_cast<void (*)(unsigned int, bool, const char*)>(GETSYM(m_coreHandle, "retro_cheat_set"));
	retro_set_environment = reinterpret_cast<void (*)(retro_environment_t)>(GETSYM(m_coreHandle, "retro_set_environment"));
	retro_set_video_refresh = reinterpret_cast<void (*)(retro_video_refresh_t)>(GETSYM(m_coreHandle, "retro_set_video_refresh"));
	retro_set_audio_sample = reinterpret_cast<void (*)(retro_audio_sample_t)>(GETSYM(m_coreHandle, "retro_set_audio_sample"));
	retro_set_audio_sample_batch = reinterpret_cast<void (*)(retro_audio_sample_batch_t)>(GETSYM(m_coreHandle, "retro_set_audio_sample_batch"));
	retro_set_input_poll = reinterpret_cast<void (*)(retro_input_poll_t)>(GETSYM(m_coreHandle, "retro_set_input_poll"));
	retro_set_input_state = reinterpret_cast<void (*)(short (*)(unsigned int, unsigned int, unsigned int, unsigned int))>(GETSYM(m_coreHandle, "retro_set_input_state"));

	// The default according to the docs
	m_imgDepth = 15;
	s_loadedEmulator = this;

	SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    window = SDL_CreateWindow("Libretro Frontend OpenGL",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);

	retro_set_environment(cbEnvironment);
	retro_set_video_refresh(cbVideoRefresh);
	retro_set_audio_sample(cbAudioSample);
	retro_set_audio_sample_batch(cbAudioSampleBatch);
	retro_set_input_poll(cbInputPoll);
	retro_set_input_state(cbInputState);
	retro_init();

	// if (hw_render.context_reset)
    //     hw_render.context_reset();

	return true;
}

void Emulator::fixScreenSize(const string& romName) {
	retro_system_info systemInfo;
	retro_get_system_info(&systemInfo);

	if (!strcmp(systemInfo.library_name, "Genesis Plus GX")) {
		switch (romName.back()) {
		case 'd': // Mega Drive
			// Genesis Plus GX gives us too small a resolution initially
			m_avInfo.geometry.base_width = 320;
			m_avInfo.geometry.base_height = 224;
			break;
		case 's': // Master System
			// Genesis Plus GX gives us too small a resolution initially
			m_avInfo.geometry.base_width = 256;
			m_avInfo.geometry.base_height = 192;
			break;
		case 'g': // Game Gear
			m_avInfo.geometry.base_width = 160;
			m_avInfo.geometry.base_height = 144;
			break;
		}
	} else if (!strcmp(systemInfo.library_name, "Stella")) {
		// Stella gives confusing values to pretend the pixel width is 2x
		m_avInfo.geometry.base_width = 160;
	} else if (!strcmp(systemInfo.library_name, "Mednafen PCE Fast")) {
		m_avInfo.geometry.base_width = 256;
		m_avInfo.geometry.base_height = 242;
	}
}

void Emulator::reconfigureAddressSpace() {
	if (!m_addressSpace) {
		return;
	}
	if (!m_map.empty() && m_addressSpace->blocks().empty()) {
		for (const auto& desc : m_map) {
			if (desc.flags & RETRO_MEMDESC_CONST) {
				continue;
			}
			size_t len = desc.len;
			if (desc.select) {
				len = ((~desc.select & ~desc.start) + 1) & desc.select;
			}
			if (desc.len && desc.len < len) {
				len = desc.len;
			}
			m_addressSpace->addBlock(desc.start, len, desc.ptr);
		}
	}
}

// callback for logging from emulator
// turned off by default to avoid spamming the log, only used for debugging issues within cores
static void cbLog(enum retro_log_level level, const char *fmt, ...) {
// #if 0
	char buffer[4096] = {0};
	static const char * levelName[] = { "DEBUG", "INFO", "WARNING", "ERROR" };
	va_list va;

	va_start(va, fmt);
	std::vsnprintf(buffer, sizeof(buffer), fmt, va);
	va_end(va);

	if (level == 0)
		return;

	std::cerr << "[" << levelName[level] << "] " << buffer << std::flush;
// #endif
}

static void  context_reset(void)
{
    // TODO: Create context reset
	printf("Context RESET called!\n");
}

static void context_destroy(void)
{
    // TODO: Create context destroy
    printf("Context DESTROY called!\n");
}

bool Emulator::cbEnvironment(unsigned cmd, void* data) {
	assert(s_loadedEmulator);

	printf("cbEnvironment: ----->>>>>>>> %d\n", cmd);

	switch (cmd) {
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		switch (*reinterpret_cast<retro_pixel_format*>(data)) {
		case RETRO_PIXEL_FORMAT_XRGB8888:
			s_loadedEmulator->m_imgDepth = 32;
			break;
		case RETRO_PIXEL_FORMAT_RGB565:
			s_loadedEmulator->m_imgDepth = 16;
			break;
		case RETRO_PIXEL_FORMAT_0RGB1555:
			s_loadedEmulator->m_imgDepth = 15;
			break;
		default:
			s_loadedEmulator->m_imgDepth = 0;
			break;
		}
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable* var = reinterpret_cast<struct retro_variable*>(data);
		if (s_envVariables.count(string(var->key))) {
			var->value = s_envVariables[string(var->key)];
			return true;
		}
		return false;
	}
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
		if (!s_loadedEmulator->m_corePath) {
			s_loadedEmulator->m_corePath = strdup(corePath().c_str());
		}
		*reinterpret_cast<const char**>(data) = s_loadedEmulator->m_corePath;
		return true;
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
		*reinterpret_cast<bool*>(data) = true;
		return true;
	case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
		s_loadedEmulator->m_map.clear();
		for (size_t i = 0; i < static_cast<const retro_memory_map*>(data)->num_descriptors; ++i) {
			s_loadedEmulator->m_map.emplace_back(static_cast<const retro_memory_map*>(data)->descriptors[i]);
		}
		s_loadedEmulator->reconfigureAddressSpace();
		return true;
	// Logs needs to be handled even when not used, otherwise some cores (ex: mame2003_plus) will crash
	// Also very useful when integrating new emulators to debug issues within the core itself
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
		struct retro_log_callback *cb = (struct retro_log_callback *)data;
		cb->log = cbLog;
		return true;
	}

	// 3DS:
	case RETRO_ENVIRONMENT_SET_HW_RENDER: {
		hw_render = *(struct retro_hw_render_callback *)data;
		hw_render.context_type = RETRO_HW_CONTEXT_OPENGL;
		hw_render.version_major = 3;
		hw_render.version_minor = 3;
		hw_render.context_reset = context_reset;
		hw_render.context_destroy = context_destroy;
		return true;
	}

	default:
		return false;
	}
	return false;
}

void Emulator::cbVideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch) {
	assert(s_loadedEmulator);

	printf("cbVideoRefresh: ----->>>>>>>> %d %d %d\n", width, height, pitch);

	if (data) {
		s_loadedEmulator->m_imgData = data;
	}
	if (pitch) {
		s_loadedEmulator->m_imgPitch = pitch;
	}


	s_loadedEmulator -> m_avInfo.geometry.base_width = width;
	s_loadedEmulator -> m_avInfo.geometry.base_height = height;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    SDL_GL_SwapWindow(window);
}

void Emulator::cbAudioSample(int16_t left, int16_t right) {
	assert(s_loadedEmulator);
	s_loadedEmulator->m_audioData.push_back(left);
	s_loadedEmulator->m_audioData.push_back(right);
}

size_t Emulator::cbAudioSampleBatch(const int16_t* data, size_t frames) {
	assert(s_loadedEmulator);
	s_loadedEmulator->m_audioData.insert(s_loadedEmulator->m_audioData.end(), data, &data[frames * 2]);
	return frames;
}

void Emulator::cbInputPoll() {
	assert(s_loadedEmulator);
}

int16_t Emulator::cbInputState(unsigned port, unsigned, unsigned, unsigned id) {
	assert(s_loadedEmulator);
	return s_loadedEmulator->m_buttonMask[port][id];
}

void Emulator::configureData(GameData* data) {
	m_addressSpace = &data->addressSpace();
	m_addressSpace->reset();
	Retro::configureData(data, m_core);
	reconfigureAddressSpace();
	if (m_addressSpace->blocks().empty() && retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)) {
		m_addressSpace->addBlock(Retro::ramBase(m_core), retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM), retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
	}
}

vector<string> Emulator::buttons() const {
	return Retro::buttons(m_core);
}

vector<string> Emulator::keybinds() const {
	return Retro::keybinds(m_core);
}

}
