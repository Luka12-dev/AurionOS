/*
 * AurionGL Example Application
 * Demonstrates basic 3D rendering with AurionGL
*/

#include "auriongl.h"

/* External AurionOS functions */
extern uint32_t *gpu_get_framebuffer(void);
extern uint32_t *gpu_get_backbuffer(void);
extern int gpu_get_width(void);
extern int gpu_get_height(void);
extern int gpu_flush(void);
extern uint32_t get_ticks(void);
extern uint16_t c_getkey_nonblock(void);
extern void c_puts(const char *s);

/* Demo state */
static int current_demo = 0;
static float rotation_angle = 0.0f;
static bool lighting_enabled = true;
static bool wireframe_mode = false;

/* DEMO 1: Spinning Cube */

void demo_spinning_cube(float time) {
    aglClearColor(0.1f, 0.1f, 0.2f, 1.0f);
    aglClear(AGL_COLOR_BUFFER_BIT | AGL_DEPTH_BUFFER_BIT);
    
    /* Setup projection */
    aglMatrixMode(AGL_PROJECTION);
    aglLoadIdentity();
    aglPerspective(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    
    /* Setup camera */
    aglMatrixMode(AGL_MODELVIEW);
    aglLoadIdentity();
    aglTranslate(0.0f, 0.0f, -5.0f);
    aglRotate(time * 50.0f, 1.0f, 1.0f, 0.0f);
    
    /* Draw cube */
    aglColor3f(1.0f, 0.5f, 0.2f);
    aglDrawCube();
}

/* DEMO 2: Multiple Objects */

void demo_multiple_objects(float time) {
    aglClearColor(0.05f, 0.05f, 0.1f, 1.0f);
    aglClear(AGL_COLOR_BUFFER_BIT | AGL_DEPTH_BUFFER_BIT);
    
    /* Setup projection */
    aglMatrixMode(AGL_PROJECTION);
    aglLoadIdentity();
    aglPerspective(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    
    /* Setup camera */
    aglMatrixMode(AGL_MODELVIEW);
    aglLoadIdentity();
    aglLookAt(0.0f, 2.0f, 8.0f,
              0.0f, 0.0f, 0.0f,
              0.0f, 1.0f, 0.0f);
    
    /* Rotating cube */
    aglPushMatrix();
    aglTranslate(-2.5f, 0.0f, 0.0f);
    aglRotate(time * 40.0f, 0.0f, 1.0f, 0.0f);
    aglColor3f(1.0f, 0.3f, 0.3f);
    aglDrawCube();
    aglPopMatrix();
    
    /* Sphere */
    aglPushMatrix();
    aglTranslate(0.0f, 0.0f, 0.0f);
    aglRotate(time * 20.0f, 1.0f, 0.0f, 0.0f);
    aglColor3f(0.3f, 0.3f, 1.0f);
    aglDrawSphere(0.7f, 20, 20);
    aglPopMatrix();
    
    /* Cylinder */
    aglPushMatrix();
    aglTranslate(2.5f, 0.0f, 0.0f);
    aglRotate(time * 30.0f, 1.0f, 1.0f, 0.0f);
    aglColor3f(0.3f, 1.0f, 0.3f);
    aglDrawCylinder(0.5f, 1.5f, 20);
    aglPopMatrix();
}

/* DEMO 3: Lighting Demo */

void demo_lighting(float time) {
    aglClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    aglClear(AGL_COLOR_BUFFER_BIT | AGL_DEPTH_BUFFER_BIT);
    
    /* Setup lighting */
    if (lighting_enabled) {
        aglEnable(AGL_LIGHTING);
        aglEnable(AGL_LIGHT0);
        
        /* Rotating light */
        float light_x = 5.0f * cosf(time * 0.5f);
        float light_z = 5.0f * sinf(time * 0.5f);
        float light_pos[] = {light_x, 3.0f, light_z, 1.0f};
        float light_diffuse[] = {1.0f, 1.0f, 1.0f, 1.0f};
        float light_ambient[] = {0.2f, 0.2f, 0.2f, 1.0f};
        
        aglLightfv(AGL_LIGHT0, AGL_POSITION, light_pos);
        aglLightfv(AGL_LIGHT0, AGL_DIFFUSE, light_diffuse);
        aglLightfv(AGL_LIGHT0, AGL_AMBIENT, light_ambient);
        
        /* Material properties */
        float mat_diffuse[] = {0.8f, 0.8f, 0.8f, 1.0f};
        float mat_specular[] = {1.0f, 1.0f, 1.0f, 1.0f};
        aglMaterialfv(AGL_FRONT, AGL_DIFFUSE, mat_diffuse);
        aglMaterialfv(AGL_FRONT, AGL_SPECULAR, mat_specular);
        aglMaterialf(AGL_FRONT, AGL_SHININESS, 32.0f);
    } else {
        aglDisable(AGL_LIGHTING);
    }
    
    /* Setup projection */
    aglMatrixMode(AGL_PROJECTION);
    aglLoadIdentity();
    aglPerspective(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    
    /* Setup camera */
    aglMatrixMode(AGL_MODELVIEW);
    aglLoadIdentity();
    aglLookAt(0.0f, 3.0f, 10.0f,
              0.0f, 0.0f, 0.0f,
              0.0f, 1.0f, 0.0f);
    
    /* Draw torus */
    aglPushMatrix();
    aglRotate(time * 20.0f, 1.0f, 0.5f, 0.0f);
    aglColor3f(0.8f, 0.6f, 0.2f);
    aglDrawTorus(0.5f, 1.5f, 30, 30);
    aglPopMatrix();
    
    /* Draw light position indicator */
    if (lighting_enabled) {
        aglDisable(AGL_LIGHTING);
        aglPushMatrix();
        float light_x = 5.0f * cosf(time * 0.5f);
        float light_z = 5.0f * sinf(time * 0.5f);
        aglTranslate(light_x, 3.0f, light_z);
        aglColor3f(1.0f, 1.0f, 0.0f);
        aglDrawSphere(0.2f, 10, 10);
        aglPopMatrix();
    }
}

/* DEMO 4: Solar System */

void demo_solar_system(float time) {
    aglClearColor(0.0f, 0.0f, 0.05f, 1.0f);
    aglClear(AGL_COLOR_BUFFER_BIT | AGL_DEPTH_BUFFER_BIT);
    
    /* Setup projection */
    aglMatrixMode(AGL_PROJECTION);
    aglLoadIdentity();
    aglPerspective(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    
    /* Setup camera */
    aglMatrixMode(AGL_MODELVIEW);
    aglLoadIdentity();
    aglLookAt(0.0f, 8.0f, 15.0f,
              0.0f, 0.0f, 0.0f,
              0.0f, 1.0f, 0.0f);
    
    /* Sun */
    aglPushMatrix();
    aglRotate(time * 10.0f, 0.0f, 1.0f, 0.0f);
    aglColor3f(1.0f, 1.0f, 0.0f);
    aglDrawSphere(1.0f, 20, 20);
    aglPopMatrix();
    
    /* Earth orbit */
    aglPushMatrix();
    aglRotate(time * 30.0f, 0.0f, 1.0f, 0.0f);
    aglTranslate(4.0f, 0.0f, 0.0f);
    aglRotate(time * 100.0f, 0.0f, 1.0f, 0.0f);
    aglColor3f(0.2f, 0.4f, 1.0f);
    aglDrawSphere(0.4f, 15, 15);
    
    /* Moon */
    aglPushMatrix();
    aglRotate(time * 200.0f, 0.0f, 1.0f, 0.0f);
    aglTranslate(0.8f, 0.0f, 0.0f);
    aglColor3f(0.7f, 0.7f, 0.7f);
    aglDrawSphere(0.15f, 10, 10);
    aglPopMatrix();
    
    aglPopMatrix();
    
    /* Mars orbit */
    aglPushMatrix();
    aglRotate(time * 20.0f, 0.0f, 1.0f, 0.0f);
    aglTranslate(6.5f, 0.0f, 0.0f);
    aglRotate(time * 90.0f, 0.0f, 1.0f, 0.0f);
    aglColor3f(1.0f, 0.3f, 0.1f);
    aglDrawSphere(0.3f, 15, 15);
    aglPopMatrix();
}

/* DEMO 5: Geometric Patterns */

void demo_geometric_patterns(float time) {
    aglClearColor(0.15f, 0.15f, 0.15f, 1.0f);
    aglClear(AGL_COLOR_BUFFER_BIT | AGL_DEPTH_BUFFER_BIT);
    
    /* Setup projection */
    aglMatrixMode(AGL_PROJECTION);
    aglLoadIdentity();
    aglPerspective(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    
    /* Setup camera */
    aglMatrixMode(AGL_MODELVIEW);
    aglLoadIdentity();
    aglLookAt(0.0f, 5.0f, 12.0f,
              0.0f, 0.0f, 0.0f,
              0.0f, 1.0f, 0.0f);
    
    /* Grid of rotating cubes */
    for (int x = -2; x <= 2; x++) {
        for (int z = -2; z <= 2; z++) {
            aglPushMatrix();
            aglTranslate(x * 2.0f, 0.0f, z * 2.0f);
            
            float phase = (x + z) * 0.5f;
            aglRotate(time * 50.0f + phase * 45.0f, 0.0f, 1.0f, 0.0f);
            
            float r = 0.5f + 0.5f * sinf(time + phase);
            float g = 0.5f + 0.5f * sinf(time + phase + 2.0f);
            float b = 0.5f + 0.5f * sinf(time + phase + 4.0f);
            aglColor3f(r, g, b);
            
            float scale = 0.3f + 0.2f * sinf(time * 2.0f + phase);
            aglScale(scale, scale, scale);
            
            aglDrawCube();
            aglPopMatrix();
        }
    }
}

/* Main Demo Loop */

void auriongl_demo_main(void) {
    c_puts("[AurionGL] Starting 3D demo...\n");
    
    /* Get framebuffer */
    uint32_t *fb = gpu_get_backbuffer();
    if (!fb) fb = gpu_get_framebuffer();
    
    int width = gpu_get_width();
    int height = gpu_get_height();
    
    if (width <= 0) width = 1920;
    if (height <= 0) height = 1080;
    
    /* Initialize AurionGL */
    aglInit(width, height, fb);
    
    /* Enable depth testing and backface culling */
    aglEnable(AGL_DEPTH_TEST);
    aglEnable(AGL_CULL_FACE);
    aglCullFace(AGL_BACK);
    aglShadeModel(AGL_SMOOTH);
    
    c_puts("[AurionGL] Initialized successfully\n");
    c_puts("[AurionGL] Controls:\n");
    c_puts("  1-5: Switch demos\n");
    c_puts("  L: Toggle lighting\n");
    c_puts("  Q: Quit\n\n");
    
    uint32_t start_time = get_ticks();
    bool running = true;
    
    while (running) {
        /* Calculate time */
        uint32_t current_time = get_ticks();
        float time = (current_time - start_time) / 100.0f;
        
        /* Handle input */
        uint16_t key = c_getkey_nonblock();
        if (key != 0) {
            char c = (char)(key & 0xFF);
            
            if (c == 'q' || c == 'Q' || c == 27) {
                running = false;
            } else if (c >= '1' && c <= '5') {
                current_demo = c - '1';
                c_puts("[AurionGL] Switched to demo ");
                c_puts(&c);
                c_puts("\n");
            } else if (c == 'l' || c == 'L') {
                lighting_enabled = !lighting_enabled;
                c_puts(lighting_enabled ? "[AurionGL] Lighting ON\n" : "[AurionGL] Lighting OFF\n");
            }
        }
        
        /* Render current demo */
        switch (current_demo) {
            case 0:
                demo_spinning_cube(time);
                break;
            case 1:
                demo_multiple_objects(time);
                break;
            case 2:
                demo_lighting(time);
                break;
            case 3:
                demo_solar_system(time);
                break;
            case 4:
                demo_geometric_patterns(time);
                break;
        }
        
        /* Flush to screen */
        aglFlush();
        gpu_flush();
        
        /* Simple frame limiter */
        while (get_ticks() - current_time < 2) {
            /* Wait */
        }
    }
    
    /* Cleanup */
    aglShutdown();
    c_puts("[AurionGL] Demo ended\n");
}

/* Simple Test Function */

void auriongl_simple_test(void) {
    c_puts("[AurionGL] Running simple test...\n");
    
    uint32_t *fb = gpu_get_backbuffer();
    if (!fb) fb = gpu_get_framebuffer();
    
    int width = gpu_get_width();
    int height = gpu_get_height();
    
    aglInit(width, height, fb);
    aglEnable(AGL_DEPTH_TEST);
    
    /* Clear screen */
    aglClearColor(0.2f, 0.2f, 0.3f, 1.0f);
    aglClear(AGL_COLOR_BUFFER_BIT | AGL_DEPTH_BUFFER_BIT);
    
    /* Setup matrices */
    aglMatrixMode(AGL_PROJECTION);
    aglLoadIdentity();
    aglPerspective(45.0f, (float)width / height, 0.1f, 100.0f);
    
    aglMatrixMode(AGL_MODELVIEW);
    aglLoadIdentity();
    aglTranslate(0.0f, 0.0f, -5.0f);
    aglRotate(30.0f, 1.0f, 1.0f, 0.0f);
    
    /* Draw a colored cube */
    aglColor3f(1.0f, 0.5f, 0.2f);
    aglDrawCube();
    
    /* Flush */
    aglFlush();
    gpu_flush();
    
    aglShutdown();
    c_puts("[AurionGL] Test complete\n");
}
