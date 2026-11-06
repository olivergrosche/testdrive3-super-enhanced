#pragma once
/* Host services on top of SDL3: window/present, the 145.65 Hz timer tick, VGA vertical retrace,
 * keyboard (XT byte stream for the game's own INT 9 handler), mouse, gamepad, audio (OPL2 through
 * Nuked-OPL3 and the PC speaker), game file lookup and fatal errors. No game logic lives here. */
#include "types.h"

#define PIT_HZ        1193182u
#define PIT_DIV_GAME  0x2000u        /* 8192 -> 145.652 Hz, timer_install 0c1c:109f (platform.md 4.4.2) */

bool host_init(const char *game_dir, int window_scale, bool fullscreen);

/* OG: widescreen. host_set_aspect before host_init: the picture's width / height (0 = the desktop's, 4/3 = the
 * original). The 320 x 200 picture keeps its place in the middle; host_wide_margin is how many 320-wide pixel
 * columns are added on each side (0 at 4:3). The frame source's width includes them. */
void host_set_aspect(double aspect);
int  host_wide_margin(void);
void host_shutdown(void);

/* Called once per 145.652 Hz tick from host_pump() (the port's replacement for INT 8). The platform
 * timer module installs the ISR body (snd_tick, tick counter DS:00A0, BIOS tick every 8th). */
void host_set_tick_handler(void (*handler)(void));

/* Source of the displayed image: fills a w x h XRGB8888 frame and returns true if it changed since the
 * last call. Installed by the VGA model (platform/vga.c: 320x200 mode 13h times the output scale of the
 * enhanced renderer, ENH). Shown with 4:3 aspect. */
#define HOST_FRAME_MAX_W (320 * 8)
#define HOST_FRAME_MAX_H (200 * 8)
void host_set_frame_source(bool (*compose)(u32 *xrgb), int w, int h);

/* ENH: monotonic host clock in nanoseconds, and the due time of the latest timer tick run (the time the
 * tick belongs to, independent of when host_pump got to it). */
u64 host_time_ns(void);
u64 host_tick_ns(void);

/* ENH: runs fn(i, ctx) for i = 0..n-1 on a worker pool (the calling thread takes part) and returns when
 * all are done. fn must only touch data that no other index touches. */
void host_parallel_for(int n, void (*fn)(int i, void *ctx), void *ctx);

/* Runs due timer ticks (and their audio), handles window events and presents the screen when it
 * changed. Every busy-wait loop of the original (tick waits, key polls, delays) must call this.
 * Sleeps briefly when nothing was due, so tight polling loops do not spin the CPU. */
void host_pump(void);

/* Waits for the start of the next vertical retrace of the emulated VGA (mode 13h: 70.086 Hz), pumping
 * meanwhile, then presents. Replaces the port 3DAh polls (platform.md: race frame copy, fades). */
void host_wait_vretrace(void);

/* Race frame pacing (PLAN.md decision 4). race_run waits until at least this many timer ticks have
 * passed since the frame started; the original constant is 5 (engine cap, ~29 fps: the game runs
 * 5.8x faster than the race clock's design rate of 5 fps, 29 ticks); the faithful port defaults to 23
 * (~6.3 fps). ENH: 14 (~10.4 fps, chosen by play-testing; 10 passes the scenery at about the speedometer's speed), and
 * the race clock counts real time (hud_topbar.c race_clock_hud; ENHANCED.md "Game speed"). */
#define HOST_DEFAULT_FRAME_TICKS 14   /* ENH: 23 in the faithful port; see ENHANCED.md "Game speed" */
void host_set_frame_ticks(int ticks);
int  host_frame_ticks(void);

/* ---- Keyboard: the game's INT 9 handler (kbd_isr 0c1c:0e98). The handler receives the XT byte
 * sequence the keyboard would send, in event order (platform.md 4.4.1): normal keys sc / sc|80h, grey
 * keys E0 sc / E0 sc|80h, Pause E1 1D 45 E1 9D C5, Print Screen E0 2A E0 37 / E0 B7 E0 AA. Key repeats
 * feed the make code again. */
void host_set_kbd_handler(void (*handler)(u8 byte));
/* Called when the window loses keyboard focus (keys released outside it never send their break). */
void host_set_focus_lost_handler(void (*handler)(void));

/* ---- Mouse (INT 33h replacement): position in 320x200 screen pixels, buttons bit0 left, bit1 right.
 * Motion in mickeys since the last call (for the driver's relative reads). */
void host_mouse_read(s16 *x, s16 *y, u8 *buttons);
void host_mouse_motion(s16 *dx, s16 *dy);

/* ---- Joystick (port 201h replacement): first connected gamepad. Axes -32768..32767, buttons bit0 = A,
 * bit1 = B. */
bool host_joy_read(s16 *x, s16 *y, u8 *buttons);

/* ---- OG: modern gamepad controls (CHASE.md "Gamepad"). While a race runs the pad drives the game through
 * the keys it would press (triggers = Up / Down, buttons = the action keys), and the left stick steers:
 * host_pad_steer gives its wheel position -1 .. 1 while it is the steering source (it was moved since the
 * last steering key; host_pad_steer_off = a key was pressed). In the menus it is the cursor keys and Enter / Esc. */
bool host_pad_steer(double *pos);
void host_pad_steer_off(void);

/* ---- OG: on foot (game/foot.c, FOOT.md). In the car, holding the pad's Y (a short press still switches the chase
 * view) or pressing E asks to get out; on foot, Y or E asks to get in. host_foot_request returns (and clears) that
 * request. host_set_foot switches the pad from driving to walking. host_foot_input: the walking controls now. */
typedef struct {
    double move_x, move_y;      /* sideways (+ right), forward (+), -1 .. 1 */
    double turn, look;          /* turning (+ right), looking up (+), -1 .. 1 */
    bool run, jump;
    bool fire, aim, reload;     /* OG (WEAPON.md): pad RT / LT / X, mouse left / right, keys F / R */
} HostFootInput;
bool host_foot_request(void);
void host_set_foot(bool on);
void host_foot_input(HostFootInput *in);
/* OG: the mouse's motion since the last call, window pixels (on foot the mouse looks) */
void host_foot_mouse(double *dx, double *dy);
/* OG (WEAPON.md): the weapon asked for since the last call: *select 1..3 (keys 1 2 3; 0 none), *cycle steps
 * (pad RB / d-pad right +1, d-pad left -1, key Q +1, the mouse wheel) */
void host_foot_weapon(int *select, int *cycle);
/* OG (TOWNS.md): a shop's menu on foot. While it is on, the walking controls rest (host_foot_input gives nothing)
 * and the menu's presses come here: up (W, Up, d-pad, stick), down, OK (Space, Enter, pad A), back (E, Backspace,
 * Esc, pad B / Y). host_menu_input returns (and clears) the presses since the last call. */
enum { HOST_MENU_UP = 1, HOST_MENU_DOWN = 2, HOST_MENU_OK = 4, HOST_MENU_BACK = 8 };
void host_menu(bool on);
int  host_menu_input(void);

/* ---- OG: sampled sounds (WEAPON.md): mono 16-bit at HOST_AUDIO_RATE, mixed over the OPL2 and the speaker. The
 * samples must stay valid while they play. vol: 0 .. 1; pan: -1 (left) .. 1 (right). */
#define HOST_AUDIO_RATE 44100
void host_pcm_play(const s16 *samples, int n, double vol, double pan);

/* ---- Audio. The sound driver writes OPL2 registers and the PC speaker as the original does; writes
 * take effect from the current tick onward (sound.md, port design). */
void host_opl_write(u8 reg, u8 value);
void host_speaker(u16 divisor, bool on);        /* PIT ch2 divisor (0 = 65536) and the port 61h gate */

/* ---- Game files: case-insensitive lookup inside the game directory. Returns a malloc'd path (free with
 * host_free) or NULL if the file does not exist. For new files (.HI, PLAYDISK.DAT) pass create = true. */
char *host_game_path(const char *name, bool create);
void  host_free(void *p);

/* ---- Errors: shows a message box, shuts down and exits with code 3. */
_Noreturn void host_fatal(const char *fmt, ...);

/* ---- Developer aids (environment variables):
 *   TD3_SNAPSHOT_DIR=dir   every presented frame >= 2 s after the previous one is saved as snapNNNN.bmp
 *                          (works with SDL_VIDEO_DRIVER=dummy)
 *   TD3_KEYS="<seconds>:<xt>[+<xt>...],..."  presses (in order) and releases (in reverse) the XT keys
 *                          at that many seconds after start-up; a "p" after the codes only presses
 *                          (held), an "r" only releases: "9:48p,20:48r" holds Up for 11 s. Grey keys
 *                          are written with their E0 prefix as e048. */
