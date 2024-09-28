#include "melonDS-highscore.h"

#include "FreeBIOS.h"
#include "GPU.h"
#include "NDS.h"
#include "Platform.h"
#include "SPI.h"
#include "SPU.h"

#include <cmath>

#include "glad/glad.h"

#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 192
#define SAMPLE_RATE 32823.6328125

#define USE_GL 1

using namespace melonDS;

struct _melonDSCore
{
  HsCore parent_instance;

  NDS *console;
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

void main()
{
  gl_Position = vec4(vPosition * 2.0 - 1.0, 0.0, 1.0);
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
    0.f, 0.f,  0.f, 0.f,
    0.f, 0.5f, 0.f, 0.5f - pad_pixels,
    1.f, 0.5f, 1.f, 0.5f - pad_pixels,
    0.f, 0.f,  0.f, 0.f,
    1.f, 0.5f, 1.f, 0.5f - pad_pixels,
    1.f, 0.f,  1.f, 0.f,

    0.f, 0.5f, 0.f, 0.5f + pad_pixels,
    0.f, 1.f,  0.f, 1.f,
    1.f, 1.f,  1.f, 1.f,
    0.f, 0.5f, 0.f, 0.5f + pad_pixels,
    1.f, 1.f,  1.f, 1.f,
    1.f, 0.5f, 1.f, 0.5f + pad_pixels,
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
}

static void
gl_draw_frame (melonDSCore *self)
{
  int front_buf = self->console->GPU.FrontBuffer;
//  if (!self->console->GPU.Framebuffer[front_buf][0] || !self->console->GPU.Framebuffer[front_buf][1])
//    return;

  glBindFramebuffer (GL_FRAMEBUFFER, hs_gl_context_get_default_framebuffer (self->context));
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

  self->console->GPU.CurGLCompositor->BindOutputTexture (front_buf);

  glBindBuffer (GL_ARRAY_BUFFER, self->vertex_buffer);
  glBindVertexArray (self->vertex_array);

  glDrawArrays (GL_TRIANGLES, 0, 12);

  glBindBuffer (GL_PIXEL_PACK_BUFFER, 0);
  glBindFramebuffer (GL_FRAMEBUFFER, 0);
}

static gpointer
get_proc_address (const char *name)
{
  melonDSCore *self = MELONDS_CORE (core);

  return hs_gl_context_get_proc_address (self->context, name);
}
#endif

static void
load_bios (melonDSCore *self)
{
  memcpy (self->console->ARM9BIOS, bios_arm9_bin, sizeof (bios_arm9_bin));
  memcpy (self->console->ARM7BIOS, bios_arm7_bin, sizeof (bios_arm7_bin));

  std::unique_ptr<Firmware> firmware = std::make_unique<Firmware>(0); // 1 for DSi

  self->console->SPI.GetFirmwareMem ()->InstallFirmware (std::move (firmware));
}

static gboolean
melonds_core_load_rom (HsCore      *core,
                       const char **rom_paths,
                       int          n_rom_paths,
                       const char  *save_path,
                       GError     **error)
{
  melonDSCore *self = MELONDS_CORE (core);

  g_assert (n_rom_paths == 1);

  g_set_str (&self->save_path, save_path);

  self->console = new NDS ();
  NDS::Current = self->console;

  if (!self->console) {
    g_set_error (error, HS_CORE_ERROR, HS_CORE_ERROR_INTERNAL, "Failed to init the console");
    return FALSE;
  }

  RenderSettings vsettings;

#if USE_GL
  self->context = hs_core_create_gl_context (core, HS_GL_PROFILE_CORE, 3, 2, HS_GL_FLAGS_DEFAULT);

  if (!hs_gl_context_realize (self->context, error))
    return FALSE; // TODO fall back

  hs_gl_context_set_size (self->context, SCREEN_WIDTH, SCREEN_HEIGHT * 2);

  if (!gladLoadGLLoader (get_proc_address)) {
    g_set_error (error, HS_CORE_ERROR, HS_CORE_ERROR_INTERNAL, "Failed to load GL function for GLAD");
    return FALSE;
  }

  vsettings.GL_ScaleFactor = 1;
  vsettings.GL_BetterPolygons = false;

  self->console->GPU.InitRenderer (1); // GL
  self->console->GPU.SetRenderSettings (1, vsettings);

  gl_init (self);
#else
  self->context = hs_core_create_software_context (core, SCREEN_WIDTH, SCREEN_HEIGHT * 2, HS_PIXEL_FORMAT_XRGB8888_REV);

  vsettings.Soft_Threaded = false;

  self->console->GPU.InitRenderer (0); // Software
  self->console->GPU.SetRenderSettings (0, vsettings);
#endif

  self->console->SPU.SetInterpolation (0); // 0: none, 1: linear, 2: cosine, 3: cubic

  load_bios (self);

  g_autofree char *rom_data = NULL;
  gsize rom_length;
  if (!g_file_get_contents (rom_paths[0], &rom_data, &rom_length, error))
    return FALSE;

  g_autofree char *save_data = NULL;
  gsize save_length = 0;
  g_autoptr (GFile) save_file = g_file_new_for_path (save_path);
  if (g_file_query_exists (save_file, NULL) && !g_file_get_contents (save_path, &save_data, &save_length, error))
    return FALSE;

  if (!self->console->LoadCart ((const u8*) rom_data, rom_length, (const u8*) save_data, save_length)) {
    g_set_error (error, HS_CORE_ERROR, HS_CORE_ERROR_INTERNAL, "Failed to load ROM");
    return FALSE;
  }

  self->console->Reset ();

  if (self->console->NeedsDirectBoot ())
    self->console->SetupDirectBoot ("");

  return TRUE;
}

static void
melonds_core_start (HsCore *core)
{
  melonDSCore *self = MELONDS_CORE (core);

  self->console->Start ();
}

static void
melonds_core_reset (HsCore *core)
{
  melonDSCore *self = MELONDS_CORE (core);

  self->console->Reset ();

  if (self->console->NeedsDirectBoot ())
    self->console->SetupDirectBoot ("");
}

static void
melonds_core_stop (HsCore *core)
{
  melonDSCore *self = MELONDS_CORE (core);

  self->console->Halt ();
  self->console->Stop ();

#if USE_GL
  glDeleteTextures (1, &self->texture);
  glDeleteVertexArrays (1, &self->vertex_array);
  glDeleteBuffers (1, &self->vertex_buffer);

  OpenGL::DeleteShaderProgram (self->program);
#endif

  delete self->console;
  self->console = NULL;

  NDS::Current = NULL;

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
  melonDSCore *self = MELONDS_CORE (core);
  u32 mask = 0xfff;

  for (int btn = 0; btn < HS_NINTENDO_DS_N_BUTTONS; btn++) {
    if (input_state->nintendo_ds.buttons & 1 << btn)
      mask &= ~(1 << BUTTON_MAPPING[btn]);
  }

  self->console->SetKeyMask (mask);

  if (input_state->nintendo_ds.touch_pressed) {
    u16 x = (u16) round (input_state->nintendo_ds.touch_x * SCREEN_WIDTH);
    u16 y = (u16) round (input_state->nintendo_ds.touch_y * SCREEN_HEIGHT);
    self->console->TouchScreen (x, y);
  } else {
    self->console->ReleaseScreen ();
  }
}

static void
melonds_core_run_frame (HsCore *core)
{
  melonDSCore *self = MELONDS_CORE (core);

  self->console->RunFrame ();

  u32 n_samples = self->console->SPU.GetOutputSize ();
  gint16 *samples = g_new0 (gint16, n_samples * 2);
  self->console->SPU.ReadOutput (samples, n_samples);
  hs_core_play_samples (core, samples, n_samples * 2);
  g_free (samples);

#if USE_GL
  gl_draw_frame (self);
  hs_gl_context_swap_buffers (self->context);
#else
  size_t screen_size = (SCREEN_WIDTH * SCREEN_HEIGHT * 4);
  u8 *vbuf0 = (u8*) hs_software_context_get_framebuffer (self->context);
  u8 *vbuf1 = vbuf0 + screen_size;
  memcpy (vbuf0, self->console->GPU.Framebuffer[self->console->GPU.FrontBuffer][0], screen_size);
  memcpy (vbuf1, self->console->GPU.Framebuffer[self->console->GPU.FrontBuffer][1], screen_size);
#endif
}

static gboolean
melonds_core_reload_save (HsCore      *core,
                          const char  *save_path,
                          GError     **error)
{
  melonDSCore *self = MELONDS_CORE (core);

  g_set_str (&self->save_path, save_path);

  g_autofree char *save_data = NULL;
  gsize save_length = 0;
  g_autoptr (GFile) save_file = g_file_new_for_path (save_path);
  if (g_file_query_exists (save_file, NULL) && !g_file_get_contents (save_path, &save_data, &save_length, error))
    return FALSE;

  self->console->LoadSave ((const u8*) save_data, save_length);

  return TRUE;
}

static void
melonds_core_load_state (HsCore          *core,
                         const char      *path,
                         HsStateCallback  callback)
{
  melonDSCore *self = MELONDS_CORE (core);
  g_autofree char *data = NULL;
  gsize length;
  GError *error = NULL;

  if (!g_file_get_contents (path, &data, &length, &error)) {
    callback (core, &error);
    return;
  }

  Savestate *state = new Savestate (data, length, false);

  if (!self->console->DoSavestate (state) || state->Error) {
    g_set_error (&error, HS_CORE_ERROR, HS_CORE_ERROR_INTERNAL, "Failed to load state");
    callback (core, &error);
    return;
  }

  delete state;
  callback (core, NULL);
}

static void
melonds_core_save_state (HsCore          *core,
                         const char      *path,
                         HsStateCallback  callback)
{
  melonDSCore *self = MELONDS_CORE (core);
  g_autoptr (GFile) file = g_file_new_for_path (path);
  GError *error = NULL;

  Savestate state (Savestate::DEFAULT_SIZE);

  if (!self->console->DoSavestate (&state) || state.Error) {
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

  core_class->reload_save = melonds_core_reload_save;

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
