/* Otter Shell plugin ABI v1 (SD-first).
 *
 * C entrypoints for .so plugins. Zig hosts/plugins may mirror these types
 * (see otter-bar/src/plugin/abi.zig). Plugins contribute Surface Description
 * via host-mediated sd_* helpers on OtterPluginHost during bar contribute.
 *
 * Layout names MUST use the mandatory prefix: plugin:<id>
 *
 * BarVTable.on_event is optional (NULL) and appended for forward-compat.
 * OtterPluginHost may append nullable helpers (sd_text_input) for forward-compat;
 * plugins MUST null-check new function pointers. See docs/plugin-text-input.md.
 */
#ifndef OTTER_PLUGIN_ABI_H
#define OTTER_PLUGIN_ABI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OTTER_PLUGIN_ABI_VERSION 1u

#define OTTER_PLUGIN_LAUNCHER_MAX_TITLE 64u
#define OTTER_PLUGIN_LAUNCHER_MAX_SUBTITLE 96u
#define OTTER_PLUGIN_LAUNCHER_MAX_PAYLOAD 128u
#define OTTER_PLUGIN_LAUNCHER_ENTRY_FLAG_SD_ROW 1u

#define OTTER_PLUGIN_EVENT_CLICK 1u
#define OTTER_PLUGIN_EVENT_HOVER_ENTER 2u
#define OTTER_PLUGIN_EVENT_HOVER_LEAVE 3u
#define OTTER_PLUGIN_EVENT_TEXT_INPUT 4u
#define OTTER_PLUGIN_EVENT_FOCUS 5u
#define OTTER_PLUGIN_EVENT_BLUR 6u

#define OTTER_PLUGIN_TEXT_FLAG_BUFFER 0u
#define OTTER_PLUGIN_TEXT_FLAG_PREEDIT 1u

#define OTTER_PLUGIN_SD_TEXT_HOST_STYLE (1u << 0)
#define OTTER_PLUGIN_SD_TEXT_SEARCH (1u << 1)

#define OTTER_PLUGIN_COLOR_THEME_FOREGROUND 1u
#define OTTER_PLUGIN_COLOR_THEME_MUTED 2u
#define OTTER_PLUGIN_COLOR_THEME_ACCENT 3u
#define OTTER_PLUGIN_COLOR_THEME_BAR_ITEM 4u
#define OTTER_PLUGIN_COLOR_THEME_BAR_ACTIVE 5u
#define OTTER_PLUGIN_COLOR_THEME_BACKGROUND_ALT 6u
#define OTTER_PLUGIN_COLOR_THEME_SUCCESS 7u
#define OTTER_PLUGIN_COLOR_THEME_WARNING 8u
#define OTTER_PLUGIN_COLOR_THEME_CRITICAL 9u

#define OTTER_PLUGIN_BAR_ITEM_NORMAL 0u
#define OTTER_PLUGIN_BAR_ITEM_ACTIVE 1u
#define OTTER_PLUGIN_BAR_ITEM_SUCCESS 2u
#define OTTER_PLUGIN_BAR_ITEM_WARNING 3u
#define OTTER_PLUGIN_BAR_ITEM_CRITICAL 4u

typedef struct OtterPluginHost OtterPluginHost;
typedef struct OtterPluginFrameCtx OtterPluginFrameCtx;

typedef struct OtterPluginRect {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} OtterPluginRect;

typedef struct OtterPluginSlotConstraints {
    OtterPluginRect area;
    OtterPluginRect clip;
} OtterPluginSlotConstraints;

typedef struct OtterPluginManifest {
    const char *id; /* bare id, no "plugin:" prefix */
    const char *name;
    const char *version;
    uint32_t abi_version;
    uint8_t provides_bar_widget;
    uint8_t provides_launcher_provider;
    uint8_t provides_launcher_sd_rows;
    uint8_t provides_settings;
} OtterPluginManifest;

typedef struct OtterPluginEvent {
    uint32_t kind;
    uint64_t hit_data;
    const char *text; /* UTF-8 for TEXT_INPUT; may be NULL */
    uint32_t text_len;
    uint32_t reserved;
} OtterPluginEvent;

typedef struct OtterPluginBarVTable {
    uint32_t (*preferred_width)(void *self, const OtterPluginSlotConstraints *constraints);
    int (*contribute)(void *self, OtterPluginFrameCtx *frame, const OtterPluginSlotConstraints *constraints);
    void (*destroy)(void *self);
    /* Optional: NULL if the plugin ignores host events. */
    int (*on_event)(void *self, const OtterPluginEvent *event);
} OtterPluginBarVTable;

typedef struct OtterPluginBarInstance {
    const OtterPluginBarVTable *vtable;
    void *user_data;
} OtterPluginBarInstance;

typedef struct OtterPluginLauncherEntry {
    char title[OTTER_PLUGIN_LAUNCHER_MAX_TITLE];
    char subtitle[OTTER_PLUGIN_LAUNCHER_MAX_SUBTITLE];
    char payload[OTTER_PLUGIN_LAUNCHER_MAX_PAYLOAD];
    uint8_t flags;
    uint8_t reserved[7];
} OtterPluginLauncherEntry;

/* Host v1 core + additive nullable helpers (null-check before call). */
struct OtterPluginHost {
    uint32_t abi_version;
    void *userdata;
    void (*log)(void *userdata, int level, const char *msg);
    void (*request_frame)(void *userdata);
    int (*sd_rect)(OtterPluginFrameCtx *frame, const char *id_key, const OtterPluginRect *rect, uint32_t rgba);
    int (*sd_label)(OtterPluginFrameCtx *frame, const char *id_key, const OtterPluginRect *rect, const char *text, uint32_t font_size, uint32_t rgba);
    int (*sd_hit_button)(OtterPluginFrameCtx *frame, const char *id_key, const OtterPluginRect *rect, uint64_t hit_data);
    /* Optional: SD text field with IME preedit/focus (M2). May be NULL on older hosts. */
    int (*sd_text_input)(OtterPluginFrameCtx *frame, const char *id_key, const OtterPluginRect *rect,
                         const char *text, const char *preedit, const char *placeholder,
                         uint32_t font_size, uint32_t text_rgba, uint32_t bg_rgba,
                         uint32_t flags, uint64_t hit_data);
    /* Optional: host-themed bar item using an XDG icon name with bundled fallback. */
    int (*sd_bar_item)(OtterPluginFrameCtx *frame, const char *id_key, const OtterPluginRect *rect,
                       const char *text, uint32_t font_size, const char *icon_name,
                       uint32_t state, uint64_t hit_data);
};

uint32_t otter_plugin_abi_version(void);
int otter_plugin_init(const OtterPluginHost *host);
void otter_plugin_deinit(void);
int otter_plugin_query(OtterPluginManifest *out);
OtterPluginBarInstance *otter_plugin_bar_attach(const OtterPluginHost *host, const char *instance_id);
int otter_plugin_launcher_query(const OtterPluginHost *host, const char *query, OtterPluginLauncherEntry *out, uint32_t capacity);
int otter_plugin_launcher_activate(const OtterPluginHost *host, const char *payload);
int otter_plugin_launcher_contribute_row(const OtterPluginHost *host, OtterPluginFrameCtx *frame, const OtterPluginSlotConstraints *constraints, uint32_t entry_index, const char *payload);

#ifdef __cplusplus
}
#endif

#endif /* OTTER_PLUGIN_ABI_H */
