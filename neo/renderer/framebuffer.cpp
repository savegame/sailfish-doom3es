/*
Emile Belanger
GPL3
*/
#include "renderer/tr_local.h"
#include "renderer/VertexCache.h"


idCVar r_framebufferFilter( "r_framebufferFilter", "0", CVAR_RENDERER | CVAR_BOOL, "Image filter when using the framebuffer. 0 = Nearest, 1 = Linear" );

static GLuint m_framebuffer = -1;
static GLuint m_depthbuffer;
static GLuint m_stencilbuffer;

static int m_framebuffer_width, m_framebuffer_height;
static GLuint m_framebuffer_texture;

static GLuint m_positionLoc;
static GLuint m_texCoordLoc;
static GLuint m_samplerLoc;
static GLuint m_coordMaxLoc;

static GLuint r_program;

static int fixNpot(int v)
{
	int ret = 1;
	while(ret < v)
		ret <<= 1;
	return ret;
}

#define LOG common->Printf
int loadShader(int shaderType, const char * source)
{
	int shader = qglCreateShader(shaderType);

	if(shader != 0)
	{
		qglShaderSource(shader, 1, &source, NULL);
		qglCompileShader(shader);

		GLint  length;

		qglGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

		if(length)
		{
			char* buffer  =  new char [ length ];
			qglGetShaderInfoLog(shader, length, NULL, buffer);
			LOG("shader = %s\n", buffer);
			delete [] buffer;

			GLint success;
			qglGetShaderiv(shader, GL_COMPILE_STATUS, &success);

			if(success != GL_TRUE)
			{
				//LOG("ERROR compiling shader\n");
			}
		}
	}
	else
	{
		//LOG("FAILED to create shader");
	}

	return shader;
}

int createProgram(const char * vertexSource, const char *  fragmentSource)
{
	int vertexShader = loadShader(GL_VERTEX_SHADER, vertexSource);
	int pixelShader = loadShader(GL_FRAGMENT_SHADER, fragmentSource);

	int program = glCreateProgram();

	if(program != 0)
	{
		glAttachShader(program, vertexShader);
		// checkGlError("glAttachShader");
		glAttachShader(program, pixelShader);
		// checkGlError("glAttachShader");
		glLinkProgram(program);
#define GL_LINK_STATUS 0x8B82
		int linkStatus[1];
		glGetProgramiv(program, GL_LINK_STATUS, linkStatus);

		if(linkStatus[0] != GL_TRUE)
		{
			LOG("Could not link program: ");
			char log[256];
			GLsizei size;
			glGetProgramInfoLog(program, 256, &size, log);
			//LOG("Log: %s", log);
			//glDeleteProgram(program);
			program = 0;
		}

	}
	else
	{
		LOG("FAILED to create program");
	}

	LOG("Program linked OK %d", program);
	return program;
}

static void createShaders (void)
{
	const GLchar *vertSource = \
           "attribute vec4 a_position;                                     \n \
			attribute vec2 a_texCoord;                                     \n \
			varying vec2 v_texCoord;                                       \n \
			void main()                                                    \n \
			{                                                              \n \
			   gl_Position = a_position;                                   \n \
			   v_texCoord = a_texCoord;                                    \n \
			}                                                              \n \
			";
#ifdef LIPSICK_HACK
	const GLchar *fragSource = \
			"precision mediump float;                                \n  \
			varying vec2 v_texCoord;                                 \n  \
			uniform sampler2D s_texture;                             \n  \
			void main()                                              \n  \
			{                                                        \n  \
				gl_FragColor = texture2D( s_texture, v_texCoord );  \n  \
			}                                                        \n  \
			";
#else
	const GLchar *fragSource = \
			"precision highp float;                                     \n \
			varying vec2 v_texCoord;                                      \n \
			uniform sampler2D s_texture;                                  \n \
			uniform vec4 v_coordMax;                                      \n \
			void main()                                                   \n \
			{                                                             \n \
				gl_FragColor = texture2D( s_texture, vec2(v_texCoord.y * v_coordMax[3],   \n \
				                          v_coordMax[0] - v_texCoord.x * v_coordMax[2]) ); \n \
			}                                                             \n \
			";
#endif

	r_program = createProgram(vertSource, fragSource);

	glUseProgram(r_program);

	// get attrib locations
	m_positionLoc = glGetAttribLocation(r_program, "a_position");
	m_texCoordLoc = glGetAttribLocation(r_program, "a_texCoord");
	m_samplerLoc  = glGetUniformLocation(r_program, "s_texture");
	m_coordMaxLoc = glGetUniformLocation(r_program, "v_coordMax");

	glUniform1i(m_samplerLoc, 0);
	glUniform4f(m_coordMaxLoc, 0.0, 0.0, 0.0, 0.0);
}


void R_InitFrameBuffer()
{
	common->Printf("R_InitFrameBuffer Real[%d, %d] -> Framebuffer[%d,%d]\n", glConfig.vidWidthReal, glConfig.vidHeightReal, glConfig.vidWidth, glConfig.vidHeight);

	if(glConfig.vidWidthReal == glConfig.vidWidth && glConfig.vidHeightReal == glConfig.vidHeight)
	{
		common->Printf("Not using framebuffer\n");
		return;
	}

	glConfig.npotAvailable = false;

	m_framebuffer_width = glConfig.vidWidth;
	m_framebuffer_height = glConfig.vidHeight;

	if (!glConfig.npotAvailable)
	{
		m_framebuffer_width = fixNpot(m_framebuffer_width);
		m_framebuffer_height = fixNpot(m_framebuffer_height);
	}

	common->Printf("Framebuffer buffer size = [%d, %d]\n", m_framebuffer_width, m_framebuffer_height);

	// Create texture
	glGenTextures(1, &m_framebuffer_texture);
	glBindTexture(GL_TEXTURE_2D, m_framebuffer_texture);
	
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_framebuffer_width, m_framebuffer_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	if(r_framebufferFilter.GetInteger() == 0)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
	else
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}

	// Create framebuffer
	glGenFramebuffers(1, &m_framebuffer);

	// Create renderbuffer
	glGenRenderbuffers(1, &m_depthbuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, m_depthbuffer);

	if(glConfig.depthStencilAvailable)
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, m_framebuffer_width, m_framebuffer_height);
	else
	{
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, m_framebuffer_width, m_framebuffer_height);

		// Need separate Stencil buffer
		glGenRenderbuffers(1, &m_stencilbuffer);
		glBindRenderbuffer(GL_RENDERBUFFER, m_stencilbuffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, m_framebuffer_width, m_framebuffer_height);
	}

	createShaders();
}

void R_FrameBufferStart()
{
	if(m_framebuffer == -1)
		return;

	// Render to framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_framebuffer_texture, 0);

    // Attach combined depth+stencil
    if(glConfig.depthStencilAvailable)
    {
    	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthbuffer);
    	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depthbuffer);
	}
	else
	{
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthbuffer);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_stencilbuffer);
	}

    GLenum result = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if(result != GL_FRAMEBUFFER_COMPLETE)
	{
	    common->Error( "Error binding Framebuffer: %d\n", result );
	}
}


void R_FrameBufferEnd()
{
	if(m_framebuffer == -1)
		return;

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	// Bind the texture
	glBindTexture(GL_TEXTURE_2D, m_framebuffer_texture);

	// Unbind any VBOs
	vertexCache.UnbindIndex();
	vertexCache.UnbindVertex();

	glUseProgram(r_program);

	GLfloat vert[] =
	{
		-1.f, -1.f,  0.0f,  // 0. left-bottom
		-1.f,  1.f,  0.0f,  // 1. left-top
		1.f, 1.f,  0.0f,    // 2. right-top
		1.f, -1.f,  0.0f,   // 3. right-bottom
	};

	GLfloat smax = 1;
	GLfloat tmax = 1;

	if (!glConfig.npotAvailable)
	{
		smax =  (float)glConfig.vidWidth / (float)m_framebuffer_width;
		tmax =   (float)glConfig.vidHeight / (float)m_framebuffer_height;
	}

	GLfloat texVert[] =
	{
		0.0f, 0.0f, // TexCoord 0
		0.0f, tmax, // TexCoord 1
		smax, tmax, // TexCoord 2
		smax, 0.0f  // TexCoord 3
	};

	glVertexAttribPointer(m_positionLoc, 3, GL_FLOAT,
				  false,
				  3 * 4,
				  vert);

	glVertexAttribPointer(m_texCoordLoc, 2, GL_FLOAT,
						  false,
						  2 * 4,
						  texVert);

	glEnableVertexAttribArray(m_positionLoc);
	glEnableVertexAttribArray(m_texCoordLoc);


	// Set the sampler texture unit to 0
	glUniform1i(m_samplerLoc, 0);
	glUniform4f(m_coordMaxLoc, tmax, smax, tmax / smax, smax / tmax);

	glViewport (0, 0, glConfig.vidWidthReal, glConfig.vidHeightReal );

	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);

	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}


