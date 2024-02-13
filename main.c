#include <stdio.h>
#include <stdint.h>
#include <libserialport.h>
#include <SDL2/SDL.h>

#define SENSORS_MAX 64
#define SENSOR_MAX_VALUE 1023

typedef struct context
{
    struct sp_port *port;
    SDL_Window *window;
    SDL_Renderer *renderer;

    size_t sensors;
    int thresholds[SENSORS_MAX];
    int values[SENSORS_MAX];

    struct {
        float x;
        float y;
        float w;
        float h;
    } ui_panel;
} context_t;

const char cmd_values[]     = { 'v', '\n' };
const char cmd_thresholds[] = { 't', '\n' };

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int pad_parse_values(context_t *ctx, char *report, int *dest, size_t size)
{
    size_t count = 0;
    char c;

    switch (report[0]) {
        case 'v': dest = ctx->values; break;
        case 't': dest = ctx->thresholds; break;
        default: return 0;
    }

    report++;

    while ((c = *report) && c != '\n') {
        if (is_digit(c)) {
            char *end = report;
            int value = strtol(report, &end, 10);
            if (errno) {
                return 0;
            } else if (is_digit(*end)) {
                return 0;
            }
            if (dest && size) {
                *dest = value;
                dest++;
                size--;
            }
            count++;
            report = end;
            continue;
        }
        report++;
    }

    return count;
}

/* if not NULL, writes read values into dest
 * returns value count or zero */
int pad_read_parse_values(context_t *ctx)
{
    char report[256] = {0};
    size_t count = 0;
    enum sp_return bytes;

    char buf[256];
    for (;;) {
        bytes = sp_blocking_read_next(ctx->port, buf, sizeof(buf), 100);
        buf[bytes] = 0;

        if (bytes == 0) {
            return 0;
        }

        if (bytes < 0) {
            printf("sp_blocking_read_next error, exiting...\n");
            return 0;
        }

        for (int i = 0; i < bytes; i++) {
            if (buf[i] == '\n') {
                report[count] = '\n';
                return pad_parse_values(ctx, report, NULL, ctx->sensors);
            } else if (count < sizeof(report)) {
                report[count] = buf[i];
                count++;
            } else {
                printf("not a valid report...\n");
                return 0;
            }
        }
    }
}

/* returns 0 on success */
int pad_serial_write_cmd(context_t *ctx, const char *cmd, size_t cmd_len)
{
    enum sp_return err;
    err = sp_blocking_write(ctx->port, cmd, cmd_len, 0);
    if (err < 0) {
        printf("Failed to write to the port: %d\n", err);
        return 1;
    }
    err = sp_drain(ctx->port);
    if (err != SP_OK) {
        printf("Failed to drain the port\n");
        return 2;
    }
    return 0;
}

/* requests current sensor values or thresholds from the pad
   returns sensor count, or zero on failure */
int pad_get_values(context_t *ctx, int is_thresholds)
{
    const char *cmd = is_thresholds ? cmd_thresholds : cmd_values;
    int err = pad_serial_write_cmd(ctx, cmd, 2);
    if (err) {
        return 0;
    }

    return pad_read_parse_values(ctx);
}

/* returns zero on success */
int pad_set_threshold(context_t *ctx, int sensor, int value)
{
    value = value < 0 ? 0 : value;
    value = value > SENSOR_MAX_VALUE ? SENSOR_MAX_VALUE : value;

    ctx->thresholds[sensor] = value;
    char buf[64];
    int s = snprintf(buf, sizeof(buf), "%d %d\n", sensor, value);
    int err = pad_serial_write_cmd(ctx, buf, s);
    if (err) {
        return 1;
    }

    pad_read_parse_values(ctx);

    return 0;
}

static SDL_FRect ui_sensor_bounds(context_t *ctx, int sensor)
{
    float w = ctx->ui_panel.w;
    float h = ctx->ui_panel.h;
    float x = ctx->ui_panel.x;
    float y = ctx->ui_panel.y;

    SDL_FRect bounds = {
        x + (w * sensor / (float)ctx->sensors),
        y,
        w / (float)ctx->sensors,
        h,
    };

    return bounds;
}

static SDL_FRect ui_sensor_bounds_margin(
    context_t *ctx, int sensor, float margin_ratio
) {
    SDL_FRect bounds = ui_sensor_bounds(ctx, sensor);
    float w = ctx->ui_panel.w;
    float h = ctx->ui_panel.h;
    float margin = (w > h ? h : w) * margin_ratio;

    SDL_FRect bounds_m = (SDL_FRect){
        bounds.x + margin,
        bounds.y + margin,
        bounds.w - margin * 2,
        bounds.h - margin * 2,
    };

    return bounds_m;
}

void ui_draw_sensor_panel(context_t *ctx)
{
    float w = ctx->ui_panel.w;
    float h = ctx->ui_panel.h;
    float margin = (w > h ? h : w) * 0.05;

    for (int i = 0; i < ctx->sensors; i++) {
        SDL_FRect outline = ui_sensor_bounds_margin(ctx, i, 0.02);

        if (ctx->values[i] > ctx->thresholds[i]) {
            SDL_SetRenderDrawColor(ctx->renderer, 255, 255, 255, 128);
        } else {
            SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 128);
        }

        SDL_RenderFillRectF(ctx->renderer, &outline);

        SDL_FRect bounds_m = ui_sensor_bounds_margin(ctx, i, 0.05);

        float sh = 1.0 - (float)ctx->values[i] / (float)SENSOR_MAX_VALUE;
        sh *= bounds_m.h;

        SDL_FRect r = bounds_m;
        r.y = r.y + sh;
        r.h = r.h - sh;

        SDL_SetRenderDrawColor(ctx->renderer, 255, 0, 255, 255);
        SDL_RenderFillRectF(ctx->renderer, &r);

        r = bounds_m;
        r.h = margin / 4.0;

        float th = 1.0 - (float)ctx->thresholds[i] / (float)SENSOR_MAX_VALUE;
        th *= bounds_m.h;
        r.y += th - r.h / 2;

        SDL_SetRenderDrawColor(ctx->renderer, 255, 255, 255, 192);
        SDL_RenderFillRectF(ctx->renderer, &r);
    }
}

void ui_redraw(context_t *ctx)
{
    int w, h;

    SDL_GetRendererOutputSize(ctx->renderer, &w, &h);
    SDL_SetRenderDrawColor(ctx->renderer, 64, 64, 64, 255);
    SDL_RenderClear(ctx->renderer);

    ctx->ui_panel.x = 0;
    ctx->ui_panel.y = 0;
    ctx->ui_panel.w = w;
    ctx->ui_panel.h = h;

    ui_draw_sensor_panel(ctx);

    SDL_RenderPresent(ctx->renderer);
}

/* opens port of specified name, otherwise opens the first USB port
 * returns 0 on success, error code otherwise */
int serial_open_port(context_t *ctx, const char *port_name)
{
    if (ctx->port) {
        return 0;
    }

    struct sp_port *port = NULL;

    if (port_name) {
        enum sp_return err = sp_get_port_by_name(port_name, &port);
        if (err != SP_OK) {
            printf("Failed to get port '%s': %d\n", port_name, err);
            return 3;
        }

        ctx->port = port;
        goto port_open;
    }

    struct sp_port **port_list;
    sp_list_ports(&port_list);

    while (*port_list) {
        const char *name = sp_get_port_name(*port_list);
        const char *description = sp_get_port_description(*port_list);

        int vid, pid;
        // list only USB COM ports
        if (sp_get_port_usb_vid_pid(*port_list, &vid, &pid) == SP_OK) {
            printf("Port %s: %s [VID %04X PID %04X]\n", name, description, vid, pid);
            if (!port_name && !port) {
                port = *port_list;
            }
        }
        port_list++;
    }

    if (!port) {
        printf("No USB serial devices found!\n");
        return 1;
    }

port_open:
    printf("Attempting to open the port...\n");
    enum sp_return err = sp_open(port, SP_MODE_READ_WRITE);
    if (err != SP_OK) {
        printf("Error opening port, code: %d\n", err);
        return 2;
    }

    ctx->port = port;
    return 0;
}

/* returns 0 on success */
int profile_load(context_t *ctx, const char *profile)
{
    if (!profile) {
        return 0;
    }

    FILE *f = fopen(profile, "rb");
    if (!f) {
        return 2;
    }

    static const char expected[8] = "danspad ";
    uint64_t signature;

    fread(&signature, 1, sizeof(signature), f);
    if (signature != *(uint64_t *)expected) {
        printf("Profile signature does not match\n");
        fclose(f);
        return 3;
    }

    uint32_t sensor_count;
    size_t bytes = fread(&sensor_count, 1, sizeof(sensor_count), f);
    if (bytes != 4) {
        printf("Invalid profile file\n");
        fclose(f);
        return 4;
    }

    if (sensor_count != ctx->sensors) {
        printf("Profile has %d thresholds, connected pad has %lld\n",
               sensor_count, ctx->sensors);
        fclose(f);
        return 5;
    }

    uint32_t thresholds[SENSORS_MAX];

    for (size_t i = 0; i < sensor_count && i < SENSORS_MAX; i++) {
        bytes = fread(&thresholds[i], 1, sizeof(*thresholds), f);
        if (bytes != 4) {
            printf("Invalid profile file\n");
            fclose(f);
            return 6;
        }
    }

    fclose(f);

    printf("Setting thresholds from profile file...\n");
    for (size_t i = 0; i < sensor_count && i < SENSORS_MAX; i++) {
        pad_set_threshold(ctx, i, thresholds[i]);
    }

    return 0;
}

/* returns 0 on success */
int profile_save(context_t *ctx, const char *profile)
{
    if (!profile) {
        return 0;
    }

    FILE *f = fopen(profile, "wb");
    if (!f) {
        return 2;
    }

    static const char signature[8] = "danspad ";

    fwrite(&signature, 1, sizeof(signature), f);
    uint32_t sensor_count = ctx->sensors;
    fwrite(&sensor_count, 1, sizeof(sensor_count), f);

    for (size_t i = 0; i < sensor_count && i < SENSORS_MAX; i++) {
        uint32_t threshold = ctx->thresholds[i];
        fwrite(&threshold, 1, sizeof(threshold), f);
    }

    fclose(f);
    printf("Saved profile '%s'\n", profile);

    return 0;
}

int main(int argc, char *argv[])
{
    context_t ctx = {0};

    char *port_name = NULL;
    char *profile = NULL;

    if (argc >= 2) {
        port_name = argv[1];
    }
    if (argc >= 3) {
        profile = argv[2];
    }

    int err = serial_open_port(&ctx, port_name);
    if (err) {
        return 1;
    }

    ctx.sensors = pad_get_values(&ctx, 0);
    if (ctx.sensors) {
        printf("Sensor count: %lld\n", ctx.sensors);
    } else {
        printf("Failed to get response from pad, exiting...\n");
        return 3;
    }

    SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO);

    SDL_CreateWindowAndRenderer(800, 200, SDL_WINDOW_RESIZABLE, &ctx.window, &ctx.renderer);
    SDL_SetRenderDrawBlendMode(ctx.renderer, SDL_BLENDMODE_BLEND);

reconnect:
    pad_get_values(&ctx, 1);

    profile_load(&ctx, profile);

    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type)
            {
            case SDL_QUIT:
                profile_save(&ctx, profile);
                return 0;
            case SDL_MOUSEBUTTONUP:
                profile_save(&ctx, profile);
                break;
            }
        }

        int x, y;
        uint32_t mb = SDL_GetMouseState(&x, &y);
        if (mb & SDL_BUTTON(1)) {
            for (int i = 0; i < ctx.sensors; i++) {
                SDL_FRect b = ui_sensor_bounds_margin(&ctx, i, 0.05);
                if (x >= b.x
                && x <= b.x + b.w
                && y >= b.y
                && y <= b.y + b.h
                ) {
                    float v = 1.0 - (y - b.y) / b.h;
                    v *= SENSOR_MAX_VALUE;
                    pad_set_threshold(&ctx, i, v);
                }
            }
        }

        int count = pad_get_values(&ctx, 0);
        if (!count) {
            if (SP_OK == sp_open(ctx.port, SP_MODE_READ_WRITE)) {
                goto reconnect;
            }
        }

        ui_redraw(&ctx);
    }

    return 0;
}
