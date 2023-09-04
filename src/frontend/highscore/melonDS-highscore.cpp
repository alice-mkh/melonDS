#include "melonDS-highscore.h"

#include "GPU.h"
#include "NDS.h"
#include "SPU.h"

#include <cmath>

#include "glad/glad.h"

#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 192
#define SAMPLE_RATE 32823.6328125

#define USE_GL 1

struct _melonDSCore
{
  HsCore parent_instance;

  char *save_path;

#if USE_GL
  HsGLContext *context;

  GLuint vertex_buffer;
  GLuint vertex_array;
  GLuint texture;
  GLuint program[3];
#else
  HsSoftwareContext *context;
#endif
};

static HsCore *core;

static void melonds_nintendo_ds_core_init (HsNintendoDsCoreInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (melonDSCore, melonds_core, HS_TYPE_CORE,
                               G_IMPLEMENT_INTERFACE (HS_TYPE_NINTENDO_DS_CORE, melonds_nintendo_ds_core_init))

#if USE_GL
static const char *VERTEX_SHADER = R"(#version 140

in vec2 vPosition;
in vec2 vTexcoord;

smooth out vec2 fTexcoord;

#define SCREEN_SIZE vec2(256, 384)

void main()
{
  vec4 fpos;

  fpos.xy = vPosition;

  fpos.xy = ((fpos.xy * 2.0) / SCREEN_SIZE) - 1.0;
  fpos.y *= -1;
  fpos.z = 0.0;
  fpos.w = 1.0;

  gl_Position = fpos;
  fTexcoord = vTexcoord;
}
)";

static const char *FRAGMENT_SHADER = R"(#version 140

uniform sampler2D ScreenTex;

smooth in vec2 fTexcoord;

out vec4 oColor;

void main()
{
  vec4 pixel = texture(ScreenTex, fTexcoord);

  oColor = vec4(pixel.bgr, 1.0);
}
)";

static void
gl_init (melonDSCore *self)
{
  OpenGL::BuildShaderProgram (VERTEX_SHADER, FRAGMENT_SHADER, self->program, "ScreenShader");

  GLuint pid = self->program[2];
  glBindAttribLocation (pid, 0, "vPosition");
  glBindAttribLocation (pid, 1, "vTexcoord");
  glBindFragDataLocation (pid, 0, "oColor");

  OpenGL::LinkShaderProgram (self->program);

  glUseProgram (pid);
  glUniform1i (glGetUniformLocation (pid, "ScreenTex"), 0);

  const int padded_height = SCREEN_HEIGHT * 2 + 2;
  const float pad_pixels = 1.f / padded_height;

  const float vertices[] = {
    0.f,   0.f,    0.f, 0.f,
    0.f,   192.f,  0.f, 0.5f - pad_pixels,
    256.f, 192.f,  1.f, 0.5f - pad_pixels,
    0.f,   0.f,    0.f, 0.f,
    256.f, 192.f,  1.f, 0.5f - pad_pixels,
    256.f, 0.f,    1.f, 0.f,

    0.f,   192.f,  0.f, 0.5f + pad_pixels,
    0.f,   384.f,  0.f, 1.f,
    256.f, 384.f,  1.f, 1.f,
    0.f,   192.f,  0.f, 0.5f + pad_pixels,
    256.f, 384.f,  1.f, 1.f,
    256.f, 192.f,  1.f, 0.5f + pad_pixels,
  };

  glGenBuffers (1, &self->vertex_buffer);
  glBindBuffer (GL_ARRAY_BUFFER, self->vertex_buffer);
  glBufferData (GL_ARRAY_BUFFER, sizeof (vertices), vertices, GL_STATIC_DRAW);

  glGenVertexArrays (1, &self->vertex_array);
  glBindVertexArray (self->vertex_array);
  glEnableVertexAttribArray (0); // position
  glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 4*4, (void*)(0));
  glEnableVertexAttribArray (1); // texcoord
  glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 4*4, (void*)(2*4));

  glGenTextures (1, &self->texture);
  glActiveTexture (GL_TEXTURE0);
  glBindTexture (GL_TEXTURE_2D, self->texture);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, SCREEN_WIDTH, SCREEN_HEIGHT * 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

  // fill the padding
  u8 zeroData[SCREEN_WIDTH * 4 * 4];
  memset (zeroData, 0, sizeof (zeroData));
  glTexSubImage2D (GL_TEXTURE_2D, 0, 0, SCREEN_HEIGHT, SCREEN_WIDTH, 2, GL_RGBA, GL_UNSIGNED_BYTE, zeroData);
}

static void
gl_draw_frame (melonDSCore *self)
{
  glBindFramebuffer (GL_FRAMEBUFFER, 0);
  glDisable (GL_DEPTH_TEST);
  glDepthMask (false);
  glDisable (GL_BLEND);
  glDisable (GL_SCISSOR_TEST);
  glDisable (GL_STENCIL_TEST);
  glClearColor (0, 0, 0, 1);
  glClear (GL_COLOR_BUFFER_BIT);

  glViewport (0, 0, SCREEN_WIDTH, SCREEN_HEIGHT * 2);
  glUseProgram (self->program[2]);
  glActiveTexture (GL_TEXTURE0);

  GPU::CurGLCompositor->BindOutputTexture (GPU::FrontBuffer);

  glBindBuffer (GL_ARRAY_BUFFER, self->vertex_buffer);
  glBindVertexArray (self->vertex_array);

  glDrawArrays (GL_TRIANGLES, 0, 12);

  glBindBuffer (GL_PIXEL_PACK_BUFFER, 0);
}

static gpointer
get_proc_address (const char *name)
{
  melonDSCore *self = MELONDS_CORE (core);

  return hs_gl_context_get_proc_address (self->context, name);
}
#endif

static gboolean
melonds_core_load_rom (HsCore      *core,
                       const char  *rom_path,
                       const char  *save_path,
                       GError     **error)
{
  melonDSCore *self = MELONDS_CORE (core);

  g_set_str (&self->save_path, save_path);

  if (!NDS::Init ()) {
    g_set_error (error, HS_CORE_ERROR, HS_CORE_ERROR_INTERNAL, "Failed to init core");
    return FALSE;
  }

  GPU::RenderSettings vsettings;

#if USE_GL
  self->context = hs_core_create_gl_context (core, HS_GL_PROFILE_CORE, 3, 2, HS_GL_FLAGS_DEFAULT);

  hs_gl_context_realize (self->context);
  hs_gl_context_set_size (self->context, SCREEN_WIDTH, SCREEN_HEIGHT * 2);

  if (!gladLoadGLLoader (get_proc_address)) {
    g_set_error (error, HS_CORE_ERROR, HS_CORE_ERROR_INTERNAL, "Failed to load GL function for GLAD");
    return FALSE;
  }

  vsettings.GL_ScaleFactor = 1;
  vsettings.GL_BetterPolygons = false;

  GPU::InitRenderer (1); // GL
  GPU::SetRenderSettings (1, vsettings);

  gl_init (self);
#else
  self->context = hs_core_create_software_context (core, SCREEN_WIDTH, SCREEN_HEIGHT * 2, HS_PIXEL_FORMAT_XRGB8888_REV);

  vsettings.Soft_Threaded = false;

  GPU::InitRenderer (0); // Software
  GPU::SetRenderSettings (0, vsettings);
#endif

  SPU::SetInterpolation (0); // 0: none, 1: linear, 2: cosine, 3: cubic
  NDS::SetConsoleType (0); // 0: DS, 1: DSi

  NDS::LoadBIOS ();

  g_autofree char *rom_data = NULL;
  gsize rom_length;
  if (!g_file_get_contents (rom_path, &rom_data, &rom_length, error))
    return FALSE;

  g_autofree char *save_data = NULL;
  gsize save_length = 0;
  g_autoptr (GFile) save_file = g_file_new_for_path (save_path);
  if (g_file_query_exists (save_file, NULL) && !g_file_get_contents (save_path, &save_data, &save_length, error))
    return FALSE;

  if (!NDS::LoadCart ((const u8*) rom_data, rom_length, (const u8*) save_data, save_length)) {
    g_set_error (error, HS_CORE_ERROR, HS_CORE_ERROR_INTERNAL, "Failed to load ROM");
    return FALSE;
  }

  NDS::SetupDirectBoot ("");

  return TRUE;
}

static void
melonds_core_start (HsCore *core)
{
  NDS::Start ();
}

static void
melonds_core_reset (HsCore *core)
{
  NDS::Reset ();
  NDS::SetupDirectBoot ("");
}

static void
melonds_core_stop (HsCore *core)
{
  melonDSCore *self = MELONDS_CORE (core);

  NDS::Halt ();
  NDS::Stop ();

#if USE_GL
  glDeleteTextures (1, &self->texture);
  glDeleteVertexArrays (1, &self->vertex_array);
  glDeleteBuffers (1, &self->vertex_buffer);

  OpenGL::DeleteShaderProgram (self->program);
#endif

  GPU::DeInitRenderer ();
  NDS::DeInit ();

  g_clear_object (&self->context);
  g_clear_pointer (&self->save_path, g_free);
}

const int BUTTON_MAPPING[] = {
  6,  // UP
  7,  // DOWN
  5,  // LEFT
  4,  // RIGHT
  0,  // A
  1,  // B
  10, // X
  11, // Y
  2,  // SELECT
  3,  // START
  9,  // L
  8,  // R
};

static void
melonds_core_poll_input (HsCore *core, HsInputState *input_state)
{
  u32 mask = 0xfff;

  for (int btn = 0; btn < HS_NINTENDO_DS_N_BUTTONS; btn++) {
    if (input_state->nintendo_ds.buttons & 1 << btn)
      mask &= ~(1 << BUTTON_MAPPING[btn]);
  }

  NDS::SetKeyMask (mask);

  if (input_state->nintendo_ds.touch_pressed) {
    u16 x = (u16) round (input_state->nintendo_ds.touch_x * SCREEN_WIDTH);
    u16 y = (u16) round (input_state->nintendo_ds.touch_y * SCREEN_HEIGHT);
    NDS::TouchScreen (x, y);
  } else {
    NDS::ReleaseScreen ();
  }
}

static void
melonds_core_run_frame (HsCore *core)
{
  melonDSCore *self = MELONDS_CORE (core);

  NDS::RunFrame ();

  u32 n_samples = SPU::GetOutputSize ();
  gint16 *samples = g_new0 (gint16, n_samples * 2);
  SPU::ReadOutput (samples, n_samples);
  hs_core_play_samples (core, samples, n_samples * 2);
  g_free (samples);

#if USE_GL
  gl_draw_frame (self);
  hs_gl_context_swap_buffers (self->context);
#else
  size_t screen_size = (SCREEN_WIDTH * SCREEN_HEIGHT * 4);
  u8 *vbuf0 = (u8*) hs_software_context_get_framebuffer (self->context);
  u8 *vbuf1 = vbuf0 + screen_size;
  memcpy (vbuf0, GPU::Framebuffer[GPU::FrontBuffer][0], screen_size);
  memcpy (vbuf1, GPU::Framebuffer[GPU::FrontBuffer][1], screen_size);
#endif
}

static void
melonds_core_load_state (HsCore          *core,
                         const char      *path,
                         HsStateCallback  callback)
{
  g_autofree char *data = NULL;
  gsize length;
  GError *error = NULL;

  if (!g_file_get_contents (path, &data, &length, &error)) {
    callback (core, &error);
    return;
  }

  Savestate *state = new Savestate (data, length, false);

  if (!NDS::DoSavestate (state)) {
    g_set_error (&error, HS_CORE_ERROR, HS_CORE_ERROR_INTERNAL, "Failed to load state");
    callback (core, &error);
  }

  delete state;
  callback (core, NULL);
}

static void
melonds_core_save_state (HsCore          *core,
                         const char      *path,
                         HsStateCallback  callback)
{
  g_autoptr (GFile) file = g_file_new_for_path (path);
  Savestate state;
  GError *error = NULL;

  if (!NDS::DoSavestate (&state)) {
    g_set_error (&error, HS_CORE_ERROR, HS_CORE_ERROR_INTERNAL, "Failed to save state");
    callback (core, &error);
    return;
  }

  if (!g_file_replace_contents (file, (char*) state.Buffer (), state.Length (), NULL, FALSE,
                                G_FILE_CREATE_NONE, NULL, NULL, &error)) {
    callback (core, &error);
    return;
  }


  callback (core, NULL);
}

static double
melonds_core_get_frame_rate (HsCore *core)
{
  return 60;
}

static double
melonds_core_get_aspect_ratio (HsCore *core)
{
  return SCREEN_WIDTH / (double) SCREEN_HEIGHT / 2;
}

static double
melonds_core_get_sample_rate (HsCore *core)
{
  return SAMPLE_RATE;
}

static void
melonds_core_finalize (GObject *object)
{
  core = NULL;

  G_OBJECT_CLASS (melonds_core_parent_class)->finalize (object);
}

static void
melonds_core_class_init (melonDSCoreClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  HsCoreClass *core_class = HS_CORE_CLASS (klass);

  object_class->finalize = melonds_core_finalize;

  core_class->load_rom = melonds_core_load_rom;
  core_class->start = melonds_core_start;
  core_class->reset = melonds_core_reset;
  core_class->stop = melonds_core_stop;
  core_class->poll_input = melonds_core_poll_input;
  core_class->run_frame = melonds_core_run_frame;

  core_class->load_state = melonds_core_load_state;
  core_class->save_state = melonds_core_save_state;

  core_class->get_frame_rate = melonds_core_get_frame_rate;
  core_class->get_aspect_ratio = melonds_core_get_aspect_ratio;

  core_class->get_sample_rate = melonds_core_get_sample_rate;
}

static void
melonds_core_init (melonDSCore *self)
{
  g_assert (core == NULL);

  core = HS_CORE (self);
}

static void
melonds_nintendo_ds_core_init (HsNintendoDsCoreInterface *iface)
{
}

void
melonds_core_log (HsLogLevel level, const char *message)
{
  hs_core_log (core, level, message);
}

const char *
melonds_core_get_save_path (void)
{
  return MELONDS_CORE (core)->save_path;
}

GType
hs_get_core_type (void)
{
  return MELONDS_TYPE_CORE;
}
