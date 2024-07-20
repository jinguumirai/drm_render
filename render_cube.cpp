#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// The following code related to DRM/GBM was adapted from the following sources:
// https://github.com/eyelash/tutorials/blob/master/drm-gbm.c
// and
// https://www.raspberrypi.org/forums/viewtopic.php?t=243707#p1499181
//
// I am not the original author of this code, I have only modified it.
int device;
drmModeModeInfo mode;
struct gbm_device *gbmDevice;
struct gbm_surface *gbmSurface;
drmModeCrtc *crtc;
uint32_t connectorId;

static drmModeConnector *getConnector(drmModeRes *resources)
{
    for (int i = 0; i < resources->count_connectors; i++)
    {
        drmModeConnector *connector = drmModeGetConnector(device, resources->connectors[i]);
        if (connector->connection == DRM_MODE_CONNECTED)
        {
            return connector;
        }
        drmModeFreeConnector(connector);
    }

    return NULL;
}

static drmModeEncoder *findEncoder(drmModeConnector *connector)
{
    if (connector->encoder_id)
    {
        return drmModeGetEncoder(device, connector->encoder_id);
    }
    return NULL;
}

static int getDisplay(EGLDisplay *display, drmModeCrtcPtr& scr_crtc)
{
    drmModeRes *resources = drmModeGetResources(device);
    if (resources == NULL)
    {
        fprintf(stderr, "Unable to get DRM resources\n");
        return -1;
    }

    drmModeConnector *connector = getConnector(resources);
    if (connector == NULL)
    {
        fprintf(stderr, "Unable to get connector\n");
        drmModeFreeResources(resources);
        return -1;
    }

    connectorId = connector->connector_id;
    mode = connector->modes[0];
    printf("resolution: %ix%i\n", mode.hdisplay, mode.vdisplay);

    drmModeEncoder *encoder = findEncoder(connector);
    if (encoder == NULL)
    {
        fprintf(stderr, "Unable to get encoder\n");
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        return -1;
    }

    crtc = drmModeGetCrtc(device, encoder->crtc_id);
	scr_crtc = crtc;
    drmModeFreeEncoder(encoder);
    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);
    gbmDevice = gbm_create_device(device);
    gbmSurface = gbm_surface_create(gbmDevice, mode.hdisplay, mode.vdisplay, GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    *display = eglGetDisplay(gbmDevice);
    return 0;
}

static int matchConfigToVisual(EGLDisplay display, EGLint visualId, EGLConfig *configs, int count)
{
    EGLint id;
    for (int i = 0; i < count; ++i)
    {
        if (!eglGetConfigAttrib(display, configs[i], EGL_NATIVE_VISUAL_ID, &id))
            continue;
        if (id == visualId)
            return i;
    }
    return -1;
}

static struct gbm_bo *previousBo = NULL;
static uint32_t previousFb;

static void gbmSwapBuffers(EGLDisplay *display, EGLSurface *surface)
{
    eglSwapBuffers(*display, *surface);
    struct gbm_bo *bo = gbm_surface_lock_front_buffer(gbmSurface);
    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t pitch = gbm_bo_get_stride(bo);
    uint32_t fb;
    drmModeAddFB(device, mode.hdisplay, mode.vdisplay, 24, 32, pitch, handle, &fb);
    drmModeSetCrtc(device, crtc->crtc_id, fb, 0, 0, &connectorId, 1, &mode);

    if (previousBo)
    {
        drmModeRmFB(device, previousFb);
        gbm_surface_release_buffer(gbmSurface, previousBo);
    }
    previousBo = bo;
    previousFb = fb;
}

static void gbmClean()
{
    // set the previous crtc
    drmModeSetCrtc(device, crtc->crtc_id, crtc->buffer_id, crtc->x, crtc->y, &connectorId, 1, &crtc->mode);
    drmModeFreeCrtc(crtc);

    if (previousBo)
    {
        drmModeRmFB(device, previousFb);
        gbm_surface_release_buffer(gbmSurface, previousBo);
    }

    gbm_surface_destroy(gbmSurface);
    gbm_device_destroy(gbmDevice);
}

// The following code was adopted from
// https://github.com/matusnovak/rpi-opengl-without-x/blob/master/triangle.c
// and is licensed under the Unlicense.
static const EGLint configAttribs[] = {
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_DEPTH_SIZE, 8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE};

static const EGLint contextAttribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE};

// The following array holds vec3 data of
// three vertex positions

// The following are GLSL shaders for rendering a triangle on the screen

// Get the EGL error back as a string. Useful for debugging.
static const char *eglGetErrorStr()
{
    switch (eglGetError())
    {
    case EGL_SUCCESS:
        return "The last function succeeded without error.";
    case EGL_NOT_INITIALIZED:
        return "EGL is not initialized, or could not be initialized, for the "
               "specified EGL display connection.";
    case EGL_BAD_ACCESS:
        return "EGL cannot access a requested resource (for example a context "
               "is bound in another thread).";
    case EGL_BAD_ALLOC:
        return "EGL failed to allocate resources for the requested operation.";
    case EGL_BAD_ATTRIBUTE:
        return "An unrecognized attribute or attribute value was passed in the "
               "attribute list.";
    case EGL_BAD_CONTEXT:
        return "An EGLContext argument does not name a valid EGL rendering "
               "context.";
    case EGL_BAD_CONFIG:
        return "An EGLConfig argument does not name a valid EGL frame buffer "
               "configuration.";
    case EGL_BAD_CURRENT_SURFACE:
        return "The current surface of the calling thread is a window, pixel "
               "buffer or pixmap that is no longer valid.";
    case EGL_BAD_DISPLAY:
        return "An EGLDisplay argument does not name a valid EGL display "
               "connection.";
    case EGL_BAD_SURFACE:
        return "An EGLSurface argument does not name a valid surface (window, "
               "pixel buffer or pixmap) configured for GL rendering.";
    case EGL_BAD_MATCH:
        return "Arguments are inconsistent (for example, a valid context "
               "requires buffers not supplied by a valid surface).";
    case EGL_BAD_PARAMETER:
        return "One or more argument values are invalid.";
    case EGL_BAD_NATIVE_PIXMAP:
        return "A NativePixmapType argument does not refer to a valid native "
               "pixmap.";
    case EGL_BAD_NATIVE_WINDOW:
        return "A NativeWindowType argument does not refer to a valid native "
               "window.";
    case EGL_CONTEXT_LOST:
        return "A power management event has occurred. The application must "
               "destroy all contexts and reinitialise OpenGL ES state and "
               "objects to continue rendering.";
    default:
        break;
    }
    return "Unknown error!";
}

GLuint make_shader(const char * src, const int * len, GLenum type)
{
	GLuint shader;
	GLuint vert_compliled;
	shader = glCreateShader(type);
	glShaderSource(shader, 1, (const char* const *)&src, len);
	glCompileShader(shader);
	GLint mess;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &mess);
	if (mess == 0)
	{
		GLint infoLogLength;
    	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);
    	GLchar *infoLog = (GLchar *)malloc(infoLogLength);
    	glGetShaderInfoLog(shader, infoLogLength, NULL, infoLog);
		std::cout << infoLog << std::endl;
	}
	return shader;
}

int main()
{
    // You can try chaning this to "card0" if "card1" does not work.
    device = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	EGLDisplay display;
	drmModeCrtcPtr new_crtc;

    if (getDisplay(&display, new_crtc) != 0)
    {
        fprintf(stderr, "Unable to get EGL display\n");
        close(device);
        return -1;
    }

    // We will use the screen resolution as the desired width and height for the viewport.
    int desiredWidth = mode.hdisplay;
    int desiredHeight = mode.vdisplay;

    // Other variables we will need further down the code.
    int major, minor;
    // GLuint vert, frag;

    if (eglInitialize(display, &major, &minor) == EGL_FALSE)
    {
        fprintf(stderr, "Failed to get EGL version! Error: %s\n",
                eglGetErrorStr());
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }

    // Make sure that we can use OpenGL in this EGL app.
    eglBindAPI(EGL_OPENGL_API);

    printf("Initialized EGL version: %d.%d\n", major, minor);

    EGLint count;
    EGLint numConfigs;
    eglGetConfigs(display, NULL, 0, &count);
    EGLConfig *configs = (EGLConfig *)malloc(count * sizeof(configs));

    if (!eglChooseConfig(display, configAttribs, configs, count, &numConfigs))
    {
        fprintf(stderr, "Failed to get EGL configs! Error: %s\n",
                eglGetErrorStr());
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }

    // I am not exactly sure why the EGL config must match the GBM format.
    // But it works!
    int configIndex = matchConfigToVisual(display, GBM_FORMAT_XRGB8888, configs, numConfigs);
    if (configIndex < 0)
    {
        fprintf(stderr, "Failed to find matching EGL config! Error: %s\n",
                eglGetErrorStr());
        eglTerminate(display);
        gbm_surface_destroy(gbmSurface);
        gbm_device_destroy(gbmDevice);
        return EXIT_FAILURE;
    }

    EGLContext context =
        eglCreateContext(display, configs[configIndex], EGL_NO_CONTEXT, contextAttribs);
    if (context == EGL_NO_CONTEXT)
    {
        fprintf(stderr, "Failed to create EGL context! Error: %s\n",
                eglGetErrorStr());
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }

    EGLSurface surface =
        eglCreateWindowSurface(display, configs[configIndex], gbmSurface, NULL);
    if (surface == EGL_NO_SURFACE)
    {
        fprintf(stderr, "Failed to create EGL surface! Error: %s\n",
                eglGetErrorStr());
        eglDestroyContext(display, context);
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }

    free(configs);
    eglMakeCurrent(display, surface, surface, context);

    // Set GL Viewport size, always needed!
    glViewport(0, 0, desiredWidth, desiredHeight);

    // Get GL Viewport size and test if it is correct.
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    // viewport[2] and viewport[3] are viewport width and height respectively
    printf("GL Viewport size: %dx%d\n", viewport[2], viewport[3]);

    if (viewport[2] != desiredWidth || viewport[3] != desiredHeight)
    {
        fprintf(stderr, "Error! The glViewport returned incorrect values! Something is wrong!\n");
        eglDestroyContext(display, context);
        eglDestroySurface(display, surface);
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }

    // Clear whole screen (front buffer)
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Create a shader program
    // NO ERRRO CHECKING IS DONE! (for the purpose of this example)
    // Read an OpenGL tutorial to properly implement shader creation
    std::ifstream vs_file("cube.vs");
	std::stringstream vs_stream;
	vs_stream << vs_file.rdbuf();
	std::string vs_sourse = vs_stream.str();
	const char * vs_str = vs_sourse.c_str();
	vs_file.close();
	GLuint vs = make_shader(vs_str, NULL, GL_VERTEX_SHADER);

	std::ifstream fs_file("cube.fs");
	std::stringstream fs_stream;
	fs_stream << fs_file.rdbuf();
	std::string fs_source = fs_stream.str();
	const char * fs_str = fs_source.c_str();
	GLuint fs = make_shader(fs_str, NULL, GL_FRAGMENT_SHADER);
	
	GLuint program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
    glUseProgram(program);
	GLint mess;
	glGetProgramiv(program, GL_LINK_STATUS, &mess);
	if (mess == 0)
	{
		GLint infoLogLength;
    	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);
    	GLchar *infoLog = (GLchar *)malloc(infoLogLength);
    	glGetProgramInfoLog(program, infoLogLength, NULL, infoLog);
		std::cout << infoLog << std::endl;
	}

    GLint vert_pos = glGetAttribLocation(program, "apos");
	GLint vert_color = glGetAttribLocation(program, "acolor");
	GLint model_mat = glGetUniformLocation(program, "model");
	GLint view_mat = glGetUniformLocation(program, "view");
	GLint projection_mat = glGetUniformLocation(program, "projection");

    GLfloat vertices[] = {
		-0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
         0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f, 
        -0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,

        -0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
        -0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
        -0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f,

        -0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
        -0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
        -0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
        -0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
        -0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f,

         0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
		 0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
         0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f,

        -0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
         0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
        -0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
        -0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,

        -0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
        -0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
	};

    // Create Vertex Buffer Object
    // Again, NO ERRRO CHECKING IS DONE! (for the purpose of this example)
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(vert_pos);
	glVertexAttribPointer(vert_pos, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 6, (void*)0);
	glEnableVertexAttribArray(vert_color);
	glVertexAttribPointer(vert_color, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 6, (void*)(3 * sizeof(GLfloat)));
	
    glm::mat4 projectction = glm::perspective(glm::radians(45.0f), float(desiredWidth) / float(desiredHeight), 0.1f, 100.0f);
    glUniformMatrix4fv(projection_mat, 1, GL_FALSE, &projectction[0][0]);
    
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, {0.f, 0.f, -10.f});
    model = glm::rotate(model, glm::radians(20.f), glm::vec3(1.0f, 0.3f, 0.5f));
    glUniformMatrix4fv(model_mat, 1, GL_FALSE, &model[0][0]);

    glm::mat4 view = glm::mat4(1.0f);
    glUniformMatrix4fv(view_mat, 1, GL_FALSE, &view[0][0]);

    // Get vertex attribute and uniform locations

    // Set the desired color of the triangle to pink
    // 100% red, 0% green, 50% blue, 100% alpha

    // Set our vertex data

    // Render a triangle consisting of 3 vertices:
    glDrawArrays(GL_TRIANGLES, 0, 36);

    // Depending on your application, you might need to swap the buffers.
    // If you only want to render to an image file (as shown below) then this is not necessary.
    // But if you want to show the OpenGL render on a screen through HDMI, you might need it.
    gbmSwapBuffers(&display, &surface);
    sleep(5);

    // Create buffer to hold entire front buffer pixels
    // We multiply width and height by 3 to because we use RGB!
    /*
    unsigned char *buffer =
        (unsigned char *)malloc(desiredWidth * desiredHeight * 3);

    // Copy entire screen
    glReadPixels(0, 0, desiredWidth, desiredHeight, GL_RGB, GL_UNSIGNED_BYTE,
                 buffer);
	sleep(5);

    // Write all pixels to a file
    FILE *output = fopen("triangle.raw", "wb");
    if (output)
    {
        fwrite(buffer, 1, desiredWidth * desiredHeight * 3, output);
        fclose(output);
    }
    else
    {
        fprintf(stderr, "Failed to open file triangle.raw for writing!\n");
    }
    */

    // Free copied pixels
    // free(buffer);

    // Cleanup
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    gbmClean();

    close(device);
    return EXIT_SUCCESS;
}