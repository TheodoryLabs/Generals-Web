/*
** GeneralsX Web Port — OpenGL ES 3.0 / WebGL 2.0 Graphics Backend
** Implementation of the DX8Wrapper-compatible API using OpenGL ES 3.0.
**
** This file replaces dx8wrapper.cpp for Emscripten builds.
** The #ifdef __EMSCRIPTEN__ guard ensures it only compiles for web targets.
**
** KEY MAPPING (DX8 → OpenGL ES 3.0):
**   IDirect3DDevice8::DrawIndexedPrimitive → glDrawElements
**   IDirect3DDevice8::SetRenderState       → glEnable/glDisable/glBlendFunc/etc
**   IDirect3DDevice8::SetTexture           → glActiveTexture + glBindTexture
**   IDirect3DDevice8::SetTransform         → uniform mat4 in shader
**   IDirect3DDevice8::CreateVertexBuffer   → glGenBuffers + glBufferData
**   IDirect3DDevice8::CreateTexture        → glGenTextures + glTexImage2D
**   IDirect3DDevice8::BeginScene/EndScene  → no-op (GL has no scene concept)
**   IDirect3DDevice8::Present              → emscripten_webgl_commit_frame
**
** LICENSE: GPL-3.0
*/

#if defined(__EMSCRIPTEN__)

#include "gles3_wrapper.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>

// Forward-declare SDL types so we don't need <SDL.h> at compile time.
// The linker resolves SDL_GL_SwapWindow via -sUSE_SDL=2 at link time.
// EmscriptenMain.cpp (which does include <SDL.h>) provides TheSDL3Window.
struct SDL_Window;
extern "C" void SDL_GL_SwapWindow(SDL_Window *window);

// The SDL window created in EmscriptenMain.cpp
extern SDL_Window *TheSDL3Window;

// ============================================================================
// Static state
// ============================================================================
#include <unordered_map>

static GLES3_PipelineState g_State;
static bool g_Initialized = false;
static std::unordered_map<GLuint, bool> g_TextureHasMipmaps;

void GLES3_Register_Texture(GLuint tex, bool has_mipmaps) {
    g_TextureHasMipmaps[tex] = has_mipmaps;
}

void GLES3_Unregister_Texture(GLuint tex) {
    g_TextureHasMipmaps.erase(tex);
}

bool GLES3_Texture_Has_Mipmaps(GLuint tex) {
    auto it = g_TextureHasMipmaps.find(tex);
    if (it != g_TextureHasMipmaps.end()) {
        return it->second;
    }
    return false;
}


// Shader manager statics
GLuint GLES3_ShaderManager::cached_programs[256] = {0};
GLES3_ProgramUniformLocations GLES3_ShaderManager::cached_locations[256];

// ============================================================================
// Helper: DX8 Blend Mode → GL Blend Factor
// ============================================================================
namespace GLES3_Helpers {

GLenum Blend_To_GL(unsigned int dx8_blend) {
    switch (dx8_blend) {
        case GLES3_BLEND_ZERO:         return GL_ZERO;
        case GLES3_BLEND_ONE:          return GL_ONE;
        case GLES3_BLEND_SRCCOLOR:     return GL_SRC_COLOR;
        case GLES3_BLEND_INVSRCCOLOR:  return GL_ONE_MINUS_SRC_COLOR;
        case GLES3_BLEND_SRCALPHA:     return GL_SRC_ALPHA;
        case GLES3_BLEND_INVSRCALPHA:  return GL_ONE_MINUS_SRC_ALPHA;
        case GLES3_BLEND_DESTALPHA:    return GL_DST_ALPHA;
        case GLES3_BLEND_INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
        case GLES3_BLEND_DESTCOLOR:    return GL_DST_COLOR;
        case GLES3_BLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
        case GLES3_BLEND_SRCALPHASAT:  return GL_SRC_ALPHA_SATURATE;
        default: return GL_ONE;
    }
}

GLenum CmpFunc_To_GL(unsigned int dx8_cmp) {
    switch (dx8_cmp) {
        case GLES3_CMP_NEVER:        return GL_NEVER;
        case GLES3_CMP_LESS:         return GL_LESS;
        case GLES3_CMP_EQUAL:        return GL_EQUAL;
        case GLES3_CMP_LESSEQUAL:    return GL_LEQUAL;
        case GLES3_CMP_GREATER:      return GL_GREATER;
        case GLES3_CMP_NOTEQUAL:     return GL_NOTEQUAL;
        case GLES3_CMP_GREATEREQUAL: return GL_GEQUAL;
        case GLES3_CMP_ALWAYS:       return GL_ALWAYS;
        default: return GL_LEQUAL;
    }
}

GLenum CullMode_To_GL(unsigned int dx8_cull) {
    // DX8: 1=NONE, 2=CW, 3=CCW.  glFrontFace(GL_CW) is set at init, so:
    //   CW-wound  polygons = GL "front faces"
    //   CCW-wound polygons = GL "back faces"
    //
    // D3DCULL_CW(2)  = cull CW-wound polygons  → GL_FRONT
    // D3DCULL_CCW(3) = cull CCW-wound polygons → GL_BACK  ← DX8 default
    // (previous mapping was reversed — caused inside-out rendering whenever
    //  the game explicitly set D3DRS_CULLMODE to D3DCULL_CCW)
    switch (dx8_cull) {
        case 1: return 0;           // NONE — caller disables GL_CULL_FACE
        case 2: return GL_FRONT;    // D3DCULL_CW  → cull CW-wound faces
        case 3: return GL_BACK;     // D3DCULL_CCW → cull CCW-wound faces
        default: return GL_BACK;
    }
}

GLenum PrimType_To_GL(unsigned int dx8_prim) {
    switch (dx8_prim) {
        case GLES3_PT_POINTLIST:     return GL_POINTS;
        case GLES3_PT_LINELIST:      return GL_LINES;
        case GLES3_PT_LINESTRIP:     return GL_LINE_STRIP;
        case GLES3_PT_TRIANGLELIST:  return GL_TRIANGLES;
        case GLES3_PT_TRIANGLESTRIP: return GL_TRIANGLE_STRIP;
        case GLES3_PT_TRIANGLEFAN:   return GL_TRIANGLE_FAN;
        default: return GL_TRIANGLES;
    }
}

GLenum AddressMode_To_GL(unsigned int dx8_addr) {
    // DX8: 1=WRAP, 2=MIRROR, 3=CLAMP
    switch (dx8_addr) {
        case 1: return GL_REPEAT;
        case 2: return GL_MIRRORED_REPEAT;
        case 3: return GL_CLAMP_TO_EDGE;
        default: return GL_REPEAT;
    }
}

GLenum FilterMode_To_GL(unsigned int dx8_filter) {
    // DX8: 1=POINT, 2=LINEAR, 3=ANISOTROPIC
    switch (dx8_filter) {
        case 1: return GL_NEAREST;
        case 2: return GL_LINEAR;
        case 3: return GL_LINEAR; // anisotropic handled via extension
        default: return GL_LINEAR;
    }
}

void Matrix4x4_To_GL(const float* src_row_major, float* dst_col_major) {
    // DX8 uses row-major matrices, OpenGL uses column-major.
    // Since we multiply matrices on the left of column vectors in the shader
    // (e.g., u_World * vec4), and DX8 multiplies row vectors on the left of row-major matrices,
    // the row-major memory layout read by GL as column-major is already the correct transpose (M^T).
    // Therefore, we do NOT transpose the memory in code. Doing so would warp translations and projection.
    memcpy(dst_col_major, src_row_major, 16 * sizeof(float));
}

} // namespace GLES3_Helpers


// ============================================================================
// DX8Wrapper API Implementation — Lifecycle
// ============================================================================

/*
** DX8Wrapper::Init — Create WebGL 2.0 context via Emscripten
**
** Original DX8 version: Loads d3d8.dll, creates IDirect3D8, enumerates
** adapters, creates IDirect3DDevice8 with present parameters.
**
** WebGL version: Creates an Emscripten WebGL2 context on the canvas element.
*/
// This would be called as: DX8Wrapper::Init(canvas_handle)
bool GLES3_Init(void* hwnd, bool lite) {
    if (g_Initialized) return true;

    // Create WebGL 2.0 context
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;  // WebGL 2.0 = OpenGL ES 3.0
    attrs.minorVersion = 0;
    attrs.alpha = false;
    attrs.depth = true;
    attrs.stencil = true;
    attrs.antialias = true;
    attrs.premultipliedAlpha = false;
    attrs.preserveDrawingBuffer = false;
    attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if (ctx <= 0) {
        fprintf(stderr, "ERROR: Failed to create WebGL 2.0 context (code: %d)\n", ctx);
        return false;
    }
    emscripten_webgl_make_context_current(ctx);

    fprintf(stderr, "INFO: WebGL 2.0 context created successfully\n");
    fprintf(stderr, "INFO: GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    fprintf(stderr, "INFO: GL_VERSION: %s\n", glGetString(GL_VERSION));
    fprintf(stderr, "INFO: GL_SHADING_LANGUAGE_VERSION: %s\n",
            glGetString(GL_SHADING_LANGUAGE_VERSION));

    // Initialize state
    memset(&g_State, 0, sizeof(g_State));
    g_State.texture_factor[0] = 1.0f;
    g_State.texture_factor[1] = 1.0f;
    g_State.texture_factor[2] = 1.0f;
    g_State.texture_factor[3] = 1.0f;
    for (int i = 0; i < 8; i++) {
        g_State.texture_stage_state[i][GLES3_TSS_ADDRESSU] = 1; // WRAP
        g_State.texture_stage_state[i][GLES3_TSS_ADDRESSV] = 1; // WRAP
        g_State.texture_stage_state[i][GLES3_TSS_MAGFILTER] = 2; // LINEAR
        g_State.texture_stage_state[i][GLES3_TSS_MINFILTER] = 2; // LINEAR
        g_State.texture_stage_state[i][GLES3_TSS_MIPFILTER] = 0; // NONE
    }


    // Set identity matrices
    float identity[16] = {
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };
    memcpy(g_State.world_matrix, identity, sizeof(identity));
    memcpy(g_State.view_matrix, identity, sizeof(identity));
    memcpy(g_State.projection_matrix, identity, sizeof(identity));
    for (int i = 0; i < 8; i++)
        memcpy(g_State.texture_matrices[i], identity, sizeof(identity));

    // Default material (white)
    g_State.material.diffuse[0] = g_State.material.diffuse[1] =
    g_State.material.diffuse[2] = g_State.material.diffuse[3] = 1.0f;
    g_State.material.ambient[0] = g_State.material.ambient[1] =
    g_State.material.ambient[2] = 0.2f;
    g_State.material.ambient[3] = 1.0f;

    // Default global ambient (gray/dark gray fallback matching shader)
    g_State.global_ambient[0] = g_State.global_ambient[1] =
    g_State.global_ambient[2] = 0.2f;
    g_State.global_ambient[3] = 1.0f;

    // Create VAO (required in OpenGL ES 3.0 / WebGL 2.0)
    glGenVertexArrays(1, &g_State.vao);
    glBindVertexArray(g_State.vao);

    // Default GL state matching DX8 defaults
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW);  // DX8 default winding

    // Initialize shader manager
    if (!GLES3_ShaderManager::Init()) {
        fprintf(stderr, "ERROR: Failed to initialize shader manager\n");
        return false;
    }

    g_Initialized = true;
    return true;
}

void GLES3_Shutdown() {
    if (!g_Initialized) return;

    GLES3_ShaderManager::Shutdown();

    if (g_State.vao) {
        glDeleteVertexArrays(1, &g_State.vao);
        g_State.vao = 0;
    }

    g_Initialized = false;
}


// ============================================================================
// DX8Wrapper API — Scene Management
// ============================================================================

/*
** BeginScene/EndScene — In DX8 these bracket a frame.
** In OpenGL there's no equivalent — these are essentially no-ops.
** We use them to reset per-frame statistics.
*/
void GLES3_Begin_Scene() {
    g_State.draw_calls = 0;
    g_State.triangles_drawn = 0;
}

void GLES3_End_Scene(bool flip_frame) {
    if (flip_frame && TheSDL3Window) {
        SDL_GL_SwapWindow(TheSDL3Window);
    }
}

void GLES3_Swap_Buffers() {
    if (TheSDL3Window) {
        SDL_GL_SwapWindow(TheSDL3Window);
    }
}

void GLES3_Clear(bool clear_color, bool clear_z_stencil,
                  float r, float g, float b, float a,
                  float z, unsigned int stencil) {
    GLbitfield mask = 0;
    if (clear_color) {
        glClearColor(r, g, b, a);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (clear_z_stencil) {
        glClearDepthf(z);
        glClearStencil(stencil);
        mask |= GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    }
    if (mask) glClear(mask);
}


// ============================================================================
// DX8Wrapper API — Render State
// ============================================================================

/*
** Set_DX8_Render_State — Maps D3DRENDERSTATETYPE values to GL calls.
**
** The original dx8wrapper caches these to avoid redundant D3D calls.
** We do the same, plus translate to GL state.
*/
void GLES3_Set_Render_State(unsigned int state, unsigned int value) {
    if (state >= GLES3_RS_MAX) return;
    if (g_State.render_states[state] == value) return; // skip redundant
    g_State.render_states[state] = value;

    switch (state) {
        case GLES3_RS_ZENABLE:
            if (value) glEnable(GL_DEPTH_TEST);
            else       glDisable(GL_DEPTH_TEST);
            break;

        case GLES3_RS_ZWRITEENABLE:
            glDepthMask(value ? GL_TRUE : GL_FALSE);
            break;

        case GLES3_RS_ZFUNC:
            glDepthFunc(GLES3_Helpers::CmpFunc_To_GL(value));
            break;

        case GLES3_RS_ALPHABLENDENABLE:
            if (value) glEnable(GL_BLEND);
            else       glDisable(GL_BLEND);
            break;

        case GLES3_RS_SRCBLEND:
            glBlendFunc(
                GLES3_Helpers::Blend_To_GL(value),
                GLES3_Helpers::Blend_To_GL(
                    g_State.render_states[GLES3_RS_DESTBLEND]));
            break;

        case GLES3_RS_DESTBLEND:
            glBlendFunc(
                GLES3_Helpers::Blend_To_GL(
                    g_State.render_states[GLES3_RS_SRCBLEND]),
                GLES3_Helpers::Blend_To_GL(value));
            break;

        case GLES3_RS_CULLMODE:
            if (value == 1) { // D3DCULL_NONE
                glDisable(GL_CULL_FACE);
            } else {
                glEnable(GL_CULL_FACE);
                glCullFace(GLES3_Helpers::CullMode_To_GL(value));
            }
            break;

        case GLES3_RS_ALPHATESTENABLE:
            // OpenGL ES 3.0 removed alpha test — emulated in fragment shader
            g_State.alpha_test_enabled = (value != 0);
            break;

        case GLES3_RS_ALPHAREF:
            g_State.alpha_ref = value;
            break;

        case GLES3_RS_ALPHAFUNC:
            g_State.alpha_func = value;
            break;

        case GLES3_RS_FOGENABLE:
            g_State.fog_enabled = (value != 0);
            break;

        case GLES3_RS_FOGCOLOR: {
            // DX8 packs as 0xAARRGGBB
            g_State.fog_color[0] = ((value >> 16) & 0xFF) / 255.0f;
            g_State.fog_color[1] = ((value >>  8) & 0xFF) / 255.0f;
            g_State.fog_color[2] = ((value      ) & 0xFF) / 255.0f;
            break;
        }

        case GLES3_RS_LIGHTING:
            g_State.lighting_enabled = (value != 0);
            break;

        case GLES3_RS_AMBIENT: {
            g_State.global_ambient[0] = ((value >> 16) & 0xFF) / 255.0f;
            g_State.global_ambient[1] = ((value >>  8) & 0xFF) / 255.0f;
            g_State.global_ambient[2] = ((value      ) & 0xFF) / 255.0f;
            g_State.global_ambient[3] = ((value >> 24) & 0xFF) / 255.0f;
            break;
        }

        case GLES3_RS_STENCILENABLE:
            if (value) glEnable(GL_STENCIL_TEST);
            else       glDisable(GL_STENCIL_TEST);
            break;

        // States handled in shader uniforms (no direct GL equivalent):
        case GLES3_RS_FOGSTART:
        case GLES3_RS_FOGEND:
        case GLES3_RS_FOGDENSITY:
        case GLES3_RS_COLORVERTEX:
        case GLES3_RS_NORMALIZENORMALS:
            // Stored in g_State, applied via shader uniforms
            break;

        case GLES3_RS_TEXTUREFACTOR: {
            // Unpack 0xAARRGGBB
            g_State.texture_factor[0] = ((value >> 16) & 0xFF) / 255.0f; // Red
            g_State.texture_factor[1] = ((value >>  8) & 0xFF) / 255.0f; // Green
            g_State.texture_factor[2] = ((value      ) & 0xFF) / 255.0f; // Blue
            g_State.texture_factor[3] = ((value >> 24) & 0xFF) / 255.0f; // Alpha
            break;
        }

        case GLES3_RS_ZBIAS:
            if (value > 0) {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-1.0f, -((float)value) * 2.0f);
            } else {
                glDisable(GL_POLYGON_OFFSET_FILL);
            }
            break;

        default:
            // Unhandled state — log for debugging
            // fprintf(stderr, "WARN: Unhandled render state %d = %u\n", state, value);
            break;
    }
}


// ============================================================================
// DX8Wrapper API — Transforms
// ============================================================================

void GLES3_Set_Transform(unsigned int type, const float* matrix_row_major) {
    float* target = nullptr;
    switch (type) {
        case GLES3_TS_WORLD:      target = g_State.world_matrix; break;
        case GLES3_TS_VIEW:       target = g_State.view_matrix; break;
        case GLES3_TS_PROJECTION: target = g_State.projection_matrix; break;
        default:
            if (type >= GLES3_TS_TEXTURE0 && type <= GLES3_TS_TEXTURE3)
                target = g_State.texture_matrices[type - GLES3_TS_TEXTURE0];
            break;
    }
    if (target) {
        memcpy(target, matrix_row_major, 16 * sizeof(float));
    }
}

void GLES3_Get_Transform(unsigned int type, float* matrix_out) {
    const float* src = nullptr;
    switch (type) {
        case GLES3_TS_WORLD:      src = g_State.world_matrix; break;
        case GLES3_TS_VIEW:       src = g_State.view_matrix; break;
        case GLES3_TS_PROJECTION: src = g_State.projection_matrix; break;
        default:
            if (type >= GLES3_TS_TEXTURE0 && type <= GLES3_TS_TEXTURE3)
                src = g_State.texture_matrices[type - GLES3_TS_TEXTURE0];
            break;
    }
    if (src) memcpy(matrix_out, src, 16 * sizeof(float));
}


// ============================================================================
// DX8Wrapper API — Viewport
// ============================================================================

void GLES3_Set_Viewport(unsigned int x, unsigned int y,
                         unsigned int w, unsigned int h,
                         float min_z, float max_z) {
    g_State.viewport = { x, y, w, h, min_z, max_z };
    glViewport(x, y, w, h);
    glDepthRangef(min_z, max_z);
}


// ============================================================================
// DX8Wrapper API — Texture Binding
// ============================================================================

void GLES3_Apply_Texture_Stage_Parameters(unsigned int stage) {
    GLuint tex = g_State.bound_textures[stage];
    if (tex == 0) return;

    glActiveTexture(GL_TEXTURE0 + stage);

    // Apply wrap mode U (S)
    unsigned int u = g_State.texture_stage_state[stage][GLES3_TSS_ADDRESSU];
    if (u != 0) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLES3_Helpers::AddressMode_To_GL(u));
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    }

    // Apply wrap mode V (T)
    unsigned int v = g_State.texture_stage_state[stage][GLES3_TSS_ADDRESSV];
    if (v != 0) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLES3_Helpers::AddressMode_To_GL(v));
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    // Apply filter modes
    unsigned int mag = g_State.texture_stage_state[stage][GLES3_TSS_MAGFILTER];
    if (mag != 0) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLES3_Helpers::FilterMode_To_GL(mag));
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    unsigned int min = g_State.texture_stage_state[stage][GLES3_TSS_MINFILTER];
    unsigned int mip = g_State.texture_stage_state[stage][GLES3_TSS_MIPFILTER];

    // Determine if texture has mipmaps
    bool has_mipmaps = GLES3_Texture_Has_Mipmaps(tex);

    if (min == 0) min = 1; // Default D3D8: POINT (1)

    GLenum gl_min_filter = GL_LINEAR;
    if (!has_mipmaps || mip == 0) {
        // No mipmapping
        gl_min_filter = GLES3_Helpers::FilterMode_To_GL(min);
    } else if (mip == 1) {
        // D3DTEXF_POINT: Nearest mipmap
        gl_min_filter = (min == 1) ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_NEAREST;
    } else {
        // D3DTEXF_LINEAR: Linear mipmap interpolation
        gl_min_filter = (min == 1) ? GL_NEAREST_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_LINEAR;
    }
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_min_filter);
}

void GLES3_Set_Texture(unsigned int stage, GLuint gl_texture) {
    if (stage >= 8) return;
    g_State.bound_textures[stage] = gl_texture;
    glActiveTexture(GL_TEXTURE0 + stage);
    if (gl_texture) {
        glBindTexture(GL_TEXTURE_2D, gl_texture);
        GLES3_Apply_Texture_Stage_Parameters(stage);
    } else {
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void GLES3_Set_Texture_Stage_State(unsigned int stage, unsigned int state,
                                    unsigned int value) {
    if (stage >= 8 || state >= GLES3_TSS_MAX) return;
    g_State.texture_stage_state[stage][state] = value;

    if (g_State.bound_textures[stage] != 0) {
        GLES3_Apply_Texture_Stage_Parameters(stage);
    }
}



// ============================================================================
// DX8Wrapper API — Lighting
// ============================================================================

void GLES3_Set_Light(unsigned int index, const GLES3_Light* light) {
    if (index >= 4) return;
    memcpy(&g_State.lights[index], light, sizeof(GLES3_Light));
}

void GLES3_Enable_Light(unsigned int index, bool enable) {
    if (index >= 4) return;
    g_State.light_enabled[index] = enable;
}

void GLES3_Set_Material(const GLES3_Material* mat) {
    memcpy(&g_State.material, mat, sizeof(GLES3_Material));
}


// ============================================================================
// DX8Wrapper API — Fog
// ============================================================================

void GLES3_Set_Fog(bool enable, float r, float g, float b,
                    float start, float end) {
    g_State.fog_enabled = enable;
    g_State.fog_color[0] = r;
    g_State.fog_color[1] = g;
    g_State.fog_color[2] = b;
    g_State.fog_start = start;
    g_State.fog_end = end;
}


// ============================================================================
// DX8Wrapper API — Drawing
// ============================================================================

/*
** Apply_Pipeline_State — Called before every draw call.
** Uploads the current DX8 state to the emulation shader as uniforms.
** This is where the DX8 fixed-function pipeline gets translated to GLSL.
*/
static void Apply_Pipeline_State() {
    GLuint program = GLES3_ShaderManager::Get_Program(g_State);
    if (program != g_State.active_program) {
        glUseProgram(program);
        g_State.active_program = program;
    }
    GLES3_ShaderManager::Apply_Uniforms(program, g_State);
}

/*
** Draw_Triangles — The main draw call. Maps to glDrawElements.
**
** Original DX8: IDirect3DDevice8::DrawIndexedPrimitive(
**   D3DPT_TRIANGLELIST, min_vertex, num_vertices,
**   start_index, primitive_count)
**
** OpenGL ES 3.0: glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, offset)
*/
void GLES3_Draw_Triangles(unsigned int prim_type,
                           unsigned int start_index,
                           unsigned int prim_count,
                           unsigned int min_vertex,
                           unsigned int num_vertices) {
    Apply_Pipeline_State();

    GLenum gl_prim = GLES3_Helpers::PrimType_To_GL(prim_type);

    unsigned int index_count = 0;
    switch (prim_type) {
        case GLES3_PT_TRIANGLELIST:  index_count = prim_count * 3; break;
        case GLES3_PT_TRIANGLESTRIP: index_count = prim_count + 2; break;
        case GLES3_PT_TRIANGLEFAN:   index_count = prim_count + 2; break;
        case GLES3_PT_LINELIST:      index_count = prim_count * 2; break;
        case GLES3_PT_LINESTRIP:     index_count = prim_count + 1; break;
        case GLES3_PT_POINTLIST:     index_count = prim_count; break;
    }

    glDrawElements(gl_prim, index_count, GL_UNSIGNED_SHORT,
                   (const void*)(start_index * sizeof(unsigned short)));

    g_State.draw_calls++;
    g_State.triangles_drawn += prim_count;
}

/*
** Draw (non-indexed) — Maps to glDrawArrays.
*/
void GLES3_Draw_Arrays(unsigned int prim_type,
                        unsigned int start_vertex,
                        unsigned int prim_count) {
    Apply_Pipeline_State();

    GLenum gl_prim = GLES3_Helpers::PrimType_To_GL(prim_type);
    unsigned int vertex_count = 0;
    switch (prim_type) {
        case GLES3_PT_TRIANGLELIST:  vertex_count = prim_count * 3; break;
        case GLES3_PT_TRIANGLESTRIP: vertex_count = prim_count + 2; break;
        default: vertex_count = prim_count; break;
    }

    glDrawArrays(gl_prim, start_vertex, vertex_count);

    g_State.draw_calls++;
    g_State.triangles_drawn += prim_count;
}


// ============================================================================
// DX8Wrapper API — Vertex/Index Buffer Management
// ============================================================================

GLES3_VertexBuffer* GLES3_VertexBuffer::Create(unsigned int size,
                                                 unsigned int stride,
                                                 unsigned int fvf,
                                                 bool dynamic) {
    auto* vb = new GLES3_VertexBuffer();
    vb->size = size;
    vb->stride = stride;
    vb->fvf = fvf;
    vb->is_dynamic = dynamic;
    vb->lock_ptr = nullptr;

    glGenBuffers(1, &vb->gl_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, vb->gl_buffer);
    glBufferData(GL_ARRAY_BUFFER, size,
                 nullptr, dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);

    return vb;
}

void GLES3_VertexBuffer::Lock(void** data, unsigned int offset,
                               unsigned int lock_size) {
    // WebGL2 doesn't support glMapBuffer. Use a CPU-side staging buffer.
    if (!lock_ptr) {
        lock_ptr = malloc(lock_size > 0 ? lock_size : size);
    }
    *data = lock_ptr;
}

void GLES3_VertexBuffer::Unlock() {
    if (lock_ptr) {
        glBindBuffer(GL_ARRAY_BUFFER, gl_buffer);
        glBufferSubData(GL_ARRAY_BUFFER, 0, size, lock_ptr);
        free(lock_ptr);
        lock_ptr = nullptr;
    }
}

void GLES3_VertexBuffer::Destroy() {
    if (gl_buffer) {
        glDeleteBuffers(1, &gl_buffer);
        gl_buffer = 0;
    }
    if (lock_ptr) {
        free(lock_ptr);
        lock_ptr = nullptr;
    }
}


GLES3_IndexBuffer* GLES3_IndexBuffer::Create(unsigned int size, bool dynamic) {
    auto* ib = new GLES3_IndexBuffer();
    ib->size = size;
    ib->index_count = size / sizeof(unsigned short);
    ib->is_dynamic = dynamic;
    ib->lock_ptr = nullptr;

    glGenBuffers(1, &ib->gl_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->gl_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, size,
                 nullptr, dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);

    return ib;
}

void GLES3_IndexBuffer::Lock(void** data, unsigned int offset,
                              unsigned int lock_size) {
    if (!lock_ptr) {
        lock_ptr = malloc(lock_size > 0 ? lock_size : size);
    }
    *data = lock_ptr;
}

void GLES3_IndexBuffer::Unlock() {
    if (lock_ptr) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl_buffer);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, size, lock_ptr);
        free(lock_ptr);
        lock_ptr = nullptr;
    }
}

void GLES3_IndexBuffer::Destroy() {
    if (gl_buffer) {
        glDeleteBuffers(1, &gl_buffer);
        gl_buffer = 0;
    }
    if (lock_ptr) {
        free(lock_ptr);
        lock_ptr = nullptr;
    }
}


// ============================================================================
// Texture Format Conversion
// ============================================================================

GLenum GLES3_TextureConverter::DX8_Format_To_GL_Internal(unsigned int dx8_fmt) {
    // Map common DX8 formats (D3DFMT_*) to GL internal formats
    // These are the formats actually used by Generals/Zero Hour
    switch (dx8_fmt) {
        case 21: // D3DFMT_A8R8G8B8
            return GL_RGBA8;
        case 22: // D3DFMT_X8R8G8B8
            return GL_RGBA8; // treat as RGBA, ignore alpha
        case 23: // D3DFMT_R5G6B5
            return GL_RGB565;
        case 25: // D3DFMT_A1R5G5B5
            return GL_RGB5_A1;
        case 26: // D3DFMT_A4R4G4B4
            return GL_RGBA4;
        case 50: // D3DFMT_L8
            return GL_R8; // luminance → red channel
        case 51: // D3DFMT_A8L8
            return GL_RG8; // luminance+alpha → RG
        case 28: // D3DFMT_A8
            return GL_R8;
        case 827611204: // D3DFMT_DXT1 (FOURCC)
            return GL_COMPRESSED_RGB_S3TC_DXT1_EXT; // if available
        case 861165636: // D3DFMT_DXT3
            return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
        case 894720068: // D3DFMT_DXT5
            return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        default:
            fprintf(stderr, "WARN: Unknown DX8 texture format %u, defaulting to RGBA8\n",
                    dx8_fmt);
            return GL_RGBA8;
    }
}

// ---------------------------------------------------------------------------
// Create_Texture — Upload one mip level (level 0) of a D3D8 texture to GL.
//
// Each D3D format needs a specific GL source-format + source-type pair.
// The most important correctness issue is BGRA vs RGBA byte ordering:
//   D3DFMT_A8R8G8B8 / X8R8G8B8 pixels are stored BGRA in memory (the 'R'
//   in the name is the most-significant byte; in little-endian the byte
//   stream is B,G,R,A).  We upload them with GL_RGBA/GL_UNSIGNED_BYTE which
//   assigns B→R slot, G→G slot, R→B slot, A→A slot.  We then fix the
//   channel mapping with GL_TEXTURE_SWIZZLE so that sampling reads correctly.
//   WebGL2 / GLES3 support GL_TEXTURE_SWIZZLE_* natively.
//
// For DXT compressed textures glGenerateMipmap on a compressed base is
// unreliable; Upload_To_GL (d3d8.h) uploads each mip level separately and
// passes mip_levels=1 here to suppress the auto-generate call.
// ---------------------------------------------------------------------------
GLuint GLES3_TextureConverter::Create_Texture(unsigned int width,
                                               unsigned int height,
                                               unsigned int dx8_format,
                                               unsigned int mip_levels,
                                               const void* data) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    GLenum internal_fmt = DX8_Format_To_GL_Internal(dx8_format);

    bool is_compressed = (internal_fmt == GL_COMPRESSED_RGB_S3TC_DXT1_EXT  ||
                          internal_fmt == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT ||
                          internal_fmt == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT);

    if (is_compressed) {
        // WebGL2: WEBGL_compressed_texture_s3tc extension required.
        // The caller is responsible for uploading all mip levels if mip_levels > 1;
        // we only upload the base level here.
        unsigned int block_size = (internal_fmt == GL_COMPRESSED_RGB_S3TC_DXT1_EXT) ? 8 : 16;
        unsigned int data_size = ((width + 3) / 4) * ((height + 3) / 4) * block_size;
        // Set mip level storage range so the driver knows more levels will arrive
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (int)mip_levels - 1);
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, internal_fmt,
                               (GLsizei)width, (GLsizei)height, 0,
                               (GLsizei)data_size, data);
    } else {
        // --- Uncompressed: choose correct GL source format + type ---
        GLenum src_fmt  = GL_RGBA;
        GLenum src_type = GL_UNSIGNED_BYTE;
        bool need_rb_swizzle = false;   // true: stored as BGRA → swizzle R↔B
        bool need_a4r4g4b4_swizzle = false; // true: A4R4G4B4 → swizzle to match GL_RGBA4
        bool need_a1r5g5b5_swizzle = false; // true: A1R5G5B5 → swizzle to match GL_RGBA5551
        bool force_alpha_one = false;   // true: X8R8G8B8 — force alpha channel=1
        bool is_luminance    = false;   // true: L8 — replicate R to G,B; A=1
        bool is_lum_alpha    = false;   // true: A8L8 — R/G → L/A swizzle
        bool is_alpha_only   = false;   // true: A8 — R=G=B=0, A=texel

        switch (dx8_format) {
            case 21: // D3DFMT_A8R8G8B8 — memory bytes: B G R A
                src_fmt = GL_RGBA; src_type = GL_UNSIGNED_BYTE;
                need_rb_swizzle = true;
                break;
            case 22: // D3DFMT_X8R8G8B8 — memory bytes: B G R X
                src_fmt = GL_RGBA; src_type = GL_UNSIGNED_BYTE;
                need_rb_swizzle = true;
                force_alpha_one = true;
                break;
            case 23: // D3DFMT_R5G6B5 — 16-bit, R[15:11] G[10:5] B[4:0]
                src_fmt = GL_RGB; src_type = GL_UNSIGNED_SHORT_5_6_5;
                break;
            case 25: // D3DFMT_A1R5G5B5 — 16-bit, A[15] R[14:10] G[9:5] B[4:0]
                src_fmt = GL_RGBA; src_type = GL_UNSIGNED_SHORT_5_5_5_1;
                need_a1r5g5b5_swizzle = true;
                break;
            case 26: // D3DFMT_A4R4G4B4
                src_fmt = GL_RGBA; src_type = GL_UNSIGNED_SHORT_4_4_4_4;
                need_a4r4g4b4_swizzle = true;
                break;
            case 50: // D3DFMT_L8 — 1 byte luminance per pixel
                src_fmt = GL_RED; src_type = GL_UNSIGNED_BYTE;
                is_luminance = true;
                break;
            case 51: // D3DFMT_A8L8 — 2 bytes: L, A
                src_fmt = GL_RG; src_type = GL_UNSIGNED_BYTE;
                is_lum_alpha = true;
                break;
            case 28: // D3DFMT_A8 — 1 byte alpha per pixel
                src_fmt = GL_RED; src_type = GL_UNSIGNED_BYTE;
                is_alpha_only = true;
                break;
            default:
                src_fmt = GL_RGBA; src_type = GL_UNSIGNED_BYTE;
                need_rb_swizzle = true; // assume BGRA unless otherwise known
                break;
        }

        const void* upload_data = data;
        std::vector<unsigned char> temp_buffer;

        if (data) {
            if (need_rb_swizzle) {
                unsigned int num_pixels = width * height;
                temp_buffer.resize(num_pixels * 4);
                const unsigned char* src = (const unsigned char*)data;
                unsigned char* dst = temp_buffer.data();
                for (unsigned int i = 0; i < num_pixels; i++) {
                    dst[i*4 + 0] = src[i*4 + 2]; // B -> R
                    dst[i*4 + 1] = src[i*4 + 1]; // G -> G
                    dst[i*4 + 2] = src[i*4 + 0]; // R -> B
                    dst[i*4 + 3] = force_alpha_one ? 255 : src[i*4 + 3];
                }
                upload_data = temp_buffer.data();
                src_fmt = GL_RGBA;
            } else if (need_a4r4g4b4_swizzle) {
                unsigned int num_pixels = width * height;
                temp_buffer.resize(num_pixels * sizeof(unsigned short));
                const unsigned short* src = (const unsigned short*)data;
                unsigned short* dst = (unsigned short*)temp_buffer.data();
                for (unsigned int i = 0; i < num_pixels; i++) {
                    unsigned short val = src[i];
                    unsigned short a = (val >> 12) & 0xF;
                    unsigned short r = (val >> 8) & 0xF;
                    unsigned short g = (val >> 4) & 0xF;
                    unsigned short b = val & 0xF;
                    dst[i] = (r << 12) | (g << 8) | (b << 4) | a;
                }
                upload_data = temp_buffer.data();
            } else if (need_a1r5g5b5_swizzle) {
                unsigned int num_pixels = width * height;
                temp_buffer.resize(num_pixels * sizeof(unsigned short));
                const unsigned short* src = (const unsigned short*)data;
                unsigned short* dst = (unsigned short*)temp_buffer.data();
                for (unsigned int i = 0; i < num_pixels; i++) {
                    unsigned short val = src[i];
                    unsigned short a = (val >> 15) & 1;
                    unsigned short r = (val >> 10) & 0x1F;
                    unsigned short g = (val >> 5) & 0x1F;
                    unsigned short b = val & 0x1F;
                    dst[i] = (r << 11) | (g << 6) | (b << 1) | a;
                }
                upload_data = temp_buffer.data();
            } else if (is_luminance) {
                unsigned int num_pixels = width * height;
                temp_buffer.resize(num_pixels * 4);
                const unsigned char* src = (const unsigned char*)data;
                unsigned char* dst = temp_buffer.data();
                for (unsigned int i = 0; i < num_pixels; i++) {
                    unsigned char l = src[i];
                    dst[i*4 + 0] = l;
                    dst[i*4 + 1] = l;
                    dst[i*4 + 2] = l;
                    dst[i*4 + 3] = 255;
                }
                upload_data = temp_buffer.data();
                internal_fmt = GL_RGBA8;
                src_fmt = GL_RGBA;
            } else if (is_lum_alpha) {
                unsigned int num_pixels = width * height;
                temp_buffer.resize(num_pixels * 4);
                const unsigned char* src = (const unsigned char*)data;
                unsigned char* dst = temp_buffer.data();
                for (unsigned int i = 0; i < num_pixels; i++) {
                    unsigned char l = src[i*2 + 0];
                    unsigned char a = src[i*2 + 1];
                    dst[i*4 + 0] = l;
                    dst[i*4 + 1] = l;
                    dst[i*4 + 2] = l;
                    dst[i*4 + 3] = a;
                }
                upload_data = temp_buffer.data();
                internal_fmt = GL_RGBA8;
                src_fmt = GL_RGBA;
            } else if (is_alpha_only) {
                unsigned int num_pixels = width * height;
                temp_buffer.resize(num_pixels * 4);
                const unsigned char* src = (const unsigned char*)data;
                unsigned char* dst = temp_buffer.data();
                for (unsigned int i = 0; i < num_pixels; i++) {
                    unsigned char a = src[i];
                    dst[i*4 + 0] = 0;
                    dst[i*4 + 1] = 0;
                    dst[i*4 + 2] = 0;
                    dst[i*4 + 3] = a;
                }
                upload_data = temp_buffer.data();
                internal_fmt = GL_RGBA8;
                src_fmt = GL_RGBA;
            }
        }

        glTexImage2D(GL_TEXTURE_2D, 0, internal_fmt,
                     (GLsizei)width, (GLsizei)height, 0,
                     src_fmt, src_type, upload_data);

        // Generate mipmaps from the uploaded base level (uncompressed only)
        if (mip_levels != 1) {
            glGenerateMipmap(GL_TEXTURE_2D);
        }
    }

    if (mip_levels != 1) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    GLES3_Register_Texture(tex, mip_levels > 1);
    return tex;
}


// ---------------------------------------------------------------------------
// Update_Texture — Update one mip level (level 0) of an existing D3D8 texture in GL.
// ---------------------------------------------------------------------------
void GLES3_TextureConverter::Update_Texture(GLuint gl_tex_id,
                                            unsigned int width,
                                            unsigned int height,
                                            unsigned int dx8_format,
                                            unsigned int mip_levels,
                                            const void* data) {
    if (!gl_tex_id) return;
    glBindTexture(GL_TEXTURE_2D, gl_tex_id);

    if (!data) return;

    // --- Choose correct GL source format + type ---
    GLenum src_fmt  = GL_RGBA;
    GLenum src_type = GL_UNSIGNED_BYTE;
    bool need_rb_swizzle = false;   // true: stored as BGRA → swizzle R↔B
    bool need_a4r4g4b4_swizzle = false; // true: A4R4G4B4 → swizzle to match GL_RGBA4
    bool need_a1r5g5b5_swizzle = false; // true: A1R5G5B5 → swizzle to match GL_RGBA5551
    bool force_alpha_one = false;   // true: X8R8G8B8 — force alpha channel=1
    bool is_luminance    = false;   // true: L8 — replicate R to G,B; A=1
    bool is_lum_alpha    = false;   // true: A8L8 — R/G → L/A swizzle
    bool is_alpha_only   = false;   // true: A8 — R=G=B=0, A=texel

    switch (dx8_format) {
        case 21: // D3DFMT_A8R8G8B8 — memory bytes: B G R A
            src_fmt = GL_RGBA; src_type = GL_UNSIGNED_BYTE;
            need_rb_swizzle = true;
            break;
        case 22: // D3DFMT_X8R8G8B8 — memory bytes: B G R X
            src_fmt = GL_RGBA; src_type = GL_UNSIGNED_BYTE;
            need_rb_swizzle = true;
            force_alpha_one = true;
            break;
        case 23: // D3DFMT_R5G6B5 — 16-bit, R[15:11] G[10:5] B[4:0]
            src_fmt = GL_RGB; src_type = GL_UNSIGNED_SHORT_5_6_5;
            break;
        case 25: // D3DFMT_A1R5G5B5 — 16-bit, A[15] R[14:10] G[9:5] B[4:0]
            src_fmt = GL_RGBA; src_type = GL_UNSIGNED_SHORT_5_5_5_1;
            need_a1r5g5b5_swizzle = true;
            break;
        case 26: // D3DFMT_A4R4G4B4
            src_fmt = GL_RGBA; src_type = GL_UNSIGNED_SHORT_4_4_4_4;
            need_a4r4g4b4_swizzle = true;
            break;
        case 50: // D3DFMT_L8 — 1 byte luminance per pixel
            src_fmt = GL_RED; src_type = GL_UNSIGNED_BYTE;
            is_luminance = true;
            break;
        case 51: // D3DFMT_A8L8 — 2 bytes: L, A
            src_fmt = GL_RG; src_type = GL_UNSIGNED_BYTE;
            is_lum_alpha = true;
            break;
        case 28: // D3DFMT_A8 — 1 byte alpha per pixel
            src_fmt = GL_RED; src_type = GL_UNSIGNED_BYTE;
            is_alpha_only = true;
            break;
        default:
            src_fmt = GL_RGBA; src_type = GL_UNSIGNED_BYTE;
            need_rb_swizzle = true; // assume BGRA unless otherwise known
            break;
    }

    const void* upload_data = data;
    std::vector<unsigned char> temp_buffer;

    if (need_rb_swizzle) {
        unsigned int num_pixels = width * height;
        temp_buffer.resize(num_pixels * 4);
        const unsigned char* src = (const unsigned char*)data;
        unsigned char* dst = temp_buffer.data();
        for (unsigned int i = 0; i < num_pixels; i++) {
            dst[i*4 + 0] = src[i*4 + 2]; // B -> R
            dst[i*4 + 1] = src[i*4 + 1]; // G -> G
            dst[i*4 + 2] = src[i*4 + 0]; // R -> B
            dst[i*4 + 3] = force_alpha_one ? 255 : src[i*4 + 3];
        }
        upload_data = temp_buffer.data();
        src_fmt = GL_RGBA;
    } else if (need_a4r4g4b4_swizzle) {
        unsigned int num_pixels = width * height;
        temp_buffer.resize(num_pixels * sizeof(unsigned short));
        const unsigned short* src = (const unsigned short*)data;
        unsigned short* dst = (unsigned short*)temp_buffer.data();
        for (unsigned int i = 0; i < num_pixels; i++) {
            unsigned short val = src[i];
            unsigned short a = (val >> 12) & 0xF;
            unsigned short r = (val >> 8) & 0xF;
            unsigned short g = (val >> 4) & 0xF;
            unsigned short b = val & 0xF;
            dst[i] = (r << 12) | (g << 8) | (b << 4) | a;
        }
        upload_data = temp_buffer.data();
    } else if (need_a1r5g5b5_swizzle) {
        unsigned int num_pixels = width * height;
        temp_buffer.resize(num_pixels * sizeof(unsigned short));
        const unsigned short* src = (const unsigned short*)data;
        unsigned short* dst = (unsigned short*)temp_buffer.data();
        for (unsigned int i = 0; i < num_pixels; i++) {
            unsigned short val = src[i];
            unsigned short a = (val >> 15) & 1;
            unsigned short r = (val >> 10) & 0x1F;
            unsigned short g = (val >> 5) & 0x1F;
            unsigned short b = val & 0x1F;
            dst[i] = (r << 11) | (g << 6) | (b << 1) | a;
        }
        upload_data = temp_buffer.data();
    } else if (is_luminance) {
        unsigned int num_pixels = width * height;
        temp_buffer.resize(num_pixels * 4);
        const unsigned char* src = (const unsigned char*)data;
        unsigned char* dst = temp_buffer.data();
        for (unsigned int i = 0; i < num_pixels; i++) {
            unsigned char l = src[i];
            dst[i*4 + 0] = l;
            dst[i*4 + 1] = l;
            dst[i*4 + 2] = l;
            dst[i*4 + 3] = 255;
        }
        upload_data = temp_buffer.data();
        src_fmt = GL_RGBA;
    } else if (is_lum_alpha) {
        unsigned int num_pixels = width * height;
        temp_buffer.resize(num_pixels * 4);
        const unsigned char* src = (const unsigned char*)data;
        unsigned char* dst = temp_buffer.data();
        for (unsigned int i = 0; i < num_pixels; i++) {
            unsigned char l = src[i*2 + 0];
            unsigned char a = src[i*2 + 1];
            dst[i*4 + 0] = l;
            dst[i*4 + 1] = l;
            dst[i*4 + 2] = l;
            dst[i*4 + 3] = a;
        }
        upload_data = temp_buffer.data();
        src_fmt = GL_RGBA;
    } else if (is_alpha_only) {
        unsigned int num_pixels = width * height;
        temp_buffer.resize(num_pixels * 4);
        const unsigned char* src = (const unsigned char*)data;
        unsigned char* dst = temp_buffer.data();
        for (unsigned int i = 0; i < num_pixels; i++) {
            unsigned char a = src[i];
            dst[i*4 + 0] = 0;
            dst[i*4 + 1] = 0;
            dst[i*4 + 2] = 0;
            dst[i*4 + 3] = a;
        }
        upload_data = temp_buffer.data();
        src_fmt = GL_RGBA;
    }

    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    (GLsizei)width, (GLsizei)height,
                    src_fmt, src_type, upload_data);

    if (mip_levels > 1) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
}



// ============================================================================
// Shader Manager Implementation
// ============================================================================

bool GLES3_ShaderManager::Init() {
    memset(cached_programs, 0, sizeof(cached_programs));
    memset(cached_locations, -1, sizeof(cached_locations));
    // The fixed-function emulation shaders are compiled on first use
    // via Get_Program(). This avoids compiling unused variants.
    return true;
}

void GLES3_ShaderManager::Shutdown() {
    for (int i = 0; i < 256; i++) {
        if (cached_programs[i]) {
            glDeleteProgram(cached_programs[i]);
            cached_programs[i] = 0;
        }
    }
}

GLuint GLES3_ShaderManager::Compile_Shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        fprintf(stderr, "ERROR: Shader compilation failed:\n%s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint GLES3_ShaderManager::Link_Program(GLuint vert, GLuint frag) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        fprintf(stderr, "ERROR: Shader program link failed:\n%s\n", log);
        glDeleteProgram(program);
        return 0;
    }

    // Shaders can be detached after linking
    glDetachShader(program, vert);
    glDetachShader(program, frag);
    return program;
}

unsigned int GLES3_ShaderManager::Hash_Pipeline_Config(
        const GLES3_PipelineState& state) {
    // Create a simple hash from the active texture stage ops + lighting/fog state
    // This determines which shader variant to use
    unsigned int hash = 0;
    hash |= (state.lighting_enabled ? 1 : 0);
    hash |= (state.fog_enabled ? 2 : 0);
    hash |= (state.alpha_test_enabled ? 4 : 0);

    // Texture stage count (how many stages are active)
    for (int i = 0; i < 4; i++) {
        if (state.bound_textures[i] != 0) {
            hash |= (8 << i);
        }
    }
    return hash & 0xFF; // 256 variants max
}

// Forward declaration — defined with extern "C" in dx8_fixedfunction_shaders.cpp
extern "C" const char* GLES3_Get_Vertex_Shader_Source(unsigned int config_hash);
extern "C" const char* GLES3_Get_Fragment_Shader_Source(unsigned int config_hash);

void GLES3_ShaderManager::Populate_Locations(GLuint program, unsigned int hash) {
    GLES3_ProgramUniformLocations& locs = cached_locations[hash];
    locs.u_World = glGetUniformLocation(program, "u_World");
    locs.u_View = glGetUniformLocation(program, "u_View");
    locs.u_Projection = glGetUniformLocation(program, "u_Projection");
    locs.u_MatDiffuse = glGetUniformLocation(program, "u_MatDiffuse");
    locs.u_MatAmbient = glGetUniformLocation(program, "u_MatAmbient");
    locs.u_MatSpecular = glGetUniformLocation(program, "u_MatSpecular");
    locs.u_MatEmissive = glGetUniformLocation(program, "u_MatEmissive");
    locs.u_MatPower = glGetUniformLocation(program, "u_MatPower");
    locs.u_LightingEnabled = glGetUniformLocation(program, "u_LightingEnabled");
    locs.u_GlobalAmbient = glGetUniformLocation(program, "u_GlobalAmbient");

    for (int i = 0; i < 4; i++) {
        char name[64];
        snprintf(name, sizeof(name), "u_Lights[%d].diffuse", i);
        locs.u_Lights[i].diffuse = glGetUniformLocation(program, name);
        snprintf(name, sizeof(name), "u_Lights[%d].position", i);
        locs.u_Lights[i].position = glGetUniformLocation(program, name);
        snprintf(name, sizeof(name), "u_Lights[%d].direction", i);
        locs.u_Lights[i].direction = glGetUniformLocation(program, name);
        snprintf(name, sizeof(name), "u_Lights[%d].enabled", i);
        locs.u_Lights[i].enabled = glGetUniformLocation(program, name);
        snprintf(name, sizeof(name), "u_Lights[%d].type", i);
        locs.u_Lights[i].type = glGetUniformLocation(program, name);
        snprintf(name, sizeof(name), "u_Lights[%d].attenuation0", i);
        locs.u_Lights[i].attenuation0 = glGetUniformLocation(program, name);
        snprintf(name, sizeof(name), "u_Lights[%d].attenuation1", i);
        locs.u_Lights[i].attenuation1 = glGetUniformLocation(program, name);
        snprintf(name, sizeof(name), "u_Lights[%d].attenuation2", i);
        locs.u_Lights[i].attenuation2 = glGetUniformLocation(program, name);
    }

    locs.u_FogEnabled = glGetUniformLocation(program, "u_FogEnabled");
    locs.u_FogColor = glGetUniformLocation(program, "u_FogColor");
    locs.u_FogStart = glGetUniformLocation(program, "u_FogStart");
    locs.u_FogEnd = glGetUniformLocation(program, "u_FogEnd");
    locs.u_AlphaTestEnabled = glGetUniformLocation(program, "u_AlphaTestEnabled");
    locs.u_AlphaRef = glGetUniformLocation(program, "u_AlphaRef");
    locs.u_AlphaFunc = glGetUniformLocation(program, "u_AlphaFunc");

    for (int i = 0; i < 4; i++) {
        char name[32];
        snprintf(name, sizeof(name), "u_Texture%d", i);
        locs.u_Texture[i] = glGetUniformLocation(program, name);
    }

    locs.u_TextureStageCount = glGetUniformLocation(program, "u_TextureStageCount");
    locs.u_Stage0_ColorOp = glGetUniformLocation(program, "u_Stage0_ColorOp");
    locs.u_Stage1_ColorOp = glGetUniformLocation(program, "u_Stage1_ColorOp");
    locs.u_Stage0_AlphaOp = glGetUniformLocation(program, "u_Stage0_AlphaOp");
    locs.u_Stage1_AlphaOp = glGetUniformLocation(program, "u_Stage1_AlphaOp");
    locs.u_ColorVertex = glGetUniformLocation(program, "u_ColorVertex");
    locs.u_TextureFactor = glGetUniformLocation(program, "u_TextureFactor");
}

GLuint GLES3_ShaderManager::Get_Program(const GLES3_PipelineState& state) {
    unsigned int hash = Hash_Pipeline_Config(state);

    if (cached_programs[hash] != 0) {
        return cached_programs[hash];
    }

    // Compile shader variant for this pipeline configuration
    const char* vs_src = GLES3_Get_Vertex_Shader_Source(hash);
    const char* fs_src = GLES3_Get_Fragment_Shader_Source(hash);

    GLuint vs = Compile_Shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = Compile_Shader(GL_FRAGMENT_SHADER, fs_src);

    if (vs && fs) {
        cached_programs[hash] = Link_Program(vs, fs);
        if (cached_programs[hash] != 0) {
            Populate_Locations(cached_programs[hash], hash);
        }
    }

    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);

    return cached_programs[hash];
}

void GLES3_ShaderManager::Apply_Uniforms(GLuint program,
                                          const GLES3_PipelineState& state) {
    unsigned int hash = Hash_Pipeline_Config(state);
    const GLES3_ProgramUniformLocations& locs = cached_locations[hash];

    // Upload matrices (transposed for GL column-major order)
    float gl_matrix[16];

    GLES3_Helpers::Matrix4x4_To_GL(state.world_matrix, gl_matrix);
    if (locs.u_World != -1) {
        glUniformMatrix4fv(locs.u_World, 1, GL_FALSE, gl_matrix);
    }

    GLES3_Helpers::Matrix4x4_To_GL(state.view_matrix, gl_matrix);
    if (locs.u_View != -1) {
        glUniformMatrix4fv(locs.u_View, 1, GL_FALSE, gl_matrix);
    }

    GLES3_Helpers::Matrix4x4_To_GL(state.projection_matrix, gl_matrix);
    if (locs.u_Projection != -1) {
        glUniformMatrix4fv(locs.u_Projection, 1, GL_FALSE, gl_matrix);
    }

    // Material
    if (locs.u_MatDiffuse != -1) glUniform4fv(locs.u_MatDiffuse, 1, state.material.diffuse);
    if (locs.u_MatAmbient != -1) glUniform4fv(locs.u_MatAmbient, 1, state.material.ambient);
    if (locs.u_MatSpecular != -1) glUniform4fv(locs.u_MatSpecular, 1, state.material.specular);
    if (locs.u_MatEmissive != -1) glUniform4fv(locs.u_MatEmissive, 1, state.material.emissive);
    if (locs.u_MatPower != -1) glUniform1f(locs.u_MatPower, state.material.power);

    // Lighting
    if (locs.u_LightingEnabled != -1) {
        glUniform1i(locs.u_LightingEnabled, state.lighting_enabled ? 1 : 0);
    }
    if (locs.u_GlobalAmbient != -1) {
        glUniform4fv(locs.u_GlobalAmbient, 1, state.global_ambient);
    }
    for (int i = 0; i < 4; i++) {
        if (state.light_enabled[i]) {
            if (locs.u_Lights[i].diffuse != -1) {
                glUniform4fv(locs.u_Lights[i].diffuse, 1, state.lights[i].diffuse);
            }
            if (locs.u_Lights[i].position != -1) {
                glUniform3fv(locs.u_Lights[i].position, 1, state.lights[i].position);
            }
            if (locs.u_Lights[i].direction != -1) {
                glUniform3fv(locs.u_Lights[i].direction, 1, state.lights[i].direction);
            }
            if (locs.u_Lights[i].enabled != -1) {
                glUniform1i(locs.u_Lights[i].enabled, 1);
            }
        } else {
            // Ensure disabled lights are not used by the vertex shader.
            // Without this, a light that was previously enabled keeps
            // contributing after LightEnable(i, FALSE) is called.
            if (locs.u_Lights[i].enabled != -1) {
                glUniform1i(locs.u_Lights[i].enabled, 0);
            }
        }
    }

    // Fog
    if (locs.u_FogEnabled != -1) {
        glUniform1i(locs.u_FogEnabled, state.fog_enabled ? 1 : 0);
    }
    if (state.fog_enabled) {
        if (locs.u_FogColor != -1) glUniform3fv(locs.u_FogColor, 1, state.fog_color);
        if (locs.u_FogStart != -1) glUniform1f(locs.u_FogStart, state.fog_start);
        if (locs.u_FogEnd != -1) glUniform1f(locs.u_FogEnd, state.fog_end);
    }

    // Alpha test (emulated in shader)
    if (locs.u_AlphaTestEnabled != -1) {
        glUniform1i(locs.u_AlphaTestEnabled, state.alpha_test_enabled ? 1 : 0);
    }
    if (locs.u_AlphaRef != -1) {
        glUniform1f(locs.u_AlphaRef, state.alpha_ref / 255.0f);
    }
    if (locs.u_AlphaFunc != -1) {
        glUniform1i(locs.u_AlphaFunc, state.alpha_func);
    }

    // Texture samplers
    for (int i = 0; i < 4; i++) {
        if (locs.u_Texture[i] != -1) {
            glUniform1i(locs.u_Texture[i], i);
        }
    }

    // Texture stage count — highest active stage index + 1.
    // Without this, u_TextureStageCount defaults to 0 and the fragment
    // shader skips all texture stages, rendering everything untextured.
    {
        int tex_stage_count = 0;
        for (int i = 3; i >= 0; i--) {
            bool has_texture = (state.bound_textures[i] != 0);
            int cop = (int)state.texture_stage_state[i][GLES3_TSS_COLOROP];
            int aop = (int)state.texture_stage_state[i][GLES3_TSS_ALPHAOP];
            // D3DTOP_DISABLE is 1, unset/default is 0. Active ops are anything else.
            bool has_op = (cop > 1) || (aop > 1);
            if (has_texture || has_op) {
                tex_stage_count = i + 1;
                break;
            }
        }
        if (locs.u_TextureStageCount != -1) {
            glUniform1i(locs.u_TextureStageCount, tex_stage_count);
        }
    }

    // Texture stage color/alpha ops (D3DTEXTUREOP per-stage)
    // DX8 default for stage 0 when a texture is set: COLOROP=MODULATE(4),
    // ALPHAOP=SELECTARG1(2). Default for stage 1+: COLOROP=DISABLE(1).
    {
        int s0_cop = (int)state.texture_stage_state[0][GLES3_TSS_COLOROP];
        int s1_cop = (int)state.texture_stage_state[1][GLES3_TSS_COLOROP];
        int s0_aop = (int)state.texture_stage_state[0][GLES3_TSS_ALPHAOP];
        int s1_aop = (int)state.texture_stage_state[1][GLES3_TSS_ALPHAOP];
        // If stage 0 op is 0 (unset), default to MODULATE (4) so texture * diffuse
        if (s0_cop == 0 && state.bound_textures[0] != 0) s0_cop = 4;
        if (s0_aop == 0 && state.bound_textures[0] != 0) s0_aop = 2;
        if (locs.u_Stage0_ColorOp != -1) glUniform1i(locs.u_Stage0_ColorOp, s0_cop);
        if (locs.u_Stage1_ColorOp != -1) glUniform1i(locs.u_Stage1_ColorOp, s1_cop);
        if (locs.u_Stage0_AlphaOp != -1) glUniform1i(locs.u_Stage0_AlphaOp, s0_aop);
        if (locs.u_Stage1_AlphaOp != -1) glUniform1i(locs.u_Stage1_AlphaOp, s1_aop);
    }

    // Color vertex mode (D3DRS_COLORVERTEX): when true, vertex color overrides
    // the material diffuse color during lighting calculations.
    if (locs.u_ColorVertex != -1) {
        glUniform1i(locs.u_ColorVertex, state.render_states[GLES3_RS_COLORVERTEX] ? 1 : 0);
    }

    // Light type and attenuation per light (needed for correct per-type dispatch
    // in the vertex shader — type was uploaded but attenuation was missing).
    for (int i = 0; i < 4; i++) {
        if (state.light_enabled[i]) {
            if (locs.u_Lights[i].type != -1) {
                glUniform1i(locs.u_Lights[i].type, (int)state.lights[i].type);
            }
            if (locs.u_Lights[i].attenuation0 != -1) {
                glUniform1f(locs.u_Lights[i].attenuation0, state.lights[i].attenuation0);
            }
            if (locs.u_Lights[i].attenuation1 != -1) {
                glUniform1f(locs.u_Lights[i].attenuation1, state.lights[i].attenuation1);
            }
            if (locs.u_Lights[i].attenuation2 != -1) {
                glUniform1f(locs.u_Lights[i].attenuation2, state.lights[i].attenuation2);
            }
        }
    }

    if (locs.u_TextureFactor != -1) {
        glUniform4fv(locs.u_TextureFactor, 1, state.texture_factor);
    }
}


// ============================================================================
// Access to pipeline state (for external code that needs it)
// ============================================================================
GLES3_PipelineState* GLES3_Get_State() {
    return &g_State;
}

bool GLES3_Is_Initialized() {
    return g_Initialized;
}

#endif // __EMSCRIPTEN__
