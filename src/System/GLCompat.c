// GLCOMPAT.C
// Minimal emulation of the OpenGL 1.x fixed-function pipeline on top of OpenGL ES 3,
// covering exactly the subset of features that Bugdom uses.
// Only compiled on Android (Meta Quest).

#if __ANDROID__

#define GLC_IMPLEMENTATION
#include "game.h"
#include <SDL3/SDL.h>

#define MAX_LIGHTS				4
#define MODELVIEW_STACK_DEPTH	32
#define PROJECTION_STACK_DEPTH	8
#define MAX_IMMEDIATE_VERTICES	1024

enum
{
	kAttrib_Position = 0,
	kAttrib_Normal = 1,
	kAttrib_Color = 2,
	kAttrib_TexCoord = 3,
};

typedef struct
{
	bool		enabled;
	GLint		size;
	GLenum		type;
	GLsizei		stride;
	const void*	ptr;
} ClientArray;

static struct
{
	bool		initialized;

	GLenum		matrixMode;
	GLfloat		modelview[MODELVIEW_STACK_DEPTH][16];
	GLfloat		projection[PROJECTION_STACK_DEPTH][16];
	int			modelviewTop;
	int			projectionTop;

	GLfloat		color[4];

	bool		lighting;
	bool		alphaTest;
	bool		texture2D;
	bool		fog;
	bool		lightEnabled[MAX_LIGHTS];
	GLfloat		lightEyeDir[MAX_LIGHTS][3];
	GLfloat		lightDiffuse[MAX_LIGHTS][3];
	GLfloat		ambient[3];

	GLfloat		fogStart;
	GLfloat		fogEnd;
	GLfloat		fogColor[3];
	GLfloat		alphaRef;

	ClientArray	arrays[4];

	GLuint		program;
	GLint		uMVP;
	GLint		uMV;
	GLint		uNormalMat;
	GLint		uLighting;
	GLint		uNumLights;
	GLint		uLightDir;
	GLint		uLightDiffuse;
	GLint		uAmbient;
	GLint		uTexturing;
	GLint		uAlphaTest;
	GLint		uAlphaRef;
	GLint		uFog;
	GLint		uFogParams;
	GLint		uFogColor;
	GLint		uTexture;

	GLenum		immediateMode;
	int			immediateCount;
	GLfloat		immediateVerts[MAX_IMMEDIATE_VERTICES * 3];

	uint8_t*	textureOpaqueFlags;		// indexed by texture name: 1 if the game asked for an RGB (alpha-less) texture
	size_t		textureOpaqueFlagsCapacity;

	uint8_t*	convertBuffer;
	size_t		convertBufferSize;
} gGLC;

#pragma mark - Shaders

static const char* kVertexShader =
	"#version 300 es\n"
	"layout(location=0) in vec3 aPos;\n"
	"layout(location=1) in vec3 aNormal;\n"
	"layout(location=2) in vec4 aColor;\n"
	"layout(location=3) in vec2 aUV;\n"
	"uniform mat4 uMVP;\n"
	"uniform mat4 uMV;\n"
	"uniform mat3 uNormalMat;\n"
	"uniform bool uLighting;\n"
	"uniform int uNumLights;\n"
	"uniform vec3 uLightDir[4];\n"
	"uniform vec3 uLightDiffuse[4];\n"
	"uniform vec3 uAmbient;\n"
	"out vec4 vColor;\n"
	"out vec2 vUV;\n"
	"out float vEyeDist;\n"
	"void main() {\n"
	"	vec4 eyePos = uMV * vec4(aPos, 1.0);\n"
	"	gl_Position = uMVP * vec4(aPos, 1.0);\n"
	"	vec4 c = aColor;\n"
	"	if (uLighting) {\n"
	"		vec3 n = normalize(uNormalMat * aNormal);\n"
	"		vec3 lit = uAmbient * c.rgb;\n"
	"		for (int i = 0; i < uNumLights; i++)\n"
	"			lit += max(dot(n, uLightDir[i]), 0.0) * uLightDiffuse[i] * c.rgb;\n"
	"		c.rgb = lit;\n"
	"	}\n"
	"	vColor = clamp(c, 0.0, 1.0);\n"
	"	vUV = aUV;\n"
	"	vEyeDist = abs(eyePos.z);\n"
	"}\n";

static const char* kFragmentShader =
	"#version 300 es\n"
	"precision mediump float;\n"
	"in vec4 vColor;\n"
	"in vec2 vUV;\n"
	"in highp float vEyeDist;\n"
	"uniform sampler2D uTexture;\n"
	"uniform bool uTexturing;\n"
	"uniform bool uAlphaTest;\n"
	"uniform float uAlphaRef;\n"
	"uniform bool uFog;\n"
	"uniform highp vec2 uFogParams;\n"		// start, end
	"uniform vec3 uFogColor;\n"
	"out vec4 fragColor;\n"
	"void main() {\n"
	"	vec4 c = vColor;\n"
	"	if (uTexturing)\n"
	"		c *= texture(uTexture, vUV);\n"
	"	if (uAlphaTest && !(c.a > uAlphaRef))\n"
	"		discard;\n"
	"	if (uFog) {\n"
	"		float f = clamp((uFogParams.y - vEyeDist) / (uFogParams.y - uFogParams.x), 0.0, 1.0);\n"
	"		c.rgb = mix(uFogColor, c.rgb, f);\n"
	"	}\n"
	"	fragColor = c;\n"
	"}\n";

static GLuint CompileShader(GLenum type, const char* source)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);

	GLint ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char log[1024];
		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		SDL_LogError(SDL_LOG_CATEGORY_RENDER, "GLCompat shader compile error: %s", log);
		DoFatalAlert("GLCompat: shader compile error");
	}
	return shader;
}

void GLC_Init(void)
{
	SDL_memset(&gGLC.modelview[0], 0, sizeof(gGLC.modelview[0]));
	SDL_memset(&gGLC.projection[0], 0, sizeof(gGLC.projection[0]));

	gGLC.matrixMode = GL_MODELVIEW;
	gGLC.modelviewTop = 0;
	gGLC.projectionTop = 0;
	GLC_LoadIdentity();
	gGLC.matrixMode = GL_PROJECTION;
	GLC_LoadIdentity();
	gGLC.matrixMode = GL_MODELVIEW;

	gGLC.color[0] = gGLC.color[1] = gGLC.color[2] = gGLC.color[3] = 1;
	gGLC.ambient[0] = gGLC.ambient[1] = gGLC.ambient[2] = 0.2f;
	gGLC.alphaRef = 0;
	gGLC.fogStart = 0;
	gGLC.fogEnd = 1;
	gGLC.lighting = gGLC.alphaTest = gGLC.texture2D = gGLC.fog = false;
	for (int i = 0; i < MAX_LIGHTS; i++)
	{
		gGLC.lightEnabled[i] = false;
		gGLC.lightEyeDir[i][0] = 0;
		gGLC.lightEyeDir[i][1] = 0;
		gGLC.lightEyeDir[i][2] = 1;
		gGLC.lightDiffuse[i][0] = gGLC.lightDiffuse[i][1] = gGLC.lightDiffuse[i][2] = (i == 0) ? 1 : 0;
	}
	SDL_memset(gGLC.arrays, 0, sizeof(gGLC.arrays));

	// (Re)create shader program -- the GL context may have been recreated
	GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShader);
	GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
	gGLC.program = glCreateProgram();
	glAttachShader(gGLC.program, vs);
	glAttachShader(gGLC.program, fs);
	glLinkProgram(gGLC.program);
	glDeleteShader(vs);
	glDeleteShader(fs);

	GLint ok = 0;
	glGetProgramiv(gGLC.program, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		char log[1024];
		glGetProgramInfoLog(gGLC.program, sizeof(log), NULL, log);
		SDL_LogError(SDL_LOG_CATEGORY_RENDER, "GLCompat program link error: %s", log);
		DoFatalAlert("GLCompat: program link error");
	}

#define LOC(name) gGLC.name = glGetUniformLocation(gGLC.program, #name)
	LOC(uMVP);
	LOC(uMV);
	LOC(uNormalMat);
	LOC(uLighting);
	LOC(uNumLights);
	LOC(uLightDir);
	LOC(uLightDiffuse);
	LOC(uAmbient);
	LOC(uTexturing);
	LOC(uAlphaTest);
	LOC(uAlphaRef);
	LOC(uFog);
	LOC(uFogParams);
	LOC(uFogColor);
	LOC(uTexture);
#undef LOC

	glUseProgram(gGLC.program);
	glUniform1i(gGLC.uTexture, 0);

	gGLC.initialized = true;
}

#pragma mark - Matrices

static void Mat4Mul(const GLfloat* a, const GLfloat* b, GLfloat* out)		// out = a * b (column-major)
{
	GLfloat r[16];
	for (int c = 0; c < 4; c++)
	{
		for (int row = 0; row < 4; row++)
		{
			r[c*4+row] =  a[0*4+row] * b[c*4+0]
						+ a[1*4+row] * b[c*4+1]
						+ a[2*4+row] * b[c*4+2]
						+ a[3*4+row] * b[c*4+3];
		}
	}
	SDL_memcpy(out, r, sizeof(r));
}

static GLfloat* CurrentMatrix(void)
{
	if (gGLC.matrixMode == GL_PROJECTION)
		return gGLC.projection[gGLC.projectionTop];
	else
		return gGLC.modelview[gGLC.modelviewTop];
}

const GLfloat* GLC_GetMatrix(GLenum mode)
{
	return mode == GL_PROJECTION ? gGLC.projection[gGLC.projectionTop] : gGLC.modelview[gGLC.modelviewTop];
}

void GLC_MatrixMode(GLenum mode)
{
	gGLC.matrixMode = mode;
}

void GLC_PushMatrix(void)
{
	if (gGLC.matrixMode == GL_PROJECTION)
	{
		GAME_ASSERT(gGLC.projectionTop + 1 < PROJECTION_STACK_DEPTH);
		SDL_memcpy(gGLC.projection[gGLC.projectionTop+1], gGLC.projection[gGLC.projectionTop], sizeof(GLfloat)*16);
		gGLC.projectionTop++;
	}
	else
	{
		GAME_ASSERT(gGLC.modelviewTop + 1 < MODELVIEW_STACK_DEPTH);
		SDL_memcpy(gGLC.modelview[gGLC.modelviewTop+1], gGLC.modelview[gGLC.modelviewTop], sizeof(GLfloat)*16);
		gGLC.modelviewTop++;
	}
}

void GLC_PopMatrix(void)
{
	if (gGLC.matrixMode == GL_PROJECTION)
	{
		GAME_ASSERT(gGLC.projectionTop > 0);
		gGLC.projectionTop--;
	}
	else
	{
		GAME_ASSERT(gGLC.modelviewTop > 0);
		gGLC.modelviewTop--;
	}
}

void GLC_LoadIdentity(void)
{
	GLfloat* m = CurrentMatrix();
	SDL_memset(m, 0, sizeof(GLfloat)*16);
	m[0] = m[5] = m[10] = m[15] = 1;
}

void GLC_LoadMatrixf(const GLfloat* src)
{
	SDL_memcpy(CurrentMatrix(), src, sizeof(GLfloat)*16);
}

void GLC_MultMatrixf(const GLfloat* src)
{
	GLfloat* m = CurrentMatrix();
	Mat4Mul(m, src, m);
}

void GLC_Ortho(double l, double r, double b, double t, double n, double f)
{
	GLfloat o[16] = {0};
	o[0]  = (GLfloat) (2.0 / (r - l));
	o[5]  = (GLfloat) (2.0 / (t - b));
	o[10] = (GLfloat) (-2.0 / (f - n));
	o[12] = (GLfloat) (-(r + l) / (r - l));
	o[13] = (GLfloat) (-(t + b) / (t - b));
	o[14] = (GLfloat) (-(f + n) / (f - n));
	o[15] = 1;
	GLC_MultMatrixf(o);
}

// Inverse-transpose of the upper 3x3 of a column-major 4x4 matrix
static void NormalMatrix(const GLfloat* m, GLfloat* out)
{
	float a = m[0], b = m[4], c = m[8];
	float d = m[1], e = m[5], f = m[9];
	float g = m[2], h = m[6], i = m[10];

	float A =  (e*i - f*h);
	float B = -(d*i - f*g);
	float C =  (d*h - e*g);
	float det = a*A + b*B + c*C;
	float invDet = fabsf(det) > 1e-12f ? 1.0f / det : 0.0f;

	// cofactor matrix / det == inverse transpose; store column-major
	out[0] = A * invDet;				// row 0
	out[3] = B * invDet;
	out[6] = C * invDet;
	out[1] = -(b*i - c*h) * invDet;		// row 1
	out[4] =  (a*i - c*g) * invDet;
	out[7] = -(a*h - b*g) * invDet;
	out[2] =  (b*f - c*e) * invDet;		// row 2
	out[5] = -(a*f - c*d) * invDet;
	out[8] =  (a*e - b*d) * invDet;
}

#pragma mark - State

void GLC_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
	gGLC.color[0] = r;
	gGLC.color[1] = g;
	gGLC.color[2] = b;
	gGLC.color[3] = a;
}

void GLC_Enable(GLenum cap)
{
	switch (cap)
	{
		case GL_LIGHTING:			gGLC.lighting = true;	break;
		case GL_ALPHA_TEST:			gGLC.alphaTest = true;	break;
		case GL_TEXTURE_2D:			gGLC.texture2D = true;	break;
		case GL_FOG:				gGLC.fog = true;		break;
		case GL_NORMALIZE:									break;		// always normalized in shader
		case GL_COLOR_MATERIAL:								break;		// always on
		default:
			if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + MAX_LIGHTS)
				gGLC.lightEnabled[cap - GL_LIGHT0] = true;
			else
				glEnable(cap);
			break;
	}
}

void GLC_Disable(GLenum cap)
{
	switch (cap)
	{
		case GL_LIGHTING:			gGLC.lighting = false;	break;
		case GL_ALPHA_TEST:			gGLC.alphaTest = false;	break;
		case GL_TEXTURE_2D:			gGLC.texture2D = false;	break;
		case GL_FOG:				gGLC.fog = false;		break;
		case GL_NORMALIZE:									break;
		case GL_COLOR_MATERIAL:								break;
		default:
			if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + MAX_LIGHTS)
				gGLC.lightEnabled[cap - GL_LIGHT0] = false;
			else
				glDisable(cap);
			break;
	}
}

static ClientArray* GetClientArray(GLenum array)
{
	switch (array)
	{
		case GL_VERTEX_ARRAY:			return &gGLC.arrays[kAttrib_Position];
		case GL_NORMAL_ARRAY:			return &gGLC.arrays[kAttrib_Normal];
		case GL_COLOR_ARRAY:			return &gGLC.arrays[kAttrib_Color];
		case GL_TEXTURE_COORD_ARRAY:	return &gGLC.arrays[kAttrib_TexCoord];
		default:						return NULL;
	}
}

void GLC_EnableClientState(GLenum array)
{
	ClientArray* a = GetClientArray(array);
	if (a) a->enabled = true;
}

void GLC_DisableClientState(GLenum array)
{
	ClientArray* a = GetClientArray(array);
	if (a) a->enabled = false;
}

static void SetArray(int index, GLint size, GLenum type, GLsizei stride, const void* ptr)
{
	gGLC.arrays[index].size = size;
	gGLC.arrays[index].type = type;
	gGLC.arrays[index].stride = stride;
	gGLC.arrays[index].ptr = ptr;
}

void GLC_VertexPointer(GLint size, GLenum type, GLsizei stride, const void* ptr)	{ SetArray(kAttrib_Position, size, type, stride, ptr); }
void GLC_NormalPointer(GLenum type, GLsizei stride, const void* ptr)				{ SetArray(kAttrib_Normal, 3, type, stride, ptr); }
void GLC_ColorPointer(GLint size, GLenum type, GLsizei stride, const void* ptr)		{ SetArray(kAttrib_Color, size, type, stride, ptr); }
void GLC_TexCoordPointer(GLint size, GLenum type, GLsizei stride, const void* ptr)	{ SetArray(kAttrib_TexCoord, size, type, stride, ptr); }

void GLC_Lightfv(GLenum light, GLenum pname, const GLfloat* params)
{
	int i = light - GL_LIGHT0;
	if (i < 0 || i >= MAX_LIGHTS)
		return;

	switch (pname)
	{
		case GL_POSITION:
		{
			// Directional light: transform direction into eye space by the current modelview matrix
			const GLfloat* m = gGLC.modelview[gGLC.modelviewTop];
			float x = params[0], y = params[1], z = params[2];
			float ex = m[0]*x + m[4]*y + m[8]*z;
			float ey = m[1]*x + m[5]*y + m[9]*z;
			float ez = m[2]*x + m[6]*y + m[10]*z;
			float len = sqrtf(ex*ex + ey*ey + ez*ez);
			if (len > 0)
			{
				ex /= len;
				ey /= len;
				ez /= len;
			}
			gGLC.lightEyeDir[i][0] = ex;
			gGLC.lightEyeDir[i][1] = ey;
			gGLC.lightEyeDir[i][2] = ez;
			break;
		}

		case GL_DIFFUSE:
			gGLC.lightDiffuse[i][0] = params[0];
			gGLC.lightDiffuse[i][1] = params[1];
			gGLC.lightDiffuse[i][2] = params[2];
			break;

		default:		// GL_AMBIENT is always black in this game
			break;
	}
}

void GLC_LightModelfv(GLenum pname, const GLfloat* params)
{
	if (pname == GL_LIGHT_MODEL_AMBIENT)
	{
		gGLC.ambient[0] = params[0];
		gGLC.ambient[1] = params[1];
		gGLC.ambient[2] = params[2];
	}
}

void GLC_Fogi(GLenum pname, GLint param)
{
	(void) pname;
	(void) param;		// only linear fog is used
}

void GLC_Fogf(GLenum pname, GLfloat param)
{
	if (pname == GL_FOG_START)
		gGLC.fogStart = param;
	else if (pname == GL_FOG_END)
		gGLC.fogEnd = param;
}

void GLC_Fogfv(GLenum pname, const GLfloat* params)
{
	if (pname == GL_FOG_COLOR)
	{
		gGLC.fogColor[0] = params[0];
		gGLC.fogColor[1] = params[1];
		gGLC.fogColor[2] = params[2];
	}
}

void GLC_AlphaFunc(GLenum func, GLfloat ref)
{
	(void) func;		// always GL_GREATER in this game
	gGLC.alphaRef = ref;
}

// Keep destination alpha meaningful (premultiplied), so that a render target
// with transparent areas (the VR HUD layer) composites correctly.
void GLC_BlendFunc(GLenum sfactor, GLenum dfactor)
{
	if (sfactor == GL_SRC_ALPHA && dfactor == GL_ONE)			// additive
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
	else if (sfactor == GL_SRC_ALPHA && dfactor == GL_ONE_MINUS_SRC_ALPHA)
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	else
		glBlendFunc(sfactor, dfactor);
}

// Light directions are stored in eye space (like fixed-function GL).
// When switching between views (e.g. per-eye VR rendering), move them from one
// view's eye space to another. Assumes rigid (rotation + translation) view matrices.
void GLC_RebaseLights(const GLfloat* a, const GLfloat* b)
{
	for (int i = 0; i < MAX_LIGHTS; i++)
	{
		const float* e = gGLC.lightEyeDir[i];

		// world = transpose(A3x3) * eyeA
		float wx = a[0]*e[0] + a[1]*e[1] + a[2]*e[2];
		float wy = a[4]*e[0] + a[5]*e[1] + a[6]*e[2];
		float wz = a[8]*e[0] + a[9]*e[1] + a[10]*e[2];

		// eyeB = B3x3 * world
		gGLC.lightEyeDir[i][0] = b[0]*wx + b[4]*wy + b[8]*wz;
		gGLC.lightEyeDir[i][1] = b[1]*wx + b[5]*wy + b[9]*wz;
		gGLC.lightEyeDir[i][2] = b[2]*wx + b[6]*wy + b[10]*wz;
	}
}

#pragma mark - Drawing

static void ApplyState(void)
{
	GAME_ASSERT(gGLC.initialized);

	glUseProgram(gGLC.program);

	const GLfloat* mv = gGLC.modelview[gGLC.modelviewTop];
	const GLfloat* proj = gGLC.projection[gGLC.projectionTop];
	GLfloat mvp[16];
	Mat4Mul(proj, mv, mvp);
	glUniformMatrix4fv(gGLC.uMVP, 1, GL_FALSE, mvp);
	glUniformMatrix4fv(gGLC.uMV, 1, GL_FALSE, mv);

	// Lighting only matters if we have normals; otherwise fixed-function GL would use the current normal
	glUniform1i(gGLC.uLighting, gGLC.lighting);
	if (gGLC.lighting)
	{
		GLfloat nm[9];
		NormalMatrix(mv, nm);
		glUniformMatrix3fv(gGLC.uNormalMat, 1, GL_FALSE, nm);

		GLfloat dirs[MAX_LIGHTS*3];
		GLfloat diffuse[MAX_LIGHTS*3];
		int n = 0;
		for (int i = 0; i < MAX_LIGHTS; i++)
		{
			if (!gGLC.lightEnabled[i])
				continue;
			SDL_memcpy(&dirs[n*3], gGLC.lightEyeDir[i], sizeof(GLfloat)*3);
			SDL_memcpy(&diffuse[n*3], gGLC.lightDiffuse[i], sizeof(GLfloat)*3);
			n++;
		}
		glUniform1i(gGLC.uNumLights, n);
		if (n > 0)
		{
			glUniform3fv(gGLC.uLightDir, n, dirs);
			glUniform3fv(gGLC.uLightDiffuse, n, diffuse);
		}
		glUniform3fv(gGLC.uAmbient, 1, gGLC.ambient);
	}

	glUniform1i(gGLC.uTexturing, gGLC.texture2D && gGLC.arrays[kAttrib_TexCoord].enabled);
	glUniform1i(gGLC.uAlphaTest, gGLC.alphaTest);
	glUniform1f(gGLC.uAlphaRef, gGLC.alphaRef);
	glUniform1i(gGLC.uFog, gGLC.fog);
	if (gGLC.fog)
	{
		glUniform2f(gGLC.uFogParams, gGLC.fogStart, gGLC.fogEnd);
		glUniform3fv(gGLC.uFogColor, 1, gGLC.fogColor);
	}

	glActiveTexture(GL_TEXTURE0);

	// Vertex attributes (client-side arrays are legal in GLES3 with the default VAO)
	for (int i = 0; i < 4; i++)
	{
		const ClientArray* a = &gGLC.arrays[i];
		if (a->enabled && a->ptr)
		{
			glEnableVertexAttribArray(i);
			glVertexAttribPointer(i, a->size, a->type, GL_FALSE, a->stride, a->ptr);
		}
		else
		{
			glDisableVertexAttribArray(i);
			switch (i)
			{
				case kAttrib_Normal:	glVertexAttrib4f(i, 0, 0, 1, 0); break;
				case kAttrib_Color:		glVertexAttrib4fv(i, gGLC.color); break;
				case kAttrib_TexCoord:	glVertexAttrib4f(i, 0, 0, 0, 1); break;
				default:				glVertexAttrib4f(i, 0, 0, 0, 1); break;
			}
		}
	}
}

void GLC_DrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices)
{
	ApplyState();
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glDrawElements(mode, count, type, indices);
}

void GLC_Begin(GLenum mode)
{
	gGLC.immediateMode = mode;
	gGLC.immediateCount = 0;
}

void GLC_Vertex3f(GLfloat x, GLfloat y, GLfloat z)
{
	if (gGLC.immediateCount >= MAX_IMMEDIATE_VERTICES)
		return;
	GLfloat* v = &gGLC.immediateVerts[gGLC.immediateCount * 3];
	v[0] = x;
	v[1] = y;
	v[2] = z;
	gGLC.immediateCount++;
}

void GLC_End(void)
{
	// Immediate mode is only used for debug visualizations: draw untextured, unlit
	ClientArray saved[4];
	SDL_memcpy(saved, gGLC.arrays, sizeof(saved));
	bool savedLighting = gGLC.lighting;
	bool savedTexture = gGLC.texture2D;

	SDL_memset(gGLC.arrays, 0, sizeof(gGLC.arrays));
	gGLC.arrays[kAttrib_Position] = (ClientArray) { true, 3, GL_FLOAT, 0, gGLC.immediateVerts };
	gGLC.lighting = false;
	gGLC.texture2D = false;

	ApplyState();
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDrawArrays(gGLC.immediateMode, 0, gGLC.immediateCount);

	SDL_memcpy(gGLC.arrays, saved, sizeof(saved));
	gGLC.lighting = savedLighting;
	gGLC.texture2D = savedTexture;
	gGLC.immediateCount = 0;
}

#pragma mark - Textures

static bool* GetOpaqueFlagForBoundTexture(void)
{
	GLint name = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &name);
	if (name <= 0)
		return NULL;

	if ((size_t) name >= gGLC.textureOpaqueFlagsCapacity)
	{
		size_t newCap = SDL_max(1024, (size_t) name * 2);
		gGLC.textureOpaqueFlags = SDL_realloc(gGLC.textureOpaqueFlags, newCap);
		SDL_memset(gGLC.textureOpaqueFlags + gGLC.textureOpaqueFlagsCapacity, 0, newCap - gGLC.textureOpaqueFlagsCapacity);
		gGLC.textureOpaqueFlagsCapacity = newCap;
	}

	return (bool*) &gGLC.textureOpaqueFlags[name];
}

// Converts a desktop-GL pixel buffer into tightly-packed RGBA8.
static const void* ConvertToRGBA8(
		int w, int h, GLenum format, GLenum type, const void* pixels, int srcRowPixels, bool forceOpaque)
{
	if (!pixels)
		return NULL;

	if (srcRowPixels <= 0)
		srcRowPixels = w;

	size_t needed = (size_t) w * h * 4;
	if (needed > gGLC.convertBufferSize)
	{
		gGLC.convertBuffer = SDL_realloc(gGLC.convertBuffer, needed);
		gGLC.convertBufferSize = needed;
	}

	uint8_t* out = gGLC.convertBuffer;

	for (int y = 0; y < h; y++)
	{
		uint8_t* dst = out + (size_t) y * w * 4;

		if (type == GL_UNSIGNED_SHORT_1_5_5_5_REV)
		{
			const uint16_t* src = (const uint16_t*) pixels + (size_t) y * srcRowPixels;
			for (int x = 0; x < w; x++)
			{
				uint16_t p = src[x];
				uint8_t b5 = (p      ) & 0x1F;
				uint8_t g5 = (p >>  5) & 0x1F;
				uint8_t r5 = (p >> 10) & 0x1F;
				dst[x*4+0] = (r5 << 3) | (r5 >> 2);
				dst[x*4+1] = (g5 << 3) | (g5 >> 2);
				dst[x*4+2] = (b5 << 3) | (b5 >> 2);
				dst[x*4+3] = (p & 0x8000) ? 0xFF : 0x00;
			}
		}
		else if (format == GL_BGRA)		// GL_UNSIGNED_INT_8_8_8_8_REV or GL_UNSIGNED_BYTE: B,G,R,A in memory
		{
			const uint8_t* src = (const uint8_t*) pixels + (size_t) y * srcRowPixels * 4;
			for (int x = 0; x < w; x++)
			{
				dst[x*4+0] = src[x*4+2];
				dst[x*4+1] = src[x*4+1];
				dst[x*4+2] = src[x*4+0];
				dst[x*4+3] = src[x*4+3];
			}
		}
		else if (format == GL_BGR)
		{
			const uint8_t* src = (const uint8_t*) pixels + (size_t) y * srcRowPixels * 3;
			for (int x = 0; x < w; x++)
			{
				dst[x*4+0] = src[x*3+2];
				dst[x*4+1] = src[x*3+1];
				dst[x*4+2] = src[x*3+0];
				dst[x*4+3] = 0xFF;
			}
		}
		else if (format == GL_RGB)
		{
			const uint8_t* src = (const uint8_t*) pixels + (size_t) y * srcRowPixels * 3;
			for (int x = 0; x < w; x++)
			{
				dst[x*4+0] = src[x*3+0];
				dst[x*4+1] = src[x*3+1];
				dst[x*4+2] = src[x*3+2];
				dst[x*4+3] = 0xFF;
			}
		}
		else		// GL_RGBA, GL_UNSIGNED_BYTE
		{
			const uint8_t* src = (const uint8_t*) pixels + (size_t) y * srcRowPixels * 4;
			SDL_memcpy(dst, src, (size_t) w * 4);
		}

		if (forceOpaque)
		{
			for (int x = 0; x < w; x++)
				dst[x*4+3] = 0xFF;
		}
	}

	return out;
}

void GLC_TexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void* pixels)
{
	(void) border;

	bool forceOpaque = (internalFormat == GL_RGB);
	bool* opaqueFlag = GetOpaqueFlagForBoundTexture();
	if (opaqueFlag)
		*opaqueFlag = forceOpaque;

	GLint rowLength = 0;
	glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	const void* rgba = ConvertToRGBA8(w, h, format, type, pixels, rowLength, forceOpaque);
	glTexImage2D(target, level, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

	glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
}

void GLC_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const void* pixels)
{
	bool* opaqueFlag = GetOpaqueFlagForBoundTexture();
	bool forceOpaque = opaqueFlag && *opaqueFlag;

	GLint rowLength = 0;
	glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	const void* rgba = ConvertToRGBA8(w, h, format, type, pixels, rowLength, forceOpaque);
	glTexSubImage2D(target, level, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

	glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
}

#endif
