// GL_COMPAT.H
// On desktop, this just pulls in regular OpenGL.
// On Android (Meta Quest), the game's fixed-function OpenGL 1.x calls are
// routed to a small shader-based emulation layer on top of OpenGL ES 3.

#pragma once

#if !defined(__ANDROID__)

#include <SDL3/SDL_opengl.h>

#else

#include <GLES3/gl3.h>

#ifdef __cplusplus
extern "C" {
#endif

// Enums that don't exist in GLES
#define GL_PROJECTION					0x1701
#define GL_MODELVIEW					0x1700
#define GL_LIGHTING						0x0B50
#define GL_LIGHT0						0x4000
#define GL_NORMALIZE					0x0BA1
#define GL_ALPHA_TEST					0x0BC0
#define GL_COLOR_MATERIAL				0x0B57
#define GL_FOG							0x0B60
#define GL_FOG_MODE						0x0B65
#define GL_FOG_START					0x0B63
#define GL_FOG_END						0x0B64
#define GL_FOG_COLOR					0x0B66
#define GL_FOG_HINT						0x0C54
#define GL_AMBIENT						0x1200
#define GL_DIFFUSE						0x1201
#define GL_POSITION						0x1203
#define GL_LIGHT_MODEL_AMBIENT			0x0B53
#define GL_AMBIENT_AND_DIFFUSE			0x1602
#define GL_VERTEX_ARRAY					0x8074
#define GL_NORMAL_ARRAY					0x8075
#define GL_COLOR_ARRAY					0x8076
#define GL_TEXTURE_COORD_ARRAY			0x8078
#define GL_BGR							0x80E0
#define GL_BGRA							0x80E1
#define GL_BGRA_EXT						0x80E1
#define GL_UNSIGNED_INT_8_8_8_8_REV		0x8367
#define GL_UNSIGNED_SHORT_1_5_5_5_REV	0x8366
#define GL_LINE							0x1B01
#define GL_FILL							0x1B02
#define GL_QUADS						0x0007


// Fixed-function emulation entry points (implemented in System/GLCompat.c)
void GLC_Init(void);
void GLC_MatrixMode(GLenum mode);
void GLC_PushMatrix(void);
void GLC_PopMatrix(void);
void GLC_LoadIdentity(void);
void GLC_LoadMatrixf(const GLfloat* m);
void GLC_MultMatrixf(const GLfloat* m);
void GLC_Ortho(double l, double r, double b, double t, double n, double f);
void GLC_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void GLC_Enable(GLenum cap);
void GLC_Disable(GLenum cap);
void GLC_EnableClientState(GLenum array);
void GLC_DisableClientState(GLenum array);
void GLC_VertexPointer(GLint size, GLenum type, GLsizei stride, const void* ptr);
void GLC_NormalPointer(GLenum type, GLsizei stride, const void* ptr);
void GLC_ColorPointer(GLint size, GLenum type, GLsizei stride, const void* ptr);
void GLC_TexCoordPointer(GLint size, GLenum type, GLsizei stride, const void* ptr);
void GLC_DrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices);
void GLC_Lightfv(GLenum light, GLenum pname, const GLfloat* params);
void GLC_LightModelfv(GLenum pname, const GLfloat* params);
void GLC_Fogi(GLenum pname, GLint param);
void GLC_Fogf(GLenum pname, GLfloat param);
void GLC_Fogfv(GLenum pname, const GLfloat* params);
void GLC_AlphaFunc(GLenum func, GLfloat ref);
void GLC_Begin(GLenum mode);
void GLC_Vertex3f(GLfloat x, GLfloat y, GLfloat z);
void GLC_End(void);
void GLC_TexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void* pixels);
void GLC_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const void* pixels);
const GLfloat* GLC_GetMatrix(GLenum mode);
void GLC_BlendFunc(GLenum sfactor, GLenum dfactor);
void GLC_RebaseLights(const GLfloat* oldWorldToView, const GLfloat* newWorldToView);

// GLCompat.c itself must call the real GLES functions
#ifndef GLC_IMPLEMENTATION

#define glMatrixMode			GLC_MatrixMode
#define glPushMatrix			GLC_PushMatrix
#define glPopMatrix				GLC_PopMatrix
#define glLoadIdentity			GLC_LoadIdentity
#define glLoadMatrixf			GLC_LoadMatrixf
#define glMultMatrixf			GLC_MultMatrixf
#define glOrtho					GLC_Ortho
#define glColor4f				GLC_Color4f
#define glEnable				GLC_Enable
#define glDisable				GLC_Disable
#define glEnableClientState		GLC_EnableClientState
#define glDisableClientState	GLC_DisableClientState
#define glVertexPointer			GLC_VertexPointer
#define glNormalPointer			GLC_NormalPointer
#define glColorPointer			GLC_ColorPointer
#define glTexCoordPointer		GLC_TexCoordPointer
#define glDrawElements			GLC_DrawElements
#define glLightfv				GLC_Lightfv
#define glLightModelfv			GLC_LightModelfv
#define glFogi					GLC_Fogi
#define glFogf					GLC_Fogf
#define glFogfv					GLC_Fogfv
#define glAlphaFunc				GLC_AlphaFunc
#define glBegin					GLC_Begin
#define glVertex3f				GLC_Vertex3f
#define glEnd					GLC_End
#define glTexImage2D			GLC_TexImage2D
#define glTexSubImage2D			GLC_TexSubImage2D
#define glBlendFunc				GLC_BlendFunc
#define glColorMaterial(face, mode)		((void)0)	// always GL_AMBIENT_AND_DIFFUSE in this game
#define glPolygonMode(face, mode)		((void)0)	// wireframe debug mode unsupported on GLES
#define glHint(target, mode)			((void)0)

#endif // GLC_IMPLEMENTATION

#ifdef __cplusplus
}
#endif

#endif
