#ifndef PLUVIORA_H_
#define PLUVIORA_H_

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define PLUVIORA_EXPORT __declspec(dllexport)
#else
#define PLUVIORA_EXPORT __attribute__((visibility("default"))) __attribute__((used))
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum { PLUVIORA_ABI_VERSION = 1 };

typedef void* PluvioraHandle;

typedef uint32_t PluvioraStatus;

enum {
  PLUVIORA_OK = 0,
  PLUVIORA_INVALID_ARGUMENT = 1,
  PLUVIORA_INVALID_HANDLE = 2,
  PLUVIORA_JSON_ERROR = 3,
  PLUVIORA_CHART_ERROR = 4,
  PLUVIORA_NOT_LOADED = 5,
  PLUVIORA_INTERNAL_ERROR = 255
};

typedef enum PluvioraCommandType {
  PLUVIORA_COMMAND_RECT = 1,
  PLUVIORA_COMMAND_QUAD = 2,
  PLUVIORA_COMMAND_SPRITE = 3,
  PLUVIORA_COMMAND_NOTE = 4,
  /* Command values 5 and 6 are reserved by ABI v1. */
  PLUVIORA_COMMAND_HIT_RING = 7,
  PLUVIORA_COMMAND_PARTICLE = 8,
  PLUVIORA_COMMAND_TEXT_BITMAP = 9
} PluvioraCommandType;

typedef enum PluvioraSpriteKind {
  PLUVIORA_SPRITE_LINE_HEAD = 0,
  PLUVIORA_SPRITE_PAUSE = 1
} PluvioraSpriteKind;

typedef enum PluvioraNoteKind {
  PLUVIORA_NOTE_TAP = 0,
  PLUVIORA_NOTE_HOLD = 1,
  PLUVIORA_NOTE_DRAG = 2,
  PLUVIORA_NOTE_FRACTURE = 3
} PluvioraNoteKind;

typedef struct PluvioraFrameView {
  const uint8_t* data;
  uint32_t length;
  uint32_t capacity;
  uint32_t command_count;
  uint32_t hit_count;
  uint32_t drag_count;
  uint32_t fracture_count;
  uint32_t reserved;
} PluvioraFrameView;

typedef struct PluvioraMetadataView {
  const char* title;
  const char* composer;
  const char* illustrator;
  const char* beatmapper;
  const char* difficulty;
  const char* audio_file;
  const char* illustration_file;
  double difficulty_value;
  double chart_duration;
  uint32_t line_count;
  uint32_t note_count;
  uint32_t animation_count;
  uint32_t storyboard_count;
  uint64_t content_hash;
} PluvioraMetadataView;

PLUVIORA_EXPORT uint32_t pluviora_abi_version(void);
PLUVIORA_EXPORT PluvioraHandle pluviora_create(void);
PLUVIORA_EXPORT PluvioraStatus pluviora_destroy(PluvioraHandle handle);

PLUVIORA_EXPORT PluvioraStatus pluviora_load(
    PluvioraHandle handle,
    const uint8_t* json_data,
    size_t json_length,
    const uint8_t* js_data,
    size_t js_length);

PLUVIORA_EXPORT PluvioraStatus pluviora_render(
    PluvioraHandle handle,
    double time_seconds,
    float width,
    float height,
    double song_length,
    PluvioraFrameView* out_frame);

PLUVIORA_EXPORT PluvioraStatus pluviora_seek(
    PluvioraHandle handle,
    double time_seconds);

PLUVIORA_EXPORT PluvioraStatus pluviora_get_metadata(
    PluvioraHandle handle,
    PluvioraMetadataView* out_metadata);

PLUVIORA_EXPORT PluvioraStatus pluviora_set_font_data(
    PluvioraHandle handle,
    const uint8_t* font_data,
    size_t font_length);

PLUVIORA_EXPORT PluvioraStatus pluviora_set_note_scale(
    PluvioraHandle handle,
    double scale);

PLUVIORA_EXPORT PluvioraStatus pluviora_set_flow_speed(
    PluvioraHandle handle,
    double speed);

PLUVIORA_EXPORT uint32_t pluviora_warning_count(PluvioraHandle handle);
PLUVIORA_EXPORT const char* pluviora_warning_at(
    PluvioraHandle handle,
    uint32_t index);
PLUVIORA_EXPORT const char* pluviora_last_error(PluvioraHandle handle);

#ifdef __cplusplus
}
#endif

#endif  // PLUVIORA_H_
