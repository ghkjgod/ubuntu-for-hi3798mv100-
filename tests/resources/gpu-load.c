/* SPDX-License-Identifier: MIT */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static GLuint shader(GLenum type, const char *text)
{
    GLuint s = glCreateShader(type);
    GLint ok;
    glShaderSource(s, 1, &text, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "%s\n", log);
        exit(2);
    }
    return s;
}

int main(void)
{
    EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
    EGLConfig cfg;
    EGLint count, major, minor;
    const EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE };
    const EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    const EGLint pb_attrs[] = { EGL_WIDTH, 768, EGL_HEIGHT, 512, EGL_NONE };
    if (!eglInitialize(d, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API) ||
        !eglChooseConfig(d, attrs, &cfg, 1, &count) || count != 1) return 1;
    EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ctx_attrs);
    EGLSurface pb = eglCreatePbufferSurface(d, cfg, pb_attrs);
    if (!eglMakeCurrent(d, pb, pb, ctx)) return 1;
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    if (!renderer || !strstr(renderer, "Mali450")) return 2;
    GLuint program = glCreateProgram();
    glAttachShader(program, shader(GL_VERTEX_SHADER,
        "attribute vec2 p; void main(){gl_Position=vec4(p,0.,1.);}"));
    glAttachShader(program, shader(GL_FRAGMENT_SHADER,
        "precision mediump float; uniform float phase; void main(){"
        "vec2 p=gl_FragCoord.xy*0.002+phase;"
        "for(int i=0;i<8;i++){p=fract(vec2(dot(p,vec2(1.21,.41)),"
        "dot(p,vec2(.77,1.19)))+vec2(.14,.27));}"
        "gl_FragColor=vec4(p,.25,1.);}"));
    glBindAttribLocation(program, 0, "p");
    glLinkProgram(program);
    GLint ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) return 2;
    glUseProgram(program);
    const GLfloat vertices[] = {-1,-1,3,-1,-1,3};
    glViewport(0, 0, 768, 512);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(0);
    GLint phase = glGetUniformLocation(program, "phase");
    const double start = now();
    unsigned frames = 0;
    do {
        glUniform1f(phase, (frames % 100) * .001f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();
        ++frames;
    } while (now() - start < 15.0);
    const double elapsed = now() - start;
    unsigned char color[4];
    glReadPixels(410, 210, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, color);
    if (glGetError() || color[2] < 62 || color[2] > 66) return 3;
    printf("renderer=%s frames=%u seconds=%.3f offscreen_fps=%.2f pixel=%u,%u,%u,%u\n",
           renderer, frames, elapsed, frames/elapsed, color[0], color[1], color[2], color[3]);
    eglTerminate(d);
    return 0;
}
