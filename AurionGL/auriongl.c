/*
 * AurionGL - 3D Graphics Library Implementation
 * Software rasterizer with perspective-correct rendering
*/

#include "auriongl.h"
#include <string.h>

/* OS memory functions */
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);

/* INTERNAL FORWARD DECLARATIONS */

static void agl_rasterize_primitives(void);

/* MATH FUNCTIONS */

static float agl_sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    float guess = x;
    for (int i = 0; i < 10; i++) {
        guess = (guess + x / guess) * 0.5f;
    }
    return guess;
}

float agl_sinf(float x) {
    while (x > 3.14159265f) x -= 6.28318531f;
    while (x < -3.14159265f) x += 6.28318531f;
    
    // Using a simple Taylor expansion for better speed in software
    float x2 = x * x;
    return x * (1.0f - x2 * (1.66666666e-1f - x2 * (8.33333333e-3f - x2 * 1.98412698e-4f)));
}

float agl_cosf(float x) {
    return agl_sinf(x + 1.57079632f);
}

static float agl_tanf(float x) {
    float c = agl_cosf(x);
    return (c != 0.0f) ? (agl_sinf(x) / c) : 0.0f;
}

static float agl_floorf(float x) {
    return (float)((int)x - (x < 0.0f && x != (int)x));
}

static float agl_ceilf(float x) {
    return (float)((int)x + (x > 0.0f && x != (int)x));
}

static float agl_powf(float base, float exp) {
    if (base <= 0.0f) return 0.0f;
    if (exp == 0.0f) return 1.0f;
    if (exp == 1.0f) return base;
    if (exp == 2.0f) return base * base;
    float result = 1.0f;
    int e = (int)exp;
    for (int i = 0; i < e; i++) result *= base;
    return result;
}

static float agl_expf(float x) {
    if (x == 0.0f) return 1.0f;
    float result = 1.0f;
    float term = 1.0f;
    for (int i = 1; i < 20; i++) {
        term *= x / i;
        result += term;
    }
    return result;
}

static float agl_fabsf(float x) {
    return (x < 0.0f) ? -x : x;
}

static int agl_abs(int x) {
    return x < 0 ? -x : x;
}

static float agl_clampf(float x, float min, float max) {
    if (x < min) return min;
    if (x > max) return max;
    return x;
}

/* INTERNAL CONSTANTS */

#define MAX_VERTICES        10000
#define MAX_MATRIX_STACK    32
#define MAX_TEXTURES        256
#define MAX_LIGHTS          4
#define PI                  3.14159265358979323846f
#define DEG_TO_RAD(x)       ((x) * PI / 180.0f)
#define RAD_TO_DEG(x)       ((x) * 180.0f / PI)

/* INTERNAL STATE */

typedef struct {
    /* Framebuffer */
    uint32_t *framebuffer;
    float *depth_buffer;
    int fb_width;
    int fb_height;
    
    /* Viewport */
    int vp_x, vp_y;
    int vp_width, vp_height;
    
    /* Clear values */
    agl_color_t clear_color;
    float clear_depth;
    
    /* State flags */
    bool depth_test_enabled;
    bool cull_face_enabled;
    bool lighting_enabled;
    bool texture_2d_enabled;
    bool blend_enabled;
    bool fog_enabled;
    
    /* Depth function */
    uint32_t depth_func;
    
    /* Culling mode */
    uint32_t cull_face_mode;
    
    /* Shading model */
    uint32_t shade_model;
    
    /* Blend factors */
    uint32_t blend_src;
    uint32_t blend_dst;
    
    /* Current color */
    agl_color_t current_color;
    
    /* Current normal */
    agl_vec3_t current_normal;
    
    /* Current texture coordinate */
    agl_vec2_t current_texcoord;
    
    /* Matrix stacks */
    uint32_t matrix_mode;
    agl_mat4_t modelview_stack[MAX_MATRIX_STACK];
    int modelview_stack_top;
    agl_mat4_t projection_stack[MAX_MATRIX_STACK];
    int projection_stack_top;
    agl_mat4_t texture_stack[MAX_MATRIX_STACK];
    int texture_stack_top;
    
    /* Current matrices */
    agl_mat4_t modelview_matrix;
    agl_mat4_t projection_matrix;
    agl_mat4_t texture_matrix;
    
    /* Combined MVP matrix */
    agl_mat4_t mvp_matrix;
    
    /* Lights */
    agl_light_t lights[MAX_LIGHTS];
    
    /* Material */
    agl_material_t material;
    
    /* Textures */
    agl_texture_t textures[MAX_TEXTURES];
    uint32_t current_texture;
    uint32_t next_texture_id;
    
    /* Immediate mode state */
    bool in_begin_end;
    uint32_t current_primitive;
    agl_vertex_t vertex_buffer[MAX_VERTICES];
    int vertex_count;
    
    /* Error state */
    uint32_t last_error;
    
    /* Fog parameters */
    agl_color_t fog_color;
    float fog_density;
    float fog_start;
    float fog_end;
    uint32_t fog_mode;
    
} agl_state_t;

static agl_state_t *g_state = NULL;

/* VECTOR OPERATIONS */

agl_vec3_t agl_vec3_add(agl_vec3_t a, agl_vec3_t b) {
    agl_vec3_t result;
    result.x = a.x + b.x;
    result.y = a.y + b.y;
    result.z = a.z + b.z;
    return result;
}

agl_vec3_t agl_vec3_sub(agl_vec3_t a, agl_vec3_t b) {
    agl_vec3_t result;
    result.x = a.x - b.x;
    result.y = a.y - b.y;
    result.z = a.z - b.z;
    return result;
}

agl_vec3_t agl_vec3_scale(agl_vec3_t v, float s) {
    agl_vec3_t result;
    result.x = v.x * s;
    result.y = v.y * s;
    result.z = v.z * s;
    return result;
}

float agl_vec3_dot(agl_vec3_t a, agl_vec3_t b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

agl_vec3_t agl_vec3_cross(agl_vec3_t a, agl_vec3_t b) {
    agl_vec3_t result;
    result.x = a.y * b.z - a.z * b.y;
    result.y = a.z * b.x - a.x * b.z;
    result.z = a.x * b.y - a.y * b.x;
    return result;
}

float agl_vec3_length(agl_vec3_t v) {
    return agl_sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

agl_vec3_t agl_vec3_normalize(agl_vec3_t v) {
    float len = agl_vec3_length(v);
    if (len > 0.0001f) {
        return agl_vec3_scale(v, 1.0f / len);
    }
    agl_vec3_t zero = {0.0f, 0.0f, 0.0f};
    return zero;
}

static agl_vec4_t agl_vec4_add(agl_vec4_t a, agl_vec4_t b) {
    agl_vec4_t result;
    result.x = a.x + b.x;
    result.y = a.y + b.y;
    result.z = a.z + b.z;
    result.w = a.w + b.w;
    return result;
}

static agl_vec4_t agl_vec4_scale(agl_vec4_t v, float s) {
    agl_vec4_t result;
    result.x = v.x * s;
    result.y = v.y * s;
    result.z = v.z * s;
    result.w = v.w * s;
    return result;
}

static float agl_vec4_length(agl_vec4_t v) {
    return agl_sqrtf(v.x * v.x + v.y * v.y + v.z * v.z + v.w * v.w);
}

static agl_vec4_t agl_vec4_normalize(agl_vec4_t v) {
    float len = agl_vec4_length(v);
    if (len > 0.0001f) {
        return agl_vec4_scale(v, 1.0f / len);
    }
    agl_vec4_t zero = {0.0f, 0.0f, 0.0f, 0.0f};
    return zero;
}

/* MATRIX OPERATIONS */

agl_mat4_t agl_mat4_identity(void) {
    agl_mat4_t m;
    m.m[0] = 1.0f; m.m[4] = 0.0f; m.m[8]  = 0.0f; m.m[12] = 0.0f;
    m.m[1] = 0.0f; m.m[5] = 1.0f; m.m[9]  = 0.0f; m.m[13] = 0.0f;
    m.m[2] = 0.0f; m.m[6] = 0.0f; m.m[10] = 1.0f; m.m[14] = 0.0f;
    m.m[3] = 0.0f; m.m[7] = 0.0f; m.m[11] = 0.0f; m.m[15] = 1.0f;
    return m;
}

agl_mat4_t agl_mat4_multiply(agl_mat4_t a, agl_mat4_t b) {
    agl_mat4_t result;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.m[j * 4 + i] = 
                a.m[i] * b.m[j * 4] +
                a.m[i + 4] * b.m[j * 4 + 1] +
                a.m[i + 8] * b.m[j * 4 + 2] +
                a.m[i + 12] * b.m[j * 4 + 3];
        }
    }
    return result;
}

agl_vec4_t agl_mat4_mul_vec4(agl_mat4_t m, agl_vec4_t v) {
    agl_vec4_t result;
    result.x = m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12] * v.w;
    result.y = m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13] * v.w;
    result.z = m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] * v.w;
    result.w = m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15] * v.w;
    return result;
}

static agl_vec3_t agl_mat4_mul_vec3_point(agl_mat4_t m, agl_vec3_t v) {
    agl_vec4_t v4 = {v.x, v.y, v.z, 1.0f};
    agl_vec4_t result = agl_mat4_mul_vec4(m, v4);
    agl_vec3_t r3 = {result.x, result.y, result.z};
    return r3;
}

static agl_vec3_t agl_mat4_mul_vec3_vector(agl_mat4_t m, agl_vec3_t v) {
    agl_vec4_t v4 = {v.x, v.y, v.z, 0.0f};
    agl_vec4_t result = agl_mat4_mul_vec4(m, v4);
    agl_vec3_t r3 = {result.x, result.y, result.z};
    return r3;
}

static agl_mat4_t agl_mat4_translation(float x, float y, float z) {
    agl_mat4_t m = agl_mat4_identity();
    m.m[12] = x;
    m.m[13] = y;
    m.m[14] = z;
    return m;
}

static agl_mat4_t agl_mat4_rotation_x(float angle) {
    float c = agl_cosf(angle);
    float s = agl_sinf(angle);
    agl_mat4_t m = agl_mat4_identity();
    m.m[5] = c;  m.m[9]  = -s;
    m.m[6] = s;  m.m[10] = c;
    return m;
}

static agl_mat4_t agl_mat4_rotation_y(float angle) {
    float c = agl_cosf(angle);
    float s = agl_sinf(angle);
    agl_mat4_t m = agl_mat4_identity();
    m.m[0] = c;  m.m[8]  = s;
    m.m[2] = -s; m.m[10] = c;
    return m;
}

static agl_mat4_t agl_mat4_rotation_z(float angle) {
    float c = agl_cosf(angle);
    float s = agl_sinf(angle);
    agl_mat4_t m = agl_mat4_identity();
    m.m[0] = c;  m.m[4] = -s;
    m.m[1] = s;  m.m[5] = c;
    return m;
}

static agl_mat4_t agl_mat4_rotation(float angle, float x, float y, float z) {
    float len = agl_sqrtf(x * x + y * y + z * z);
    if (len < 0.0001f) return agl_mat4_identity();
    x /= len;
    y /= len;
    z /= len;
    
    float c = agl_cosf(angle);
    float s = agl_sinf(angle);
    float t = 1.0f - c;
    
    agl_mat4_t m;
    m.m[0] = t * x * x + c;
    m.m[4] = t * x * y - s * z;
    m.m[8] = t * x * z + s * y;
    m.m[12] = 0.0f;
    
    m.m[1] = t * x * y + s * z;
    m.m[5] = t * y * y + c;
    m.m[9] = t * y * z - s * x;
    m.m[13] = 0.0f;
    
    m.m[2] = t * x * z - s * y;
    m.m[6] = t * y * z + s * x;
    m.m[10] = t * z * z + c;
    m.m[14] = 0.0f;
    
    m.m[3] = 0.0f;
    m.m[7] = 0.0f;
    m.m[11] = 0.0f;
    m.m[15] = 1.0f;
    
    return m;
}

static agl_mat4_t agl_mat4_scale(float x, float y, float z) {
    agl_mat4_t m = agl_mat4_identity();
    m.m[0] = x;
    m.m[5] = y;
    m.m[10] = z;
    return m;
}

static agl_mat4_t agl_mat4_perspective(float fovy, float aspect, float near, float far) {
    float f = 1.0f / agl_tanf(fovy * 0.5f);
    agl_mat4_t m;
    m.m[0] = f / aspect;
    m.m[4] = 0.0f;
    m.m[8] = 0.0f;
    m.m[12] = 0.0f;
    
    m.m[1] = 0.0f;
    m.m[5] = f;
    m.m[9] = 0.0f;
    m.m[13] = 0.0f;
    
    m.m[2] = 0.0f;
    m.m[6] = 0.0f;
    m.m[10] = (far + near) / (near - far);
    m.m[14] = (2.0f * far * near) / (near - far);
    
    m.m[3] = 0.0f;
    m.m[7] = 0.0f;
    m.m[11] = -1.0f;
    m.m[15] = 0.0f;
    
    return m;
}

static agl_mat4_t agl_mat4_ortho(float left, float right, float bottom, float top, float near, float far) {
    float tx = -(right + left) / (right - left);
    float ty = -(top + bottom) / (top - bottom);
    float tz = -(far + near) / (far - near);
    
    agl_mat4_t m;
    m.m[0] = 2.0f / (right - left);
    m.m[4] = 0.0f;
    m.m[8] = 0.0f;
    m.m[12] = tx;
    
    m.m[1] = 0.0f;
    m.m[5] = 2.0f / (top - bottom);
    m.m[9] = 0.0f;
    m.m[13] = ty;
    
    m.m[2] = 0.0f;
    m.m[6] = 0.0f;
    m.m[10] = -2.0f / (far - near);
    m.m[14] = tz;
    
    m.m[3] = 0.0f;
    m.m[7] = 0.0f;
    m.m[11] = 0.0f;
    m.m[15] = 1.0f;
    
    return m;
}

static agl_mat4_t agl_mat4_frustum(float left, float right, float bottom, float top, float near, float far) {
    agl_mat4_t m;
    m.m[0] = (2.0f * near) / (right - left);
    m.m[4] = 0.0f;
    m.m[8] = (right + left) / (right - left);
    m.m[12] = 0.0f;
    
    m.m[1] = 0.0f;
    m.m[5] = (2.0f * near) / (top - bottom);
    m.m[9] = (top + bottom) / (top - bottom);
    m.m[13] = 0.0f;
    
    m.m[2] = 0.0f;
    m.m[6] = 0.0f;
    m.m[10] = -(far + near) / (far - near);
    m.m[14] = -(2.0f * far * near) / (far - near);
    
    m.m[3] = 0.0f;
    m.m[7] = 0.0f;
    m.m[11] = -1.0f;
    m.m[15] = 0.0f;
    
    return m;
}

static agl_mat4_t agl_mat4_lookat(float eyeX, float eyeY, float eyeZ,
                                   float centerX, float centerY, float centerZ,
                                   float upX, float upY, float upZ) {
    agl_vec3_t eye = {eyeX, eyeY, eyeZ};
    agl_vec3_t center = {centerX, centerY, centerZ};
    agl_vec3_t up = {upX, upY, upZ};
    
    agl_vec3_t f = agl_vec3_normalize(agl_vec3_sub(center, eye));
    agl_vec3_t s = agl_vec3_normalize(agl_vec3_cross(f, up));
    agl_vec3_t u = agl_vec3_cross(s, f);
    
    agl_mat4_t m;
    m.m[0] = s.x;  m.m[4] = s.y;  m.m[8]  = s.z;  m.m[12] = -agl_vec3_dot(s, eye);
    m.m[1] = u.x;  m.m[5] = u.y;  m.m[9]  = u.z;  m.m[13] = -agl_vec3_dot(u, eye);
    m.m[2] = -f.x; m.m[6] = -f.y; m.m[10] = -f.z; m.m[14] = agl_vec3_dot(f, eye);
    m.m[3] = 0.0f; m.m[7] = 0.0f; m.m[11] = 0.0f; m.m[15] = 1.0f;
    
    return m;
}

static void agl_update_mvp_matrix(void) {
    g_state->mvp_matrix = agl_mat4_multiply(g_state->projection_matrix, g_state->modelview_matrix);
}

/* INITIALIZATION AND CONTEXT */

void aglInit(int width, int height, uint32_t *framebuffer) {
    if (g_state != NULL) {
        kfree(g_state);
    }
    
    g_state = (agl_state_t *)kmalloc(sizeof(agl_state_t));
    if (g_state == NULL) return;
    
    /* Clear state */
    memset(g_state, 0, sizeof(agl_state_t));
    
    /* Setup framebuffer */
    g_state->framebuffer = framebuffer;
    g_state->fb_width = width;
    g_state->fb_height = height;
    
    /* Allocate depth buffer */
    g_state->depth_buffer = (float *)kmalloc(width * height * sizeof(float));
    if (g_state->depth_buffer != NULL) {
        for (int i = 0; i < width * height; i++) {
            g_state->depth_buffer[i] = 1.0f;
        }
    }
    
    /* Setup viewport */
    g_state->vp_x = 0;
    g_state->vp_y = 0;
    g_state->vp_width = width;
    g_state->vp_height = height;
    
    /* Default clear values */
    g_state->clear_color.r = 0.0f;
    g_state->clear_color.g = 0.0f;
    g_state->clear_color.b = 0.0f;
    g_state->clear_color.a = 1.0f;
    g_state->clear_depth = 1.0f;
    
    /* Default state */
    g_state->depth_test_enabled = false;
    g_state->cull_face_enabled = false;
    g_state->lighting_enabled = false;
    g_state->texture_2d_enabled = false;
    g_state->blend_enabled = false;
    g_state->fog_enabled = false;
    
    g_state->depth_func = AGL_LESS;
    g_state->cull_face_mode = AGL_BACK;
    g_state->shade_model = AGL_SMOOTH;
    g_state->blend_src = AGL_SRC_ALPHA;
    g_state->blend_dst = AGL_ONE_MINUS_SRC_ALPHA;
    
    /* Default color */
    g_state->current_color.r = 1.0f;
    g_state->current_color.g = 1.0f;
    g_state->current_color.b = 1.0f;
    g_state->current_color.a = 1.0f;
    
    /* Default normal */
    g_state->current_normal.x = 0.0f;
    g_state->current_normal.y = 0.0f;
    g_state->current_normal.z = 1.0f;
    
    /* Default texcoord */
    g_state->current_texcoord.u = 0.0f;
    g_state->current_texcoord.v = 0.0f;
    
    /* Matrix stacks */
    g_state->matrix_mode = AGL_MODELVIEW;
    g_state->modelview_stack_top = 0;
    g_state->projection_stack_top = 0;
    g_state->texture_stack_top = 0;
    
    g_state->modelview_matrix = agl_mat4_identity();
    g_state->projection_matrix = agl_mat4_identity();
    g_state->texture_matrix = agl_mat4_identity();
    g_state->mvp_matrix = agl_mat4_identity();
    
    /* Initialize lights */
    for (int i = 0; i < MAX_LIGHTS; i++) {
        g_state->lights[i].enabled = false;
        g_state->lights[i].position.x = 0.0f;
        g_state->lights[i].position.y = 0.0f;
        g_state->lights[i].position.z = 1.0f;
        g_state->lights[i].position.w = 0.0f;
        g_state->lights[i].ambient.r = 0.0f;
        g_state->lights[i].ambient.g = 0.0f;
        g_state->lights[i].ambient.b = 0.0f;
        g_state->lights[i].ambient.a = 1.0f;
        g_state->lights[i].diffuse.r = 0.0f;
        g_state->lights[i].diffuse.g = 0.0f;
        g_state->lights[i].diffuse.b = 0.0f;
        g_state->lights[i].diffuse.a = 1.0f;
        g_state->lights[i].specular.r = 0.0f;
        g_state->lights[i].specular.g = 0.0f;
        g_state->lights[i].specular.b = 0.0f;
        g_state->lights[i].specular.a = 1.0f;
    }
    
    /* Default material */
    g_state->material.ambient.r = 0.2f;
    g_state->material.ambient.g = 0.2f;
    g_state->material.ambient.b = 0.2f;
    g_state->material.ambient.a = 1.0f;
    g_state->material.diffuse.r = 0.8f;
    g_state->material.diffuse.g = 0.8f;
    g_state->material.diffuse.b = 0.8f;
    g_state->material.diffuse.a = 1.0f;
    g_state->material.specular.r = 0.0f;
    g_state->material.specular.g = 0.0f;
    g_state->material.specular.b = 0.0f;
    g_state->material.specular.a = 1.0f;
    g_state->material.emission.r = 0.0f;
    g_state->material.emission.g = 0.0f;
    g_state->material.emission.b = 0.0f;
    g_state->material.emission.a = 1.0f;
    g_state->material.shininess = 0.0f;
    
    /* Initialize textures */
    for (int i = 0; i < MAX_TEXTURES; i++) {
        g_state->textures[i].id = 0;
        g_state->textures[i].width = 0;
        g_state->textures[i].height = 0;
        g_state->textures[i].format = 0;
        g_state->textures[i].data = NULL;
        g_state->textures[i].mag_filter = AGL_LINEAR;
        g_state->textures[i].min_filter = AGL_LINEAR;
        g_state->textures[i].wrap_s = AGL_REPEAT;
        g_state->textures[i].wrap_t = AGL_REPEAT;
    }
    g_state->current_texture = 0;
    g_state->next_texture_id = 1;
    
    /* Immediate mode state */
    g_state->in_begin_end = false;
    g_state->current_primitive = 0;
    g_state->vertex_count = 0;
    
    /* Error state */
    g_state->last_error = AGL_NO_ERROR;
    
    /* Fog parameters */
    g_state->fog_color.r = 0.0f;
    g_state->fog_color.g = 0.0f;
    g_state->fog_color.b = 0.0f;
    g_state->fog_color.a = 0.0f;
    g_state->fog_density = 1.0f;
    g_state->fog_start = 0.0f;
    g_state->fog_end = 1.0f;
    g_state->fog_mode = AGL_EXP;
}

void aglSetFramebuffer(uint32_t *framebuffer) {
    if (g_state != NULL) {
        g_state->framebuffer = framebuffer;
    }
}

void aglGetViewport(int *x, int *y, int *width, int *height) {
    if (g_state != NULL) {
        if (x != NULL) *x = g_state->vp_x;
        if (y != NULL) *y = g_state->vp_y;
        if (width != NULL) *width = g_state->vp_width;
        if (height != NULL) *height = g_state->vp_height;
    }
}

void aglViewport(int x, int y, int width, int height) {
    if (g_state != NULL) {
        g_state->vp_x = x;
        g_state->vp_y = y;
        g_state->vp_width = width;
        g_state->vp_height = height;
    }
}

void aglShutdown(void) {
    if (g_state != NULL) {
        if (g_state->depth_buffer != NULL) {
            kfree(g_state->depth_buffer);
        }
        for (int i = 0; i < MAX_TEXTURES; i++) {
            if (g_state->textures[i].data != NULL) {
                kfree(g_state->textures[i].data);
            }
        }
        kfree(g_state);
        g_state = NULL;
    }
}

/* STATE MANAGEMENT */

void aglEnable(uint32_t cap) {
    if (g_state == NULL) return;
    switch (cap) {
        case AGL_DEPTH_TEST:
            g_state->depth_test_enabled = true;
            break;
        case AGL_CULL_FACE:
            g_state->cull_face_enabled = true;
            break;
        case AGL_LIGHTING:
            g_state->lighting_enabled = true;
            break;
        case AGL_TEXTURE_2D:
            g_state->texture_2d_enabled = true;
            break;
        case AGL_BLEND:
            g_state->blend_enabled = true;
            break;
        case AGL_FOG:
            g_state->fog_enabled = true;
            break;
        default:
            g_state->last_error = AGL_INVALID_ENUM;
            break;
    }
}

void aglDisable(uint32_t cap) {
    if (g_state == NULL) return;
    switch (cap) {
        case AGL_DEPTH_TEST:
            g_state->depth_test_enabled = false;
            break;
        case AGL_CULL_FACE:
            g_state->cull_face_enabled = false;
            break;
        case AGL_LIGHTING:
            g_state->lighting_enabled = false;
            break;
        case AGL_TEXTURE_2D:
            g_state->texture_2d_enabled = false;
            break;
        case AGL_BLEND:
            g_state->blend_enabled = false;
            break;
        case AGL_FOG:
            g_state->fog_enabled = false;
            break;
        default:
            g_state->last_error = AGL_INVALID_ENUM;
            break;
    }
}

bool aglIsEnabled(uint32_t cap) {
    if (g_state == NULL) return false;
    switch (cap) {
        case AGL_DEPTH_TEST:
            return g_state->depth_test_enabled;
        case AGL_CULL_FACE:
            return g_state->cull_face_enabled;
        case AGL_LIGHTING:
            return g_state->lighting_enabled;
        case AGL_TEXTURE_2D:
            return g_state->texture_2d_enabled;
        case AGL_BLEND:
            return g_state->blend_enabled;
        case AGL_FOG:
            return g_state->fog_enabled;
        default:
            g_state->last_error = AGL_INVALID_ENUM;
            return false;
    }
}

void aglClearColor(float r, float g, float b, float a) {
    if (g_state != NULL) {
        g_state->clear_color.r = agl_clampf(r, 0.0f, 1.0f);
        g_state->clear_color.g = agl_clampf(g, 0.0f, 1.0f);
        g_state->clear_color.b = agl_clampf(b, 0.0f, 1.0f);
        g_state->clear_color.a = agl_clampf(a, 0.0f, 1.0f);
    }
}

void aglClearDepth(float depth) {
    if (g_state != NULL) {
        g_state->clear_depth = depth;
    }
}

void aglClear(uint32_t mask) {
    if (g_state == NULL) return;
    
    if ((mask & AGL_COLOR_BUFFER_BIT) && g_state->framebuffer != NULL) {
        /* Framebuffer format: ARGB (0xAARRGGBB) - Alpha<<24 | Red<<16 | Green<<8 | Blue */
        uint32_t color = ((uint32_t)(g_state->clear_color.a * 255.0f) << 24) |
                         ((uint32_t)(g_state->clear_color.r * 255.0f) << 16) |
                         ((uint32_t)(g_state->clear_color.g * 255.0f) << 8) |
                         ((uint32_t)(g_state->clear_color.b * 255.0f));
        int x0 = g_state->vp_x;
        int y0 = g_state->vp_y;
        int x1 = g_state->vp_x + g_state->vp_width;
        int y1 = g_state->vp_y + g_state->vp_height;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > g_state->fb_width) x1 = g_state->fb_width;
        if (y1 > g_state->fb_height) y1 = g_state->fb_height;

        for (int y = y0; y < y1; y++) {
            uint32_t *row = g_state->framebuffer + y * g_state->fb_width + x0;
            for (int x = x0; x < x1; x++) {
                *row++ = color;
            }
        }
    }
    
    if ((mask & AGL_DEPTH_BUFFER_BIT) && g_state->depth_buffer != NULL) {
        int x0 = g_state->vp_x;
        int y0 = g_state->vp_y;
        int x1 = g_state->vp_x + g_state->vp_width;
        int y1 = g_state->vp_y + g_state->vp_height;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > g_state->fb_width) x1 = g_state->fb_width;
        if (y1 > g_state->fb_height) y1 = g_state->fb_height;

        for (int y = y0; y < y1; y++) {
            float *row = g_state->depth_buffer + y * g_state->fb_width + x0;
            for (int x = x0; x < x1; x++) {
                *row++ = g_state->clear_depth;
            }
        }
    }
}

void aglDepthFunc(uint32_t func) {
    if (g_state == NULL) return;
    switch (func) {
        case AGL_NEVER:
        case AGL_LESS:
        case AGL_EQUAL:
        case AGL_LEQUAL:
        case AGL_GREATER:
        case AGL_NOTEQUAL:
        case AGL_GEQUAL:
        case AGL_ALWAYS:
            g_state->depth_func = func;
            break;
        default:
            g_state->last_error = AGL_INVALID_ENUM;
            break;
    }
}

void aglCullFace(uint32_t mode) {
    if (g_state == NULL) return;
    switch (mode) {
        case AGL_FRONT:
        case AGL_BACK:
        case AGL_FRONT_AND_BACK:
            g_state->cull_face_mode = mode;
            break;
        default:
            g_state->last_error = AGL_INVALID_ENUM;
            break;
    }
}

void aglShadeModel(uint32_t mode) {
    if (g_state == NULL) return;
    switch (mode) {
        case AGL_FLAT:
        case AGL_SMOOTH:
            g_state->shade_model = mode;
            break;
        default:
            g_state->last_error = AGL_INVALID_ENUM;
            break;
    }
}

void aglBlendFunc(uint32_t sfactor, uint32_t dfactor) {
    if (g_state == NULL) return;
    
    bool src_valid = false;
    bool dst_valid = false;
    
    switch (sfactor) {
        case AGL_ZERO:
        case AGL_ONE:
        case AGL_SRC_COLOR:
        case AGL_ONE_MINUS_SRC_COLOR:
        case AGL_SRC_ALPHA:
        case AGL_ONE_MINUS_SRC_ALPHA:
        case AGL_DST_ALPHA:
        case AGL_ONE_MINUS_DST_ALPHA:
        case AGL_DST_COLOR:
        case AGL_ONE_MINUS_DST_COLOR:
            src_valid = true;
            break;
        default:
            break;
    }
    
    switch (dfactor) {
        case AGL_ZERO:
        case AGL_ONE:
        case AGL_SRC_COLOR:
        case AGL_ONE_MINUS_SRC_COLOR:
        case AGL_SRC_ALPHA:
        case AGL_ONE_MINUS_SRC_ALPHA:
        case AGL_DST_ALPHA:
        case AGL_ONE_MINUS_DST_ALPHA:
        case AGL_DST_COLOR:
        case AGL_ONE_MINUS_DST_COLOR:
            dst_valid = true;
            break;
        default:
            break;
    }
    
    if (src_valid && dst_valid) {
        g_state->blend_src = sfactor;
        g_state->blend_dst = dfactor;
    } else {
        g_state->last_error = AGL_INVALID_ENUM;
    }
}

uint32_t aglGetError(void) {
    if (g_state == NULL) return AGL_INVALID_OPERATION;
    uint32_t error = g_state->last_error;
    g_state->last_error = AGL_NO_ERROR;
    return error;
}

/* MATRIX OPERATIONS (PUBLIC API) */

void aglMatrixMode(uint32_t mode) {
    if (g_state == NULL) return;
    switch (mode) {
        case AGL_MODELVIEW:
        case AGL_PROJECTION:
        case AGL_TEXTURE:
            g_state->matrix_mode = mode;
            break;
        default:
            g_state->last_error = AGL_INVALID_ENUM;
            break;
    }
}

void aglLoadIdentity(void) {
    if (g_state == NULL) return;
    agl_mat4_t identity = agl_mat4_identity();
    switch (g_state->matrix_mode) {
        case AGL_MODELVIEW:
            g_state->modelview_matrix = identity;
            break;
        case AGL_PROJECTION:
            g_state->projection_matrix = identity;
            break;
        case AGL_TEXTURE:
            g_state->texture_matrix = identity;
            break;
    }
    agl_update_mvp_matrix();
}

void aglPushMatrix(void) {
    if (g_state == NULL) return;
    switch (g_state->matrix_mode) {
        case AGL_MODELVIEW:
            if (g_state->modelview_stack_top < MAX_MATRIX_STACK - 1) {
                g_state->modelview_stack[g_state->modelview_stack_top++] = g_state->modelview_matrix;
            } else {
                g_state->last_error = AGL_STACK_OVERFLOW;
            }
            break;
        case AGL_PROJECTION:
            if (g_state->projection_stack_top < MAX_MATRIX_STACK - 1) {
                g_state->projection_stack[g_state->projection_stack_top++] = g_state->projection_matrix;
            } else {
                g_state->last_error = AGL_STACK_OVERFLOW;
            }
            break;
        case AGL_TEXTURE:
            if (g_state->texture_stack_top < MAX_MATRIX_STACK - 1) {
                g_state->texture_stack[g_state->texture_stack_top++] = g_state->texture_matrix;
            } else {
                g_state->last_error = AGL_STACK_OVERFLOW;
            }
            break;
    }
}

void aglPopMatrix(void) {
    if (g_state == NULL) return;
    switch (g_state->matrix_mode) {
        case AGL_MODELVIEW:
            if (g_state->modelview_stack_top > 0) {
                g_state->modelview_matrix = g_state->modelview_stack[--g_state->modelview_stack_top];
            } else {
                g_state->last_error = AGL_STACK_UNDERFLOW;
            }
            break;
        case AGL_PROJECTION:
            if (g_state->projection_stack_top > 0) {
                g_state->projection_matrix = g_state->projection_stack[--g_state->projection_stack_top];
            } else {
                g_state->last_error = AGL_STACK_UNDERFLOW;
            }
            break;
        case AGL_TEXTURE:
            if (g_state->texture_stack_top > 0) {
                g_state->texture_matrix = g_state->texture_stack[--g_state->texture_stack_top];
            } else {
                g_state->last_error = AGL_STACK_UNDERFLOW;
            }
            break;
    }
    agl_update_mvp_matrix();
}

void aglLoadMatrix(const float *m) {
    if (g_state == NULL || m == NULL) return;
    agl_mat4_t mat;
    for (int i = 0; i < 16; i++) {
        mat.m[i] = m[i];
    }
    switch (g_state->matrix_mode) {
        case AGL_MODELVIEW:
            g_state->modelview_matrix = mat;
            break;
        case AGL_PROJECTION:
            g_state->projection_matrix = mat;
            break;
        case AGL_TEXTURE:
            g_state->texture_matrix = mat;
            break;
    }
    agl_update_mvp_matrix();
}

void aglMultMatrix(const float *m) {
    if (g_state == NULL || m == NULL) return;
    agl_mat4_t mat;
    for (int i = 0; i < 16; i++) {
        mat.m[i] = m[i];
    }
    switch (g_state->matrix_mode) {
        case AGL_MODELVIEW:
            g_state->modelview_matrix = agl_mat4_multiply(g_state->modelview_matrix, mat);
            break;
        case AGL_PROJECTION:
            g_state->projection_matrix = agl_mat4_multiply(g_state->projection_matrix, mat);
            break;
        case AGL_TEXTURE:
            g_state->texture_matrix = agl_mat4_multiply(g_state->texture_matrix, mat);
            break;
    }
    agl_update_mvp_matrix();
}

void aglTranslate(float x, float y, float z) {
    if (g_state == NULL) return;
    agl_mat4_t trans = agl_mat4_translation(x, y, z);
    switch (g_state->matrix_mode) {
        case AGL_MODELVIEW:
            g_state->modelview_matrix = agl_mat4_multiply(g_state->modelview_matrix, trans);
            break;
        case AGL_PROJECTION:
            g_state->projection_matrix = agl_mat4_multiply(g_state->projection_matrix, trans);
            break;
        case AGL_TEXTURE:
            g_state->texture_matrix = agl_mat4_multiply(g_state->texture_matrix, trans);
            break;
    }
    agl_update_mvp_matrix();
}

void aglRotate(float angle, float x, float y, float z) {
    if (g_state == NULL) return;
    float rad = DEG_TO_RAD(angle);
    agl_mat4_t rot = agl_mat4_rotation(rad, x, y, z);
    switch (g_state->matrix_mode) {
        case AGL_MODELVIEW:
            g_state->modelview_matrix = agl_mat4_multiply(g_state->modelview_matrix, rot);
            break;
        case AGL_PROJECTION:
            g_state->projection_matrix = agl_mat4_multiply(g_state->projection_matrix, rot);
            break;
        case AGL_TEXTURE:
            g_state->texture_matrix = agl_mat4_multiply(g_state->texture_matrix, rot);
            break;
    }
    agl_update_mvp_matrix();
}

void aglScale(float x, float y, float z) {
    if (g_state == NULL) return;
    agl_mat4_t scale = agl_mat4_scale(x, y, z);
    switch (g_state->matrix_mode) {
        case AGL_MODELVIEW:
            g_state->modelview_matrix = agl_mat4_multiply(g_state->modelview_matrix, scale);
            break;
        case AGL_PROJECTION:
            g_state->projection_matrix = agl_mat4_multiply(g_state->projection_matrix, scale);
            break;
        case AGL_TEXTURE:
            g_state->texture_matrix = agl_mat4_multiply(g_state->texture_matrix, scale);
            break;
    }
    agl_update_mvp_matrix();
}

void aglPerspective(float fovy, float aspect, float near, float far) {
    if (g_state == NULL) return;
    agl_mat4_t persp = agl_mat4_perspective(fovy, aspect, near, far);
    if (g_state->matrix_mode == AGL_PROJECTION) {
        g_state->projection_matrix = persp;
    } else {
        g_state->projection_matrix = agl_mat4_multiply(g_state->projection_matrix, persp);
    }
    agl_update_mvp_matrix();
}

void aglOrtho(float left, float right, float bottom, float top, float near, float far) {
    if (g_state == NULL) return;
    agl_mat4_t ortho = agl_mat4_ortho(left, right, bottom, top, near, far);
    if (g_state->matrix_mode == AGL_PROJECTION) {
        g_state->projection_matrix = ortho;
    } else {
        g_state->projection_matrix = agl_mat4_multiply(g_state->projection_matrix, ortho);
    }
    agl_update_mvp_matrix();
}

void aglLookAt(float eyeX, float eyeY, float eyeZ,
               float centerX, float centerY, float centerZ,
               float upX, float upY, float upZ) {
    if (g_state == NULL) return;
    agl_mat4_t look = agl_mat4_lookat(eyeX, eyeY, eyeZ, centerX, centerY, centerZ, upX, upY, upZ);
    if (g_state->matrix_mode == AGL_MODELVIEW) {
        g_state->modelview_matrix = look;
    } else {
        g_state->modelview_matrix = agl_mat4_multiply(g_state->modelview_matrix, look);
    }
    agl_update_mvp_matrix();
}

void aglFrustum(float left, float right, float bottom, float top, float near, float far) {
    if (g_state == NULL) return;
    agl_mat4_t frust = agl_mat4_frustum(left, right, bottom, top, near, far);
    if (g_state->matrix_mode == AGL_PROJECTION) {
        g_state->projection_matrix = frust;
    } else {
        g_state->projection_matrix = agl_mat4_multiply(g_state->projection_matrix, frust);
    }
    agl_update_mvp_matrix();
}

/* IMMEDIATE MODE RENDERING */

void aglBegin(uint32_t mode) {
    if (g_state == NULL) return;
    if (g_state->in_begin_end) {
        g_state->last_error = AGL_INVALID_OPERATION;
        return;
    }
    
    switch (mode) {
        case AGL_POINTS:
        case AGL_LINES:
        case AGL_LINE_STRIP:
        case AGL_TRIANGLES:
        case AGL_TRIANGLE_STRIP:
        case AGL_TRIANGLE_FAN:
        case AGL_QUADS:
            g_state->current_primitive = mode;
            g_state->in_begin_end = true;
            g_state->vertex_count = 0;
            break;
        default:
            g_state->last_error = AGL_INVALID_ENUM;
            break;
    }
}

void aglEnd(void) {
    if (g_state == NULL) return;
    if (!g_state->in_begin_end) {
        g_state->last_error = AGL_INVALID_OPERATION;
        return;
    }
    
    g_state->in_begin_end = false;
    
    /* Rasterize the primitives */
    agl_rasterize_primitives();
}

void aglColor3f(float r, float g, float b) {
    if (g_state != NULL) {
        g_state->current_color.r = agl_clampf(r, 0.0f, 1.0f);
        g_state->current_color.g = agl_clampf(g, 0.0f, 1.0f);
        g_state->current_color.b = agl_clampf(b, 0.0f, 1.0f);
        g_state->current_color.a = 1.0f;
    }
}

void aglColor4f(float r, float g, float b, float a) {
    if (g_state != NULL) {
        g_state->current_color.r = agl_clampf(r, 0.0f, 1.0f);
        g_state->current_color.g = agl_clampf(g, 0.0f, 1.0f);
        g_state->current_color.b = agl_clampf(b, 0.0f, 1.0f);
        g_state->current_color.a = agl_clampf(a, 0.0f, 1.0f);
    }
}

void aglColor3ub(uint8_t r, uint8_t g, uint8_t b) {
    if (g_state != NULL) {
        g_state->current_color.r = r / 255.0f;
        g_state->current_color.g = g / 255.0f;
        g_state->current_color.b = b / 255.0f;
        g_state->current_color.a = 1.0f;
    }
}

void aglColor4ub(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (g_state != NULL) {
        g_state->current_color.r = r / 255.0f;
        g_state->current_color.g = g / 255.0f;
        g_state->current_color.b = b / 255.0f;
        g_state->current_color.a = a / 255.0f;
    }
}

void aglNormal3f(float x, float y, float z) {
    if (g_state != NULL) {
        g_state->current_normal.x = x;
        g_state->current_normal.y = y;
        g_state->current_normal.z = z;
    }
}

void aglTexCoord2f(float u, float v) {
    if (g_state != NULL) {
        g_state->current_texcoord.u = u;
        g_state->current_texcoord.v = v;
    }
}

void aglVertex2f(float x, float y) {
    aglVertex3f(x, y, 0.0f);
}

void aglVertex3f(float x, float y, float z) {
    if (g_state == NULL) return;
    if (!g_state->in_begin_end) {
        g_state->last_error = AGL_INVALID_OPERATION;
        return;
    }
    
    if (g_state->vertex_count >= MAX_VERTICES) {
        g_state->last_error = AGL_OUT_OF_MEMORY;
        return;
    }
    
    /* Create vertex */
    agl_vertex_t *v = &g_state->vertex_buffer[g_state->vertex_count];
    
    /* Transform position */
    agl_vec4_t pos = {x, y, z, 1.0f};
    pos = agl_mat4_mul_vec4(g_state->mvp_matrix, pos);
    v->position = pos;
    
    /* Transform normal (for lighting) */
    v->normal = agl_mat4_mul_vec3_vector(g_state->modelview_matrix, g_state->current_normal);
    v->normal = agl_vec3_normalize(v->normal);
    
    /* Copy color */
    v->color = g_state->current_color;
    
    /* Copy texcoord */
    v->texcoord = g_state->current_texcoord;
    
    /* Calculate depth */
    v->depth = pos.z / pos.w;
    
    g_state->vertex_count++;
}

void aglVertex4f(float x, float y, float z, float w) {
    if (g_state == NULL) return;
    if (!g_state->in_begin_end) {
        g_state->last_error = AGL_INVALID_OPERATION;
        return;
    }
    
    if (g_state->vertex_count >= MAX_VERTICES) {
        g_state->last_error = AGL_OUT_OF_MEMORY;
        return;
    }
    
    /* Create vertex */
    agl_vertex_t *v = &g_state->vertex_buffer[g_state->vertex_count];
    
    /* Transform position */
    agl_vec4_t pos = {x, y, z, w};
    pos = agl_mat4_mul_vec4(g_state->mvp_matrix, pos);
    v->position = pos;
    
    /* Transform normal (for lighting) */
    v->normal = agl_mat4_mul_vec3_vector(g_state->modelview_matrix, g_state->current_normal);
    v->normal = agl_vec3_normalize(v->normal);
    
    /* Copy color */
    v->color = g_state->current_color;
    
    /* Copy texcoord */
    v->texcoord = g_state->current_texcoord;
    
    /* Calculate depth */
    v->depth = pos.z / pos.w;
    
    g_state->vertex_count++;
}

/* LIGHTING SYSTEM */

void aglLight(uint32_t light, uint32_t pname, const float *params) {
    aglLightfv(light, pname, params);
}

void aglLightfv(uint32_t light, uint32_t pname, const float *params) {
    if (g_state == NULL || params == NULL) return;
    
    int light_index;
    switch (light) {
        case AGL_LIGHT0: light_index = 0; break;
        case AGL_LIGHT1: light_index = 1; break;
        case AGL_LIGHT2: light_index = 2; break;
        case AGL_LIGHT3: light_index = 3; break;
        default:
            g_state->last_error = AGL_INVALID_ENUM;
            return;
    }
    
    if (light_index >= MAX_LIGHTS) {
        g_state->last_error = AGL_INVALID_VALUE;
        return;
    }
    
    agl_light_t *l = &g_state->lights[light_index];
    
    switch (pname) {
        case AGL_AMBIENT:
            l->ambient.r = params[0];
            l->ambient.g = params[1];
            l->ambient.b = params[2];
            l->ambient.a = params[3];
            break;
        case AGL_DIFFUSE:
            l->diffuse.r = params[0];
            l->diffuse.g = params[1];
            l->diffuse.b = params[2];
            l->diffuse.a = params[3];
            break;
        case AGL_SPECULAR:
            l->specular.r = params[0];
            l->specular.g = params[1];
            l->specular.b = params[2];
            l->specular.a = params[3];
            break;
        case AGL_POSITION:
            l->position.x = params[0];
            l->position.y = params[1];
            l->position.z = params[2];
            l->position.w = params[3];
            break;
        default:
            g_state->last_error = AGL_INVALID_ENUM;
            break;
    }
}

void aglMaterial(uint32_t face, uint32_t pname, const float *params) {
    aglMaterialfv(face, pname, params);
}

void aglMaterialfv(uint32_t face, uint32_t pname, const float *params) {
    if (g_state == NULL || params == NULL) return;
    
    switch (face) {
        case AGL_FRONT:
        case AGL_BACK:
        case AGL_FRONT_AND_BACK:
            break;
        default:
            g_state->last_error = AGL_INVALID_ENUM;
            return;
    }
    
    agl_material_t *mat = &g_state->material;
    
    switch (pname) {
        case AGL_AMBIENT:
            mat->ambient.r = params[0];
            mat->ambient.g = params[1];
            mat->ambient.b = params[2];
            mat->ambient.a = params[3];
            break;
        case AGL_DIFFUSE:
            mat->diffuse.r = params[0];
            mat->diffuse.g = params[1];
            mat->diffuse.b = params[2];
            mat->diffuse.a = params[3];
            break;
        case AGL_SPECULAR:
            mat->specular.r = params[0];
            mat->specular.g = params[1];
            mat->specular.b = params[2];
            mat->specular.a = params[3];
            break;
        case AGL_EMISSION:
            mat->emission.r = params[0];
            mat->emission.g = params[1];
            mat->emission.b = params[2];
            mat->emission.a = params[3];
            break;
        case AGL_AMBIENT_AND_DIFFUSE:
            mat->ambient.r = params[0];
            mat->ambient.g = params[1];
            mat->ambient.b = params[2];
            mat->ambient.a = params[3];
            mat->diffuse.r = params[0];
            mat->diffuse.g = params[1];
            mat->diffuse.b = params[2];
            mat->diffuse.a = params[3];
            break;
        default:
            g_state->last_error = AGL_INVALID_ENUM;
            break;
    }
}

void aglMaterialf(uint32_t face, uint32_t pname, float param) {
    if (g_state == NULL) return;
    
    switch (face) {
        case AGL_FRONT:
        case AGL_BACK:
        case AGL_FRONT_AND_BACK:
            break;
        default:
            g_state->last_error = AGL_INVALID_ENUM;
            return;
    }
    
    agl_material_t *mat = &g_state->material;
    
    switch (pname) {
        case AGL_SHININESS:
            mat->shininess = param;
            break;
        default:
            g_state->last_error = AGL_INVALID_ENUM;
            break;
    }
}

/* TEXTURING */

void aglGenTextures(int n, uint32_t *textures) {
    if (g_state == NULL || textures == NULL) return;
    
    for (int i = 0; i < n; i++) {
        textures[i] = g_state->next_texture_id++;
    }
}

void aglDeleteTextures(int n, const uint32_t *textures) {
    if (g_state == NULL || textures == NULL) return;
    
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < MAX_TEXTURES; j++) {
            if (g_state->textures[j].id == textures[i]) {
                if (g_state->textures[j].data != NULL) {
                    kfree(g_state->textures[j].data);
                }
                g_state->textures[j].id = 0;
                g_state->textures[j].width = 0;
                g_state->textures[j].height = 0;
                g_state->textures[j].format = 0;
                g_state->textures[j].data = NULL;
                break;
            }
        }
    }
}

void aglBindTexture(uint32_t target, uint32_t texture) {
    if (g_state == NULL) return;
    
    if (target != AGL_TEXTURE_2D) {
        g_state->last_error = AGL_INVALID_ENUM;
        return;
    }
    
    g_state->current_texture = texture;
}

void aglTexImage2D(uint32_t target, int level, int internalformat,
                   int width, int height, int border,
                   uint32_t format, uint32_t type, const void *pixels) {
    if (g_state == NULL) return;
    
    if (target != AGL_TEXTURE_2D) {
        g_state->last_error = AGL_INVALID_ENUM;
        return;
    }
    
    if (level != 0 || border != 0) {
        /* Not supported in this implementation */
        return;
    }
    
    /* Find texture slot */
    int slot = -1;
    for (int i = 0; i < MAX_TEXTURES; i++) {
        if (g_state->textures[i].id == g_state->current_texture) {
            slot = i;
            break;
        }
        if (slot == -1 && g_state->textures[i].id == 0) {
            slot = i;
        }
    }
    
    if (slot == -1) {
        g_state->last_error = AGL_OUT_OF_MEMORY;
        return;
    }
    
    /* Free old data if exists */
    if (g_state->textures[slot].data != NULL) {
        kfree(g_state->textures[slot].data);
    }
    
    /* Allocate new texture */
    int bytes_per_pixel = (format == AGL_RGB) ? 3 : 4;
    g_state->textures[slot].data = (uint8_t *)kmalloc(width * height * bytes_per_pixel);
    
    if (g_state->textures[slot].data == NULL) {
        g_state->last_error = AGL_OUT_OF_MEMORY;
        return;
    }
    
    /* Copy texture data */
    if (pixels != NULL) {
        for (int i = 0; i < width * height * bytes_per_pixel; i++) {
            g_state->textures[slot].data[i] = ((uint8_t *)pixels)[i];
        }
    }
    
    g_state->textures[slot].id = g_state->current_texture;
    g_state->textures[slot].width = width;
    g_state->textures[slot].height = height;
    g_state->textures[slot].format = format;
}

void aglTexParameteri(uint32_t target, uint32_t pname, int param) {
    if (g_state == NULL) return;
    
    if (target != AGL_TEXTURE_2D) {
        g_state->last_error = AGL_INVALID_ENUM;
        return;
    }
    
    /* Find current texture */
    for (int i = 0; i < MAX_TEXTURES; i++) {
        if (g_state->textures[i].id == g_state->current_texture) {
            switch (pname) {
                case AGL_TEXTURE_MAG_FILTER:
                    g_state->textures[i].mag_filter = param;
                    break;
                case AGL_TEXTURE_MIN_FILTER:
                    g_state->textures[i].min_filter = param;
                    break;
                case AGL_TEXTURE_WRAP_S:
                    g_state->textures[i].wrap_s = param;
                    break;
                case AGL_TEXTURE_WRAP_T:
                    g_state->textures[i].wrap_t = param;
                    break;
                default:
                    g_state->last_error = AGL_INVALID_ENUM;
                    break;
            }
            return;
        }
    }
}

/* UTILITY FUNCTIONS */

void aglFlush(void) {
    /* In a real implementation, this would flush any pending rendering */
    /* For software rasterizer, rendering happens immediately */
}

const char *aglGetString(uint32_t name) {
    if (name == 0) {
        return "AurionGL 1.0";
    }
    return NULL;
}

/* HELPER FUNCTIONS - PRIMITIVE GENERATION */

void aglDrawCube(void) {
    aglBegin(AGL_QUADS);
    
    /* Front face */
    aglNormal3f(0.0f, 0.0f, 1.0f);
    aglTexCoord2f(0.0f, 0.0f); aglVertex3f(-0.5f, -0.5f, 0.5f);
    aglTexCoord2f(1.0f, 0.0f); aglVertex3f(0.5f, -0.5f, 0.5f);
    aglTexCoord2f(1.0f, 1.0f); aglVertex3f(0.5f, 0.5f, 0.5f);
    aglTexCoord2f(0.0f, 1.0f); aglVertex3f(-0.5f, 0.5f, 0.5f);
    
    /* Back face */
    aglNormal3f(0.0f, 0.0f, -1.0f);
    aglTexCoord2f(1.0f, 0.0f); aglVertex3f(-0.5f, -0.5f, -0.5f);
    aglTexCoord2f(1.0f, 1.0f); aglVertex3f(-0.5f, 0.5f, -0.5f);
    aglTexCoord2f(0.0f, 1.0f); aglVertex3f(0.5f, 0.5f, -0.5f);
    aglTexCoord2f(0.0f, 0.0f); aglVertex3f(0.5f, -0.5f, -0.5f);
    
    /* Top face */
    aglNormal3f(0.0f, 1.0f, 0.0f);
    aglTexCoord2f(0.0f, 1.0f); aglVertex3f(-0.5f, 0.5f, -0.5f);
    aglTexCoord2f(0.0f, 0.0f); aglVertex3f(-0.5f, 0.5f, 0.5f);
    aglTexCoord2f(1.0f, 0.0f); aglVertex3f(0.5f, 0.5f, 0.5f);
    aglTexCoord2f(1.0f, 1.0f); aglVertex3f(0.5f, 0.5f, -0.5f);
    
    /* Bottom face */
    aglNormal3f(0.0f, -1.0f, 0.0f);
    aglTexCoord2f(1.0f, 1.0f); aglVertex3f(-0.5f, -0.5f, -0.5f);
    aglTexCoord2f(0.0f, 1.0f); aglVertex3f(0.5f, -0.5f, -0.5f);
    aglTexCoord2f(0.0f, 0.0f); aglVertex3f(0.5f, -0.5f, 0.5f);
    aglTexCoord2f(1.0f, 0.0f); aglVertex3f(-0.5f, -0.5f, 0.5f);
    
    /* Right face */
    aglNormal3f(1.0f, 0.0f, 0.0f);
    aglTexCoord2f(1.0f, 0.0f); aglVertex3f(0.5f, -0.5f, -0.5f);
    aglTexCoord2f(1.0f, 1.0f); aglVertex3f(0.5f, 0.5f, -0.5f);
    aglTexCoord2f(0.0f, 1.0f); aglVertex3f(0.5f, 0.5f, 0.5f);
    aglTexCoord2f(0.0f, 0.0f); aglVertex3f(0.5f, -0.5f, 0.5f);
    
    /* Left face */
    aglNormal3f(-1.0f, 0.0f, 0.0f);
    aglTexCoord2f(0.0f, 0.0f); aglVertex3f(-0.5f, -0.5f, -0.5f);
    aglTexCoord2f(1.0f, 0.0f); aglVertex3f(-0.5f, -0.5f, 0.5f);
    aglTexCoord2f(1.0f, 1.0f); aglVertex3f(-0.5f, 0.5f, 0.5f);
    aglTexCoord2f(0.0f, 1.0f); aglVertex3f(-0.5f, 0.5f, -0.5f);
    
    aglEnd();
}

void aglDrawSphere(float radius, int slices, int stacks) {
    if (slices < 3 || stacks < 3) return;
    
    for (int i = 0; i < stacks; i++) {
        float lat0 = PI * (-0.5f + (float)i / stacks);
        float z0 = agl_sinf(lat0) * radius;
        float zr0 = agl_cosf(lat0) * radius;
        
        float lat1 = PI * (-0.5f + (float)(i + 1) / stacks);
        float z1 = agl_sinf(lat1) * radius;
        float zr1 = agl_cosf(lat1) * radius;
        
        aglBegin(AGL_TRIANGLE_STRIP);
        for (int j = 0; j <= slices; j++) {
            float lng = 2 * PI * (float)j / slices;
            float x = agl_cosf(lng);
            float y = agl_sinf(lng);
            
            /* Normal for first vertex */
            aglNormal3f(x * zr0 / radius, y * zr0 / radius, z0 / radius);
            aglTexCoord2f((float)j / slices, (float)i / stacks);
            aglVertex3f(x * zr0, y * zr0, z0);
            
            /* Normal for second vertex */
            aglNormal3f(x * zr1 / radius, y * zr1 / radius, z1 / radius);
            aglTexCoord2f((float)j / slices, (float)(i + 1) / stacks);
            aglVertex3f(x * zr1, y * zr1, z1);
        }
        aglEnd();
    }
}

void aglDrawCylinder(float radius, float height, int slices) {
    if (slices < 3) return;
    
    float half_height = height * 0.5f;
    
    /* Side */
    for (int i = 0; i < slices; i++) {
        float angle1 = 2.0f * PI * (float)i / slices;
        float angle2 = 2.0f * PI * (float)(i + 1) / slices;
        
        float x1 = agl_cosf(angle1) * radius;
        float y1 = agl_sinf(angle1) * radius;
        float x2 = agl_cosf(angle2) * radius;
        float y2 = agl_sinf(angle2) * radius;
        
        aglBegin(AGL_QUADS);
        aglNormal3f(agl_cosf(angle1), agl_sinf(angle1), 0.0f);
        aglTexCoord2f((float)i / slices, 0.0f);
        aglVertex3f(x1, y1, -half_height);
        aglTexCoord2f((float)i / slices, 1.0f);
        aglVertex3f(x1, y1, half_height);
        aglNormal3f(agl_cosf(angle2), agl_sinf(angle2), 0.0f);
        aglTexCoord2f((float)(i + 1) / slices, 1.0f);
        aglVertex3f(x2, y2, half_height);
        aglTexCoord2f((float)(i + 1) / slices, 0.0f);
        aglVertex3f(x2, y2, -half_height);
        aglEnd();
    }
    
    /* Top cap */
    aglBegin(AGL_TRIANGLE_FAN);
    aglNormal3f(0.0f, 0.0f, 1.0f);
    aglTexCoord2f(0.5f, 0.5f);
    aglVertex3f(0.0f, 0.0f, half_height);
    for (int i = 0; i <= slices; i++) {
        float angle = 2.0f * PI * (float)i / slices;
        aglTexCoord2f(0.5f + 0.5f * agl_cosf(angle), 0.5f + 0.5f * agl_sinf(angle));
        aglVertex3f(agl_cosf(angle) * radius, agl_sinf(angle) * radius, half_height);
    }
    aglEnd();
    
    /* Bottom cap */
    aglBegin(AGL_TRIANGLE_FAN);
    aglNormal3f(0.0f, 0.0f, -1.0f);
    aglTexCoord2f(0.5f, 0.5f);
    aglVertex3f(0.0f, 0.0f, -half_height);
    for (int i = slices; i >= 0; i--) {
        float angle = 2.0f * PI * (float)i / slices;
        aglTexCoord2f(0.5f + 0.5f * agl_cosf(angle), 0.5f + 0.5f * agl_sinf(angle));
        aglVertex3f(agl_cosf(angle) * radius, agl_sinf(angle) * radius, -half_height);
    }
    aglEnd();
}

void aglDrawTorus(float innerRadius, float outerRadius, int sides, int rings) {
    if (sides < 3 || rings < 3) return;
    
    for (int i = 0; i < rings; i++) {
        float theta1 = 2.0f * PI * (float)i / rings;
        float theta2 = 2.0f * PI * (float)(i + 1) / rings;
        
        for (int j = 0; j < sides; j++) {
            float phi1 = 2.0f * PI * (float)j / sides;
            float phi2 = 2.0f * PI * (float)(j + 1) / sides;
            
            float r1 = outerRadius + innerRadius * agl_cosf(phi1);
            float r2 = outerRadius + innerRadius * agl_cosf(phi2);
            
            float z1 = innerRadius * agl_sinf(phi1);
            float z2 = innerRadius * agl_sinf(phi2);
            
            aglBegin(AGL_QUADS);
            
            /* Vertex 1 */
            float nx1 = agl_cosf(theta1) * agl_cosf(phi1);
            float ny1 = agl_sinf(theta1) * agl_cosf(phi1);
            float nz1 = agl_sinf(phi1);
            aglNormal3f(nx1, ny1, nz1);
            aglTexCoord2f((float)i / rings, (float)j / sides);
            aglVertex3f(r1 * agl_cosf(theta1), r1 * agl_sinf(theta1), z1);
            
            /* Vertex 2 */
            float nx2 = agl_cosf(theta2) * agl_cosf(phi1);
            float ny2 = agl_sinf(theta2) * agl_cosf(phi1);
            float nz2 = agl_sinf(phi1);
            aglNormal3f(nx2, ny2, nz2);
            aglTexCoord2f((float)(i + 1) / rings, (float)j / sides);
            aglVertex3f(r1 * agl_cosf(theta2), r1 * agl_sinf(theta2), z1);
            
            /* Vertex 3 */
            float nx3 = agl_cosf(theta2) * agl_cosf(phi2);
            float ny3 = agl_sinf(theta2) * agl_cosf(phi2);
            float nz3 = agl_sinf(phi2);
            aglNormal3f(nx3, ny3, nz3);
            aglTexCoord2f((float)(i + 1) / rings, (float)(j + 1) / sides);
            aglVertex3f(r2 * agl_cosf(theta2), r2 * agl_sinf(theta2), z2);
            
            /* Vertex 4 */
            float nx4 = agl_cosf(theta1) * agl_cosf(phi2);
            float ny4 = agl_sinf(theta1) * agl_cosf(phi2);
            float nz4 = agl_sinf(phi2);
            aglNormal3f(nx4, ny4, nz4);
            aglTexCoord2f((float)i / rings, (float)(j + 1) / sides);
            aglVertex3f(r2 * agl_cosf(theta1), r2 * agl_sinf(theta1), z2);
            
            aglEnd();
        }
    }
}

/* RASTERIZATION PIPELINE */

static void agl_set_pixel(int x, int y, agl_color_t color) {
    if (g_state == NULL || g_state->framebuffer == NULL) return;
    
    /* Check viewport bounds */
    if (x < g_state->vp_x || x >= g_state->vp_x + g_state->vp_width) return;
    if (y < g_state->vp_y || y >= g_state->vp_y + g_state->vp_height) return;
    
    int index = y * g_state->fb_width + x;
    
    /* Apply blending if enabled */
    if (g_state->blend_enabled) {
        uint32_t current = g_state->framebuffer[index];
        /* Framebuffer is BGRA: bits 0-7=Blue, 8-15=Green, 16-23=Red, 24-31=Alpha */
        float src_r = color.r;
        float src_g = color.g;
        float src_b = color.b;
        float src_a = color.a;
        
        float dst_r = ((current >> 16) & 0xFF) / 255.0f;
        float dst_g = ((current >> 8) & 0xFF) / 255.0f;
        float dst_b = (current & 0xFF) / 255.0f;
        float dst_a = ((current >> 24) & 0xFF) / 255.0f;
        
        float blend_src_factor, blend_dst_factor;
        
        /* Calculate source blend factor */
        switch (g_state->blend_src) {
            case AGL_ZERO: blend_src_factor = 0.0f; break;
            case AGL_ONE: blend_src_factor = 1.0f; break;
            case AGL_SRC_COLOR: blend_src_factor = src_r; break;
            case AGL_ONE_MINUS_SRC_COLOR: blend_src_factor = 1.0f - src_r; break;
            case AGL_SRC_ALPHA: blend_src_factor = src_a; break;
            case AGL_ONE_MINUS_SRC_ALPHA: blend_src_factor = 1.0f - src_a; break;
            case AGL_DST_ALPHA: blend_src_factor = dst_a; break;
            case AGL_ONE_MINUS_DST_ALPHA: blend_src_factor = 1.0f - dst_a; break;
            case AGL_DST_COLOR: blend_src_factor = dst_r; break;
            case AGL_ONE_MINUS_DST_COLOR: blend_src_factor = 1.0f - dst_r; break;
            default: blend_src_factor = src_a; break;
        }
        
        /* Calculate destination blend factor */
        switch (g_state->blend_dst) {
            case AGL_ZERO: blend_dst_factor = 0.0f; break;
            case AGL_ONE: blend_dst_factor = 1.0f; break;
            case AGL_SRC_COLOR: blend_dst_factor = src_r; break;
            case AGL_ONE_MINUS_SRC_COLOR: blend_dst_factor = 1.0f - src_r; break;
            case AGL_SRC_ALPHA: blend_dst_factor = src_a; break;
            case AGL_ONE_MINUS_SRC_ALPHA: blend_dst_factor = 1.0f - src_a; break;
            case AGL_DST_ALPHA: blend_dst_factor = dst_a; break;
            case AGL_ONE_MINUS_DST_ALPHA: blend_dst_factor = 1.0f - dst_a; break;
            case AGL_DST_COLOR: blend_dst_factor = dst_r; break;
            case AGL_ONE_MINUS_DST_COLOR: blend_dst_factor = 1.0f - dst_r; break;
            default: blend_dst_factor = 1.0f - src_a; break;
        }
        
        float final_r = src_r * blend_src_factor + dst_r * blend_dst_factor;
        float final_g = src_g * blend_src_factor + dst_g * blend_dst_factor;
        float final_b = src_b * blend_src_factor + dst_b * blend_dst_factor;
        float final_a = src_a * blend_src_factor + dst_a * blend_dst_factor;
        
        final_r = agl_clampf(final_r, 0.0f, 1.0f);
        final_g = agl_clampf(final_g, 0.0f, 1.0f);
        final_b = agl_clampf(final_b, 0.0f, 1.0f);
        final_a = agl_clampf(final_a, 0.0f, 1.0f);
        
        /* Write as BGRA: Alpha<<24 | Red<<16 | Green<<8 | Blue */
        g_state->framebuffer[index] = 
            ((uint32_t)(final_a * 255.0f) << 24) |
            ((uint32_t)(final_r * 255.0f) << 16) |
            ((uint32_t)(final_g * 255.0f) << 8) |
            ((uint32_t)(final_b * 255.0f));
    } else {
        /* Write as BGRA: Alpha<<24 | Red<<16 | Green<<8 | Blue */
        g_state->framebuffer[index] = 
            ((uint32_t)(color.a * 255.0f) << 24) |
            ((uint32_t)(color.r * 255.0f) << 16) |
            ((uint32_t)(color.g * 255.0f) << 8) |
            ((uint32_t)(color.b * 255.0f));
    }
}

static bool agl_depth_test(int x, int y, float depth) {
    if (g_state == NULL || g_state->depth_buffer == NULL) return true;
    
    if (!g_state->depth_test_enabled) return true;
    
    if (x < 0 || x >= g_state->fb_width) return true;
    if (y < 0 || y >= g_state->fb_height) return true;
    
    int index = y * g_state->fb_width + x;
    float current_depth = g_state->depth_buffer[index];
    
    bool pass = false;
    switch (g_state->depth_func) {
        case AGL_NEVER: pass = false; break;
        case AGL_LESS: pass = depth < current_depth; break;
        case AGL_EQUAL: pass = depth == current_depth; break;
        case AGL_LEQUAL: pass = depth <= current_depth; break;
        case AGL_GREATER: pass = depth > current_depth; break;
        case AGL_NOTEQUAL: pass = depth != current_depth; break;
        case AGL_GEQUAL: pass = depth >= current_depth; break;
        case AGL_ALWAYS: pass = true; break;
        default: pass = true; break;
    }
    
    if (pass) {
        g_state->depth_buffer[index] = depth;
    }
    
    return pass;
}

static agl_color_t agl_sample_texture(agl_vec2_t texcoord) {
    agl_color_t result = {1.0f, 1.0f, 1.0f, 1.0f};
    
    if (!g_state->texture_2d_enabled) return result;
    
    /* Find current texture */
    agl_texture_t *tex = NULL;
    for (int i = 0; i < MAX_TEXTURES; i++) {
        if (g_state->textures[i].id == g_state->current_texture) {
            tex = &g_state->textures[i];
            break;
        }
    }
    
    if (tex == NULL || tex->data == NULL) return result;
    
    /* Apply texture wrap */
    float u = texcoord.u;
    float v = texcoord.v;
    
    if (tex->wrap_s == AGL_REPEAT) {
        u = u - agl_floorf(u);
    } else if (tex->wrap_s == AGL_CLAMP) {
        u = agl_clampf(u, 0.0f, 1.0f);
    }
    
    if (tex->wrap_t == AGL_REPEAT) {
        v = v - agl_floorf(v);
    } else if (tex->wrap_t == AGL_CLAMP) {
        v = agl_clampf(v, 0.0f, 1.0f);
    }
    
    /* Sample texture */
    int x = (int)(u * tex->width);
    int y = (int)(v * tex->height);
    
    x = agl_abs(x) % tex->width;
    y = agl_abs(y) % tex->height;
    
    int bytes_per_pixel = (tex->format == AGL_RGB) ? 3 : 4;
    int index = (y * tex->width + x) * bytes_per_pixel;
    
    result.r = tex->data[index] / 255.0f;
    result.g = tex->data[index + 1] / 255.0f;
    result.b = tex->data[index + 2] / 255.0f;
    result.a = (tex->format == AGL_RGBA) ? tex->data[index + 3] / 255.0f : 1.0f;
    
    return result;
}

static agl_color_t agl_apply_lighting(agl_vertex_t *v) {
    agl_color_t result = v->color;
    
    if (!g_state->lighting_enabled) return result;
    
    /* Start with material emission */
    result.r = g_state->material.emission.r;
    result.g = g_state->material.emission.g;
    result.b = g_state->material.emission.b;
    result.a = g_state->material.emission.a;
    
    /* Add global ambient */
    result.r += g_state->material.ambient.r * 0.1f;
    result.g += g_state->material.ambient.g * 0.1f;
    result.b += g_state->material.ambient.b * 0.1f;
    
    /* Add contribution from each enabled light */
    for (int i = 0; i < MAX_LIGHTS; i++) {
        if (!g_state->lights[i].enabled) continue;
        
        agl_light_t *light = &g_state->lights[i];
        agl_vec3_t light_dir;
        
        if (light->position.w == 0.0f) {
            /* Directional light */
            light_dir.x = -light->position.x;
            light_dir.y = -light->position.y;
            light_dir.z = -light->position.z;
        } else {
            /* Point light */
            light_dir.x = light->position.x - v->position.x;
            light_dir.y = light->position.y - v->position.y;
            light_dir.z = light->position.z - v->position.z;
        }
        
        light_dir = agl_vec3_normalize(light_dir);
        
        /* Diffuse component */
        float diff = agl_vec3_dot(v->normal, light_dir);
        if (diff < 0.0f) diff = 0.0f;
        
        result.r += g_state->material.diffuse.r * light->diffuse.r * diff;
        result.g += g_state->material.diffuse.g * light->diffuse.g * diff;
        result.b += g_state->material.diffuse.b * light->diffuse.b * diff;
        
        /* Specular component */
        agl_vec3_t view_dir = {0.0f, 0.0f, 1.0f};
        view_dir = agl_vec3_normalize(view_dir);
        
        agl_vec3_t half_vec = agl_vec3_add(light_dir, view_dir);
        half_vec = agl_vec3_normalize(half_vec);
        
        float spec = agl_vec3_dot(v->normal, half_vec);
        if (spec < 0.0f) spec = 0.0f;
        spec = agl_powf(spec, g_state->material.shininess);
        
        result.r += g_state->material.specular.r * light->specular.r * spec;
        result.g += g_state->material.specular.g * light->specular.g * spec;
        result.b += g_state->material.specular.b * light->specular.b * spec;
    }
    
    /* Clamp color */
    result.r = agl_clampf(result.r, 0.0f, 1.0f);
    result.g = agl_clampf(result.g, 0.0f, 1.0f);
    result.b = agl_clampf(result.b, 0.0f, 1.0f);
    
    return result;
}

static void agl_draw_line(agl_vertex_t *v0, agl_vertex_t *v1) {
    if (g_state == NULL) return;
    
    // Skip if any vertex behind camera (w <= 0)
    if (v0->position.w <= 0.0f || v1->position.w <= 0.0f) return;
    
    /* Transform to screen space */
    float x0 = v0->position.x / v0->position.w;
    float y0 = v0->position.y / v0->position.w;
    float x1 = v1->position.x / v1->position.w;
    float y1 = v1->position.y / v1->position.w;
    
    /* Convert to screen coordinates */
    x0 = (x0 + 1.0f) * 0.5f * g_state->vp_width + g_state->vp_x;
    y0 = (y0 + 1.0f) * 0.5f * g_state->vp_height + g_state->vp_y;
    x1 = (x1 + 1.0f) * 0.5f * g_state->vp_width + g_state->vp_x;
    y1 = (y1 + 1.0f) * 0.5f * g_state->vp_height + g_state->vp_y;
    
    // Skip if coordinates are extremely large (likely from near-zero w)
    if (x0 < -1000000.0f || x0 > 1000000.0f || y0 < -1000000.0f || y0 > 1000000.0f ||
        x1 < -1000000.0f || x1 > 1000000.0f || y1 < -1000000.0f || y1 > 1000000.0f) {
        return;
    }
    
    /* Bresenham's line algorithm */
    int dx = agl_abs((int)(x1 - x0));
    int dy = agl_abs((int)(y1 - y0));
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    
    int ix = (int)x0;
    int iy = (int)y0;
    
    while (true) {
        agl_color_t color = v0->color;
        
        /* Apply texture */
        if (g_state->texture_2d_enabled) {
            agl_color_t tex_color = agl_sample_texture(v0->texcoord);
            color.r *= tex_color.r;
            color.g *= tex_color.g;
            color.b *= tex_color.b;
            color.a *= tex_color.a;
        }
        
        if (agl_depth_test(ix, iy, v0->depth)) {
            agl_set_pixel(ix, iy, color);
        }
        
        if (ix == (int)x1 && iy == (int)y1) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            ix += sx;
        }
        if (e2 < dx) {
            err += dx;
            iy += sy;
        }
    }
}

static void agl_draw_point(agl_vertex_t *v) {
    if (g_state == NULL) return;
    
    // Skip vertices behind camera (w <= 0)
    if (v->position.w <= 0.0f) return;
    
    /* Transform to screen space */
    float x = v->position.x / v->position.w;
    float y = v->position.y / v->position.w;
    
    /* Convert to screen coordinates */
    x = (x + 1.0f) * 0.5f * g_state->vp_width + g_state->vp_x;
    y = (y + 1.0f) * 0.5f * g_state->vp_height + g_state->vp_y;
    
    // Skip if coordinates are extremely large (likely from near-zero w)
    if (x < -1000000.0f || x > 1000000.0f || y < -1000000.0f || y > 1000000.0f) {
        return;
    }
    
    int ix = (int)x;
    int iy = (int)y;
    
    agl_color_t color = v->color;
    
    /* Apply texture */
    if (g_state->texture_2d_enabled) {
        agl_color_t tex_color = agl_sample_texture(v->texcoord);
        color.r *= tex_color.r;
        color.g *= tex_color.g;
        color.b *= tex_color.b;
        color.a *= tex_color.a;
    }
    
    if (agl_depth_test(ix, iy, v->depth)) {
        agl_set_pixel(ix, iy, color);
    }
}

/* Interpolate vertex attributes for triangle rasterization */
typedef struct {
    float x, y;
    float depth;
    agl_color_t color;
    agl_vec2_t texcoord;
    agl_vec3_t normal;
} agl_edge_t;

static agl_edge_t agl_interpolate_edge(agl_vertex_t *v0, agl_vertex_t *v1, float t) {
    agl_edge_t e;
    e.x = v0->position.x / v0->position.w + t * (v1->position.x / v1->position.w - v0->position.x / v0->position.w);
    e.y = v0->position.y / v0->position.w + t * (v1->position.y / v1->position.w - v0->position.y / v0->position.w);
    e.depth = v0->depth + t * (v1->depth - v0->depth);
    e.color.r = v0->color.r + t * (v1->color.r - v0->color.r);
    e.color.g = v0->color.g + t * (v1->color.g - v0->color.g);
    e.color.b = v0->color.b + t * (v1->color.b - v0->color.b);
    e.color.a = v0->color.a + t * (v1->color.a - v0->color.a);
    e.texcoord.u = v0->texcoord.u + t * (v1->texcoord.u - v0->texcoord.u);
    e.texcoord.v = v0->texcoord.v + t * (v1->texcoord.v - v0->texcoord.v);
    e.normal.x = v0->normal.x + t * (v1->normal.x - v0->normal.x);
    e.normal.y = v0->normal.y + t * (v1->normal.y - v0->normal.y);
    e.normal.z = v0->normal.z + t * (v1->normal.z - v0->normal.z);
    return e;
}

static agl_color_t agl_interpolate_scanline(agl_edge_t *left, agl_edge_t *right, float t) {
    agl_color_t c;
    c.r = left->color.r + t * (right->color.r - left->color.r);
    c.g = left->color.g + t * (right->color.g - left->color.g);
    c.b = left->color.b + t * (right->color.b - left->color.b);
    c.a = left->color.a + t * (right->color.a - left->color.a);
    return c;
}

static agl_vec2_t agl_interpolate_texcoord_scanline(agl_edge_t *left, agl_edge_t *right, float t) {
    agl_vec2_t tc;
    tc.u = left->texcoord.u + t * (right->texcoord.u - left->texcoord.u);
    tc.v = left->texcoord.v + t * (right->texcoord.v - left->texcoord.v);
    return tc;
}

static float agl_interpolate_depth_scanline(agl_edge_t *left, agl_edge_t *right, float t) {
    return left->depth + t * (right->depth - left->depth);
}

static void agl_draw_triangle_scanline(agl_vertex_t *v0, agl_vertex_t *v1, agl_vertex_t *v2) {
    if (g_state == NULL) return;
    
    // Skip triangles with any vertex behind camera (w <= 0)
    if (v0->position.w <= 0.0f || v1->position.w <= 0.0f || v2->position.w <= 0.0f) {
        return;
    }
    
    /* Transform to screen space */
    float x0 = v0->position.x / v0->position.w;
    float y0 = v0->position.y / v0->position.w;
    float x1 = v1->position.x / v1->position.w;
    float y1 = v1->position.y / v1->position.w;
    float x2 = v2->position.x / v2->position.w;
    float y2 = v2->position.y / v2->position.w;
    
    /* Convert to screen coordinates */
    x0 = (x0 + 1.0f) * 0.5f * g_state->vp_width + g_state->vp_x;
    y0 = (y0 + 1.0f) * 0.5f * g_state->vp_height + g_state->vp_y;
    x1 = (x1 + 1.0f) * 0.5f * g_state->vp_width + g_state->vp_x;
    y1 = (y1 + 1.0f) * 0.5f * g_state->vp_height + g_state->vp_y;
    x2 = (x2 + 1.0f) * 0.5f * g_state->vp_width + g_state->vp_x;
    y2 = (y2 + 1.0f) * 0.5f * g_state->vp_height + g_state->vp_y;
    
    /* Sort vertices by Y */
    int top, mid, bot;
    if (y0 < y1) {
        if (y1 < y2) { top = 0; mid = 1; bot = 2; }
        else if (y0 < y2) { top = 0; mid = 2; bot = 1; }
        else { top = 2; mid = 0; bot = 1; }
    } else {
        if (y0 < y2) { top = 1; mid = 0; bot = 2; }
        else if (y1 < y2) { top = 1; mid = 2; bot = 0; }
        else { top = 2; mid = 1; bot = 0; }
    }
    
    agl_vertex_t *vt = (top == 0) ? v0 : (top == 1) ? v1 : v2;
    agl_vertex_t *vm = (mid == 0) ? v0 : (mid == 1) ? v1 : v2;
    agl_vertex_t *vb = (bot == 0) ? v0 : (bot == 1) ? v1 : v2;
    
    // Original screen coordinates (floats) for interpolation
    float yt = (top == 0) ? y0 : (top == 1) ? y1 : y2;
    float ym = (mid == 0) ? y0 : (mid == 1) ? y1 : y2;
    float yb = (bot == 0) ? y0 : (bot == 1) ? y1 : y2;
    
    // Clamp Y coordinates to safe range before integer conversion
    float yt_clamp = yt;
    float ym_clamp = ym;
    float yb_clamp = yb;
    if (yt_clamp < -1000000.0f) yt_clamp = -1000000.0f;
    if (yt_clamp > 1000000.0f) yt_clamp = 1000000.0f;
    if (ym_clamp < -1000000.0f) ym_clamp = -1000000.0f;
    if (ym_clamp > 1000000.0f) ym_clamp = 1000000.0f;
    if (yb_clamp < -1000000.0f) yb_clamp = -1000000.0f;
    if (yb_clamp > 1000000.0f) yb_clamp = 1000000.0f;
    
    int iyt = (int)yt_clamp;
    int iym = (int)ym_clamp;
    int iyb = (int)yb_clamp;
    
    /* Upper triangle */
    int y_upper_start = iyt;
    int y_upper_end = iym;
    // Clamp Y range to framebuffer height
    if (y_upper_start < 0) y_upper_start = 0;
    if (y_upper_end >= g_state->fb_height) y_upper_end = g_state->fb_height - 1;
    
    if (y_upper_start <= y_upper_end) {
        for (int y = y_upper_start; y <= y_upper_end; y++) {
            // Compute t using original float y values
            float dy_short = ym - yt;
            float dy_long = yb - yt;
            float t_short = (agl_fabsf(dy_short) < 1e-6f) ? 0.0f : (float)(y - yt) / dy_short;
            float t_long = (agl_fabsf(dy_long) < 1e-6f) ? 0.0f : (float)(y - yt) / dy_long;
            
            agl_edge_t left = agl_interpolate_edge(vt, vm, t_short);
            agl_edge_t right = agl_interpolate_edge(vt, vb, t_long);
            
            // Get X coordinates
            float left_x_orig = left.x;
            float right_x_orig = right.x;
            if (left_x_orig > right_x_orig) {
                // Swap edges to maintain left/right order
                agl_edge_t tmp = left;
                left = right;
                right = tmp;
                left_x_orig = left.x;
                right_x_orig = right.x;
            }
            
            // Clamp X to safe integer range to avoid overflow
            float left_x_clamp = left_x_orig;
            float right_x_clamp = right_x_orig;
            if (left_x_clamp < -1000000.0f) left_x_clamp = -1000000.0f;
            if (left_x_clamp > 1000000.0f) left_x_clamp = 1000000.0f;
            if (right_x_clamp < -1000000.0f) right_x_clamp = -1000000.0f;
            if (right_x_clamp > 1000000.0f) right_x_clamp = 1000000.0f;
            
            int x_left = (int)left_x_clamp;
            int x_right = (int)right_x_clamp;
            
            // Clamp X range to framebuffer width
            int x_start = x_left;
            int x_end = x_right;
            if (x_start < 0) x_start = 0;
            if (x_end >= g_state->fb_width) x_end = g_state->fb_width - 1;
            if (x_start > x_end) continue;
            
            for (int x = x_start; x <= x_end; x++) {
                float dx = right_x_orig - left_x_orig;
                float tx = (agl_fabsf(dx) < 1e-6f) ? 0.0f : (float)(x - left_x_orig) / dx;
                float depth = left.depth + tx * (right.depth - left.depth);
                
                // Early depth test
                if (!agl_depth_test(x, y, depth)) continue;
                
                // Interpolate attributes
                agl_color_t color = {
                    left.color.r + tx * (right.color.r - left.color.r),
                    left.color.g + tx * (right.color.g - left.color.g),
                    left.color.b + tx * (right.color.b - left.color.b),
                    left.color.a + tx * (right.color.a - left.color.a)
                };
                agl_vec2_t texcoord = {
                    left.texcoord.u + tx * (right.texcoord.u - left.texcoord.u),
                    left.texcoord.v + tx * (right.texcoord.v - left.texcoord.v)
                };
                agl_vec3_t normal = {
                    left.normal.x + tx * (right.normal.x - left.normal.x),
                    left.normal.y + tx * (right.normal.y - left.normal.y),
                    left.normal.z + tx * (right.normal.z - left.normal.z)
                };
                
                // Apply lighting if enabled
                if (g_state->lighting_enabled) {
                    agl_vertex_t temp_v = {0};
                    temp_v.color = color;
                    temp_v.texcoord = texcoord;
                    temp_v.normal = normal;
                    // position left as zero (unused for directional lights)
                    color = agl_apply_lighting(&temp_v);
                }
                
                // Apply texture
                if (g_state->texture_2d_enabled) {
                    agl_color_t tex_color = agl_sample_texture(texcoord);
                    color.r *= tex_color.r;
                    color.g *= tex_color.g;
                    color.b *= tex_color.b;
                    color.a *= tex_color.a;
                }
                
                // Apply fog
                if (g_state->fog_enabled) {
                    float fog_factor = 1.0f;
                    if (g_state->fog_mode == AGL_EXP) {
                        fog_factor = agl_expf(-g_state->fog_density * depth);
                    } else if (g_state->fog_mode == AGL_EXP2) {
                        fog_factor = agl_expf(-g_state->fog_density * g_state->fog_density * depth * depth);
                    } else {
                        fog_factor = (g_state->fog_end - depth) / (g_state->fog_end - g_state->fog_start);
                    }
                    fog_factor = agl_clampf(fog_factor, 0.0f, 1.0f);
                    color.r = color.r * fog_factor + g_state->fog_color.r * (1.0f - fog_factor);
                    color.g = color.g * fog_factor + g_state->fog_color.g * (1.0f - fog_factor);
                    color.b = color.b * fog_factor + g_state->fog_color.b * (1.0f - fog_factor);
                }
                
                if (agl_depth_test(x, y, depth)) {
                    agl_set_pixel(x, y, color);
                }
            }
        }
    }
    
    /* Lower triangle */
    int y_lower_start = iym;
    int y_lower_end = iyb;
    if (y_lower_start < 0) y_lower_start = 0;
    if (y_lower_end >= g_state->fb_height) y_lower_end = g_state->fb_height - 1;
    
    if (y_lower_start <= y_lower_end) {
        for (int y = y_lower_start; y <= y_lower_end; y++) {
            float dy_short = yb - ym;
            float dy_long = yb - yt;
            float t_short = (agl_fabsf(dy_short) < 1e-6f) ? 0.0f : (float)(y - ym) / dy_short;
            float t_long = (agl_fabsf(dy_long) < 1e-6f) ? 0.0f : (float)(y - yt) / dy_long;
            
            agl_edge_t left = agl_interpolate_edge(vm, vb, t_short);
            agl_edge_t right = agl_interpolate_edge(vt, vb, t_long);
            
            float left_x_orig = left.x;
            float right_x_orig = right.x;
            if (left_x_orig > right_x_orig) {
                agl_edge_t tmp = left;
                left = right;
                right = tmp;
                left_x_orig = left.x;
                right_x_orig = right.x;
            }
            
            float left_x_clamp = left_x_orig;
            float right_x_clamp = right_x_orig;
            if (left_x_clamp < -1000000.0f) left_x_clamp = -1000000.0f;
            if (left_x_clamp > 1000000.0f) left_x_clamp = 1000000.0f;
            if (right_x_clamp < -1000000.0f) right_x_clamp = -1000000.0f;
            if (right_x_clamp > 1000000.0f) right_x_clamp = 1000000.0f;
            
            int x_left = (int)left_x_clamp;
            int x_right = (int)right_x_clamp;
            
            int x_start = x_left;
            int x_end = x_right;
            if (x_start < 0) x_start = 0;
            if (x_end >= g_state->fb_width) x_end = g_state->fb_width - 1;
            if (x_start > x_end) continue;
            
            for (int x = x_start; x <= x_end; x++) {
                float dx = right_x_orig - left_x_orig;
                float tx = (agl_fabsf(dx) < 1e-6f) ? 0.0f : (float)(x - left_x_orig) / dx;
                float depth = left.depth + tx * (right.depth - left.depth);
                
                if (!agl_depth_test(x, y, depth)) continue;
                
                agl_color_t color = {
                    left.color.r + tx * (right.color.r - left.color.r),
                    left.color.g + tx * (right.color.g - left.color.g),
                    left.color.b + tx * (right.color.b - left.color.b),
                    left.color.a + tx * (right.color.a - left.color.a)
                };
                agl_vec2_t texcoord = {
                    left.texcoord.u + tx * (right.texcoord.u - left.texcoord.u),
                    left.texcoord.v + tx * (right.texcoord.v - left.texcoord.v)
                };
                agl_vec3_t normal = {
                    left.normal.x + tx * (right.normal.x - left.normal.x),
                    left.normal.y + tx * (right.normal.y - left.normal.y),
                    left.normal.z + tx * (right.normal.z - left.normal.z)
                };
                
                if (g_state->lighting_enabled) {
                    agl_vertex_t temp_v = {0};
                    temp_v.color = color;
                    temp_v.texcoord = texcoord;
                    temp_v.normal = normal;
                    color = agl_apply_lighting(&temp_v);
                }
                
                if (g_state->texture_2d_enabled) {
                    agl_color_t tex_color = agl_sample_texture(texcoord);
                    color.r *= tex_color.r;
                    color.g *= tex_color.g;
                    color.b *= tex_color.b;
                    color.a *= tex_color.a;
                }
                
                if (g_state->fog_enabled) {
                    float fog_factor = 1.0f;
                    if (g_state->fog_mode == AGL_EXP) {
                        fog_factor = agl_expf(-g_state->fog_density * depth);
                    } else if (g_state->fog_mode == AGL_EXP2) {
                        fog_factor = agl_expf(-g_state->fog_density * g_state->fog_density * depth * depth);
                    } else {
                        fog_factor = (g_state->fog_end - depth) / (g_state->fog_end - g_state->fog_start);
                    }
                    fog_factor = agl_clampf(fog_factor, 0.0f, 1.0f);
                    color.r = color.r * fog_factor + g_state->fog_color.r * (1.0f - fog_factor);
                    color.g = color.g * fog_factor + g_state->fog_color.g * (1.0f - fog_factor);
                    color.b = color.b * fog_factor + g_state->fog_color.b * (1.0f - fog_factor);
                }
                
                agl_set_pixel(x, y, color);
            }
        }
    }
}

static bool agl_backface_cull(agl_vertex_t *v0, agl_vertex_t *v1, agl_vertex_t *v2) {
    if (!g_state->cull_face_enabled) return false;
    
    /* Calculate face normal using cross product */
    float ax = v1->position.x - v0->position.x;
    float ay = v1->position.y - v0->position.y;
    float bx = v2->position.x - v0->position.x;
    float by = v2->position.y - v0->position.y;
    
    float cross_z = ax * by - ay * bx;
    
    if (g_state->cull_face_mode == AGL_BACK) {
        return cross_z < 0; // Cull back faces (CW)
    } else if (g_state->cull_face_mode == AGL_FRONT) {
        return cross_z > 0; // Cull front faces (CCW)
    }
    
    return false;
}

static void agl_rasterize_primitives(void) {
    if (g_state == NULL || g_state->vertex_count == 0) return;
    
    // Debug: log primitive drawing
    extern void serial_puts(const char *s);
    if (g_state->vertex_count > 0) {
        // serial_puts("agl: Drawing primitives\n");
    }
    
    switch (g_state->current_primitive) {
        case AGL_POINTS:
            for (int i = 0; i < g_state->vertex_count; i++) {
                agl_draw_point(&g_state->vertex_buffer[i]);
            }
            break;
            
        case AGL_LINES:
            for (int i = 0; i < g_state->vertex_count - 1; i += 2) {
                agl_draw_line(&g_state->vertex_buffer[i], &g_state->vertex_buffer[i + 1]);
            }
            break;
            
        case AGL_LINE_STRIP:
            for (int i = 0; i < g_state->vertex_count - 1; i++) {
                agl_draw_line(&g_state->vertex_buffer[i], &g_state->vertex_buffer[i + 1]);
            }
            break;
            
        case AGL_TRIANGLES:
            for (int i = 0; i < g_state->vertex_count - 2; i += 3) {
                if (!agl_backface_cull(&g_state->vertex_buffer[i], &g_state->vertex_buffer[i + 1], &g_state->vertex_buffer[i + 2])) {
                    agl_draw_triangle_scanline(&g_state->vertex_buffer[i], &g_state->vertex_buffer[i + 1], &g_state->vertex_buffer[i + 2]);
                }
            }
            break;
            
        case AGL_TRIANGLE_STRIP:
            for (int i = 0; i < g_state->vertex_count - 2; i++) {
                if (i % 2 == 0) {
                    if (!agl_backface_cull(&g_state->vertex_buffer[i], &g_state->vertex_buffer[i + 1], &g_state->vertex_buffer[i + 2])) {
                        agl_draw_triangle_scanline(&g_state->vertex_buffer[i], &g_state->vertex_buffer[i + 1], &g_state->vertex_buffer[i + 2]);
                    }
                } else {
                    if (!agl_backface_cull(&g_state->vertex_buffer[i], &g_state->vertex_buffer[i + 2], &g_state->vertex_buffer[i + 1])) {
                        agl_draw_triangle_scanline(&g_state->vertex_buffer[i], &g_state->vertex_buffer[i + 2], &g_state->vertex_buffer[i + 1]);
                    }
                }
            }
            break;
            
        case AGL_TRIANGLE_FAN:
            for (int i = 1; i < g_state->vertex_count - 1; i++) {
                if (!agl_backface_cull(&g_state->vertex_buffer[0], &g_state->vertex_buffer[i], &g_state->vertex_buffer[i + 1])) {
                    agl_draw_triangle_scanline(&g_state->vertex_buffer[0], &g_state->vertex_buffer[i], &g_state->vertex_buffer[i + 1]);
                }
            }
            break;
            
        case AGL_QUADS:
            for (int i = 0; i < g_state->vertex_count - 3; i += 4) {
                /* Quad as two triangles */
                if (!agl_backface_cull(&g_state->vertex_buffer[i], &g_state->vertex_buffer[i + 1], &g_state->vertex_buffer[i + 2])) {
                    agl_draw_triangle_scanline(&g_state->vertex_buffer[i], &g_state->vertex_buffer[i + 1], &g_state->vertex_buffer[i + 2]);
                }
                if (!agl_backface_cull(&g_state->vertex_buffer[i], &g_state->vertex_buffer[i + 2], &g_state->vertex_buffer[i + 3])) {
                    agl_draw_triangle_scanline(&g_state->vertex_buffer[i], &g_state->vertex_buffer[i + 2], &g_state->vertex_buffer[i + 3]);
                }
            }
            break;
    }
    
    g_state->vertex_count = 0;
}

/* FOG FUNCTIONS */

void aglFogf(uint32_t pname, float param) {
    if (g_state == NULL) return;
    
    switch (pname) {
        case AGL_FOG_DENSITY:
            g_state->fog_density = param;
            break;
        case AGL_FOG_START:
            g_state->fog_start = param;
            break;
        case AGL_FOG_END:
            g_state->fog_end = param;
            break;
        case AGL_FOG_MODE:
            g_state->fog_mode = (uint32_t)param;
            break;
        default:
            g_state->last_error = AGL_INVALID_ENUM;
            break;
    }
}

void aglFogfv(uint32_t pname, const float *params) {
    if (g_state == NULL || params == NULL) return;
    
    switch (pname) {
        case AGL_FOG_COLOR:
            g_state->fog_color.r = params[0];
            g_state->fog_color.g = params[1];
            g_state->fog_color.b = params[2];
            g_state->fog_color.a = params[3];
            break;
        default:
            g_state->last_error = AGL_INVALID_ENUM;
            break;
    }
}

/* ADDITIONAL TEXTURE FUNCTIONS */

void aglActiveTexture(uint32_t texture) {
    /* Multitexturing not supported in this implementation */
    (void)texture;
}

void aglBindRenderbuffer(uint32_t target, uint32_t renderbuffer) {
    /* Renderbuffers not supported in this implementation */
    (void)target;
    (void)renderbuffer;
}

void aglBindFramebuffer(uint32_t target, uint32_t framebuffer) {
    /* Framebuffers not supported in this implementation */
    (void)target;
    (void)framebuffer;
}

/* CLIPPING FUNCTIONS */

static bool agl_clip_point(agl_vec4_t *v) {
    if (v->w <= 0.0f) return true;
    if (v->x < -v->w || v->x > v->w) return true;
    if (v->y < -v->w || v->y > v->w) return true;
    if (v->z < -v->w || v->z > v->w) return true;
    return false;
}

static agl_vec4_t agl_intersect_edge(agl_vec4_t *v0, agl_vec4_t *v1, int plane) {
    agl_vec4_t result;
    float t = 0.0f;
    
    switch (plane) {
        case 0: /* Left plane: x = -w */
            t = (-v0->w - v0->x) / ((v1->x - v0->x) + (v1->w - v0->w));
            break;
        case 1: /* Right plane: x = w */
            t = (v0->w - v0->x) / ((v1->x - v0->x) - (v1->w - v0->w));
            break;
        case 2: /* Bottom plane: y = -w */
            t = (-v0->w - v0->y) / ((v1->y - v0->y) + (v1->w - v0->w));
            break;
        case 3: /* Top plane: y = w */
            t = (v0->w - v0->y) / ((v1->y - v0->y) - (v1->w - v0->w));
            break;
        case 4: /* Near plane: z = -w */
            t = (-v0->w - v0->z) / ((v1->z - v0->z) + (v1->w - v0->w));
            break;
        case 5: /* Far plane: z = w */
            t = (v0->w - v0->z) / ((v1->z - v0->z) - (v1->w - v0->w));
            break;
    }
    
    t = agl_clampf(t, 0.0f, 1.0f);
    
    result.x = v0->x + t * (v1->x - v0->x);
    result.y = v0->y + t * (v1->y - v0->y);
    result.z = v0->z + t * (v1->z - v0->z);
    result.w = v0->w + t * (v1->w - v0->w);
    
    return result;
}

/* ADDITIONAL MATRIX FUNCTIONS */

static agl_mat4_t agl_mat4_inverse(agl_mat4_t m) {
    agl_mat4_t inv;
    
    float det = 0.0f;
    
    inv.m[0] = m.m[5]  * m.m[10] * m.m[15] - m.m[5]  * m.m[11] * m.m[14] - m.m[9]  * m.m[6]  * m.m[15] + m.m[9]  * m.m[7]  * m.m[14] + m.m[13] * m.m[6]  * m.m[11] - m.m[13] * m.m[7]  * m.m[10];
    inv.m[4] = -m.m[4]  * m.m[10] * m.m[15] + m.m[4]  * m.m[11] * m.m[14] + m.m[8]  * m.m[6]  * m.m[15] - m.m[8]  * m.m[7]  * m.m[14] - m.m[12] * m.m[6]  * m.m[11] + m.m[12] * m.m[7]  * m.m[10];
    inv.m[8] = m.m[4]  * m.m[9] * m.m[15] - m.m[4]  * m.m[11] * m.m[13] - m.m[8]  * m.m[5] * m.m[15] + m.m[8]  * m.m[7] * m.m[13] + m.m[12] * m.m[5] * m.m[11] - m.m[12] * m.m[7] * m.m[9];
    inv.m[12] = -m.m[4]  * m.m[9] * m.m[14] + m.m[4]  * m.m[10] * m.m[13] + m.m[8]  * m.m[5] * m.m[14] - m.m[8]  * m.m[6] * m.m[13] - m.m[12] * m.m[5] * m.m[10] + m.m[12] * m.m[6] * m.m[9];
    inv.m[1] = -m.m[1]  * m.m[10] * m.m[15] + m.m[1]  * m.m[11] * m.m[14] + m.m[9]  * m.m[2] * m.m[15] - m.m[9]  * m.m[3] * m.m[14] - m.m[13] * m.m[2] * m.m[11] + m.m[13] * m.m[3] * m.m[10];
    inv.m[5] = m.m[0]  * m.m[10] * m.m[15] - m.m[0]  * m.m[11] * m.m[14] - m.m[8]  * m.m[2] * m.m[15] + m.m[8]  * m.m[3] * m.m[14] + m.m[12] * m.m[2] * m.m[11] - m.m[12] * m.m[3] * m.m[10];
    inv.m[9] = -m.m[0]  * m.m[9] * m.m[15] + m.m[0]  * m.m[11] * m.m[13] + m.m[8]  * m.m[1] * m.m[15] - m.m[8]  * m.m[3] * m.m[13] - m.m[12] * m.m[1] * m.m[11] + m.m[12] * m.m[3] * m.m[9];
    inv.m[13] = m.m[0]  * m.m[9] * m.m[14] - m.m[0]  * m.m[10] * m.m[13] - m.m[8]  * m.m[1] * m.m[14] + m.m[8]  * m.m[2] * m.m[13] + m.m[12] * m.m[1] * m.m[10] - m.m[12] * m.m[2] * m.m[9];
    inv.m[2] = m.m[1]  * m.m[6] * m.m[15] - m.m[1]  * m.m[7] * m.m[14] - m.m[5]  * m.m[2] * m.m[15] + m.m[5]  * m.m[3] * m.m[14] + m.m[13] * m.m[2] * m.m[7] - m.m[13] * m.m[3] * m.m[6];
    inv.m[6] = -m.m[0]  * m.m[6] * m.m[15] + m.m[0]  * m.m[7] * m.m[14] + m.m[4]  * m.m[2] * m.m[15] - m.m[4]  * m.m[3] * m.m[14] - m.m[12] * m.m[2] * m.m[7] + m.m[12] * m.m[3] * m.m[6];
    inv.m[10] = m.m[0]  * m.m[5] * m.m[15] - m.m[0]  * m.m[7] * m.m[13] - m.m[4]  * m.m[1] * m.m[15] + m.m[4]  * m.m[3] * m.m[13] + m.m[12] * m.m[1] * m.m[7] - m.m[12] * m.m[3] * m.m[5];
    inv.m[14] = -m.m[0]  * m.m[5] * m.m[14] + m.m[0]  * m.m[6] * m.m[13] + m.m[4]  * m.m[1] * m.m[14] - m.m[4]  * m.m[2] * m.m[13] - m.m[12] * m.m[1] * m.m[6] + m.m[12] * m.m[2] * m.m[5];
    inv.m[3] = -m.m[1] * m.m[6] * m.m[11] + m.m[1] * m.m[7] * m.m[10] + m.m[5] * m.m[2] * m.m[11] - m.m[5] * m.m[3] * m.m[10] - m.m[9] * m.m[2] * m.m[7] + m.m[9] * m.m[3] * m.m[6];
    inv.m[7] = m.m[0] * m.m[6] * m.m[11] - m.m[0] * m.m[7] * m.m[10] - m.m[4] * m.m[2] * m.m[11] + m.m[4] * m.m[3] * m.m[10] + m.m[8] * m.m[2] * m.m[7] - m.m[8] * m.m[3] * m.m[6];
    inv.m[11] = -m.m[0] * m.m[5] * m.m[11] + m.m[0] * m.m[7] * m.m[9] + m.m[4] * m.m[1] * m.m[11] - m.m[4] * m.m[3] * m.m[9] - m.m[8] * m.m[1] * m.m[7] + m.m[8] * m.m[3] * m.m[5];
    inv.m[15] = m.m[0] * m.m[5] * m.m[10] - m.m[0] * m.m[6] * m.m[9] - m.m[4] * m.m[1] * m.m[10] + m.m[4] * m.m[2] * m.m[9] + m.m[8] * m.m[1] * m.m[6] - m.m[8] * m.m[2] * m.m[5];
    
    det = m.m[0] * inv.m[0] + m.m[1] * inv.m[4] + m.m[2] * inv.m[8] + m.m[3] * inv.m[12];
    
    if (det == 0.0f) {
        agl_mat4_t identity = agl_mat4_identity();
        return identity;
    }
    
    float inv_det = 1.0f / det;
    
    for (int i = 0; i < 16; i++) {
        inv.m[i] *= inv_det;
    }
    
    return inv;
}

static agl_mat4_t agl_mat4_transpose(agl_mat4_t m) {
    agl_mat4_t result;
    result.m[0] = m.m[0]; result.m[4] = m.m[1]; result.m[8] = m.m[2]; result.m[12] = m.m[3];
    result.m[1] = m.m[4]; result.m[5] = m.m[5]; result.m[9] = m.m[6]; result.m[13] = m.m[7];
    result.m[2] = m.m[8]; result.m[6] = m.m[9]; result.m[10] = m.m[10]; result.m[14] = m.m[11];
    result.m[3] = m.m[12]; result.m[7] = m.m[13]; result.m[11] = m.m[14]; result.m[15] = m.m[15];
    return result;
}

void aglLoadIdentity(void);
void aglLoadTransposeMatrix(const float *m) {
    if (g_state == NULL || m == NULL) return;
    
    agl_mat4_t mat;
    mat.m[0] = m[0]; mat.m[4] = m[1]; mat.m[8] = m[2]; mat.m[12] = m[3];
    mat.m[1] = m[4]; mat.m[5] = m[5]; mat.m[9] = m[6]; mat.m[13] = m[7];
    mat.m[2] = m[8]; mat.m[6] = m[9]; mat.m[10] = m[10]; mat.m[14] = m[11];
    mat.m[3] = m[12]; mat.m[7] = m[13]; mat.m[11] = m[14]; mat.m[15] = m[15];
    
    switch (g_state->matrix_mode) {
        case AGL_MODELVIEW:
            g_state->modelview_matrix = mat;
            break;
        case AGL_PROJECTION:
            g_state->projection_matrix = mat;
            break;
        case AGL_TEXTURE:
            g_state->texture_matrix = mat;
            break;
    }
    agl_update_mvp_matrix();
}

void aglMultTransposeMatrix(const float *m) {
    if (g_state == NULL || m == NULL) return;
    
    agl_mat4_t mat;
    mat.m[0] = m[0]; mat.m[4] = m[1]; mat.m[8] = m[2]; mat.m[12] = m[3];
    mat.m[1] = m[4]; mat.m[5] = m[5]; mat.m[9] = m[6]; mat.m[13] = m[7];
    mat.m[2] = m[8]; mat.m[6] = m[9]; mat.m[10] = m[10]; mat.m[14] = m[11];
    mat.m[3] = m[12]; mat.m[7] = m[13]; mat.m[11] = m[14]; mat.m[15] = m[15];
    
    switch (g_state->matrix_mode) {
        case AGL_MODELVIEW:
            g_state->modelview_matrix = agl_mat4_multiply(g_state->modelview_matrix, mat);
            break;
        case AGL_PROJECTION:
            g_state->projection_matrix = agl_mat4_multiply(g_state->projection_matrix, mat);
            break;
        case AGL_TEXTURE:
            g_state->texture_matrix = agl_mat4_multiply(g_state->texture_matrix, mat);
            break;
    }
    agl_update_mvp_matrix();
}

/* ADDITIONAL UTILITY FUNCTIONS */

void aglFrontFace(uint32_t mode) {
    /* Front face winding order - default is counter-clockwise */
    (void)mode;
}

void aglPolygonMode(uint32_t face, uint32_t mode) {
    /* Polygon mode (fill/line/point) - not fully supported */
    (void)face;
    (void)mode;
}

void aglPolygonOffset(float factor, float units) {
    /* Polygon offset for z-fighting - not fully supported */
    (void)factor;
    (void)units;
}

void aglLineWidth(float width) {
    /* Line width - not fully supported */
    (void)width;
}

void aglPointSize(float size) {
    /* Point size - not fully supported */
    (void)size;
}

/* HINTS AND QUERY FUNCTIONS */

void aglHint(uint32_t target, uint32_t mode) {
    /* Rendering hints - not fully supported */
    (void)target;
    (void)mode;
}

void aglGetIntegerv(uint32_t pname, int *params) {
    if (g_state == NULL || params == NULL) return;
    
    switch (pname) {
        case AGL_MAX_TEXTURE_SIZE:
            *params = 1024;
            break;
        case AGL_MAX_LIGHTS:
            *params = MAX_LIGHTS;
            break;
        case AGL_MAX_MODELVIEW_STACK_DEPTH:
            *params = MAX_MATRIX_STACK;
            break;
        case AGL_MAX_PROJECTION_STACK_DEPTH:
            *params = MAX_MATRIX_STACK;
            break;
        case AGL_MAX_TEXTURE_STACK_DEPTH:
            *params = MAX_MATRIX_STACK;
            break;
        default:
            *params = 0;
            break;
    }
}

void aglGetFloatv(uint32_t pname, float *params) {
    if (g_state == NULL || params == NULL) return;
    
    switch (pname) {
        case AGL_MODELVIEW_MATRIX:
            for (int i = 0; i < 16; i++) {
                params[i] = g_state->modelview_matrix.m[i];
            }
            break;
        case AGL_PROJECTION_MATRIX:
            for (int i = 0; i < 16; i++) {
                params[i] = g_state->projection_matrix.m[i];
            }
            break;
        case AGL_TEXTURE_MATRIX:
            for (int i = 0; i < 16; i++) {
                params[i] = g_state->texture_matrix.m[i];
            }
            break;
        default:
            for (int i = 0; i < 16; i++) {
                params[i] = 0.0f;
            }
            break;
    }
}

/* DISPLAY LIST FUNCTIONS (STUBS FOR COMPATIBILITY) */

uint32_t aglGenLists(int range) {
    /* Display lists not supported in this implementation */
    (void)range;
    return 0;
}

void aglDeleteLists(uint32_t list, int range) {
    /* Display lists not supported in this implementation */
    (void)list;
    (void)range;
}

void aglNewList(uint32_t list, uint32_t mode) {
    /* Display lists not supported in this implementation */
    (void)list;
    (void)mode;
}

void aglEndList(void) {
    /* Display lists not supported in this implementation */
}

void aglCallList(uint32_t list) {
    /* Display lists not supported in this implementation */
    (void)list;
}

/* BUFFER OBJECT FUNCTIONS (STUBS FOR COMPATIBILITY) */

void aglGenBuffers(int n, uint32_t *buffers) {
    /* Vertex buffer objects not supported in this implementation */
    if (buffers != NULL) {
        for (int i = 0; i < n; i++) {
            buffers[i] = 0;
        }
    }
}

void aglDeleteBuffers(int n, const uint32_t *buffers) {
    /* Vertex buffer objects not supported in this implementation */
    (void)n;
    (void)buffers;
}

void aglBindBuffer(uint32_t target, uint32_t buffer) {
    /* Vertex buffer objects not supported in this implementation */
    (void)target;
    (void)buffer;
}

void aglBufferData(uint32_t target, int size, const void *data, uint32_t usage) {
    /* Vertex buffer objects not supported in this implementation */
    (void)target;
    (void)size;
    (void)data;
    (void)usage;
}

void aglBufferSubData(uint32_t target, int offset, int size, const void *data) {
    /* Vertex buffer objects not supported in this implementation */
    (void)target;
    (void)offset;
    (void)size;
    (void)data;
}

/* ARRAY FUNCTIONS (STUBS FOR COMPATIBILITY) */

void aglEnableClientState(uint32_t array) {
    /* Client-side vertex arrays not supported in this implementation */
    (void)array;
}

void aglDisableClientState(uint32_t array) {
    /* Client-side vertex arrays not supported in this implementation */
    (void)array;
}

void aglVertexPointer(int size, uint32_t type, int stride, const void *pointer) {
    /* Vertex pointer not supported in this implementation */
    (void)size;
    (void)type;
    (void)stride;
    (void)pointer;
}

void aglNormalPointer(uint32_t type, int stride, const void *pointer) {
    /* Normal pointer not supported in this implementation */
    (void)type;
    (void)stride;
    (void)pointer;
}

void aglColorPointer(int size, uint32_t type, int stride, const void *pointer) {
    /* Color pointer not supported in this implementation */
    (void)size;
    (void)type;
    (void)stride;
    (void)pointer;
}

void aglTexCoordPointer(int size, uint32_t type, int stride, const void *pointer) {
    /* Texcoord pointer not supported in this implementation */
    (void)size;
    (void)type;
    (void)stride;
    (void)pointer;
}

void aglDrawArrays(uint32_t mode, int first, int count) {
    /* Draw arrays not supported in this implementation */
    (void)mode;
    (void)first;
    (void)count;
}

void aglDrawElements(uint32_t mode, int count, uint32_t type, const void *indices) {
    /* Draw elements not supported in this implementation */
    (void)mode;
    (void)count;
    (void)type;
    (void)indices;
}

/* SCISSOR TEST FUNCTIONS */

static bool scissor_enabled = false;
static int scissor_x = 0, scissor_y = 0, scissor_width = 0, scissor_height = 0;

void aglScissor(int x, int y, int width, int height) {
    scissor_x = x;
    scissor_y = y;
    scissor_width = width;
    scissor_height = height;
}

void aglEnable_ScissorTest(void) {
    scissor_enabled = true;
}

void aglDisable_ScissorTest(void) {
    scissor_enabled = false;
}

/* STENCIL TEST FUNCTIONS */

static bool stencil_enabled = false;
static uint8_t *stencil_buffer = NULL;
static int stencil_clear_value = 0;
static uint32_t stencil_func = AGL_ALWAYS;
static int stencil_ref = 0;
static uint32_t stencil_mask = 0xFF;
static uint32_t stencil_fail = AGL_KEEP;
static uint32_t stencil_zfail = AGL_KEEP;
static uint32_t stencil_zpass = AGL_KEEP;

void aglStencilFunc(uint32_t func, int ref, uint32_t mask) {
    stencil_func = func;
    stencil_ref = ref;
    stencil_mask = mask;
}

void aglStencilOp(uint32_t sfail, uint32_t dpfail, uint32_t dppass) {
    stencil_fail = sfail;
    stencil_zfail = dpfail;
    stencil_zpass = dppass;
}

void aglClearStencil(int s) {
    stencil_clear_value = s;
}

void aglEnable_StencilTest(void) {
    stencil_enabled = true;
}

void aglDisable_StencilTest(void) {
    stencil_enabled = false;
}

/* COLOR MASK FUNCTIONS */

static bool color_mask_r = true;
static bool color_mask_g = true;
static bool color_mask_b = true;
static bool color_mask_a = true;

void aglColorMask(bool red, bool green, bool blue, bool alpha) {
    color_mask_r = red;
    color_mask_g = green;
    color_mask_b = blue;
    color_mask_a = alpha;
}

/* DEPTH MASK FUNCTIONS */

static bool depth_mask = true;

void aglDepthMask(bool flag) {
    depth_mask = flag;
}

/* ADDITIONAL TEXTURE FUNCTIONS */

void aglTexSubImage2D(uint32_t target, int level, int xoffset, int yoffset,
                      int width, int height, uint32_t format, uint32_t type, const void *pixels) {
    if (g_state == NULL) return;
    
    if (target != AGL_TEXTURE_2D) {
        g_state->last_error = AGL_INVALID_ENUM;
        return;
    }
    
    /* Find current texture */
    for (int i = 0; i < MAX_TEXTURES; i++) {
        if (g_state->textures[i].id == g_state->current_texture) {
            agl_texture_t *tex = &g_state->textures[i];
            
            if (tex->data == NULL) return;
            
            int bytes_per_pixel = (tex->format == AGL_RGB) ? 3 : 4;
            
            /* Copy sub-image data */
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int src_index = (y * width + x) * bytes_per_pixel;
                    int dst_index = ((y + yoffset) * tex->width + (x + xoffset)) * bytes_per_pixel;
                    
                    if (dst_index + bytes_per_pixel <= tex->width * tex->height * bytes_per_pixel) {
                        for (int c = 0; c < bytes_per_pixel; c++) {
                            tex->data[dst_index + c] = ((uint8_t *)pixels)[src_index + c];
                        }
                    }
                }
            }
            return;
        }
    }
}

void aglCopyTexImage2D(uint32_t target, int level, uint32_t internalformat,
                       int x, int y, int width, int height, int border) {
    /* Copy texture from framebuffer - not fully supported */
    (void)target;
    (void)level;
    (void)internalformat;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)border;
}

void aglCopyTexSubImage2D(uint32_t target, int level, int xoffset, int yoffset,
                          int x, int y, int width, int height) {
    /* Copy texture sub-image from framebuffer - not fully supported */
    (void)target;
    (void)level;
    (void)xoffset;
    (void)yoffset;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

void aglGenerateMipmap(uint32_t target) {
    /* Mipmap generation - not fully supported */
    (void)target;
}

/* MIPMAP FUNCTIONS */

void aglTexImage1D(uint32_t target, int level, int internalformat,
                   int width, int border, uint32_t format, uint32_t type, const void *pixels) {
    /* 1D textures not supported in this implementation */
    (void)target;
    (void)level;
    (void)internalformat;
    (void)width;
    (void)border;
    (void)format;
    (void)type;
    (void)pixels;
}

void aglTexImage3D(uint32_t target, int level, int internalformat,
                   int width, int height, int depth, int border,
                   uint32_t format, uint32_t type, const void *pixels) {
    /* 3D textures not supported in this implementation */
    (void)target;
    (void)level;
    (void)internalformat;
    (void)width;
    (void)height;
    (void)depth;
    (void)border;
    (void)format;
    (void)type;
    (void)pixels;
}

void aglTexSubImage1D(uint32_t target, int level, int xoffset, int width,
                      uint32_t format, uint32_t type, const void *pixels) {
    /* 1D textures not supported in this implementation */
    (void)target;
    (void)level;
    (void)xoffset;
    (void)width;
    (void)format;
    (void)type;
    (void)pixels;
}

void aglTexSubImage3D(uint32_t target, int level, int xoffset, int yoffset, int zoffset,
                      int width, int height, int depth, uint32_t format, uint32_t type, const void *pixels) {
    /* 3D textures not supported in this implementation */
    (void)target;
    (void)level;
    (void)xoffset;
    (void)yoffset;
    (void)zoffset;
    (void)width;
    (void)height;
    (void)depth;
    (void)format;
    (void)type;
    (void)pixels;
}

/* TEXTURE ENVIRONMENT FUNCTIONS */

void aglTexEnvf(uint32_t target, uint32_t pname, float param) {
    /* Texture environment not fully supported */
    (void)target;
    (void)pname;
    (void)param;
}

void aglTexEnvfv(uint32_t target, uint32_t pname, const float *params) {
    /* Texture environment not fully supported */
    (void)target;
    (void)pname;
    (void)params;
}

void aglTexEnvi(uint32_t target, uint32_t pname, int param) {
    /* Texture environment not fully supported */
    (void)target;
    (void)pname;
    (void)param;
}

void aglTexEnviv(uint32_t target, uint32_t pname, const int *params) {
    /* Texture environment not fully supported */
    (void)target;
    (void)pname;
    (void)params;
}

/* TEXTURE COORDINATE GENERATION */

void aglTexGenf(uint32_t coord, uint32_t pname, float param) {
    /* Texture coordinate generation not fully supported */
    (void)coord;
    (void)pname;
    (void)param;
}

void aglTexGenfv(uint32_t coord, uint32_t pname, const float *params) {
    /* Texture coordinate generation not fully supported */
    (void)coord;
    (void)pname;
    (void)params;
}

void aglTexGend(uint32_t coord, uint32_t pname, double param) {
    /* Texture coordinate generation not fully supported */
    (void)coord;
    (void)pname;
    (void)param;
}

void aglTexGendv(uint32_t coord, uint32_t pname, const double *params) {
    /* Texture coordinate generation not fully supported */
    (void)coord;
    (void)pname;
    (void)params;
}

void aglTexGeni(uint32_t coord, uint32_t pname, int param) {
    /* Texture coordinate generation not fully supported */
    (void)coord;
    (void)pname;
    (void)param;
}

void aglTexGeniv(uint32_t coord, uint32_t pname, const int *params) {
    /* Texture coordinate generation not fully supported */
    (void)coord;
    (void)pname;
    (void)params;
}

void aglEnable_TexGen(uint32_t coord) {
    /* Texture coordinate generation not fully supported */
    (void)coord;
}

void aglDisable_TexGen(uint32_t coord) {
    /* Texture coordinate generation not fully supported */
    (void)coord;
}

/* ALPHA TEST FUNCTIONS */

static bool alpha_test_enabled = false;
static uint32_t alpha_func = AGL_ALWAYS;
static float alpha_ref = 0.0f;

void aglAlphaFunc(uint32_t func, float ref) {
    alpha_func = func;
    alpha_ref = ref;
}

void aglEnable_AlphaTest(void) {
    alpha_test_enabled = true;
}

void aglDisable_AlphaTest(void) {
    alpha_test_enabled = false;
}

/* LOGIC OP FUNCTIONS */

static bool logic_op_enabled = false;
static uint32_t logic_op_mode = 0;

void aglLogicOp(uint32_t opcode) {
    logic_op_mode = opcode;
}

void aglEnable_ColorLogicOp(void) {
    logic_op_enabled = true;
}

void aglDisable_ColorLogicOp(void) {
    logic_op_enabled = false;
}

/* ACCUMULATION BUFFER FUNCTIONS */

void aglClearAccum(float r, float g, float b, float a) {
    /* Accumulation buffer not supported in this implementation */
    (void)r;
    (void)g;
    (void)b;
    (void)a;
}

void aglAccum(uint32_t op, float value) {
    /* Accumulation buffer not supported in this implementation */
    (void)op;
    (void)value;
}

void aglLoadMatrixd(const double *m) {
    if (g_state == NULL || m == NULL) return;
    
    float fm[16];
    for (int i = 0; i < 16; i++) {
        fm[i] = (float)m[i];
    }
    aglLoadMatrix(fm);
}

void aglMultMatrixd(const double *m) {
    if (g_state == NULL || m == NULL) return;
    
    float fm[16];
    for (int i = 0; i < 16; i++) {
        fm[i] = (float)m[i];
    }
    aglMultMatrix(fm);
}

void aglTranslated(double x, double y, double z) {
    aglTranslate((float)x, (float)y, (float)z);
}

void aglRotated(double angle, double x, double y, double z) {
    aglRotate((float)angle, (float)x, (float)y, (float)z);
}

void aglScaled(double x, double y, double z) {
    aglScale((float)x, (float)y, (float)z);
}

void aglOrthod(double left, double right, double bottom, double top, double near, double far) {
    aglOrtho((float)left, (float)right, (float)bottom, (float)top, (float)near, (float)far);
}

void aglFrustumd(double left, double right, double bottom, double top, double near, double far) {
    aglFrustum((float)left, (float)right, (float)bottom, (float)top, (float)near, (float)far);
}

void aglColor3d(double r, double g, double b) {
    aglColor3f((float)r, (float)g, (float)b);
}

void aglColor4d(double r, double g, double b, double a) {
    aglColor4f((float)r, (float)g, (float)b, (float)a);
}

void aglNormal3d(double x, double y, double z) {
    aglNormal3f((float)x, (float)y, (float)z);
}

void aglTexCoord2d(double u, double v) {
    aglTexCoord2f((float)u, (float)v);
}

void aglVertex2d(double x, double y) {
    aglVertex2f((float)x, (float)y);
}

void aglVertex3d(double x, double y, double z) {
    aglVertex3f((float)x, (float)y, (float)z);
}

void aglVertex4d(double x, double y, double z, double w) {
    aglVertex4f((float)x, (float)y, (float)z, (float)w);
}

void aglMaterialfv(uint32_t face, uint32_t pname, const float *params);
void aglMateriald(uint32_t face, uint32_t pname, double param) {
    if (g_state == NULL) return;
    
    aglMaterialf(face, pname, (float)param);
}

void aglMaterialdv(uint32_t face, uint32_t pname, const double *params) {
    if (g_state == NULL || params == NULL) return;
    
    float fparams[4];
    for (int i = 0; i < 4; i++) {
        fparams[i] = (float)params[i];
    }
    aglMaterialfv(face, pname, fparams);
}

void aglLightdv(uint32_t light, uint32_t pname, const double *params) {
    if (g_state == NULL || params == NULL) return;
    
    float fparams[4];
    for (int i = 0; i < 4; i++) {
        fparams[i] = (float)params[i];
    }
    aglLightfv(light, pname, fparams);
}

void aglLightd(uint32_t light, uint32_t pname, double param) {
    if (g_state == NULL) return;
    
    float fparam = (float)param;
    aglLightfv(light, pname, &fparam);
}

void aglFogdv(uint32_t pname, const double *params) {
    if (g_state == NULL || params == NULL) return;
    
    float fparams[4];
    for (int i = 0; i < 4; i++) {
        fparams[i] = (float)params[i];
    }
    aglFogfv(pname, fparams);
}

void aglFogd(uint32_t pname, double param) {
    aglFogf(pname, (float)param);
}

void aglClearColord(double r, double g, double b, double a) {
    aglClearColor((float)r, (float)g, (float)b, (float)a);
}

void aglClearDepthd(double depth) {
    aglClearDepth((float)depth);
}

void aglDepthRanged(double near_val, double far_val) {
    /* Depth range not fully supported */
    (void)near_val;
    (void)far_val;
}

void aglViewportd(int x, int y, int width, int height) {
    aglViewport(x, y, width, height);
}

/* PUSH/POP ATTRIBUTES */

typedef struct {
    bool depth_test_enabled;
    bool cull_face_enabled;
    bool lighting_enabled;
    bool texture_2d_enabled;
    bool blend_enabled;
    bool fog_enabled;
    uint32_t depth_func;
    uint32_t cull_face_mode;
    uint32_t shade_model;
    uint32_t blend_src;
    uint32_t blend_dst;
    agl_color_t clear_color;
    float clear_depth;
    agl_color_t current_color;
    agl_vec3_t current_normal;
    agl_vec2_t current_texcoord;
    uint32_t matrix_mode;
    agl_mat4_t modelview_matrix;
    agl_mat4_t projection_matrix;
    agl_mat4_t texture_matrix;
} agl_attrib_stack_t;

static agl_attrib_stack_t attrib_stack[16];
static int attrib_stack_top = 0;

void aglPushAttrib(uint32_t mask) {
    if (attrib_stack_top >= 16) {
        g_state->last_error = AGL_STACK_OVERFLOW;
        return;
    }
    
    agl_attrib_stack_t *attr = &attrib_stack[attrib_stack_top];
    
    if (mask & 0x00000001) { /* AGL_DEPTH_BUFFER_BIT */
        attr->depth_test_enabled = g_state->depth_test_enabled;
        attr->depth_func = g_state->depth_func;
        attr->clear_depth = g_state->clear_depth;
    }
    
    if (mask & 0x00000002) { /* AGL_STENCIL_BUFFER_BIT */
        /* Stencil attributes */
    }
    
    if (mask & 0x00000004) { /* AGL_COLOR_BUFFER_BIT */
        attr->blend_enabled = g_state->blend_enabled;
        attr->blend_src = g_state->blend_src;
        attr->blend_dst = g_state->blend_dst;
    }
    
    if (mask & 0x00000008) { /* AGL_ENABLE_BIT */
        attr->depth_test_enabled = g_state->depth_test_enabled;
        attr->cull_face_enabled = g_state->cull_face_enabled;
        attr->lighting_enabled = g_state->lighting_enabled;
        attr->texture_2d_enabled = g_state->texture_2d_enabled;
        attr->blend_enabled = g_state->blend_enabled;
        attr->fog_enabled = g_state->fog_enabled;
    }
    
    if (mask & 0x00000010) { /* AGL_TEXTURE_BIT */
        attr->texture_2d_enabled = g_state->texture_2d_enabled;
    }
    
    if (mask & 0x00000020) { /* AGL_VIEWPORT_BIT */
        /* Viewport attributes */
    }
    
    if (mask & 0x00000040) { /* AGL_TRANSFORM_BIT */
        attr->matrix_mode = g_state->matrix_mode;
        attr->modelview_matrix = g_state->modelview_matrix;
        attr->projection_matrix = g_state->projection_matrix;
        attr->texture_matrix = g_state->texture_matrix;
    }
    
    attrib_stack_top++;
}

void aglPopAttrib(void) {
    if (attrib_stack_top <= 0) {
        g_state->last_error = AGL_STACK_UNDERFLOW;
        return;
    }
    
    attrib_stack_top--;
    agl_attrib_stack_t *attr = &attrib_stack[attrib_stack_top];
    
    g_state->depth_test_enabled = attr->depth_test_enabled;
    g_state->cull_face_enabled = attr->cull_face_enabled;
    g_state->lighting_enabled = attr->lighting_enabled;
    g_state->texture_2d_enabled = attr->texture_2d_enabled;
    g_state->blend_enabled = attr->blend_enabled;
    g_state->fog_enabled = attr->fog_enabled;
    g_state->depth_func = attr->depth_func;
    g_state->cull_face_mode = attr->cull_face_mode;
    g_state->shade_model = attr->shade_model;
    g_state->blend_src = attr->blend_src;
    g_state->blend_dst = attr->blend_dst;
    g_state->clear_color = attr->clear_color;
    g_state->clear_depth = attr->clear_depth;
    g_state->current_color = attr->current_color;
    g_state->current_normal = attr->current_normal;
    g_state->current_texcoord = attr->current_texcoord;
    g_state->matrix_mode = attr->matrix_mode;
    g_state->modelview_matrix = attr->modelview_matrix;
    g_state->projection_matrix = attr->projection_matrix;
    g_state->texture_matrix = attr->texture_matrix;
    
    agl_update_mvp_matrix();
}
