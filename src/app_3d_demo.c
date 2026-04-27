/*
 * AurionOS 3D Demo Application
 * Interactive 3D demos and mini-game using AurionGL
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../AurionGL/auriongl.h"

/* Internal math functions from AurionGL */
extern float agl_sinf(float x);
extern float agl_cosf(float x);

#include "window_manager.h"
#include "panic.h"  /* For kernel_panic */

/* External functions */
extern uint32_t *gpu_get_backbuffer(void);
extern int gpu_get_width(void);
extern int gpu_get_height(void);
extern void gpu_draw_pixel(int x, int y, uint32_t color);
extern void gpu_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void gpu_draw_string(int x, int y, const uint8_t *str, uint32_t fg, uint32_t bg);
extern void wm_destroy_window(Window *window);

static void app_3d_demo_on_close(Window *window);

typedef struct {
    void *window;
    int demo_mode;      /* 0=menu, 1-5=demos, 6=game */
    float time;
    float rotation_x;
    float rotation_y;
    bool auto_rotate;
    bool lighting;
    int shape_type;
    bool agl_initialized;  /* Track if AurionGL is initialized */
    
    /* Game state */
    float player_x, player_y, player_z;
    float target_x, target_y, target_z;
    int score;
    float game_time;
    bool game_active;
    float aspect;
} Demo3DState;

static Demo3DState state = {0};

static void app_3d_demo_on_close(Window *window) {
    (void)window;
    if (state.agl_initialized) {
        aglShutdown();
        state.agl_initialized = false;
    }
}

/* Demo names */
static const char *demo_names[] = {
    "1. Rotating Shapes",
    "2. Solar System",
    "3. Tunnel Effect",
    "4. Particle System",
    "5. Fractal Tree",
    "6. Cube Collector Game"
};


/* Draw menu */
static void draw_menu(Window *win, int client_x, int client_y, int client_w, int client_h) {
    (void)client_x;
    (void)client_y;
    
    /* Clear background using window manager */
    wm_fill_rect(win, 0, 0, client_w, client_h, 0xFF001020);
    
    /* Title */
    const char *title = "3D Demos - Select a demo:";
    wm_draw_string(win, 20, 20, (const uint8_t *)title, 0xFFFFFFFF, 0);
    
    /* Draw menu boxes */
    for (int i = 0; i < 6; i++) {
        int box_y = 60 + i * 60;
        uint32_t color = 0xFF204060;
        wm_fill_rect(win, 20, box_y, client_w - 40, 50, color);
        
        /* Draw demo name */
        wm_draw_string(win, 35, box_y + 18, (const uint8_t *)demo_names[i], 
                       0xFFFFFFFF, 0);
    }
    
    /* Instructions */
    const char *inst = "Press 1-6 to select a demo, ESC to exit";
    wm_draw_string(win, 20, client_h - 30, 
                   (const uint8_t *)inst, 0xFFA0A0A0, 0);
}

/* Demo 1: Rotating Shapes */
static void demo_rotating_shapes(void) {
    aglClear(AGL_COLOR_BUFFER_BIT | AGL_DEPTH_BUFFER_BIT);
    
    aglMatrixMode(AGL_PROJECTION);
    aglLoadIdentity();
    aglPerspective(45.0f, state.aspect, 0.1f, 100.0f);
    
    aglMatrixMode(AGL_MODELVIEW);
    aglLoadIdentity();
    aglTranslate(0.0f, 0.0f, -8.0f);
    
    aglDisable(AGL_CULL_FACE); /* Disable backface culling for testing */
    
    if (state.lighting) {
        aglEnable(AGL_LIGHTING);
        aglEnable(AGL_LIGHT0);
        float light_pos[] = {5.0f, 5.0f, 5.0f, 1.0f};
        aglLightfv(AGL_LIGHT0, AGL_POSITION, light_pos);
    } else {
        aglDisable(AGL_LIGHTING);
    }
    
    /* Draw three shapes */
    aglPushMatrix();
    aglTranslate(-3.0f, 0.0f, 0.0f);
    aglRotate(state.rotation_y, 0.0f, 1.0f, 0.0f);
    aglRotate(state.rotation_x, 1.0f, 0.0f, 0.0f);
    aglColor3f(1.0f, 0.3f, 0.3f);
    aglDrawCube();
    aglPopMatrix();
    
    aglPushMatrix();
    aglTranslate(0.0f, 0.0f, 0.0f);
    aglRotate(state.rotation_x, 1.0f, 0.0f, 0.0f);
    aglColor3f(0.3f, 1.0f, 0.3f);
    aglDrawSphere(0.7f, 20, 20);
    aglPopMatrix();
    
    aglPushMatrix();
    aglTranslate(3.0f, 0.0f, 0.0f);
    aglRotate(state.rotation_y, 0.0f, 1.0f, 0.0f);
    aglColor3f(0.3f, 0.3f, 1.0f);
    aglDrawCylinder(0.5f, 2.0f, 20);
    aglPopMatrix();
}


/* Demo 2: Solar System */
static void demo_solar_system(void) {
    aglClear(AGL_COLOR_BUFFER_BIT | AGL_DEPTH_BUFFER_BIT);
    
    aglMatrixMode(AGL_PROJECTION);
    aglLoadIdentity();
    aglPerspective(45.0f, state.aspect, 0.1f, 100.0f);
    
    aglMatrixMode(AGL_MODELVIEW);
    aglLoadIdentity();
    aglTranslate(0.0f, 0.0f, -15.0f);
    aglRotate(20.0f, 1.0f, 0.0f, 0.0f);
    
    if (state.lighting) {
        aglEnable(AGL_LIGHTING);
        aglEnable(AGL_LIGHT0);
    } else {
        aglDisable(AGL_LIGHTING);
        aglDisable(AGL_LIGHT0);
    }
    
    /* Sun */
    aglPushMatrix();
    aglColor3f(1.0f, 1.0f, 0.0f);
    aglDrawSphere(1.0f, 20, 20);
    aglPopMatrix();
    
    /* Earth orbit */
    aglPushMatrix();
    aglRotate(state.time * 30.0f, 0.0f, 1.0f, 0.0f);
    aglTranslate(5.0f, 0.0f, 0.0f);
    aglColor3f(0.2f, 0.4f, 1.0f);
    aglDrawSphere(0.5f, 15, 15);
    
    /* Moon */
    aglPushMatrix();
    aglRotate(state.time * 100.0f, 0.0f, 1.0f, 0.0f);
    aglTranslate(1.2f, 0.0f, 0.0f);
    aglColor3f(0.7f, 0.7f, 0.7f);
    aglDrawSphere(0.2f, 10, 10);
    aglPopMatrix();
    
    aglPopMatrix();
    
    /* Mars orbit */
    aglPushMatrix();
    aglRotate(state.time * 20.0f, 0.0f, 1.0f, 0.0f);
    aglTranslate(8.0f, 0.0f, 0.0f);
    aglColor3f(1.0f, 0.3f, 0.1f);
    aglDrawSphere(0.4f, 15, 15);
    aglPopMatrix();
}

/* Demo 3: Tunnel Effect */
static void demo_tunnel(void) {
    aglClear(AGL_COLOR_BUFFER_BIT | AGL_DEPTH_BUFFER_BIT);
    
    aglMatrixMode(AGL_PROJECTION);
    aglLoadIdentity();
    aglPerspective(60.0f, state.aspect, 0.1f, 100.0f);
    
    aglMatrixMode(AGL_MODELVIEW);
    aglLoadIdentity();
    aglTranslate(0.0f, 0.0f, -5.0f + state.time * 2.0f);
    
    if (state.lighting) {
        aglEnable(AGL_LIGHTING);
        aglEnable(AGL_LIGHT0);
    } else {
        aglDisable(AGL_LIGHTING);
        aglDisable(AGL_LIGHT0);
    }
    
    /* Draw tunnel segments */
    for (int i = 0; i < 10; i++) {
        aglPushMatrix();
        aglTranslate(0.0f, 0.0f, (float)i * 3.0f);
        
        float hue = (state.time * 50.0f + i * 36.0f);
        while (hue >= 360.0f) hue -= 360.0f;
        
        float r = (hue < 120.0f) ? 1.0f : (hue < 240.0f) ? 0.0f : 1.0f;
        float g = (hue < 120.0f) ? hue / 120.0f : (hue < 240.0f) ? 1.0f : 0.0f;
        float b = (hue < 120.0f) ? 0.0f : (hue < 240.0f) ? (hue - 120.0f) / 120.0f : 1.0f;
        
        aglColor3f(r, g, b);
        aglDrawTorus(1.5f, 2.5f, 16, 16);
        aglPopMatrix();
    }
}


/* Demo 4: Particle System */
static void demo_particles(void) {
    aglClear(AGL_COLOR_BUFFER_BIT | AGL_DEPTH_BUFFER_BIT);
    
    aglMatrixMode(AGL_PROJECTION);
    aglLoadIdentity();
    aglPerspective(45.0f, state.aspect, 0.1f, 100.0f);
    
    aglMatrixMode(AGL_MODELVIEW);
    aglLoadIdentity();
    aglTranslate(0.0f, 0.0f, -10.0f);
    aglRotate(state.rotation_y, 0.0f, 1.0f, 0.0f);
    
    /* Draw particles in a spiral - fixed math to prevent overflow */
    for (int i = 0; i < 50; i++) {
        float radius = 3.0f + (float)(i % 10) * 0.3f;
        float height = (float)(i % 20) * 0.3f - 3.0f;
        
        /* Use fixed-point modular pattern instead of unbounded trig */
        int phase = ((int)(state.time * 60.0f) + i) % 50;
        float x = radius * ((float)phase / 25.0f - 1.0f);
        float z = radius * ((float)((phase + 25) % 50) / 25.0f - 1.0f);
        
        aglPushMatrix();
        aglTranslate(x, height, z);
        
        float t = (float)(i % 10) / 10.0f;
        aglColor3f(1.0f - t, t, 0.5f);
        aglColor3f(1.0f - t + 0.2f, t + 0.2f, 0.7f);
        aglDrawSphere(0.25f, 8, 8);
        aglPopMatrix();
    }
}

/* Demo 5: Torus Knot - A "Real" 3D Model Figurine */
static void demo_torus_knot(void) {
    aglClear(AGL_COLOR_BUFFER_BIT | AGL_DEPTH_BUFFER_BIT);
    
    aglMatrixMode(AGL_PROJECTION);
    aglLoadIdentity();
    aglPerspective(45.0f, state.aspect, 0.1f, 100.0f);
    
    aglMatrixMode(AGL_MODELVIEW);
    aglLoadIdentity();
    aglTranslate(0.0f, 0.0f, -10.0f);
    aglRotate(state.rotation_x, 1.0f, 0.5f, 0.2f);
    aglRotate(state.rotation_y, 0.2f, 1.0f, 0.5f);
    
    if (state.lighting) {
        aglEnable(AGL_LIGHTING);
        aglEnable(AGL_LIGHT0);
        float light_pos[] = {5.0f, 5.0f, 5.0f, 1.0f};
        aglLightfv(AGL_LIGHT0, AGL_POSITION, light_pos);
    }
    
    aglColor3f(1.0f, 0.8f, 0.2f); // Golden
    
    /* Generate Torus Knot P=2, Q=3 */
    const int segments = 120;
    const int tube_segments = 12;
    const float R = 2.5f, r = 0.5f;
    
    for (int i = 0; i < segments; i++) {
        float phi1 = (float)i * 2.0f * 3.14159f / segments;
        float phi2 = (float)(i + 1) * 2.0f * 3.14159f / segments;
        
        float p = 2.0f, q = 3.0f;
        
        /* Calculating path and frame... simplified for speed */
        float r1 = R + r * agl_cosf(q * phi1);
        float x1 = r1 * agl_cosf(p * phi1);
        float y1 = r1 * agl_sinf(p * phi1);
        float z1 = r * agl_sinf(q * phi1);
        
        float r2 = R + r * agl_cosf(q * phi2);
        float x2 = r2 * agl_cosf(p * phi2);
        float y2 = r2 * agl_sinf(p * phi2);
        float z2 = r * agl_sinf(q * phi2);
        
        aglBegin(AGL_LINES);
        aglVertex3f(x1, y1, z1);
        aglVertex3f(x2, y2, z2);
        aglEnd();
    }
}

/* Demo 6: Cube Collector Game Improved */
static void demo_game(void) {
    if (!state.game_active) {
        state.player_x = 0.0f;
        state.player_y = 0.0f;
        state.player_z = 0.0f;
        state.target_x = 4.0f;
        state.target_y = 0.0f;
        state.target_z = 4.0f;
        state.score = 0;
        state.game_active = true;
    }
    
    aglClear(AGL_COLOR_BUFFER_BIT | AGL_DEPTH_BUFFER_BIT);
    
    aglMatrixMode(AGL_PROJECTION);
    aglLoadIdentity();
    aglPerspective(45.0f, state.aspect, 0.1f, 100.0f);
    
    aglMatrixMode(AGL_MODELVIEW);
    aglLoadIdentity();
    aglTranslate(0.0f, -3.0f, -15.0f);
    aglRotate(25.0f, 1.0f, 0.0f, 0.0f);
    
    /* Draw ground grid in bright Cyan */
    aglColor3f(0.0f, 0.8f, 1.0f);
    for (int i = -10; i <= 10; i+=2) {
        aglBegin(AGL_LINES);
        aglVertex3f((float)i, 0.0f, -10.0f); aglVertex3f((float)i, 0.0f, 10.0f);
        aglVertex3f(-10.0f, 0.0f, (float)i); aglVertex3f(10.0f, 0.0f, (float)i);
        aglEnd();
    }
    
    /* Draw player as a large Red cube */
    aglPushMatrix();
    aglTranslate(state.player_x, 1.0f, state.player_z);
    aglColor3f(1.0f, 0.2f, 0.2f);
    aglScale(1.5f, 1.5f, 1.5f);
    aglDrawCube();
    aglPopMatrix();
    
    /* Draw target as a large spinning Green cube */
    aglPushMatrix();
    aglTranslate(state.target_x, 1.0f, state.target_z);
    aglRotate(state.time * 90.0f, 0.0f, 1.0f, 0.0f);
    aglColor3f(0.2f, 1.0f, 0.2f);
    aglScale(1.2f, 1.2f, 1.2f);
    aglDrawCube();
    aglPopMatrix();
}


/* Main render function */
void app_3d_demo_render(Window *window) {
    state.window = window;
    
    // Update simulation time at ~50 FPS (2 ticks = 20ms)
    extern uint32_t get_ticks(void);
    static uint32_t last_sim_tick = 0;
    uint32_t now = get_ticks();
    
    if (last_sim_tick == 0) {
        last_sim_tick = now;
    } else {
        uint32_t delta = now - last_sim_tick;
        if (delta >= 2) { 
            state.time += delta * 0.01f; // Convert ticks (10ms) to seconds
            last_sim_tick = now;
            
            /* Update auto-rotation */
            if (state.auto_rotate) {
                state.rotation_x += 1.0f;
                state.rotation_y += 1.5f;
            }
        }
    }
    
    int client_x = wm_client_x(window);
    int client_y = wm_client_y(window);
    int client_w = wm_client_w(window);
    int client_h = wm_client_h(window);
    state.aspect = (client_h > 0) ? ((float)client_w / (float)client_h) : (4.0f / 3.0f);
    
    if (state.demo_mode == 0) {
        /* Show menu */
        draw_menu(window, client_x, client_y, client_w, client_h);
        return;
    }
    
    /* Safety check - prevent infinite loops */
    uint32_t start_tick = get_ticks();
    
    /* Initialize AurionGL with full framebuffer dimensions, then use viewport for window region. */
    uint32_t *fb = gpu_get_backbuffer();
    if (!fb) {
        /* Fallback - draw error message */
        gpu_fill_rect(client_x, client_y, client_w, client_h, 0xFF000000);
        gpu_draw_string(client_x + 10, client_y + 10, 
                       (const uint8_t *)"Error: Failed to get framebuffer", 
                       0xFFFF0000, 0);
        return;
    }
    
    if (client_w <= 0 || client_h <= 0) return;
    int screen_w = gpu_get_width();
    int screen_h = gpu_get_height();
    if (screen_w <= 0 || screen_h <= 0) return;

    if (!state.agl_initialized) {
        aglInit(screen_w, screen_h, fb);
        state.agl_initialized = true;
    }
    
    /* Update viewport and matrices */
    aglSetFramebuffer(fb);
    aglViewport(client_x, client_y, client_w, client_h);
    aglClearColor(0.1f, 0.1f, 0.15f, 1.0f); /* Lighter background to see if rendering works */
    aglEnable(AGL_DEPTH_TEST);
    
    /* Render selected demo */
    switch (state.demo_mode) {
        case 1: demo_rotating_shapes(); break;
        case 2: demo_solar_system(); break;
        case 3: demo_tunnel(); break;
        case 4: demo_particles(); break;
        case 5: demo_torus_knot(); break;
        case 6: demo_game(); break;
        default:
            state.demo_mode = 0; /* Invalid mode - return to menu */
            break;
    }
    
    /* Guard against runaway frame times without hard-panicing the full OS. */
    uint32_t elapsed = get_ticks() - start_tick;
    if (elapsed > 300) {
        /* Keep running, but degrade animation pressure on very slow emulators. */
        state.auto_rotate = false;
    }

}


/* Handle mouse input */
void app_3d_demo_mouse(Window *window, int local_x, int local_y, bool left, bool right) {
    static bool prev_left = false;
    (void)right;
    (void)window;
    (void)local_x;
    (void)local_y;
    
    /* Only handle clicks in menu mode */
    if (state.demo_mode != 0) {
        prev_left = left;
        return;
    }
    
    /* Detect click (button down transition) */
    bool click = left && !prev_left;
    prev_left = left;
    
    if (!click) return;
    
    /* Check if clicking on a menu item */
    for (int i = 0; i < 6; i++) {
        int box_y = 60 + i * 60;
        if (local_y >= box_y && local_y < box_y + 50) {
            /* Select this demo */
            state.demo_mode = i + 1;
            state.time = 0.0f;
            state.rotation_x = 0.0f;
            state.rotation_y = 0.0f;
            state.auto_rotate = true;
            state.lighting = false;
            window->needs_redraw = true;
            break;
        }
    }
}

/* Handle keyboard input */
void app_3d_demo_keypress(Window *window, uint16_t key_code) {
    char key = (char)(key_code & 0xFF);

    if (key == 27) { /* ESC */
        if (state.demo_mode == 0) {
            /* Cleanup AurionGL before destroying window */
            if (state.agl_initialized) {
                aglShutdown();
                state.agl_initialized = false;
            }
            wm_destroy_window(window);
        } else {
            state.demo_mode = 0;
            state.game_active = false;
        }
        return;
    }
    
    /* Menu selection */
    if (state.demo_mode == 0) {
        if (key >= '1' && key <= '6') {
            state.demo_mode = key - '0';
            state.time = 0.0f;
            state.rotation_x = 0.0f;
            state.rotation_y = 0.0f;
            state.auto_rotate = true;
            state.lighting = false;
        }
        return;
    }
    
    /* Demo controls */
    if (key == 'l' || key == 'L') {
        state.lighting = !state.lighting;
    }
    if (key == 'r' || key == 'R') {
        state.auto_rotate = !state.auto_rotate;
    }
    
    /* Game controls */
    if (state.demo_mode == 6) {
        float speed = 0.3f;
        if (key == 'w' || key == 'W') state.player_z += speed;
        if (key == 's' || key == 'S') state.player_z -= speed;
        if (key == 'a' || key == 'A') state.player_x -= speed;
        if (key == 'd' || key == 'D') state.player_x += speed;
        
        /* Clamp to grid */
        if (state.player_x < -4.5f) state.player_x = -4.5f;
        if (state.player_x > 4.5f) state.player_x = 4.5f;
        if (state.player_z < -4.5f) state.player_z = -4.5f;
        if (state.player_z > 4.5f) state.player_z = 4.5f;
    }
}

/* Initialize app */
void app_3d_demo_init(Window *window) {
    state.window = window;
    state.demo_mode = 0;
    state.time = 0.0f;
    state.rotation_x = 0.0f;
    state.rotation_y = 0.0f;
    state.auto_rotate = true;
    state.lighting = false;
    state.shape_type = 0;
    state.game_active = false;
    state.agl_initialized = false;
    state.score = 0;
    state.game_time = 0.0f;
    state.aspect = 4.0f / 3.0f;
}


/* Create app window */
void app_3d_demo_create(void) {
    Window *win = wm_create_window("3D Demos", 80, 10, 660, 500);
    if (win) {
        app_3d_demo_init(win);
        win->on_draw = app_3d_demo_render;
        win->on_key = app_3d_demo_keypress;
        win->on_mouse = app_3d_demo_mouse;
        win->on_close = app_3d_demo_on_close;
    }
}
