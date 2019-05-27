#ifndef SND_RTK_COMMON_H
#define SND_RTK_COMMON_H

typedef enum
{
    ENUM_KERNEL_RPC_CREATE_AGENT,   // 0
    ENUM_KERNEL_RPC_INIT_RINGBUF,
    ENUM_KERNEL_RPC_PRIVATEINFO,
    ENUM_KERNEL_RPC_RUN,
    ENUM_KERNEL_RPC_PAUSE,
    ENUM_KERNEL_RPC_SWITCH_FOCUS,   // 5
    ENUM_KERNEL_RPC_MALLOC_ADDR,
    ENUM_KERNEL_RPC_VOLUME_CONTROL,      // AUDIO_CONFIG_COMMAND 
    ENUM_KERNEL_RPC_FLUSH,               // AUDIO_RPC_SENDIO 
    ENUM_KERNEL_RPC_CONNECT,             // AUDIO_RPC_CONNECTION 
    ENUM_KERNEL_RPC_SETREFCLOCK,    // 10     // AUDIO_RPC_REFCLOCK 
    ENUM_KERNEL_RPC_DAC_I2S_CONFIG,      // AUDIO_CONFIG_DAC_I2S 
    ENUM_KERNEL_RPC_DAC_SPDIF_CONFIG,    // AUDIO_CONFIG_DAC_SPDIF 
    ENUM_KERNEL_RPC_HDMI_OUT_EDID,       // AUDIO_HDMI_OUT_EDID_DATA 
    ENUM_KERNEL_RPC_HDMI_OUT_EDID2,      // AUDIO_HDMI_OUT_EDID_DATA2 
    ENUM_KERNEL_RPC_HDMI_SET,       // 15     // AUDIO_HDMI_SET 
    ENUM_KERNEL_RPC_HDMI_MUTE,           //AUDIO_HDMI_MUTE_INFO 
    ENUM_KERNEL_RPC_ASK_DBG_MEM_ADDR,    
    ENUM_KERNEL_RPC_DESTROY,
    ENUM_KERNEL_RPC_STOP,
    ENUM_KERNEL_RPC_CHECK_READY,     // 20    // check if Audio get memory pool from AP
    ENUM_KERNEL_RPC_GET_MUTE_N_VOLUME,   // get mute and volume level
    ENUM_KERNEL_RPC_EOS,
    ENUM_KERNEL_RPC_ADC0_CONFIG,
    ENUM_KERNEL_RPC_ADC1_CONFIG,
    ENUM_KERNEL_RPC_ADC2_CONFIG,    // 25
#if defined(AUDIO_TV_PLATFORM)
    ENUM_KERNEL_RPC_BBADC_CONFIG,
    ENUM_KERNEL_RPC_I2SI_CONFIG,
    ENUM_KERNEL_RPC_SPDIFI_CONFIG,
#endif // AUDIO_TV_PLATFORM
    ENUM_KERNEL_RPC_HDMI_OUT_VSDB,
    ENUM_VIDEO_KERNEL_RPC_CONFIG_TV_SYSTEM,
    ENUM_VIDEO_KERNEL_RPC_CONFIG_HDMI_INFO_FRAME,
    ENUM_VIDEO_KERNEL_RPC_QUERY_DISPLAY_WIN,
    ENUM_VIDEO_KERNEL_RPC_PP_INIT_PIN,
    ENUM_KERNEL_RPC_INIT_RINGBUF_AO //need check this enum
} ENUM_AUDIO_KERNEL_RPC_CMD;

enum AUDIO_IO_PIN {
    BASE_BS_IN = 0,
    EXT_BS_IN = 1,
    PCM_IN = 2,
    BASE_BS_OUT = 3,
    EXT_BS_OUT = 4,
    PCM_OUT = 5,
    SPDIF_IN = 6,
    SPDIF_OUT = 7,
    NON_PCM_OUT = 8,
    INBAND_QUEUE = 9,
    MESSAGE_QUEUE = 10,
    MIC_IN = 11,
    SOUND_EVENT_IN = 12,
    PCM_OUT1 = 13,
    PCM_OUT2 = 14,
    FLASH_AUDIO_PIN_1 = 15,
    FLASH_AUDIO_PIN_2 = 16,
    FLASH_AUDIO_PIN_3 = 17,
    FLASH_AUDIO_PIN_4 = 18,
    FLASH_AUDIO_PIN_5 = 19,
    FLASH_AUDIO_PIN_6 = 20,
    FLASH_AUDIO_PIN_7 = 21,
    FLASH_AUDIO_PIN_8 = 22,
    FLASH_AUDIO_PIN_9 = 23,
    FLASH_AUDIO_PIN_10 = 24,
    FLASH_AUDIO_PIN_11 = 25,
    FLASH_AUDIO_PIN_12 = 26,
    FLASH_AUDIO_PIN_13 = 27,
    FLASH_AUDIO_PIN_14 = 28,
    FLASH_AUDIO_PIN_15 = 29,
    FLASH_AUDIO_PIN_16 = 30,
    DWNSTRM_INBAND_QUEUE = 200,
};
typedef enum AUDIO_IO_PIN AUDIO_IO_PIN;


#endif


