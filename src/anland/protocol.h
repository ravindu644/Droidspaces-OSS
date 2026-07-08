#ifndef DISPLAY_PROTOCOL_H
#define DISPLAY_PROTOCOL_H

#include <stdint.h>

#define CTRL_MSG_CONSUMER_HELLO  1
#define CTRL_MSG_PRODUCER_HELLO  2
#define CTRL_MSG_SCREEN_INFO     7
#define CTRL_MSG_REJECT          8
#define CTRL_MSG_PICKUP_FDS      9
#define CTRL_MSG_FDS_READY      10

#define DATA_MSG_BUF_READY       100
#define DATA_MSG_REFRESH_DONE    101
#define DATA_MSG_INPUT_EVENT     102
#define DATA_MSG_OUTPUT_EVENT    103
#define DATA_MSG_INPUT_EXTEND_FDS  104
#define DATA_MSG_BUFS_READY      200

#define MAX_BUFS 8

struct ctrl_msg {
    uint32_t type;
    uint32_t size;
    uint8_t  payload[];
} __attribute__((packed));

struct data_msg {
    uint32_t type;
    uint32_t size;
    uint8_t  payload[];
} __attribute__((packed));

struct screen_info {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t refresh;
} __attribute__((packed));

struct buf_info {
    uint32_t stride;
    uint32_t width;      /* buffer logical width  (consumer-side native resolution) */
    uint32_t height;     /* buffer logical height (consumer-side native resolution) */
    uint32_t format;
    uint64_t modifier;
    uint32_t offset;
} __attribute__((packed));

#define INPUT_TYPE_TOUCH          1
#define INPUT_TYPE_KEY            2
#define INPUT_TYPE_POINTER_MOTION 3
#define INPUT_TYPE_POINTER_BUTTON 4
#define INPUT_TYPE_POINTER_AXIS   5
#define INPUT_TYPE_TOUCH_FRAME    6
/* Not really input: the consumer reports its current display refresh rate over
 * the same data channel so the producer can repace its RenderLoop at runtime.
 * Deliberately reuses the InputEvent framing (DATA_MSG_INPUT_EVENT) so the
 * producer's poll_input_event() drains it like any other event instead of
 * stalling the stream on an unknown DATA_MSG_* header. */
#define INPUT_TYPE_DISPLAY_REFRESH 7
#define INPUT_TYPE_CLIPBOARD      8
#define INPUT_TYPE_TEXT_INPUT      9
#define INPUT_TYPE_ACTION 10
#define INPUT_TYPE_RESOURCE 11
#define INPUT_TYPE_RESOURCE_INVALID 12

#define SERVICE_TYPE_CAMERA 1

#define INPUT_ACTION_DOWN    0

#define INPUT_ACTION_DOWN    0
#define INPUT_ACTION_UP      1
#define INPUT_ACTION_MOVE    2

#define OUTPUT_TYPE_CLIPBOARD 1
#define OUTPUT_TYPE_RESOURCES_REQUEST 2



struct InputEvent {
    uint32_t type;
    union {
        struct {
            int32_t  action;
            float    x;
            float    y;
            int32_t  pointer_id;
        } touch;
        struct {
            int32_t  action;
            int32_t  keycode;
        } key;
        struct {
            float    x;
            float    y;
            float    dx;
            float    dy;
        } pointer_motion;
        struct {
            uint32_t button;
            int32_t  pressed;
        } pointer_button;
        struct {
            uint32_t axis;
            float    value;
            int32_t  discrete;
        } pointer_axis;
        struct {
            uint32_t refresh_mhz; // current display refresh rate, milli-Hz
        } display;
        struct {
            uint32_t size; //这个packet只是通知包 作为header真正数据会集中发送,这里通知随后数据的大小
        } clipboard;
        struct {
            uint32_t size; //这个packet只是通知包 作为header真正数据会集中发送,这里通知随后数据的大小
        } text_input;
        struct {
            uint32_t action;
            int32_t value;
        } input_action;
        struct {
            uint32_t type;
            uint32_t fdnum;//fdnum是fd的数量,后续会有fdnum个fd跟随在这个结构体后面
        } resource;
        struct {
            uint32_t padding[4];
        };
    };
} __attribute__((packed));

struct OutputEvent{
    uint32_t type;
    union {
        struct {
            uint32_t size; //这个packet只是通知包 作为header真正数据会集中发送,这里通知随后数据的大小
        } clipboard;
        struct {
            uint32_t type;
            uint32_t args[3]; //support 3 args
        } resources_request;
        struct
        {
            uint32_t padding[4];
        };

    };
} __attribute__((packed));

/*
 * Audio runs on its own dedicated bidirectional socketpair (hello fd slot 4),
 * deliberately kept off the data channel so a burst of PCM never head-of-line
 * blocks input/clipboard. The socket is full duplex: the producer writes desktop
 * playback PCM that the consumer reads, and the consumer writes microphone PCM
 * that the producer reads -- each side only ever reads what the other wrote.
 * Every direction sends one AUDIO_MSG_FORMAT, then a stream of AUDIO_MSG_PCM.
 *
 * PCM is interleaved per frame. For stereo (channels == 2) the sample order
 * within each frame is LEFT then RIGHT, i.e. channel positions FL, FR -- the
 * producer's SPA channel map and the consumer's AAudio stereo layout must both
 * honour this order so the left/right channels are never swapped.
 *
 * Audio quality is negotiated, not pinned: the consumer owns the real hardware,
 * so on connect it opens its AAudio streams, reads back the rate/channels the
 * device actually chose, and sends one AUDIO_MSG_FORMAT per direction (tagged with
 * role) to the producer. The producer builds its PipeWire sink/source to match,
 * so neither side resamples blindly and the PCM byte math lines up on both ends.
 */
#define AUDIO_MSG_FORMAT 1
#define AUDIO_MSG_PCM    2

/* PCM sample format codes for struct audio_format.format. */
#define AUDIO_FORMAT_S16LE 0

/* Which direction a struct audio_format describes. */
#define AUDIO_ROLE_PLAYBACK 0   /* producer -> consumer (desktop sound -> speaker) */
#define AUDIO_ROLE_CAPTURE  1   /* consumer -> producer (mic -> virtual source)    */

struct audio_format {
    uint32_t rate;       /* frames per second the device chose, e.g. 48000 / 44100 */
    uint32_t channels;   /* interleaved; stereo is L,R (FL,FR) */
    uint32_t format;     /* AUDIO_FORMAT_* */
    uint32_t role;       /* AUDIO_ROLE_* -- which direction this describes */
    uint32_t quantum;    /* requested buffer in frames (latency preset); 0 = engine default */
} __attribute__((packed));

struct audio_msg {
    uint32_t type;       /* AUDIO_MSG_FORMAT | AUDIO_MSG_PCM */
    uint32_t size;       /* payload bytes that follow this header */
} __attribute__((packed));


#endif
