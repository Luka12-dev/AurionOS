/*
 * AurionGL - 3D Graphics Library for AurionOS
 * A lightweight OpenGL-inspired API for real-mode 3D rendering
 * 
 * Features:
 * - Software rasterization with perspective-correct texturing
 * - Z-buffering for proper depth sorting
 * - Vertex transformation pipeline (model, view, projection)
 * - Basic lighting (ambient, diffuse, specular)
 * - Texture mapping with bilinear filtering
 * - Primitive rendering (triangles, quads, lines, points)
 * - Matrix stack for hierarchical transformations
 * - Backface culling and clipping
*/

#ifndef AURIONGL_H
#define AURIONGL_H

#include <stdint.h>
#include <stdbool.h>

/* CONSTANTS AND ENUMS */

/* Matrix modes */
#define AGL_MODELVIEW           0x1700
#define AGL_PROJECTION          0x1701
#define AGL_TEXTURE             0x1702

/* Primitive types */
#define AGL_POINTS              0x0000
#define AGL_LINES               0x0001
#define AGL_LINE_STRIP          0x0003
#define AGL_TRIANGLES           0x0004
#define AGL_TRIANGLE_STRIP      0x0005
#define AGL_TRIANGLE_FAN        0x0006
#define AGL_QUADS               0x0007

/* Capabilities */
#define AGL_DEPTH_TEST          0x0B71
#define AGL_CULL_FACE           0x0B44
#define AGL_LIGHTING            0x0B50
#define AGL_TEXTURE_2D          0x0DE1
#define AGL_BLEND               0x0BE2
#define AGL_FOG                 0x0B60

/* Culling modes */
#define AGL_FRONT               0x0404
#define AGL_BACK                0x0405
#define AGL_FRONT_AND_BACK      0x0408

/* Depth functions */
#define AGL_NEVER               0x0200
#define AGL_LESS                0x0201
#define AGL_EQUAL               0x0202
#define AGL_LEQUAL              0x0203
#define AGL_GREATER             0x0204
#define AGL_NOTEQUAL            0x0205
#define AGL_GEQUAL              0x0206
#define AGL_ALWAYS              0x0207

/* Blending factors */
#define AGL_ZERO                0
#define AGL_ONE                 1
#define AGL_SRC_COLOR           0x0300
#define AGL_ONE_MINUS_SRC_COLOR 0x0301
#define AGL_SRC_ALPHA           0x0302
#define AGL_ONE_MINUS_SRC_ALPHA 0x0303
#define AGL_DST_ALPHA           0x0304
#define AGL_ONE_MINUS_DST_ALPHA 0x0305
#define AGL_DST_COLOR           0x0306
#define AGL_ONE_MINUS_DST_COLOR 0x0307

/* Shading models */
#define AGL_FLAT                0x1D00
#define AGL_SMOOTH              0x1D01

/* Light parameters */
#define AGL_LIGHT0              0x4000
#define AGL_LIGHT1              0x4001
#define AGL_LIGHT2              0x4002
#define AGL_LIGHT3              0x4003
#define AGL_AMBIENT             0x1200
#define AGL_DIFFUSE             0x1201
#define AGL_SPECULAR            0x1202
#define AGL_POSITION            0x1203

/* Material parameters */
#define AGL_EMISSION            0x1600
#define AGL_SHININESS           0x1601
#define AGL_AMBIENT_AND_DIFFUSE 0x1602

/* Fog modes */
#define AGL_EXP                 0x0800
#define AGL_EXP2                0x0801

/* Fog parameters */
#define AGL_FOG_COLOR           0x0B66
#define AGL_FOG_DENSITY         0x0B62
#define AGL_FOG_START           0x0B63
#define AGL_FOG_END             0x0B64
#define AGL_FOG_MODE            0x0B65

/* Stencil operations */
#define AGL_KEEP                0x1E00
#define AGL_REPLACE             0x1E01
#define AGL_INCR                0x1E02
#define AGL_DECR                0x1E03
#define AGL_INVERT              0x150A

/* Stencil functions */
#define AGL_STENCIL_TEST        0x0B90

/* Query targets */
#define AGL_MAX_TEXTURE_SIZE            0x0D33
#define AGL_MAX_LIGHTS                  0x0D31
#define AGL_MAX_MODELVIEW_STACK_DEPTH   0x0D36
#define AGL_MAX_PROJECTION_STACK_DEPTH  0x0D37
#define AGL_MAX_TEXTURE_STACK_DEPTH     0x0D38
#define AGL_MODELVIEW_MATRIX            0x0BA6
#define AGL_PROJECTION_MATRIX           0x0BA7
#define AGL_TEXTURE_MATRIX              0x0BA8

/* Buffer bits and mask bits */
#define AGL_COLOR_BUFFER_BIT    0x00004000
#define AGL_DEPTH_BUFFER_BIT    0x00000100
#define AGL_STENCIL_BUFFER_BIT  0x00000400
#define AGL_ENABLE_BIT          0x00002000
#define AGL_VIEWPORT_BIT        0x00000800
#define AGL_TRANSFORM_BIT       0x00001000
#define AGL_TEXTURE_BIT         0x00040000

/* Texture parameters */
#define AGL_TEXTURE_MAG_FILTER  0x2800
#define AGL_TEXTURE_MIN_FILTER  0x2801
#define AGL_TEXTURE_WRAP_S      0x2802
#define AGL_TEXTURE_WRAP_T      0x2803
#define AGL_NEAREST             0x2600
#define AGL_LINEAR              0x2601
#define AGL_REPEAT              0x2901
#define AGL_CLAMP               0x2900

/* Pixel formats */
#define AGL_RGB                 0x1907
#define AGL_RGBA                0x1908
#define AGL_UNSIGNED_BYTE       0x1401

/* Error codes */
#define AGL_NO_ERROR            0
#define AGL_INVALID_ENUM        0x0500
#define AGL_INVALID_VALUE       0x0501
#define AGL_INVALID_OPERATION   0x0502
#define AGL_STACK_OVERFLOW      0x0503
#define AGL_STACK_UNDERFLOW     0x0504
#define AGL_OUT_OF_MEMORY       0x0505

/* DATA STRUCTURES */

/* 3D Vector */
typedef struct {
    float x, y, z;
} agl_vec3_t;

/* 4D Vector */
typedef struct {
    float x, y, z, w;
} agl_vec4_t;

/* 2D Texture coordinate */
typedef struct {
    float u, v;
} agl_vec2_t;

/* Color (RGBA) */
typedef struct {
    float r, g, b, a;
} agl_color_t;

/* 4x4 Matrix */
typedef struct {
    float m[16];  /* Column-major order like OpenGL */
} agl_mat4_t;

/* Vertex structure (internal) */
typedef struct {
    agl_vec4_t position;      /* Transformed position */
    agl_vec3_t normal;        /* Normal vector */
    agl_color_t color;        /* Vertex color */
    agl_vec2_t texcoord;      /* Texture coordinates */
    float depth;              /* Z-depth for sorting */
} agl_vertex_t;

/* Texture object */
typedef struct {
    uint32_t id;
    int width;
    int height;
    int format;               /* AGL_RGB or AGL_RGBA */
    uint8_t *data;
    int mag_filter;
    int min_filter;
    int wrap_s;
    int wrap_t;
} agl_texture_t;

/* Light structure */
typedef struct {
    bool enabled;
    agl_vec4_t position;      /* Position (w=1) or direction (w=0) */
    agl_color_t ambient;
    agl_color_t diffuse;
    agl_color_t specular;
} agl_light_t;

/* Material structure */
typedef struct {
    agl_color_t ambient;
    agl_color_t diffuse;
    agl_color_t specular;
    agl_color_t emission;
    float shininess;
} agl_material_t;

/* INITIALIZATION AND CONTEXT */

/* Initialize AurionGL with framebuffer dimensions */
void aglInit(int width, int height, uint32_t *framebuffer);

/* Set the framebuffer pointer (for dynamic resolution changes) */
void aglSetFramebuffer(uint32_t *framebuffer);

/* Get current framebuffer dimensions */
void aglGetViewport(int *x, int *y, int *width, int *height);

/* Set viewport (rendering region) */
void aglViewport(int x, int y, int width, int height);

/* Shutdown and cleanup */
void aglShutdown(void);

/* STATE MANAGEMENT */

/* Enable capability */
void aglEnable(uint32_t cap);

/* Disable capability */
void aglDisable(uint32_t cap);

/* Check if capability is enabled */
bool aglIsEnabled(uint32_t cap);

/* Set clear color */
void aglClearColor(float r, float g, float b, float a);

/* Set clear depth */
void aglClearDepth(float depth);

/* Clear buffers */
void aglClear(uint32_t mask);

/* Set depth test function */
void aglDepthFunc(uint32_t func);

/* Set culling face */
void aglCullFace(uint32_t mode);

/* Set shading model */
void aglShadeModel(uint32_t mode);

/* Set blending function */
void aglBlendFunc(uint32_t sfactor, uint32_t dfactor);

/* Get last error */
uint32_t aglGetError(void);

/* MATRIX OPERATIONS */

/* Set current matrix mode */
void aglMatrixMode(uint32_t mode);

/* Load identity matrix */
void aglLoadIdentity(void);

/* Push matrix onto stack */
void aglPushMatrix(void);

/* Pop matrix from stack */
void aglPopMatrix(void);

/* Load matrix */
void aglLoadMatrix(const float *m);

/* Multiply current matrix */
void aglMultMatrix(const float *m);

/* Translation */
void aglTranslate(float x, float y, float z);

/* Rotation (angle in degrees) */
void aglRotate(float angle, float x, float y, float z);

/* Scale */
void aglScale(float x, float y, float z);

/* Perspective projection */
void aglPerspective(float fovy, float aspect, float near, float far);

/* Orthographic projection */
void aglOrtho(float left, float right, float bottom, float top, float near, float far);

/* Look-at camera */
void aglLookAt(float eyeX, float eyeY, float eyeZ,
               float centerX, float centerY, float centerZ,
               float upX, float upY, float upZ);

/* Frustum projection */
void aglFrustum(float left, float right, float bottom, float top, float near, float far);

/* IMMEDIATE MODE RENDERING */

/* Begin primitive */
void aglBegin(uint32_t mode);

/* End primitive */
void aglEnd(void);

/* Set current color */
void aglColor3f(float r, float g, float b);
void aglColor4f(float r, float g, float b, float a);
void aglColor3ub(uint8_t r, uint8_t g, uint8_t b);
void aglColor4ub(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

/* Set current normal */
void aglNormal3f(float x, float y, float z);

/* Set current texture coordinate */
void aglTexCoord2f(float u, float v);

/* Submit vertex */
void aglVertex2f(float x, float y);
void aglVertex3f(float x, float y, float z);
void aglVertex4f(float x, float y, float z, float w);

/* LIGHTING */

/* Set light parameter */
void aglLight(uint32_t light, uint32_t pname, const float *params);
void aglLightfv(uint32_t light, uint32_t pname, const float *params);

/* Set material parameter */
void aglMaterial(uint32_t face, uint32_t pname, const float *params);
void aglMaterialfv(uint32_t face, uint32_t pname, const float *params);
void aglMaterialf(uint32_t face, uint32_t pname, float param);

/* TEXTURING */

/* Generate texture IDs */
void aglGenTextures(int n, uint32_t *textures);

/* Delete textures */
void aglDeleteTextures(int n, const uint32_t *textures);

/* Bind texture */
void aglBindTexture(uint32_t target, uint32_t texture);

/* Upload texture data */
void aglTexImage2D(uint32_t target, int level, int internalformat,
                   int width, int height, int border,
                   uint32_t format, uint32_t type, const void *pixels);

/* Set texture parameter */
void aglTexParameteri(uint32_t target, uint32_t pname, int param);

/* UTILITY FUNCTIONS */

/* Flush rendering pipeline */
void aglFlush(void);

/* Get string information */
const char *aglGetString(uint32_t name);

/* Helper: Draw a cube (unit cube centered at origin) */
void aglDrawCube(void);

/* Helper: Draw a sphere (radius, slices, stacks) */
void aglDrawSphere(float radius, int slices, int stacks);

/* Helper: Draw a cylinder */
void aglDrawCylinder(float radius, float height, int slices);

/* Helper: Draw a torus */
void aglDrawTorus(float innerRadius, float outerRadius, int sides, int rings);

/* MATH UTILITIES (exposed for user convenience) */

/* Vector operations */
agl_vec3_t agl_vec3_add(agl_vec3_t a, agl_vec3_t b);
agl_vec3_t agl_vec3_sub(agl_vec3_t a, agl_vec3_t b);
agl_vec3_t agl_vec3_scale(agl_vec3_t v, float s);
float agl_vec3_dot(agl_vec3_t a, agl_vec3_t b);
agl_vec3_t agl_vec3_cross(agl_vec3_t a, agl_vec3_t b);
float agl_vec3_length(agl_vec3_t v);
agl_vec3_t agl_vec3_normalize(agl_vec3_t v);

/* Matrix operations */
agl_mat4_t agl_mat4_identity(void);
agl_mat4_t agl_mat4_multiply(agl_mat4_t a, agl_mat4_t b);
agl_vec4_t agl_mat4_mul_vec4(agl_mat4_t m, agl_vec4_t v);

#endif /* AURIONGL_H */
