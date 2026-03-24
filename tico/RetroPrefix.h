#pragma once
// Map standard libretro API calls to the prefixed symbols
// in ppsspp_libretro_libnx_prefixed.a
#define retro_init                    ppsspp_retro_init
#define retro_deinit                  ppsspp_retro_deinit
#define retro_api_version             ppsspp_retro_api_version
#define retro_get_system_info         ppsspp_retro_get_system_info
#define retro_get_system_av_info      ppsspp_retro_get_system_av_info
#define retro_set_environment         ppsspp_retro_set_environment
#define retro_set_video_refresh       ppsspp_retro_set_video_refresh
#define retro_set_audio_sample        ppsspp_retro_set_audio_sample
#define retro_set_audio_sample_batch  ppsspp_retro_set_audio_sample_batch
#define retro_set_input_poll          ppsspp_retro_set_input_poll
#define retro_set_input_state         ppsspp_retro_set_input_state
#define retro_set_controller_port_device ppsspp_retro_set_controller_port_device
#define retro_reset                   ppsspp_retro_reset
#define retro_run                     ppsspp_retro_run
#define retro_serialize_size          ppsspp_retro_serialize_size
#define retro_serialize               ppsspp_retro_serialize
#define retro_unserialize             ppsspp_retro_unserialize
#define retro_cheat_reset             ppsspp_retro_cheat_reset
#define retro_cheat_set               ppsspp_retro_cheat_set
#define retro_load_game               ppsspp_retro_load_game
#define retro_load_game_special       ppsspp_retro_load_game_special
#define retro_unload_game             ppsspp_retro_unload_game
#define retro_get_region              ppsspp_retro_get_region
#define retro_get_memory_data         ppsspp_retro_get_memory_data
#define retro_get_memory_size         ppsspp_retro_get_memory_size
