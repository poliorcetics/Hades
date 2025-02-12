/******************************************************************************\
**
**  This file is part of the Hades GBA Emulator, and is made available under
**  the terms of the GNU General Public License version 2.
**
**  Copyright (C) 2021-2024 - The Hades Authors
**
\******************************************************************************/

#include <GL/glew.h>
#include <cimgui.h>
#include <cimgui_impl.h>
#include <nfd.h>
#include <errno.h>
#include "hades.h"
#include "app/app.h"
#include "gba/gba.h"

static GLuint build_shader_program(char const *name, char const *frag_path, char const *vertex_path);

void
app_sdl_video_init(
    struct app *app
) {
    char const *glsl_version;
    SDL_DisplayMode mode;
    ImFontConfig *cfg;
    int err;

    memset(&mode, 0, sizeof(mode));

    /* Decide which OpenGL version to use */
#if __APPLE__
    // GL 3.2 Core + GLSL 150
    glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + GLSL 130
    glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    /* Prepare OpenGL stuff */
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    /* Get the display's DPI */
    SDL_GetDisplayDPI(0, &app->ui.dpi, NULL, NULL);

    /* Get the display's refresh rate */
    SDL_GetDisplayMode(0, 0, &mode);
    app->ui.refresh_rate = (uint32_t)mode.refresh_rate;

    /* Setup ImGui DPI and scaling factors */
#if __APPLE__
    /*
    ** On my MacBook (12.3.1) it looks like the system is already scaling the window in a nice, pixel-perfect way.
    **
    ** If we use our scaling on top of it, the windows gets blurry and ugly very quick so we hard-code the scaling to 1 to
    ** avoid that.
    */
    app->ui.scale = 1;
#else
    float dpi_factor;

    dpi_factor = app->ui.dpi / 96.f;
    if (dpi_factor >= (int)dpi_factor + 0.5f) {
        app->ui.scale = (int)dpi_factor + 1;
    } else {
        app->ui.scale = (int)dpi_factor ? (int)dpi_factor : 1;
    }
#endif

    /*
    ** Create the SDL window
    **
    ** The window is resized after the first frame to take into account the height of the menubar,
    ** unknown at this stage.
    ** The size given here is merely a guess as to what the real size will be, hence the magical +19.f for the window's height.
    */
    app->ui.game.width = GBA_SCREEN_WIDTH * app->video.display_size * app->ui.scale;
    app->ui.game.height = GBA_SCREEN_HEIGHT * app->video.display_size * app->ui.scale;
    app->ui.win.width = app->ui.game.width;
    app->ui.win.height = app->ui.game.height + 19.f * app->ui.scale;

    app->sdl.window = SDL_CreateWindow(
        "Hades",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        app->ui.win.width,
        app->ui.win.height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    if (!app->sdl.window) {
        logln(HS_ERROR, "Failed to create the window: %s", SDL_GetError());
        exit(EXIT_FAILURE);
    }


    /* Create the OpenGL context */
    app->gfx.gl_context = SDL_GL_CreateContext(app->sdl.window);
    SDL_GL_MakeCurrent(app->sdl.window, app->gfx.gl_context);

    /* Enable VSync */
    SDL_GL_SetSwapInterval(app->video.vsync);

    /* Initialize OpenGL */
    err = glewInit();

    if (err != GLEW_OK && err != GLEW_ERROR_NO_GLX_DISPLAY) {
        logln(HS_ERROR, "Failed to initialize OpenGL.");
        exit(EXIT_FAILURE);
    }

    /* Setup ImGui */
    igCreateContext(NULL);
    igStyleColorsDark(NULL);

    /* Set ImGui options */
    app->ui.ioptr = igGetIO();
    app->ui.ioptr->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    app->ui.ioptr->IniFilename = NULL;

    cfg = ImFontConfig_ImFontConfig();
    cfg->SizePixels = 13.f * app->ui.scale;
    cfg->GlyphOffset.y = 13.f * app->ui.scale;
    app->ui.fonts.normal = ImFontAtlas_AddFontDefault(app->ui.ioptr->Fonts, cfg);

    cfg = ImFontConfig_ImFontConfig();
    cfg->SizePixels = 13.f * app->ui.scale * 3.;
    cfg->GlyphOffset.y = 13.f * app->ui.scale * 3.;
    app->ui.fonts.big = ImFontAtlas_AddFontDefault(app->ui.ioptr->Fonts, cfg);

    ImGuiStyle_ScaleAllSizes(igGetStyle(), app->ui.scale);

    ImGui_ImplSDL2_InitForOpenGL(app->sdl.window, app->gfx.gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    /* Build all the available shaders */
    app->gfx.program_color_correction = build_shader_program("color_correction", SHADER_FRAG_COLOR_CORRECTION, SHADER_VERTEX_COMMON);
    app->gfx.program_lcd_grid = build_shader_program("lcd-grid", SHADER_FRAG_LCD_GRID, SHADER_VERTEX_COMMON);

    /* Create the OpenGL objects required to build the pipeline */
    glGenTextures(1, &app->gfx.game_texture_in);
    glGenTextures(1, &app->gfx.game_texture_a);
    glGenTextures(1, &app->gfx.game_texture_b);
    glGenFramebuffers(1, &app->gfx.fbo);
    glGenVertexArrays(1, &app->gfx.vao);
    glGenBuffers(1, &app->gfx.vbo);

    float vertices[] = {
        // position   | UV coord
        -1., 1.,        0., 1.,     // Top left
        1., 1.,         1., 1.,     // Top right
        1., -1.,        1., 0.,     // Bottom right
        1., -1.,        1., 0.,     // Bottom right
        -1., -1.,       0., 0.,     // Bottom left
        -1., 1.,        0., 1.,     // Top left
    };

    /* Setup the OpenGL objects */
    glBindVertexArray(app->gfx.vao);
    glBindBuffer(GL_ARRAY_BUFFER, app->gfx.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, false, 4 * sizeof(float), 0); // position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, false, 4 * sizeof(float), (void *)(2 * sizeof(float))); // UV
    glEnableVertexAttribArray(1);

    /* Build the OpenGL pipeline. */
    app_sdl_video_rebuild_pipeline(app);

    /* Setup the game controller stuff */
    app->sdl.controller.ptr = NULL;
    app->sdl.controller.connected = false;
    app->sdl.controller.joystick.idx = -1;

    /* Setup the Native File Dialog extension */
    NFD_Init();
}

void
app_sdl_video_rebuild_pipeline(
    struct app *app
) {
    GLint texture_filter;

    switch (app->gfx.texture_filter) {
        case TEXTURE_FILTER_LINEAR: texture_filter = GL_LINEAR; break;
        case TEXTURE_FILTER_NEAREST: texture_filter = GL_NEAREST; break;
        default: texture_filter = GL_NEAREST; break;
    }

    // Setup the input texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app->gfx.game_texture_in);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texture_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texture_filter);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        GBA_SCREEN_WIDTH,
        GBA_SCREEN_HEIGHT,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        NULL
    );

    // Setup the A texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app->gfx.game_texture_a);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texture_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texture_filter);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        GBA_SCREEN_WIDTH * 3,
        GBA_SCREEN_HEIGHT * 3,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        NULL
    );

    // Setup the B texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app->gfx.game_texture_b);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texture_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texture_filter);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        GBA_SCREEN_WIDTH * 3,
        GBA_SCREEN_HEIGHT * 3,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        NULL
    );

    app->gfx.active_programs_length = 0;

    if (app->video.color_correction) {
        app->gfx.active_programs[app->gfx.active_programs_length] = app->gfx.program_color_correction;
        ++app->gfx.active_programs_length;
    }

    if (app->video.lcd_grid) {
        app->gfx.active_programs[app->gfx.active_programs_length] = app->gfx.program_lcd_grid;
        ++app->gfx.active_programs_length;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, app->gfx.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, app->gfx.game_texture_a, 0); // TODO FIXME

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

struct shader_descriptor {
    char const *path;
    GLenum type;
    char *src;
    GLuint shader;
};

static
GLuint
build_shader_program(
    char const *name,
    char const *frag_src,
    char const *vertex_src
) {
    GLuint program;
    GLuint frag;
    GLuint vertex;
    GLint status;
    GLint len;

    // Create the program
    program = glCreateProgram();

    // Compile the fragment shader
    frag = glCreateShader(GL_FRAGMENT_SHADER);
    len = strlen(frag_src);
    glShaderSource(frag, 1, &frag_src, &len);
    glCompileShader(frag);

    // Check the fragment shader compiled correctly
    glGetShaderiv(frag, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLint log_len;
        GLchar *error;

        log_len = 0;
        glGetShaderiv(frag, GL_INFO_LOG_LENGTH, &log_len);

        error = calloc(log_len, 1);
        hs_assert(error);

        glGetShaderInfoLog(frag, log_len, &log_len, error);

        panic(
            HS_ERROR,
            "Failed to compile the \"%s%s%s/fragment%s\" shader:\n"
            "====== BEGIN ======\n"
            "%s"
            "======  END  ======",
            g_bold,
            g_magenta,
            name,
            g_reset,
            error
        );
    }

    // Attach the fragment shader to the program
    glAttachShader(program, frag);

    // Compile the vertex shader
    vertex = glCreateShader(GL_VERTEX_SHADER);
    len = strlen(vertex_src);
    glShaderSource(vertex, 1, &vertex_src, &len);
    glCompileShader(vertex);

    // Check the vertex shader compiled correctly
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLint log_len;
        GLchar *error;

        log_len = 0;
        glGetShaderiv(vertex, GL_INFO_LOG_LENGTH, &log_len);

        error = calloc(log_len, 1);
        hs_assert(error);

        glGetShaderInfoLog(vertex, log_len, &log_len, error);

        panic(
            HS_ERROR,
            "Failed to compile the \"%s%s%s/vertex%s\" shader:\n"
            "====== BEGIN ======\n"
            "%s"
            "======  END  ======",
            g_bold,
            g_magenta,
            name,
            g_reset,
            error
        );
    }

    // Attach the vertex shader to the program
    glAttachShader(program, vertex);

    // Link the program
    glLinkProgram(program);
    glGetShaderiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        panic(HS_ERROR, "Failed to link shader.");
    }

    // Detach and delete both shaders
    glDetachShader(program, frag);
    glDetachShader(program, vertex);
    glDeleteShader(frag);
    glDeleteShader(vertex);

    return program;
}

void
app_sdl_video_cleanup(
    struct app *app
) {
    /* Cleanup the Native File Dialog extension */
    NFD_Quit();

    // Shutdown ImGui
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    igDestroyContext(NULL);

    // Cleanup OpenGL
    glDeleteProgram(app->gfx.program_color_correction);
    glDeleteBuffers(1, &app->gfx.vbo);
    glDeleteVertexArrays(1, &app->gfx.vbo);
    glDeleteFramebuffers(1, &app->gfx.fbo);
    glDeleteTextures(1, &app->gfx.game_texture_in);
    glDeleteTextures(1, &app->gfx.game_texture_a);
    glDeleteTextures(1, &app->gfx.game_texture_b);
    SDL_GL_DeleteContext(app->gfx.gl_context);

    // Close the Wingowd
    SDL_DestroyWindow(app->sdl.window);
}

void
app_sdl_video_render_frame(
    struct app *app
) {
    /* Create the new frame */
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    igNewFrame();

    /* Render the ImGui stuff */

    app_win_menubar(app);

    if (app->emulation.is_started) {
        app_win_game(app);
    }

    app_win_keybinds_editor(app);

    app_win_notifications(app);

    /* Render the imGui frame */
    igRender();

    SDL_GL_MakeCurrent(app->sdl.window, app->gfx.gl_context);
    glViewport(0, 0, (int)app->ui.ioptr->DisplaySize.x, (int)app->ui.ioptr->DisplaySize.y);

    /* Change the background color if the game is running */
    if (app->emulation.is_started) {
        glClearColor(0.f, 0.f, 0.f, 1.f);
    } else {
        glClearColor(176.f / 255.f, 124.f / 255.f, 223.f / 255.f, 1.f);
    }

    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(igGetDrawData());

    SDL_GL_SwapWindow(app->sdl.window);
}
