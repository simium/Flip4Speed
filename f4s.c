#include <dolphin/dolphin.h>
#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <stdlib.h>

#define ROAD_LEFT              40
#define ROAD_RIGHT             72
#define PLAYER_Y               54
#define OBSTACLE_COUNT         2
#define INITIAL_OBSTACLE_SPEED 2
#define SPEED_INCREMENT        1
#define MAX_SPEED              6
#define INITIAL_DELAY          33
#define MIN_DELAY              16

typedef struct
{
    int road_offset;
    int player_x;
    int obstacle_x[OBSTACLE_COUNT];
    int obstacle_y[OBSTACLE_COUNT];
    unsigned int score;
    bool game_over;
    int obstacle_speed;
    int game_delay;
    FuriMutex *mutex;
} Flip4SpeedState;

typedef enum
{
    EventTypeTick, // Not used at the moment
    EventTypeKey,
} EventType;

typedef struct
{
    EventType type;
    InputEvent input;
} Flip4SpeedEvent;

const NotificationSequence sequence_fail = {
    &message_vibro_on,

    &message_note_ds4,  &message_delay_10,
    &message_sound_off, &message_delay_10,

    &message_note_ds4,  &message_delay_10,
    &message_sound_off, &message_delay_10,

    &message_note_ds4,  &message_delay_10,
    &message_sound_off, &message_delay_10,

    &message_vibro_off, NULL,
};

const NotificationSequence sequence_obstacle = {
    &message_note_c7,
    &message_delay_10,
    &message_sound_off,
    NULL,
};

/**
 * @brief Reset an obstacle to a random position
 * @param state The game state
 * @param index The index of the
 * obstacle to reset
 */
static void f4s_game_reset_obstacle(Flip4SpeedState *f4s_state, int index)
{
    f4s_state->obstacle_x[index] = ROAD_LEFT + (rand() % 3) * 16;
    f4s_state->obstacle_y[index] = -10;
}

/**
 * @brief Render the game
 * @param canvas The canvas to render to
 * @param ctx The game state
 */
static void f4s_game_render_callback(Canvas *const canvas, void *ctx)
{
    furi_assert(ctx);
    const Flip4SpeedState *f4s_state = ctx;

    furi_mutex_acquire(f4s_state->mutex, FuriWaitForever);

    // Frame
    canvas_draw_frame(canvas, 0, 0, 128, 64);

    // Road
    canvas_draw_line(canvas, 40, 0, 0, 64);
    canvas_draw_line(canvas, 88, 0, 128, 64);

    for (int i = 0; i < 5; i++)
    {
        int y = (64 - (i * 12) + f4s_state->road_offset) % 64;
        canvas_draw_line(canvas, 64, y, 64, y - 6);
    }

    // Player
    canvas_draw_box(canvas, f4s_state->player_x, PLAYER_Y, 16, 10);

    // Obstacles
    for (int i = 0; i < OBSTACLE_COUNT; i++)
    {
        canvas_draw_box(canvas, f4s_state->obstacle_x[i], f4s_state->obstacle_y[i], 16, 10);
    }

    // Score
    char score_text[20];
    snprintf(score_text, sizeof(score_text), "Score: %u>", f4s_state->score);
    canvas_draw_str(canvas, 5, 10, score_text);

    // Game Over banner
    if (f4s_state->game_over)
    {
        // Screen is 128x64 px
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 34, 20, 62, 24);

        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, 34, 20, 62, 24);

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 37, 31, "Game Over");

        canvas_set_font(canvas, FontSecondary);
        char buffer[12];
        snprintf(buffer, sizeof(buffer), "Score: %u", f4s_state->score);
        canvas_draw_str_aligned(canvas, 64, 41, AlignCenter, AlignBottom, buffer);
    }

    furi_mutex_release(f4s_state->mutex);
}

/**
 * @brief Input callback for the game
 * @param input_event The input event
 * @param context The game state
 */
static void f4s_game_input_callback(InputEvent *input_event, void *context)
{
    furi_assert(context);
    FuriMessageQueue *event_queue = context;

    Flip4SpeedEvent event = {.type = EventTypeKey, .input = *input_event};
    furi_message_queue_put(event_queue, &event, FuriWaitForever);
}

/**
 * @brief Initialize the game
 * @param f4s_state The game state
 */
static void f4s_game_init_game(Flip4SpeedState *const f4s_state)
{
    f4s_state->game_over = false;

    f4s_state->player_x = 56;
    f4s_state->score = 0;
    f4s_state->game_over = false;
    f4s_state->obstacle_speed = INITIAL_OBSTACLE_SPEED;
    f4s_state->game_delay = INITIAL_DELAY;
    for (int i = 0; i < OBSTACLE_COUNT; i++)
    {
        f4s_game_reset_obstacle(f4s_state, i);
    }
}

/**
 * @brief Process a game step
 * @param f4s_state The game state
 */
static void f4s_game_process_game_step(Flip4SpeedState *const f4s_state, NotificationApp *notification)
{
    if (!f4s_state->game_over)
    {
        f4s_state->road_offset = (f4s_state->road_offset + 2) % 12;
        f4s_state->score++;

        for (int i = 0; i < OBSTACLE_COUNT; i++)
        {
            f4s_state->obstacle_y[i] += f4s_state->obstacle_speed;
            if (f4s_state->obstacle_y[i] > 64)
            {
                f4s_game_reset_obstacle(f4s_state, i);
                notification_message(notification, &sequence_obstacle);
            }

            if (f4s_state->obstacle_y[i] + 10 >= PLAYER_Y && f4s_state->obstacle_x[i] == f4s_state->player_x)
            {
                f4s_state->game_over = true;
                notification_message_block(notification, &sequence_fail);
                return;
            }
        }

        if (f4s_state->score % 50 == 0 && f4s_state->obstacle_speed < MAX_SPEED)
        {
            f4s_state->obstacle_speed += SPEED_INCREMENT;
        }
        if (f4s_state->score % 100 == 0 && f4s_state->game_delay > MIN_DELAY)
        {
            f4s_state->game_delay -= 5;
        }
    }
    else
    {
        return;
    }
}

/**
 * @brief Main game loop
 * @param p Unused
 * @return 0
 */
int32_t f4s_game_app(void *p)
{
    UNUSED(p);

    FuriMessageQueue *event_queue = furi_message_queue_alloc(8, sizeof(Flip4SpeedEvent));

    Flip4SpeedState *f4s_state = malloc(sizeof(Flip4SpeedState));
    f4s_game_init_game(f4s_state);

    f4s_state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    ViewPort *view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, f4s_game_render_callback, f4s_state);
    view_port_input_callback_set(view_port, f4s_game_input_callback, event_queue);

    // Open GUI and register view_port
    Gui *gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    NotificationApp *notification = furi_record_open(RECORD_NOTIFICATION);

    notification_message_block(notification, &sequence_display_backlight_enforce_on);

    dolphin_deed(DolphinDeedPluginGameStart);

    Flip4SpeedEvent event;
    for (bool processing = true; processing;)
    {
        FuriStatus event_status = furi_message_queue_get(event_queue, &event, 100);

        furi_mutex_acquire(f4s_state->mutex, FuriWaitForever);

        if (event_status == FuriStatusOk)
        {
            // press events
            if (event.type == EventTypeKey)
            {
                if (event.input.type == InputTypePress)
                {
                    switch (event.input.key)
                    {
                        case InputKeyUp:
                            break;
                        case InputKeyDown:
                            break;
                        case InputKeyRight:
                            if (f4s_state->player_x < ROAD_RIGHT)
                            {
                                f4s_state->player_x += 16;
                            }
                            break;
                        case InputKeyLeft:
                            if (f4s_state->player_x > ROAD_LEFT)
                            {
                                f4s_state->player_x -= 16;
                            }
                            break;
                        case InputKeyOk:
                            if (f4s_state->game_over)
                            {
                                f4s_game_init_game(f4s_state);
                            }
                            break;
                        case InputKeyBack:
                            processing = false;
                            break;
                        default:
                            break;
                    }
                }
            }
        }
        else
        {
            // event timeout
        }

        f4s_game_process_game_step(f4s_state, notification);

        furi_mutex_release(f4s_state->mutex);
        view_port_update(view_port);
        furi_delay_ms(f4s_state->game_delay);
    }

    // Return backlight to normal state
    notification_message(notification, &sequence_display_backlight_enforce_auto);

    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_mutex_free(f4s_state->mutex);
    free(f4s_state);

    return 0;
}
