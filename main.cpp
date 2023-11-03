/*
    VeMUlator - A Dreamcast Visual Memory Unit emulator for libretro
    Copyright (C) 2018  Mahmoud Jaoune

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <streams/file_stream.h>

#include <libretro.h>
#include "vmu.h"

/* Forward declarations */
extern "C" {
   RFILE* rfopen(const char *path, const char *mode);
   int64_t rfseek(RFILE* stream, int64_t offset, int origin);
   int64_t rftell(RFILE* stream);
   int rfgetc(RFILE* stream);
   int rfclose(RFILE* stream);
}

static void dummy_log( enum retro_log_level level, const char* fmt, ... ) { }
retro_log_printf_t log_cb = dummy_log;
retro_environment_t env_cb;

retro_environment_t environment_cb;
retro_video_refresh_t video_cb;
retro_audio_sample_t audio_cb;
retro_audio_sample_batch_t audio_batch_cb;
retro_input_poll_t input_poll_cb;
retro_input_state_t input_state_cb;

struct retro_variable options[2] = {
   {"enable_flash_write", "Enable flash write (.bin, requires restart); enabled|disabled"},
   { NULL, NULL }
};

static VMU *vmu;
static uint16_t *frameBuffer;
static byte *romData;

RETRO_API void retro_set_environment(retro_environment_t env)
{
   environment_cb = env;

   env(RETRO_ENVIRONMENT_SET_VARIABLES, options);
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t vr)
{
	video_cb = vr;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t sample)
{
	audio_cb = sample;
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t batch)
{
	audio_batch_cb = batch;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t ipoll)
{
	input_poll_cb = ipoll;
}

RETRO_API void retro_set_input_state(retro_input_state_t istate)
{
	input_state_cb = istate;
}

RETRO_API void retro_init(void)
{
	struct retro_log_callback log;

	if ( env_cb( RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log ) )
		log_cb = log.log;

	frameBuffer = (uint16_t*)calloc(SCREEN_WIDTH*SCREEN_HEIGHT, sizeof(uint16_t));
	vmu         = new VMU(frameBuffer);
	log_cb( RETRO_LOG_INFO, "retro_init() exiting\n");
}

RETRO_API void retro_deinit(void)
{
	delete vmu;
	if(frameBuffer)
      free(frameBuffer);
	if(romData)
      free(romData);
   frameBuffer = NULL;
   romData     = NULL;
   log_cb( RETRO_LOG_INFO, "retro_deinit() exiting\n");
}

RETRO_API unsigned retro_api_version(void)
{
	return RETRO_API_VERSION;
}

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
	info->library_name     = "VeMUlator";
	info->library_version  = "0.1";
	info->valid_extensions = "vms|bin|dci";
	info->need_fullpath    = true;
	info->block_extract    = false;
	log_cb( RETRO_LOG_INFO, "retro_get_system_info() exiting\n");
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
	info->geometry.base_width   = SCREEN_WIDTH;
	info->geometry.base_height  = SCREEN_HEIGHT;
	info->geometry.max_width    = SCREEN_WIDTH;
	info->geometry.max_height   = SCREEN_HEIGHT;
	info->geometry.aspect_ratio = 0;
	
	info->timing.fps            = FPS;
	
#if !defined(SF2000)
	info->timing.sample_rate    = SAMPLE_RATE;
#else
	/* NOTE: The sample rate is assumed to be 31440 by default.
			Relevant code is in:
            
			vemulator-libretro\common.h, line 26
			
				#define SAMPLE_RATE 32768
			
		SF2000 can only handle 11025, 22050, and 44100.
	*/
	info->timing.sample_rate    = 22050;
#endif
log_cb( RETRO_LOG_INFO, "retro_get_system_av_info() exiting\n");
}

RETRO_API void retro_set_controller_port_device(
      unsigned port, unsigned device)
{
	
}
 
void processInput()
{
   byte P3_reg, P3_int;
   int pressFlag = 0;

   input_poll_cb();

   if(!vmu->cpu->P3_taken)
      return;	//Don't accept new input until previous is processed

   P3_reg = vmu->ram->readByte_RAW(P3);
   P3_int = vmu->ram->readByte_RAW(P3INT);
   P3_reg = ~P3_reg;	//Active low

   //Up
   if(input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))
   {
      P3_reg |= 1;
      pressFlag++;
   }
   else P3_reg &= 0xFE;

   //Down
   if(input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))
   {
      P3_reg |= 2;
      pressFlag++;
   }
   else P3_reg &= 0xFD;

   //Left
   if(input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))
   {
      P3_reg |= 4;
      pressFlag++;
   }
   else P3_reg &= 0xFB;

   //Right
   if(input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT))
   {
      P3_reg |= 8;
      pressFlag++;
   }
   else P3_reg &= 0xF7;

   //A
   if(input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A))
   {
      P3_reg |= 16;
      pressFlag++;
   }
   else P3_reg &= 0xEF;

   //B
   if(input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B))
   {
      P3_reg |= 32;
      pressFlag++;
   }
   else P3_reg &= 0xDF;

   //Start
   if(input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START))
   {
      //Clicking MODE without a BIOS causes hang
      //P3_reg |= 64;
      //pressFlag++;
   }
   //else P3_reg &= 0xBF;

   P3_reg = ~P3_reg;

   vmu->ram->writeByte_RAW(P3, P3_reg);

   if(pressFlag)
   {
      vmu->ram->writeByte_RAW(P3INT, P3_int | 2);
      vmu->intHandler->setP3();
      vmu->cpu->P3_taken = false;
   }

}

RETRO_API void retro_reset(void)
{
	vmu->reset();
}

RETRO_API void retro_run(void)
{
	log_cb( RETRO_LOG_INFO, "retro_run()\n");
   unsigned i;
   unsigned int cyclesPassed;
	log_cb( RETRO_LOG_INFO, "retro_run() 1\n");
	processInput();
	log_cb( RETRO_LOG_INFO, "retro_run() 2\n");
	//Cycles passed since last screen refresh
	cyclesPassed = vmu->cpu->getCurrentFrequency() / FPS;
	log_cb( RETRO_LOG_INFO, "retro_run() 3\n");
	for(i = 0; i < cyclesPassed; i++)
		vmu->runCycle();
log_cb( RETRO_LOG_INFO, "retro_run()\n 4");
	//Video
	vmu->video->drawFrame(frameBuffer);
	log_cb( RETRO_LOG_INFO, "retro_run()\n 5");
	if(vmu->ram->readByte_RAW(MCR) & 8)
      video_cb(frameBuffer, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH * 2);
	log_cb( RETRO_LOG_INFO, "retro_run()\n 6");
	//Audio
	vmu->audio->generateSignal(audio_cb);
	log_cb( RETRO_LOG_INFO, "retro_run() exiting\n");
}

RETRO_API size_t retro_serialize_size(void) { return 0; }
RETRO_API bool retro_serialize(void *data, size_t size) {return false;}
RETRO_API bool retro_unserialize(const void *data, size_t size) {return false;}

RETRO_API void retro_cheat_reset(void) { }

RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
}

RETRO_API bool retro_load_game(const struct retro_game_info *game)
{
   size_t i, romSize;
   //Set environment variables
   enum retro_pixel_format format = RETRO_PIXEL_FORMAT_RGB565;
   environment_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format);

   //Opening file
   RFILE *rom = rfopen(game->path, "rb");
   if(!rom)
      return false;
   rfseek(rom, 0, SEEK_END);
   romSize = rftell(rom);
   rfseek(rom, 0 , SEEK_SET);

   romData = (byte *)malloc(romSize);
   for(i = 0; i < romSize; i++)
      romData[i] = rfgetc(rom);

   rfclose(rom);

   //Check extension
   char *path = (char *)malloc(strlen(game->path) + 1);
   strcpy(path, game->path);
   char *ext = strchr(path, '.');

   //Check needed variables
   struct retro_variable var = {0};
   var.key = "enable_flash_write";
   environment_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);

   //Loading ROM
   if(!strcmp(ext, ".bin") || !strcmp(ext, ".BIN")) 
   {
      //Check if user wants core to be able to write to flash
      if(!strcmp(var.value, "enabled")) vmu->flash->loadROM(romData, romSize, 0, game->path, true);
      else vmu->flash->loadROM(romData, romSize, 0, game->path, false);
   }
   else if(!strcmp(ext, ".vms") || !strcmp(ext, ".VMS")) vmu->flash->loadROM(romData, romSize, 1, game->path, false);
   else if(!strcmp(ext, ".dci") || !strcmp(ext, ".DCI")) vmu->flash->loadROM(romData, romSize, 2, game->path, false);

   free(path);

   //Initializing system
   vmu->startCPU();

   return true;
}

RETRO_API bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
	return false;
}

RETRO_API void retro_unload_game(void)
{
	vmu->reset();
}

RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
RETRO_API void *retro_get_memory_data(unsigned id) { return NULL; }
RETRO_API size_t retro_get_memory_size(unsigned id) { return 0; }
