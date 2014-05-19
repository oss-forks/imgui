//
// Copyright (c) 2009-2010 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//

// Source altered and distributed from https://github.com/AdrienHerubel/imgui


#define _USE_MATH_DEFINES
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include "imguiRenderGL3.h"
#include <yip-imports/gl.h>

#include "imgui.h"

// Some math headers don't have PI defined.
static const float PI = 3.14159265f;

#include <yip-imports/stb_truetype.h>

static const unsigned TEMP_COORD_COUNT = 100;
static float g_tempCoords[TEMP_COORD_COUNT*2];
static float g_tempNormals[TEMP_COORD_COUNT*2];
static float g_tempVertices[TEMP_COORD_COUNT * 12 + (TEMP_COORD_COUNT - 2) * 6];
static float g_tempTextureCoords[TEMP_COORD_COUNT * 12 + (TEMP_COORD_COUNT - 2) * 6];
static float g_tempColors[TEMP_COORD_COUNT * 24 + (TEMP_COORD_COUNT - 2) * 12];

static const int CIRCLE_VERTS = 8*4;
static float g_circleVerts[CIRCLE_VERTS*2];

static stbtt_bakedchar g_cdata[96]; // ASCII 32..126 is 95 glyphs
static GL::UInt g_ftex = 0;
static GL::UInt g_whitetex = 0;
static GL::UInt g_vbos[3] = {0, 0, 0};
static GL::UInt g_program = 0;
static GL::UInt g_programViewportLocation = 0;
static GL::UInt g_programTextureLocation = 0;

inline unsigned int RGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
        return (r) | (g << 8) | (b << 16) | (a << 24);
}

static void bindVertexArray()
{
        GL::enableVertexAttribArray(0);
        GL::enableVertexAttribArray(1);
        GL::enableVertexAttribArray(2);

        GL::bindBuffer(GL::ARRAY_BUFFER, g_vbos[0]);
        GL::vertexAttribPointer(0, 2, GL::FLOAT, GL::FALSE, sizeof(GL::FLOAT)*2, (void*)0);
        GL::bufferData(GL::ARRAY_BUFFER, 0, 0, GL::STATIC_DRAW);
        GL::bindBuffer(GL::ARRAY_BUFFER, g_vbos[1]);
        GL::vertexAttribPointer(1, 2, GL::FLOAT, GL::FALSE, sizeof(GL::FLOAT)*2, (void*)0);
        GL::bufferData(GL::ARRAY_BUFFER, 0, 0, GL::STATIC_DRAW);
        GL::bindBuffer(GL::ARRAY_BUFFER, g_vbos[2]);
        GL::vertexAttribPointer(2, 4, GL::FLOAT, GL::FALSE, sizeof(GL::FLOAT)*4, (void*)0);
        GL::bufferData(GL::ARRAY_BUFFER, 0, 0, GL::STATIC_DRAW);
}

static void drawPolygon(const float* coords, unsigned numCoords, float r, unsigned int col)
{
        if (numCoords > TEMP_COORD_COUNT) numCoords = TEMP_COORD_COUNT;
        
        for (unsigned i = 0, j = numCoords-1; i < numCoords; j=i++)
        {
                const float* v0 = &coords[j*2];
                const float* v1 = &coords[i*2];
                float dx = v1[0] - v0[0];
                float dy = v1[1] - v0[1];
                float d = sqrtf(dx*dx+dy*dy);
                if (d > 0)
                {
                        d = 1.0f/d;
                        dx *= d;
                        dy *= d;
                }
                g_tempNormals[j*2+0] = dy;
                g_tempNormals[j*2+1] = -dx;
        }
        
        float colf[4] = { (float) (col&0xff) / 255.f, (float) ((col>>8)&0xff) / 255.f, (float) ((col>>16)&0xff) / 255.f, (float) ((col>>24)&0xff) / 255.f};
        float colTransf[4] = { (float) (col&0xff) / 255.f, (float) ((col>>8)&0xff) / 255.f, (float) ((col>>16)&0xff) / 255.f, 0};

        for (unsigned i = 0, j = numCoords-1; i < numCoords; j=i++)
        {
                float dlx0 = g_tempNormals[j*2+0];
                float dly0 = g_tempNormals[j*2+1];
                float dlx1 = g_tempNormals[i*2+0];
                float dly1 = g_tempNormals[i*2+1];
                float dmx = (dlx0 + dlx1) * 0.5f;
                float dmy = (dly0 + dly1) * 0.5f;
                float   dmr2 = dmx*dmx + dmy*dmy;
                if (dmr2 > 0.000001f)
                {
                        float   scale = 1.0f / dmr2;
                        if (scale > 10.0f) scale = 10.0f;
                        dmx *= scale;
                        dmy *= scale;
                }
                g_tempCoords[i*2+0] = coords[i*2+0]+dmx*r;
                g_tempCoords[i*2+1] = coords[i*2+1]+dmy*r;
        }
        
        int vSize = numCoords * 12 + (numCoords - 2) * 6;
        int uvSize = numCoords * 2 * 6 + (numCoords - 2) * 2 * 3;
        int cSize = numCoords * 4 * 6 + (numCoords - 2) * 4 * 3;
        float * v = g_tempVertices;
        float * uv = g_tempTextureCoords;
        memset(uv, 0, uvSize * sizeof(float));
        float * c = g_tempColors;
        memset(c, 1, cSize * sizeof(float));

        float * ptrV = v;
        float * ptrC = c;
        for (unsigned i = 0, j = numCoords-1; i < numCoords; j=i++)
        {
            *ptrV = coords[i*2];
            *(ptrV+1) = coords[i*2 + 1];
            ptrV += 2;
            *ptrV = coords[j*2];
            *(ptrV+1) = coords[j*2 + 1];
            ptrV += 2;          
            *ptrV = g_tempCoords[j*2];
            *(ptrV+1) = g_tempCoords[j*2 + 1];
            ptrV += 2;                     
            *ptrV = g_tempCoords[j*2];
            *(ptrV+1) = g_tempCoords[j*2 + 1];
            ptrV += 2;                     
            *ptrV = g_tempCoords[i*2];
            *(ptrV+1) = g_tempCoords[i*2 + 1];
            ptrV += 2;                     
            *ptrV = coords[i*2];
            *(ptrV+1) = coords[i*2 + 1];            
            ptrV += 2;

            *ptrC = colf[0];
            *(ptrC+1) = colf[1];
            *(ptrC+2) = colf[2];
            *(ptrC+3) = colf[3];
            ptrC += 4;
            *ptrC = colf[0];
            *(ptrC+1) = colf[1];
            *(ptrC+2) = colf[2];
            *(ptrC+3) = colf[3];
            ptrC += 4;
            *ptrC = colTransf[0];
            *(ptrC+1) = colTransf[1];
            *(ptrC+2) = colTransf[2];
            *(ptrC+3) = colTransf[3];
            ptrC += 4;
            *ptrC = colTransf[0];
            *(ptrC+1) = colTransf[1];
            *(ptrC+2) = colTransf[2];
            *(ptrC+3) = colTransf[3];
            ptrC += 4;
            *ptrC = colTransf[0];
            *(ptrC+1) = colTransf[1];
            *(ptrC+2) = colTransf[2];
            *(ptrC+3) = colTransf[3];
            ptrC += 4;
            *ptrC = colf[0];
            *(ptrC+1) = colf[1];
            *(ptrC+2) = colf[2];
            *(ptrC+3) = colf[3];
            ptrC += 4;
        }
        
        for (unsigned i = 2; i < numCoords; ++i)
        {
            *ptrV = coords[0];
            *(ptrV+1) = coords[1];
            ptrV += 2;
            *ptrV = coords[(i-1)*2];
            *(ptrV+1) = coords[(i-1)*2+1];
            ptrV += 2;  
            *ptrV = coords[i*2];
            *(ptrV+1) = coords[i*2 + 1];            
            ptrV += 2;            

            *ptrC = colf[0];
            *(ptrC+1) = colf[1];
            *(ptrC+2) = colf[2];
            *(ptrC+3) = colf[3];
            ptrC += 4;
            *ptrC = colf[0];
            *(ptrC+1) = colf[1];
            *(ptrC+2) = colf[2];
            *(ptrC+3) = colf[3];
            ptrC += 4;
            *ptrC = colf[0];
            *(ptrC+1) = colf[1];
            *(ptrC+2) = colf[2];
            *(ptrC+3) = colf[3];
            ptrC += 4;          
        }        
        GL::bindTexture(GL::TEXTURE_2D, g_whitetex);
        
        bindVertexArray();
        GL::bindBuffer(GL::ARRAY_BUFFER, g_vbos[0]);
        GL::bufferData(GL::ARRAY_BUFFER, vSize*sizeof(float), v, GL::STATIC_DRAW);
        GL::bindBuffer(GL::ARRAY_BUFFER, g_vbos[1]);
        GL::bufferData(GL::ARRAY_BUFFER, uvSize*sizeof(float), uv, GL::STATIC_DRAW);
        GL::bindBuffer(GL::ARRAY_BUFFER, g_vbos[2]);
        GL::bufferData(GL::ARRAY_BUFFER, cSize*sizeof(float), c, GL::STATIC_DRAW);
        GL::drawArrays(GL::TRIANGLES, 0, (numCoords * 2 + numCoords - 2)*3);
 
}

static void drawRect(float x, float y, float w, float h, float fth, unsigned int col)
{
        float verts[4*2] =
        {
                x+0.5f, y+0.5f,
                x+w-0.5f, y+0.5f,
                x+w-0.5f, y+h-0.5f,
                x+0.5f, y+h-0.5f,
        };
        drawPolygon(verts, 4, fth, col);
}

/*
static void drawEllipse(float x, float y, float w, float h, float fth, unsigned int col)
{
        float verts[CIRCLE_VERTS*2];
        const float* cverts = g_circleVerts;
        float* v = verts;
        
        for (int i = 0; i < CIRCLE_VERTS; ++i)
        {
                *v++ = x + cverts[i*2]*w;
                *v++ = y + cverts[i*2+1]*h;
        }
        
        drawPolygon(verts, CIRCLE_VERTS, fth, col);
}
*/

static void drawRoundedRect(float x, float y, float w, float h, float r, float fth, unsigned int col)
{
        const unsigned n = CIRCLE_VERTS/4;
        float verts[(n+1)*4*2];
        const float* cverts = g_circleVerts;
        float* v = verts;
        
        for (unsigned i = 0; i <= n; ++i)
        {
                *v++ = x+w-r + cverts[i*2]*r;
                *v++ = y+h-r + cverts[i*2+1]*r;
        }
        
        for (unsigned i = n; i <= n*2; ++i)
        {
                *v++ = x+r + cverts[i*2]*r;
                *v++ = y+h-r + cverts[i*2+1]*r;
        }
        
        for (unsigned i = n*2; i <= n*3; ++i)
        {
                *v++ = x+r + cverts[i*2]*r;
                *v++ = y+r + cverts[i*2+1]*r;
        }
        
        for (unsigned i = n*3; i < n*4; ++i)
        {
                *v++ = x+w-r + cverts[i*2]*r;
                *v++ = y+r + cverts[i*2+1]*r;
        }
        *v++ = x+w-r + cverts[0]*r;
        *v++ = y+r + cverts[1]*r;
        
        drawPolygon(verts, (n+1)*4, fth, col);
}


static void drawLine(float x0, float y0, float x1, float y1, float r, float fth, unsigned int col)
{
        float dx = x1-x0;
        float dy = y1-y0;
        float d = sqrtf(dx*dx+dy*dy);
        if (d > 0.0001f)
        {
                d = 1.0f/d;
                dx *= d;
                dy *= d;
        }
        float nx = dy;
        float ny = -dx;
        float verts[4*2];
        r -= fth;
        r *= 0.5f;
        if (r < 0.01f) r = 0.01f;
        dx *= r;
        dy *= r;
        nx *= r;
        ny *= r;
        
        verts[0] = x0-dx-nx;
        verts[1] = y0-dy-ny;
        
        verts[2] = x0-dx+nx;
        verts[3] = y0-dy+ny;
        
        verts[4] = x1+dx+nx;
        verts[5] = y1+dy+ny;
        
        verts[6] = x1+dx-nx;
        verts[7] = y1+dy-ny;
        
        drawPolygon(verts, 4, fth, col);
}


bool imguiRenderGLInit(Resource::Loader & loader, const std::string & fontpath)
{
        for (int i = 0; i < CIRCLE_VERTS; ++i)
        {
                float a = (float)i/(float)CIRCLE_VERTS * PI*2;
                g_circleVerts[i*2+0] = cosf(a);
                g_circleVerts[i*2+1] = sinf(a);
        }

        // Load font.
        std::string ttfBuffer = loader.loadResource(fontpath);
        
        unsigned char* bmap = (unsigned char*)malloc(512*512);
        if (!bmap)
        {
                return false;
        }
        
        stbtt_BakeFontBitmap((const unsigned char *)ttfBuffer.data(),0, 15.0f, bmap,512,512, 32,96, g_cdata);
        
        // can free ttf_buffer at this point
        GL::genTextures(1, &g_ftex);
        GL::bindTexture(GL::TEXTURE_2D, g_ftex);
        GL::texImage2D(GL::TEXTURE_2D, 0, GL::LUMINANCE, 512,512, 0, GL::LUMINANCE, GL::UNSIGNED_BYTE, bmap);
        GL::texParameteri(GL::TEXTURE_2D, GL::TEXTURE_MIN_FILTER, GL::LINEAR);
        GL::texParameteri(GL::TEXTURE_2D, GL::TEXTURE_MAG_FILTER, GL::LINEAR);

        // can free ttf_buffer at this point
        unsigned char white_alpha = 255;
        GL::genTextures(1, &g_whitetex);
        GL::bindTexture(GL::TEXTURE_2D, g_whitetex);
        GL::texImage2D(GL::TEXTURE_2D, 0, GL::LUMINANCE, 1, 1, 0, GL::LUMINANCE, GL::UNSIGNED_BYTE, &white_alpha);
        GL::texParameteri(GL::TEXTURE_2D, GL::TEXTURE_MIN_FILTER, GL::LINEAR);
        GL::texParameteri(GL::TEXTURE_2D, GL::TEXTURE_MAG_FILTER, GL::LINEAR);

        GL::genBuffers(3, g_vbos);

        g_program = GL::createProgram();
    
        const char * vs =
//        "#version 150\n"
        "uniform vec2 Viewport;\n"
        "attribute vec2 VertexPosition;\n"
        "attribute vec2 VertexTexCoord;\n"
        "attribute vec4 VertexColor;\n"
        "varying vec2 texCoord;\n"
        "varying vec4 vertexColor;\n"
        "void main(void)\n"
        "{\n"
        "    vertexColor = VertexColor;\n"
        "    texCoord = VertexTexCoord;\n"
        "    gl_Position = vec4(VertexPosition * 2.0 / Viewport - 1.0, 0.f, 1.0);\n"
        "}\n";
        GL::UInt vso = GL::createShader(GL::VERTEX_SHADER);
        GL::shaderSource(vso, 1, (const char **)  &vs, NULL);
        GL::compileShader(vso);
        GL::attachShader(g_program, vso);

        const char * fs =
//        "#version 150\n"
        "#ifdef GL_ES\n"
        "precision mediump float;\n"
        "#endif\n"
        "varying vec2 texCoord;\n"
        "varying vec4 vertexColor;\n"
        "uniform sampler2D Texture;\n"
        "void main(void)\n"
        "{\n"
        "    float alpha = texture(Texture, texCoord).r;\n"
        "    gl_FragColor = vec4(vertexColor.rgb, vertexColor.a * alpha);\n"
        "}\n";
        GL::UInt fso = GL::createShader(GL::FRAGMENT_SHADER);

        GL::shaderSource(fso, 1, (const char **) &fs, NULL);
        GL::compileShader(fso);
        GL::attachShader(g_program, fso);

        GL::bindAttribLocation(g_program,  0,  "VertexPosition");
        GL::bindAttribLocation(g_program,  1,  "VertexTexCoord");
        GL::bindAttribLocation(g_program,  2,  "VertexColor");
        GL::linkProgram(g_program);
        GL::deleteShader(vso);
        GL::deleteShader(fso);

        GL::useProgram(g_program);
        g_programViewportLocation = GL::getUniformLocation(g_program, "Viewport");
        g_programTextureLocation = GL::getUniformLocation(g_program, "Texture");

        GL::useProgram(0);


        free(bmap);

        return true;
}

void imguiRenderGLDestroy()
{
        if (g_ftex)
        {
                GL::deleteTextures(1, &g_ftex);
                g_ftex = 0;
        }

        if (g_vbos[0])
        {
            GL::deleteBuffers(3, g_vbos);
            g_vbos[0] = 0;
        }

        if (g_program)
        {
            GL::deleteProgram(g_program);
            g_program = 0;
        }

}

static void getBakedQuad(stbtt_bakedchar *chardata, int pw, int ph, int char_index,
                                                 float *xpos, float *ypos, stbtt_aligned_quad *q)
{
        stbtt_bakedchar *b = chardata + char_index;
        int round_x = (int)floor(*xpos + b->xoff);
        int round_y = (int)floor(*ypos - b->yoff);
        
        q->x0 = (float)round_x;
        q->y0 = (float)round_y;
        q->x1 = (float)round_x + b->x1 - b->x0;
        q->y1 = (float)round_y - b->y1 + b->y0;
        
        q->s0 = b->x0 / (float)pw;
        q->t0 = b->y0 / (float)pw;
        q->s1 = b->x1 / (float)ph;
        q->t1 = b->y1 / (float)ph;
        
        *xpos += b->xadvance;
}

static const float g_tabStops[4] = {150, 210, 270, 330};

static float getTextLength(stbtt_bakedchar *chardata, const char* text)
{
        float xpos = 0;
        float len = 0;
        while (*text)
        {
                int c = (unsigned char)*text;
                if (c == '\t')
                {
                        for (int i = 0; i < 4; ++i)
                        {
                                if (xpos < g_tabStops[i])
                                {
                                        xpos = g_tabStops[i];
                                        break;
                                }
                        }
                }
                else if (c >= 32 && c < 128)
                {
                        stbtt_bakedchar *b = chardata + c-32;
                        int round_x = (int)floor((xpos + b->xoff) + 0.5);
                        len = round_x + b->x1 - b->x0 + 0.5f;
                        xpos += b->xadvance;
                }
                ++text;
        }
        return len;
}

static void drawText(float x, float y, const char *text, int align, unsigned int col)
{
        if (!g_ftex) return;
        if (!text) return;
        
        if (align == IMGUI_ALIGN_CENTER)
                x -= getTextLength(g_cdata, text)/2;
        else if (align == IMGUI_ALIGN_RIGHT)
                x -= getTextLength(g_cdata, text);
        
        float r = (float) (col&0xff) / 255.f;
        float g = (float) ((col>>8)&0xff) / 255.f;
        float b = (float) ((col>>16)&0xff) / 255.f;
        float a = (float) ((col>>24)&0xff) / 255.f;

        // assume orthographic projection with units = screen pixels, origin at top left
        GL::bindTexture(GL::TEXTURE_2D, g_ftex);
        
        const float ox = x;
        
        while (*text)
        {
                int c = (unsigned char)*text;
                if (c == '\t')
                {
                        for (int i = 0; i < 4; ++i)
                        {
                                if (x < g_tabStops[i]+ox)
                                {
                                        x = g_tabStops[i]+ox;
                                        break;
                                }
                        }
                }
                else if (c >= 32 && c < 128)
                {                       
                        stbtt_aligned_quad q;
                        getBakedQuad(g_cdata, 512,512, c-32, &x,&y,&q);

                        float v[12] = {
                                        q.x0, q.y0, 
                                        q.x1, q.y1,
                                        q.x1, q.y0, 
                                        q.x0, q.y0,
                                        q.x0, q.y1, 
                                        q.x1, q.y1, 
                                      };
                        float uv[12] = {
                                        q.s0, q.t0,
                                        q.s1, q.t1,
                                        q.s1, q.t0,
                                        q.s0, q.t0, 
                                        q.s0, q.t1, 
                                        q.s1, q.t1, 
                                      };
                        float c[24] = {
                                        r, g, b, a,
                                        r, g, b, a,
                                        r, g, b, a,
                                        r, g, b, a,
                                        r, g, b, a,
                                        r, g, b, a,
                                      };
                        bindVertexArray();
                        GL::bindBuffer(GL::ARRAY_BUFFER, g_vbos[0]);
                        GL::bufferData(GL::ARRAY_BUFFER, 12*sizeof(float), v, GL::STATIC_DRAW);
                        GL::bindBuffer(GL::ARRAY_BUFFER, g_vbos[1]);
                        GL::bufferData(GL::ARRAY_BUFFER, 12*sizeof(float), uv, GL::STATIC_DRAW);
                        GL::bindBuffer(GL::ARRAY_BUFFER, g_vbos[2]);
                        GL::bufferData(GL::ARRAY_BUFFER, 24*sizeof(float), c, GL::STATIC_DRAW);
                        GL::drawArrays(GL::TRIANGLES, 0, 6);

                }
                ++text;
        }
        
        //glEnd();        
        //glDisable(GL::TEXTURE_2D);
}


void imguiRenderGLDraw(int width, int height)
{
        const imguiGfxCmd* q = imguiGetRenderQueue();
        int nq = imguiGetRenderQueueSize();

        const float s = 1.0f/8.0f;

        GL::viewport(0, 0, width, height);
        GL::useProgram(g_program);
	GL::activeTexture(GL::TEXTURE0);
        GL::uniform2f(g_programViewportLocation, (float) width, (float) height);
        GL::uniform1i(g_programTextureLocation, 0);


        GL::disable(GL::SCISSOR_TEST);
        for (int i = 0; i < nq; ++i)
        {
                const imguiGfxCmd& cmd = q[i];
                if (cmd.type == IMGUI_GFXCMD_RECT)
                {
                        if (cmd.rect.r == 0)
                        {
                                drawRect((float)cmd.rect.x*s+0.5f, (float)cmd.rect.y*s+0.5f,
                                                 (float)cmd.rect.w*s-1, (float)cmd.rect.h*s-1,
                                                 1.0f, cmd.col);
                        }
                        else
                        {
                                drawRoundedRect((float)cmd.rect.x*s+0.5f, (float)cmd.rect.y*s+0.5f,
                                                                (float)cmd.rect.w*s-1, (float)cmd.rect.h*s-1,
                                                                (float)cmd.rect.r*s, 1.0f, cmd.col);
                        }
                }
                else if (cmd.type == IMGUI_GFXCMD_LINE)
                {
                        drawLine(cmd.line.x0*s, cmd.line.y0*s, cmd.line.x1*s, cmd.line.y1*s, cmd.line.r*s, 1.0f, cmd.col);
                }
                else if (cmd.type == IMGUI_GFXCMD_TRIANGLE)
                {
                        if (cmd.flags == 1)
                        {
                                const float verts[3*2] =
                                {
                                        (float)cmd.rect.x*s+0.5f, (float)cmd.rect.y*s+0.5f,
                                        (float)cmd.rect.x*s+0.5f+(float)cmd.rect.w*s-1, (float)cmd.rect.y*s+0.5f+(float)cmd.rect.h*s/2-0.5f,
                                        (float)cmd.rect.x*s+0.5f, (float)cmd.rect.y*s+0.5f+(float)cmd.rect.h*s-1,
                                };
                                drawPolygon(verts, 3, 1.0f, cmd.col);
                        }
                        if (cmd.flags == 2)
                        {
                                const float verts[3*2] =
                                {
                                        (float)cmd.rect.x*s+0.5f, (float)cmd.rect.y*s+0.5f+(float)cmd.rect.h*s-1,
                                        (float)cmd.rect.x*s+0.5f+(float)cmd.rect.w*s/2-0.5f, (float)cmd.rect.y*s+0.5f,
                                        (float)cmd.rect.x*s+0.5f+(float)cmd.rect.w*s-1, (float)cmd.rect.y*s+0.5f+(float)cmd.rect.h*s-1,
                                };
                                drawPolygon(verts, 3, 1.0f, cmd.col);
                        }
                }
                else if (cmd.type == IMGUI_GFXCMD_TEXT)
                {
                        drawText(cmd.text.x, cmd.text.y, cmd.text.text, cmd.text.align, cmd.col);
                }
                else if (cmd.type == IMGUI_GFXCMD_SCISSOR)
                {
                        if (cmd.flags)
                        {
                                GL::enable(GL::SCISSOR_TEST);
                                GL::scissor(cmd.rect.x, cmd.rect.y, cmd.rect.w, cmd.rect.h);
                        }
                        else
                        {
                                GL::disable(GL::SCISSOR_TEST);
                        }
                }
        }
        GL::disable(GL::SCISSOR_TEST);
}
