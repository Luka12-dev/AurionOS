# AurionGL - 3D Graphics Library for AurionOS

A lightweight, OpenGL-inspired 3D graphics API designed specifically for AurionOS. AurionGL provides software-based 3D rendering with a familiar immediate-mode API.

## Features

- **Software Rasterization**: Pure C implementation, no GPU required
- **Perspective-Correct Rendering**: Proper texture mapping and interpolation
- **Z-Buffering**: Accurate depth sorting for complex scenes
- **Transformation Pipeline**: Full model-view-projection matrix stack
- **Lighting System**: Ambient, diffuse, and specular lighting with up to 4 lights
- **Texture Mapping**: 2D textures with bilinear filtering
- **Primitive Support**: Points, lines, triangles, quads, strips, and fans
- **Backface Culling**: Automatic hidden surface removal
- **Blending**: Alpha blending for transparency effects

## Quick Start

```c
#include "AurionGL/auriongl.h"

/* Initialize AurionGL */
uint32_t *framebuffer = gpu_get_framebuffer();
int width = gpu_get_width();
int height = gpu_get_height();

aglInit(width, height, framebuffer);

/* Setup projection */
aglMatrixMode(AGL_PROJECTION);
aglLoadIdentity();
aglPerspective(45.0f, (float)width / height, 0.1f, 100.0f);

/* Setup camera */
aglMatrixMode(AGL_MODELVIEW);
aglLoadIdentity();
aglLookAt(0.0f, 0.0f, 5.0f,    /* Eye position */
          0.0f, 0.0f, 0.0f,    /* Look at */
          0.0f, 1.0f, 0.0f);   /* Up vector */

/* Enable depth testing */
aglEnable(AGL_DEPTH_TEST);

/* Clear buffers */
aglClearColor(0.1f, 0.1f, 0.2f, 1.0f);
aglClear(AGL_COLOR_BUFFER_BIT | AGL_DEPTH_BUFFER_BIT);

/* Draw a rotating cube */
aglPushMatrix();
aglRotate(angle, 1.0f, 1.0f, 0.0f);
aglColor3f(1.0f, 0.5f, 0.2f);
aglDrawCube();
aglPopMatrix();

/* Flush to screen */
aglFlush();
gpu_flush();
```

## API Overview

### Initialization

```c
void aglInit(int width, int height, uint32_t *framebuffer);
void aglShutdown(void);
void aglViewport(int x, int y, int width, int height);
```

### State Management

```c
void aglEnable(uint32_t cap);
void aglDisable(uint32_t cap);
void aglClearColor(float r, float g, float b, float a);
void aglClear(uint32_t mask);
```

Capabilities:
- `AGL_DEPTH_TEST` - Enable depth buffering
- `AGL_CULL_FACE` - Enable backface culling
- `AGL_LIGHTING` - Enable lighting calculations
- `AGL_TEXTURE_2D` - Enable texture mapping
- `AGL_BLEND` - Enable alpha blending

### Matrix Operations

```c
void aglMatrixMode(uint32_t mode);
void aglLoadIdentity(void);
void aglPushMatrix(void);
void aglPopMatrix(void);

void aglTranslate(float x, float y, float z);
void aglRotate(float angle, float x, float y, float z);
void aglScale(float x, float y, float z);

void aglPerspective(float fovy, float aspect, float near, float far);
void aglOrtho(float left, float right, float bottom, float top, float near, float far);
void aglLookAt(float eyeX, float eyeY, float eyeZ,
               float centerX, float centerY, float centerZ,
               float upX, float upY, float upZ);
```

Matrix modes:
- `AGL_MODELVIEW` - Object transformations
- `AGL_PROJECTION` - Camera projection
- `AGL_TEXTURE` - Texture coordinate transformations

### Immediate Mode Rendering

```c
void aglBegin(uint32_t mode);
void aglEnd(void);

void aglColor3f(float r, float g, float b);
void aglColor4f(float r, float g, float b, float a);
void aglNormal3f(float x, float y, float z);
void aglTexCoord2f(float u, float v);
void aglVertex3f(float x, float y, float z);
```

Primitive modes:
- `AGL_POINTS` - Individual points
- `AGL_LINES` - Line segments
- `AGL_LINE_STRIP` - Connected lines
- `AGL_TRIANGLES` - Individual triangles
- `AGL_TRIANGLE_STRIP` - Triangle strip
- `AGL_TRIANGLE_FAN` - Triangle fan
- `AGL_QUADS` - Quadrilaterals

### Lighting

```c
void aglLightfv(uint32_t light, uint32_t pname, const float *params);
void aglMaterialfv(uint32_t face, uint32_t pname, const float *params);
void aglMaterialf(uint32_t face, uint32_t pname, float param);
```

Example:
```c
aglEnable(AGL_LIGHTING);
aglEnable(AGL_LIGHT0);

float light_pos[] = {5.0f, 5.0f, 5.0f, 1.0f};
float light_diffuse[] = {1.0f, 1.0f, 1.0f, 1.0f};
aglLightfv(AGL_LIGHT0, AGL_POSITION, light_pos);
aglLightfv(AGL_LIGHT0, AGL_DIFFUSE, light_diffuse);

float mat_ambient[] = {0.2f, 0.2f, 0.2f, 1.0f};
float mat_diffuse[] = {0.8f, 0.8f, 0.8f, 1.0f};
aglMaterialfv(AGL_FRONT, AGL_AMBIENT, mat_ambient);
aglMaterialfv(AGL_FRONT, AGL_DIFFUSE, mat_diffuse);
aglMaterialf(AGL_FRONT, AGL_SHININESS, 32.0f);
```

### Texturing

```c
void aglGenTextures(int n, uint32_t *textures);
void aglBindTexture(uint32_t target, uint32_t texture);
void aglTexImage2D(uint32_t target, int level, int internalformat,
                   int width, int height, int border,
                   uint32_t format, uint32_t type, const void *pixels);
void aglTexParameteri(uint32_t target, uint32_t pname, int param);
```

Example:
```c
uint32_t texture;
aglGenTextures(1, &texture);
aglBindTexture(AGL_TEXTURE_2D, texture);

/* Upload texture data (RGB format) */
aglTexImage2D(AGL_TEXTURE_2D, 0, AGL_RGB, 256, 256, 0,
              AGL_RGB, AGL_UNSIGNED_BYTE, texture_data);

/* Set filtering */
aglTexParameteri(AGL_TEXTURE_2D, AGL_TEXTURE_MAG_FILTER, AGL_LINEAR);
aglTexParameteri(AGL_TEXTURE_2D, AGL_TEXTURE_MIN_FILTER, AGL_LINEAR);

/* Enable texturing */
aglEnable(AGL_TEXTURE_2D);
```

### Helper Functions

```c
void aglDrawCube(void);
void aglDrawSphere(float radius, int slices, int stacks);
void aglDrawCylinder(float radius, float height, int slices);
void aglDrawTorus(float innerRadius, float outerRadius, int sides, int rings);
```

## Example: Spinning Cube

```c
#include "AurionGL/auriongl.h"

void render_frame(float time) {
    /* Clear screen */
    aglClearColor(0.1f, 0.1f, 0.2f, 1.0f);
    aglClear(AGL_COLOR_BUFFER_BIT | AGL_DEPTH_BUFFER_BIT);
    
    /* Setup camera */
    aglMatrixMode(AGL_PROJECTION);
    aglLoadIdentity();
    aglPerspective(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    
    aglMatrixMode(AGL_MODELVIEW);
    aglLoadIdentity();
    aglTranslate(0.0f, 0.0f, -5.0f);
    aglRotate(time * 50.0f, 1.0f, 1.0f, 0.0f);
    
    /* Draw cube */
    aglColor3f(1.0f, 0.5f, 0.2f);
    aglDrawCube();
    
    /* Flush to screen */
    aglFlush();
}
```

## Example: Lit Scene

```c
void render_lit_scene(float time) {
    aglClear(AGL_COLOR_BUFFER_BIT | AGL_DEPTH_BUFFER_BIT);
    
    /* Setup lighting */
    aglEnable(AGL_LIGHTING);
    aglEnable(AGL_LIGHT0);
    
    float light_pos[] = {5.0f, 5.0f, 5.0f, 1.0f};
    aglLightfv(AGL_LIGHT0, AGL_POSITION, light_pos);
    
    /* Setup camera */
    aglMatrixMode(AGL_PROJECTION);
    aglLoadIdentity();
    aglPerspective(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    
    aglMatrixMode(AGL_MODELVIEW);
    aglLoadIdentity();
    aglLookAt(0.0f, 2.0f, 8.0f,
              0.0f, 0.0f, 0.0f,
              0.0f, 1.0f, 0.0f);
    
    /* Draw rotating sphere */
    aglPushMatrix();
    aglRotate(time * 30.0f, 0.0f, 1.0f, 0.0f);
    aglTranslate(2.0f, 0.0f, 0.0f);
    aglColor3f(0.8f, 0.3f, 0.3f);
    aglDrawSphere(0.7f, 30, 30);
    aglPopMatrix();
    
    aglFlush();
}
```

## Performance Considerations

AurionGL is a software rasterizer, so performance depends on:

1. **Triangle Count**: Keep scenes under 10,000 triangles for real-time rendering
2. **Resolution**: Lower resolutions render faster (800x600 vs 1920x1080)
3. **Texture Size**: Use power-of-2 textures (256x256, 512x512)
4. **Lighting**: Each enabled light adds overhead
5. **Blending**: Alpha blending is expensive

Tips for better performance:
- Use backface culling (`aglEnable(AGL_CULL_FACE)`)
- Minimize state changes
- Use triangle strips instead of individual triangles
- Disable features you don't need (lighting, texturing)
- Use flat shading when smooth shading isn't needed

## Architecture

AurionGL uses a classic immediate-mode rendering pipeline:

1. **Vertex Submission**: `aglVertex*()` calls store vertices
2. **Transformation**: Model-view-projection matrices applied
3. **Lighting**: Per-vertex lighting calculations
4. **Clipping**: Primitives clipped to view frustum
5. **Rasterization**: Scanline conversion with interpolation
6. **Depth Test**: Z-buffer comparison
7. **Texturing**: Perspective-correct texture sampling
8. **Blending**: Alpha blending if enabled
9. **Framebuffer Write**: Final pixel color written

## Integration with AurionOS

To use AurionGL in your AurionOS application:

1. Include the header: `#include "AurionGL/auriongl.h"`
2. Link with AurionGL: Add `AurionGL/auriongl.c` to your build
3. Initialize with your framebuffer
4. Render your 3D scene
5. Call `gpu_flush()` to display

## Limitations

- Software rendering only (no hardware acceleration)
- Maximum 4 lights
- No shaders (fixed-function pipeline)
- No advanced features (shadows, reflections, etc.)
- Limited to 10,000 vertices per frame

## Future Enhancements

Potential improvements for future versions:
- Vertex buffer objects (VBO) for better performance
- Display lists for static geometry
- Mipmapping for texture filtering
- Fog effects
- Stencil buffer
- More primitive types (polygons, bezier curves)
- SIMD optimizations

## License

Part of AurionOS - see main OS license.

## Credits

Designed and implemented for AurionOS by the AurionOS development team.
Inspired by OpenGL 1.x immediate mode API.
