/*
* Copyright (c) 2025 ArqAlice 
*
* Released under the MIT license
* https://opensource.org/licenses/mit-license.php
*/

#include "usb_device_control.h"
#include "pico/usb_device.h"
#include "lufa/AudioClassCommon.h"
#include "common.h"
#include "upsampling.h"

// todo make descriptor strings should probably belong to the configs
static char *descriptor_strings[] =
	{
		"ArqAlice",
		"Pico2 UltraHiRes USB-DDC",
		"y.tomi0131@gmail.com:"};

// todo fix these
// #define VENDOR_ID   0x2e8au
// #define PRODUCT_ID  0xfeddu

#define VENDOR_ID (0x16c0u)
#define PRODUCT_ID (0x27e0u)

#define AUDIO_OUT_ENDPOINT (0x01U)
#define AUDIO_IN_ENDPOINT 0x82U

#undef AUDIO_SAMPLE_FREQ
#define AUDIO_SAMPLE_FREQ(frq) (uint8_t)(frq), (uint8_t)((frq >> 8)), (uint8_t)((frq >> 16))

#define AUDIO_MAX_SAMPLE_NUM (96 + 1)						 // (96kHz/1kHz +2)
#define AUDIO_MAX_PACKET_SIZE (2 * 3 * AUDIO_MAX_SAMPLE_NUM) // 2ch * 3byte/ch * (freq[kHz / 1[kHz] + 1)

#define FEATURE_MUTE_CONTROL (1u)
#define FEATURE_VOLUME_CONTROL (2u)

#define ENDPOINT_FREQ_CONTROL (1u)

extern uint32_t now_playing;

static void _as_audio_packet(struct usb_endpoint *ep);

// USB descriptor for HiRes Audio
struct audio_device_config
{
	struct usb_configuration_descriptor descriptor;
	struct usb_interface_descriptor ac_interface;
	struct __packed
	{
		USB_Audio_StdDescriptor_Interface_AC_t core;
		USB_Audio_StdDescriptor_InputTerminal_t input_terminal;
		USB_Audio_StdDescriptor_FeatureUnit_t feature_unit;
		USB_Audio_StdDescriptor_OutputTerminal_t output_terminal;
	} ac_audio;
	struct usb_interface_descriptor as_zero_interface;

	struct usb_interface_descriptor as_op_interface;
	struct __packed
	{
		USB_Audio_StdDescriptor_Interface_AS_t streaming;
		struct __packed
		{
			USB_Audio_StdDescriptor_Format_t core;
			USB_Audio_SampleFreq_t freqs[4]; // 44.1/48/88.2/96kHz対応のため配列数を2->4に増やす
		} format;
	} as_audio;
	struct __packed
	{
		struct usb_endpoint_descriptor_long core;
		USB_Audio_StdDescriptor_StreamEndpoint_Spc_t audio;
	} ep1;
	struct usb_endpoint_descriptor_long ep2;

	// Alternate2 : 24bit再生用の定義
	struct usb_interface_descriptor as_op_interface_2;
	struct __packed
	{
		USB_Audio_StdDescriptor_Interface_AS_t streaming;
		struct __packed
		{
			USB_Audio_StdDescriptor_Format_t core;
			USB_Audio_SampleFreq_t freqs[4]; // <- 44.1/48/88.2/96kHz対応のため配列数を4とする
		} format;
	} as_audio_2;
	struct __packed
	{
		struct usb_endpoint_descriptor_long core;
		USB_Audio_StdDescriptor_StreamEndpoint_Spc_t audio;
	} ep1_2;
	struct usb_endpoint_descriptor_long ep2_2;
};

static const struct audio_device_config audio_device_config =
	{
		.descriptor = {
			.bLength = sizeof(audio_device_config.descriptor),
			.bDescriptorType = DTYPE_Configuration,
			.wTotalLength = sizeof(audio_device_config),
			.bNumInterfaces = 2,
			.bConfigurationValue = 0x01,
			.iConfiguration = 0x00,
			.bmAttributes = 0x80,
			.bMaxPower = 0x32, // 0x32(100mA) Apple A1619(Lightning - USB 3カメラアダプタ)対応
		},
		.ac_interface = {
			.bLength = sizeof(audio_device_config.ac_interface),
			.bDescriptorType = DTYPE_Interface,
			.bInterfaceNumber = 0x00,
			.bAlternateSetting = 0x00,
			.bNumEndpoints = 0x00,
			.bInterfaceClass = AUDIO_CSCP_AudioClass,
			.bInterfaceSubClass = AUDIO_CSCP_ControlSubclass,
			.bInterfaceProtocol = AUDIO_CSCP_ControlProtocol,
			.iInterface = 0x00,
		},
		.ac_audio = {
			.core = {
				.bLength = sizeof(audio_device_config.ac_audio.core),
				.bDescriptorType = AUDIO_DTYPE_CSInterface,
				.bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_Header,
				.bcdADC = VERSION_BCD(1, 0, 0),
				.wTotalLength = sizeof(audio_device_config.ac_audio),
				.bInCollection = 1,
				.bInterfaceNumbers = 1,
			},
			.input_terminal = {
				.bLength = sizeof(audio_device_config.ac_audio.input_terminal),
				.bDescriptorType = AUDIO_DTYPE_CSInterface,
				.bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_InputTerminal,
				.bTerminalID = 1,
				.wTerminalType = AUDIO_TERMINAL_STREAMING,
				.bAssocTerminal = 0,
				.bNrChannels = 2,
				.wChannelConfig = AUDIO_CHANNEL_LEFT_FRONT | AUDIO_CHANNEL_RIGHT_FRONT,
				.iChannelNames = 0,
				.iTerminal = 0,
			},
			.feature_unit = {
				.bLength = sizeof(audio_device_config.ac_audio.feature_unit),
				.bDescriptorType = AUDIO_DTYPE_CSInterface,
				.bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_Feature,
				.bUnitID = 2,
				.bSourceID = 1,
				.bControlSize = 1,
				.bmaControls = {AUDIO_FEATURE_MUTE | AUDIO_FEATURE_VOLUME, 0, 0},
				.iFeature = 0,
			},
			.output_terminal = {
				.bLength = sizeof(audio_device_config.ac_audio.output_terminal),
				.bDescriptorType = AUDIO_DTYPE_CSInterface,
				.bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_OutputTerminal,
				.bTerminalID = 3,
				.wTerminalType = AUDIO_TERMINAL_OUT_SPEAKER,
				.bAssocTerminal = 0,
				.bSourceID = 2,
				.iTerminal = 0,
			},
		},
		.as_zero_interface = {
			.bLength = sizeof(audio_device_config.as_zero_interface),
			.bDescriptorType = DTYPE_Interface,
			.bInterfaceNumber = 0x01,
			.bAlternateSetting = 0x00,
			.bNumEndpoints = 0x00,
			.bInterfaceClass = AUDIO_CSCP_AudioClass,
			.bInterfaceSubClass = AUDIO_CSCP_AudioStreamingSubclass,
			.bInterfaceProtocol = AUDIO_CSCP_ControlProtocol,
			.iInterface = 0x00,
		},
		.as_op_interface = {
			.bLength = sizeof(audio_device_config.as_op_interface),
			.bDescriptorType = DTYPE_Interface,
			.bInterfaceNumber = 0x01,
			.bAlternateSetting = 0x01,
			.bNumEndpoints = 0x02,
			.bInterfaceClass = AUDIO_CSCP_AudioClass,
			.bInterfaceSubClass = AUDIO_CSCP_AudioStreamingSubclass,
			.bInterfaceProtocol = AUDIO_CSCP_ControlProtocol,
			.iInterface = 0x00,
		},
		.as_audio = {
			.streaming = {
				.bLength = sizeof(audio_device_config.as_audio.streaming), .bDescriptorType = AUDIO_DTYPE_CSInterface, .bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_General, .bTerminalLink = 1, .bDelay = 1,
				.wFormatTag = 1, // PCM
			},
			.format = {
				.core = {
					.bLength = sizeof(audio_device_config.as_audio.format),
					.bDescriptorType = AUDIO_DTYPE_CSInterface,
					.bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_FormatType,
					.bFormatType = 1,
					.bNrChannels = 2,
					.bSubFrameSize = 2,
					.bBitResolution = 16,
					.bSampleFrequencyType = count_of(audio_device_config.as_audio.format.freqs),
				},
				.freqs = {
					AUDIO_SAMPLE_FREQ(44100), AUDIO_SAMPLE_FREQ(48000),
					AUDIO_SAMPLE_FREQ(88200),
					AUDIO_SAMPLE_FREQ(96000),
				},
			},
		},
		.ep1 = {.core = {
					.bLength = sizeof(audio_device_config.ep1.core),
					.bDescriptorType = DTYPE_Endpoint,
					.bEndpointAddress = AUDIO_OUT_ENDPOINT,
					.bmAttributes = 5,
					.wMaxPacketSize = AUDIO_MAX_PACKET_SIZE,
					.bInterval = 1,
					.bRefresh = 0,
					.bSyncAddr = AUDIO_IN_ENDPOINT,
				},
				.audio = {
					.bLength = sizeof(audio_device_config.ep1.audio),
					.bDescriptorType = AUDIO_DTYPE_CSEndpoint,
					.bDescriptorSubtype = AUDIO_DSUBTYPE_CSEndpoint_General,
					.bmAttributes = 1,
					.bLockDelayUnits = 0,
					.wLockDelay = 0,
				}},
		.ep2 = {
			.bLength = sizeof(audio_device_config.ep2),
			.bDescriptorType = 0x05,
			.bEndpointAddress = AUDIO_IN_ENDPOINT,
			.bmAttributes = 0x01,
			.wMaxPacketSize = 3,
			.bInterval = 0x01,
			.bRefresh = 0, // 1ms
			.bSyncAddr = 0,
		},

		.as_op_interface_2 = {
			.bLength = sizeof(audio_device_config.as_op_interface_2),
			.bDescriptorType = DTYPE_Interface,
			.bInterfaceNumber = 0x01,
			.bAlternateSetting = 0x02,
			.bNumEndpoints = 0x02,
			.bInterfaceClass = AUDIO_CSCP_AudioClass,
			.bInterfaceSubClass = AUDIO_CSCP_AudioStreamingSubclass,
			.bInterfaceProtocol = AUDIO_CSCP_ControlProtocol,
			.iInterface = 0x00,
		},
		.as_audio_2 = {
			.streaming = {
				.bLength = sizeof(audio_device_config.as_audio_2.streaming), .bDescriptorType = AUDIO_DTYPE_CSInterface, .bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_General, .bTerminalLink = 1, .bDelay = 1,
				.wFormatTag = 1, // PCM
			},
			.format = {
				.core = {
					.bLength = sizeof(audio_device_config.as_audio_2.format),
					.bDescriptorType = AUDIO_DTYPE_CSInterface,
					.bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_FormatType,
					.bFormatType = 1,
					.bNrChannels = 2,
					.bSubFrameSize = 3,	  // 24bit = 3byte
					.bBitResolution = 24, // 24bit
					.bSampleFrequencyType = count_of(audio_device_config.as_audio_2.format.freqs),
				},
				.freqs = {
					AUDIO_SAMPLE_FREQ(44100),
					AUDIO_SAMPLE_FREQ(48000),
					AUDIO_SAMPLE_FREQ(88200),
					AUDIO_SAMPLE_FREQ(96000)
				},
			},
		},
		.ep1_2 = {.core = {
					  .bLength = sizeof(audio_device_config.ep1_2.core),
					  .bDescriptorType = DTYPE_Endpoint,
					  .bEndpointAddress = AUDIO_OUT_ENDPOINT,
					  .bmAttributes = 5,
					  .wMaxPacketSize = AUDIO_MAX_PACKET_SIZE,
					  .bInterval = 1,
					  .bRefresh = 0,
					  .bSyncAddr = AUDIO_IN_ENDPOINT,
				  },
				  .audio = {
					  .bLength = sizeof(audio_device_config.ep1_2.audio),
					  .bDescriptorType = AUDIO_DTYPE_CSEndpoint,
					  .bDescriptorSubtype = AUDIO_DSUBTYPE_CSEndpoint_General,
					  .bmAttributes = 1,
					  .bLockDelayUnits = 0,
					  .wLockDelay = 0,
				  }},
		.ep2_2 = {
			.bLength = sizeof(audio_device_config.ep2_2),
			.bDescriptorType = 0x05,
			.bEndpointAddress = AUDIO_IN_ENDPOINT,
			.bmAttributes = 0x01,
			.wMaxPacketSize = 3,
			.bInterval = 0x01,
			.bRefresh = 0, // 1ms
			.bSyncAddr = 0,
		}};

static struct usb_interface ac_interface;
static struct usb_interface as_op_interface;
static struct usb_endpoint ep_op_out, ep_op_sync;

static const struct usb_device_descriptor boot_device_descriptor = {
	.bLength = 18,
	.bDescriptorType = 0x01,
	.bcdUSB = 0x0110,
	.bDeviceClass = 0x00,
	.bDeviceSubClass = 0x00,
	.bDeviceProtocol = 0x00,
	.bMaxPacketSize0 = 0x40,
	.idVendor = VENDOR_ID,
	.idProduct = PRODUCT_ID,
	.bcdDevice = 0x0200,
	.iManufacturer = 0x01,
	.iProduct = 0x02,
	.iSerialNumber = 0x03,
	.bNumConfigurations = 0x01,
};

const char *_get_descriptor_string(uint index)
{
	if (index <= count_of(descriptor_strings))
	{
		return descriptor_strings[index - 1];
	}
	else
	{
		return "";
	}
}

// バッファ長制限 バッファオーバーラン防止処理
uint16_t buffer_length_limiter(uint32_t freq, uint16_t length)
{
	int32_t limit_length = get_size_remain(&buffer_upsr_data_Lch_0) / get_ratio_upsampling_core0(freq);

	limit_length = saturation_i32(limit_length, SIZE_EP_BUFFER, 0);
	
	uint16_t output = length;
	if (length > limit_length)
	{
		output = limit_length;
	}

	return output;
}

// USB EPバッファ取得処理
uint16_t __not_in_flash_func(usb_ep_data_acquire)(uint bit_depth, int16_t *ep, uint in_length,
											 int32_t *buf_left_ch, int32_t *buf_right_ch)
{
	uint sample_num;
	uint16_t *u16_ep = (uint16_t *)ep; // 24bitデータ処理のため int16_t -> uint16_t 型に変更
	volatile int32_t data = 0;
	uint length = 0;
	uint count = 0;

	switch (bit_depth)
	{
	case 32:
		length = in_length / 4; // (4byte(32bit) /ch)
		length = buffer_length_limiter(audio_state.freq, length);
		sample_num = length;
		while (sample_num--)
		{
			data = ((int32_t)(*u16_ep++) | (*u16_ep++ << 16)) >> 0;
			buf_left_ch[count] = (int32_t)((float)data * audio_state.vol_float);
			data = ((int32_t)(*u16_ep++) | (*u16_ep++ << 16)) >> 0;
			buf_right_ch[count] = (int32_t)((float)data * audio_state.vol_float);
			count++;
		}
		break;

	case 24:
		length = in_length / 3; // (3byte(24bit) /ch)
		length = buffer_length_limiter(audio_state.freq, length);
		sample_num = length;
		while (sample_num--)
		{
			data = ((int32_t)(*u16_ep++ << 8) | (*u16_ep << 24)) >> 0;
			buf_left_ch[count] = (int32_t)((float)data * audio_state.vol_float);
			data = ((int32_t)(*u16_ep++ & 0xff00) | (*u16_ep++ << 16)) >> 0;
			buf_right_ch[count] = (int32_t)((float)data * audio_state.vol_float);
			count++;
		}
		break;

	case 16:
	default:
		length = in_length / 2; // (2byte(16bit) /ch)
		length = buffer_length_limiter(audio_state.freq, length);
		sample_num = length;
		while (sample_num--)
		{
			data = ((int32_t)*ep++) << 16;
			buf_left_ch[count] = (int32_t)((float)data * audio_state.vol_float);
			data = ((int32_t)*ep++) << 16;
			buf_right_ch[count] = (int32_t)((float)data * audio_state.vol_float);
			count++;
		}
		break;
	}
	return length;
}

static void _as_sync_packet(struct usb_endpoint *ep)
{
	assert(ep->current_transfer);
	struct usb_buffer *buffer = usb_current_in_packet_buffer(ep);
	assert(buffer->data_max >= 3);
	buffer->data_len = 3;

	// Feedbackパラメータ計算 アップサンプリングバッファの使用率でFBをかけている
	float ratio = get_ratio_upsampling_core0(audio_state.freq);
	float deviation = (SIZE_BUFFER_FB_THRESHOLD - get_size_using(&buffer_upsr_data_Lch_0)) / ratio;
	int32_t adjust_value = saturation_i32((int32_t)deviation, FB_ADJ_LIMIT, -FB_ADJ_LIMIT);

	uint32_t feedback_fs = audio_state.freq + adjust_value;
	uint32_t feedback = (feedback_fs << 14) / 1000;

	buffer->data[0] = feedback;
	buffer->data[1] = feedback >> 8u;
	buffer->data[2] = feedback >> 16u;

	usb_grow_transfer(ep->current_transfer, 1);
	usb_packet_done(ep);
}

static const struct usb_transfer_type as_transfer_type = {
	.on_packet = _as_audio_packet,
	.initial_packet_count = 1,
};

static const struct usb_transfer_type as_sync_transfer_type = {
	.on_packet = _as_sync_packet,
	.initial_packet_count = 1,
};

static struct usb_transfer as_transfer;
static struct usb_transfer as_sync_transfer;

static bool do_get_current(struct usb_setup_packet *setup)
{
	if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) == USB_REQ_TYPE_RECIPIENT_INTERFACE)
	{
		switch (setup->wValue >> 8u)
		{
		case FEATURE_MUTE_CONTROL:
		{
			usb_start_tiny_control_in_transfer(audio_state.mute, 1);
			return true;
		}
		case FEATURE_VOLUME_CONTROL:
		{
			usb_start_tiny_control_in_transfer(audio_state.acq_volume, 2);
			return true;
		}
		}
	}
	else if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) == USB_REQ_TYPE_RECIPIENT_ENDPOINT)
	{
		if ((setup->wValue >> 8u) == ENDPOINT_FREQ_CONTROL)
		{
			usb_start_tiny_control_in_transfer(audio_state.freq, 3);
			return true;
		}
	}
	return false;
}

static bool do_get_minimum(struct usb_setup_packet *setup)
{
	//    usb_debug("AUDIO_REQ_GET_MIN\n");
	puts("AUDIO_REQ_GET_MIN\n");

	if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) == USB_REQ_TYPE_RECIPIENT_INTERFACE)
	{
		switch (setup->wValue >> 8u)
		{
		case FEATURE_VOLUME_CONTROL:
		{
			usb_start_tiny_control_in_transfer(MIN_VOLUME, 2);
			return true;
		}
		}
	}
	return false;
}

static bool do_get_maximum(struct usb_setup_packet *setup)
{
	//    usb_debug("AUDIO_REQ_GET_MAX\n");
	puts("AUDIO_REQ_GET_MAX\n");

	if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) == USB_REQ_TYPE_RECIPIENT_INTERFACE)
	{
		switch (setup->wValue >> 8u)
		{
		case FEATURE_VOLUME_CONTROL:
		{
			usb_start_tiny_control_in_transfer(MAX_VOLUME, 2);
			return true;
		}
		}
	}
	return false;
}

static bool do_get_resolution(struct usb_setup_packet *setup)
{
	//    usb_debug("AUDIO_REQ_GET_RES\n");
	puts("AUDIO_REQ_GET_RES\n");

	if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) == USB_REQ_TYPE_RECIPIENT_INTERFACE)
	{
		switch (setup->wValue >> 8u)
		{
		case FEATURE_VOLUME_CONTROL:
		{
			usb_start_tiny_control_in_transfer(VOLUME_RESOLUTION, 2);
			return true;
		}
		}
	}
	return false;
}

static struct audio_control_cmd
{
	uint8_t cmd;
	uint8_t type;
	uint8_t cs;
	uint8_t cn;
	uint8_t unit;
	uint8_t len;
} audio_control_cmd_t;

static void audio_set_volume(int16_t volume)
{
	audio_state.acq_volume = volume;
}

static void audio_cmd_packet(struct usb_endpoint *ep)
{
	assert(audio_control_cmd_t.cmd == AUDIO_REQ_SetCurrent);
	struct usb_buffer *buffer = usb_current_out_packet_buffer(ep);
	audio_control_cmd_t.cmd = 0;
	if (buffer->data_len >= audio_control_cmd_t.len)
	{
		if (audio_control_cmd_t.type == USB_REQ_TYPE_RECIPIENT_INTERFACE)
		{
			switch (audio_control_cmd_t.cs)
			{
			case FEATURE_MUTE_CONTROL:
			{
				audio_state.mute = buffer->data[0];
				if (audio_state.mute == true)
				{
					//if(USE_ESS_DAC && KIND_ESS_DAC == ES9038Q2M)
					//	ess_dac_mute();
					clear_ringbuffer(&buffer_ep_Lch);
					clear_ringbuffer(&buffer_ep_Rch);
					clear_ringbuffer(&buffer_upsr_data_Lch_0);
					clear_ringbuffer(&buffer_upsr_data_Rch_0);
					clear_bq_filter_delay();
				}
				break;
			}
			case FEATURE_VOLUME_CONTROL:
			{
				audio_set_volume(*(int16_t *)buffer->data);
				break;
			}
			}
		}
		else if (audio_control_cmd_t.type == USB_REQ_TYPE_RECIPIENT_ENDPOINT)
		{
			if (audio_control_cmd_t.cs == ENDPOINT_FREQ_CONTROL)
			{
				uint32_t new_freq = (*(uint32_t *)buffer->data) & 0x00ffffffu;
				if (audio_state.freq != new_freq)
				{
					audio_state.freq = new_freq;
					if(USE_ESS_DAC && KIND_ESS_DAC == ES9038Q2M)
						ess_dac_mute();
					clear_ringbuffer(&buffer_ep_Lch);
					clear_ringbuffer(&buffer_ep_Rch);
					clear_ringbuffer(&buffer_upsr_data_Lch_0);
					clear_ringbuffer(&buffer_upsr_data_Rch_0);
					clear_bq_filter_delay();
					renew_clock(is_high_power_mode);
				}
			}
		}
	}
	usb_start_empty_control_in_transfer_null_completion();
	// todo is there error handling?
}

static const struct usb_transfer_type _audio_cmd_transfer_type = {
	.on_packet = audio_cmd_packet,
	.initial_packet_count = 1,
};

static bool as_set_alternate(struct usb_interface *interface, uint alt)
{
	assert(interface == &as_op_interface);
	switch (alt)
	{
	case 3:
		audio_state.bit_depth = 32;
		break;
	case 2:
		audio_state.bit_depth = 24;
		break;
	case 1:
		audio_state.bit_depth = 16;
		break;
	case 0:
	default:
		break;
	}
	//    usb_warn("SET ALTERNATE %d, bit_depth = %d\n", alt, audio_state.bit_depth);
	return alt < 4;
}

static bool do_set_current(struct usb_setup_packet *setup)
{
#if 0
#ifndef NDEBUG
    usb_warn("AUDIO_REQ_SET_CUR\n");
#endif
#endif
	puts("AUDIO_REQ_SET_CUR\n");

	if (setup->wLength && setup->wLength < 64)
	{
		audio_control_cmd_t.cmd = AUDIO_REQ_SetCurrent;
		audio_control_cmd_t.type = setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK;
		audio_control_cmd_t.len = (uint8_t)setup->wLength;
		audio_control_cmd_t.unit = setup->wIndex >> 8u;
		audio_control_cmd_t.cs = setup->wValue >> 8u;
		audio_control_cmd_t.cn = (uint8_t)setup->wValue;
		usb_start_control_out_transfer(&_audio_cmd_transfer_type);
		return true;
	}
	return false;
}

static bool ac_setup_request_handler(__unused struct usb_interface *interface, struct usb_setup_packet *setup)
{
	setup = __builtin_assume_aligned(setup, 4);
	if (USB_REQ_TYPE_TYPE_CLASS == (setup->bmRequestType & USB_REQ_TYPE_TYPE_MASK))
	{
		switch (setup->bRequest)
		{
		case AUDIO_REQ_SetCurrent:
			return do_set_current(setup);

		case AUDIO_REQ_GetCurrent:
			return do_get_current(setup);

		case AUDIO_REQ_GetMinimum:
			return do_get_minimum(setup);

		case AUDIO_REQ_GetMaximum:
			return do_get_maximum(setup);

		case AUDIO_REQ_GetResolution:
			return do_get_resolution(setup);

		default:
			break;
		}
	}
	return false;
}

bool _as_setup_request_handler(__unused struct usb_endpoint *ep, struct usb_setup_packet *setup)
{
	setup = __builtin_assume_aligned(setup, 4);
	if (USB_REQ_TYPE_TYPE_CLASS == (setup->bmRequestType & USB_REQ_TYPE_TYPE_MASK))
	{
		switch (setup->bRequest)
		{
		case AUDIO_REQ_SetCurrent:
			return do_set_current(setup);

		case AUDIO_REQ_GetCurrent:
			return do_get_current(setup);

		case AUDIO_REQ_GetMinimum:
			return do_get_minimum(setup);

		case AUDIO_REQ_GetMaximum:
			return do_get_maximum(setup);

		case AUDIO_REQ_GetResolution:
			return do_get_resolution(setup);

		default:
			break;
		}
	}
	return false;
}

void usb_sound_card_init()
{
	// msd_interface.setup_request_handler = msd_setup_request_handler;
	usb_interface_init(&ac_interface, &audio_device_config.ac_interface, NULL, 0, true);
	ac_interface.setup_request_handler = ac_setup_request_handler;

	static struct usb_endpoint *const op_endpoints[] = {
		&ep_op_out, &ep_op_sync};
	usb_interface_init(&as_op_interface, &audio_device_config.as_op_interface, op_endpoints, count_of(op_endpoints),
					   true);
	as_op_interface.set_alternate_handler = as_set_alternate;
	ep_op_out.setup_request_handler = _as_setup_request_handler;
	as_transfer.type = &as_transfer_type;
	usb_set_default_transfer(&ep_op_out, &as_transfer);
	as_sync_transfer.type = &as_sync_transfer_type;
	usb_set_default_transfer(&ep_op_sync, &as_sync_transfer);

	static struct usb_interface *const boot_device_interfaces[] = {
		&ac_interface,
		&as_op_interface,
	};
	__unused struct usb_device *device = usb_device_init(&boot_device_descriptor, &audio_device_config.descriptor,
														 boot_device_interfaces, count_of(boot_device_interfaces),
														 _get_descriptor_string);
	assert(device);
	audio_set_volume(DEFAULT_VOLUME);
	//_audio_reconfigure();
	//    device->on_configure = _on_configure;
	usb_device_start();
}

// UAC Audio Packet受信時のデータ処理
static void _as_audio_packet(struct usb_endpoint *ep)
{
	struct usb_buffer *usb_buffer = usb_current_out_packet_buffer(ep);
	// uint8ポインタをint16にキャスト
	int16_t *ep_in = (int16_t *)usb_buffer->data;

	// ※uint8データ長をint16データ長(1/2)に変換
	uint length = (usb_buffer->data_len) >> 1;

	int32_t ep_Lch[SIZE_EP_BUFFER];
	int32_t ep_Rch[SIZE_EP_BUFFER];

	// usb epデータコピー
	length = usb_ep_data_acquire(audio_state.bit_depth, ep_in, length, ep_Lch, ep_Rch);

	now_playing++; // この処理が来ているかどうかを確認するための変数

	ringbuf_write_array_no_spinlock(ep_Lch, length, &buffer_ep_Lch);
	ringbuf_write_array_no_spinlock(ep_Rch, length, &buffer_ep_Rch);

	// usb epデータコピー完了処理
	usb_grow_transfer(ep->current_transfer, 1);
	usb_packet_done(ep);
}