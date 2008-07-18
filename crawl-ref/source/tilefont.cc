/*
 *  File:       tilefont.cc
 *  Created by: ennewalker on Sat Apr 26 01:33:53 2008 UTC
 *
 *  Modified for Crawl Reference by $Author: ennewalker $ on $Date: 2008-03-07 $
 */

#include "tilefont.h"
#include "defines.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <ft2build.h>
#include FT_FREETYPE_H

const unsigned char term_colours[MAX_TERM_COLOUR][3] =
{
    {  0,   0,   0}, // BLACK
    {  0,  82, 255}, // BLUE
    {100, 185,  70}, // GREEN
    {  0, 180, 180}, // CYAN
    {255,  48,   0}, // RED
    {238,  92, 238}, // MAGENTA
    {165,  91,   0}, // BROWN
    {162, 162, 162}, // LIGHTGREY
    { 82,  82,  82}, // DARKGREY
    { 82, 102, 255}, // LIGHTBLUE
    { 82, 255,  82}, // LIGHTGREEN
    { 82, 255, 255}, // LIGHTCYAN
    {255,  82,  82}, // LIGHTRED
    {255,  82, 255}, // LIGHTMAGENTA
    {255, 255,  82}, // YELLOW
    {255, 255, 255}  // WHITE
};

FTFont::FTFont() :
    m_glyphs(NULL),
    m_max_advance(0, 0),
    m_min_offset(0)
{
}

FTFont::~FTFont()
{
    delete[] m_glyphs;
}

bool FTFont::load_font(const char *font_name, unsigned int font_size)
{
    FT_Library library;
    FT_Face face;
    FT_Error error;

    error = FT_Init_FreeType(&library);
    if (error)
    {
        fprintf(stderr, "Failed to initialize freetype library.\n");
        return false;
    }
    
    error = FT_New_Face(library, font_name, 0, &face);
    if (error == FT_Err_Unknown_File_Format)
    {
        fprintf(stderr, "Unknown font format for file '%s'\n", font_name);
        return false;
    }
    else if (error)
    {
        fprintf(stderr, "Invalid font from file '%s'\n", font_name);
    }

    error = FT_Set_Pixel_Sizes(face, font_size, font_size);
    ASSERT(!error);

    // Get maximum advance
    m_max_advance = coord_def(0,0);
    int ascender = face->ascender >> 6;
    int min_y = 100000; // TODO enne - fix me
    int max_y = 0;
    m_min_offset = 0;
    m_glyphs = new GlyphInfo[256];
    for (unsigned int c = 0; c < 256; c++)
    {
        m_glyphs[c].offset = 0;
        m_glyphs[c].advance = 0;
        m_glyphs[c].renderable = false;

        FT_Int glyph_index = FT_Get_Char_Index(face, c);
        if (!glyph_index)
        {
            continue;
        }

        error = FT_Load_Glyph(face, glyph_index, FT_LOAD_RENDER);
        ASSERT(!error);

        FT_Bitmap *bmp = &face->glyph->bitmap;

        int advance = face->glyph->advance.x >> 6;

        m_max_advance.x = std::max(m_max_advance.x, advance);

        min_y = std::min(min_y, ascender - face->glyph->bitmap_top);
        max_y = std::max(max_y, ascender + bmp->rows - face->glyph->bitmap_top);

        m_glyphs[c].offset = face->glyph->bitmap_left;
        m_glyphs[c].advance = advance;

        m_min_offset = std::min(m_min_offset, m_glyphs[c].offset);
    }

    // TEMP enne - this seems to be broken on OSX - it returns 8, when it should be 2?
    m_max_advance.x = 10;

    // The ascender and text height given by FreeType2 is ridiculously large
    // (e.g. 37 pixels high for 14 pixel font).  Use min and max bounding
    // heights on the characters we care about to get better values for the
    // text height and the ascender.
    m_max_advance.y = max_y - min_y;
    ascender -= min_y;

    // Grow character size to power of 2
    coord_def charsz(1,1);
    while (charsz.x < m_max_advance.x)
        charsz.x *= 2;
    while (charsz.y < m_max_advance.y)
        charsz.y *= 2;

    // Fill out texture to be (16*charsz.x) X (16*charsz.y) X (32-bit)
    // Having to blow out 8-bit alpha values into full 32-bit textures is
    // kind of frustrating, but not all OpenGL implementations support the
    // "esoteric" ALPHA8 format and it's not like this texture is very large.
    unsigned int width = 16 * charsz.x;
    unsigned int height = 16 * charsz.y;
    unsigned char *pixels = new unsigned char[4 * width * height];
    memset(pixels, 0, sizeof(unsigned char) * 4 * width * height);

    for (unsigned int c = 0; c < 256; c++)
    {
        FT_Int glyph_index = FT_Get_Char_Index(face, c);
        if (!glyph_index)
        {
            // If no mapping for this character, leave blank.
            continue;
        }

        error = FT_Load_Glyph(face, glyph_index, FT_LOAD_RENDER);
        ASSERT(!error);

        FT_Bitmap *bmp = &face->glyph->bitmap;
        ASSERT(bmp);
        // Some glyphs (e.g. " ") don't get a buffer.
        if (!bmp->buffer)
            continue;

        m_glyphs[c].renderable = true;

        int vert_offset = ascender - face->glyph->bitmap_top;

        ASSERT(bmp->pixel_mode == FT_PIXEL_MODE_GRAY);
        ASSERT(bmp->num_grays == 256);
        ASSERT(bmp->width + face->glyph->bitmap_left <= m_max_advance.x);
        ASSERT(bmp->rows <= m_max_advance.y);
        ASSERT(vert_offset >= 0);
        ASSERT(vert_offset + bmp->rows <= m_max_advance.y);

        // Horizontal offset stored in m_glyphs and handled when drawing
        unsigned int offset_x = (c % 16) * charsz.x;
        unsigned int offset_y = (c / 16) * charsz.y + vert_offset;

        for (int x = 0; x < bmp->width; x++)
            for (int y = 0; y < bmp->rows; y++)
            {
                unsigned int idx = offset_x + x + (offset_y + y) * width;
                idx *= 4;
                unsigned char alpha = bmp->buffer[x + bmp->width * y];
                pixels[idx] = 255;
                pixels[idx + 1] = 255;
                pixels[idx + 2] = 255;
                pixels[idx + 3] = alpha;
            }    
    }

    bool success = m_tex.load_texture(pixels, width, height,
                                      GenericTexture::MIPMAP_NONE);

    delete[] pixels;

    return success;
}

struct FontVertLayout
{
    float pos_x;
    float pos_y;
    float tex_x;
    float tex_y;
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

void FTFont::render_textblock(unsigned int x_pos, unsigned int y_pos,
                              unsigned char *chars, unsigned char *colours,
                              unsigned int width, unsigned int height,
                              bool drop_shadow)
{
    if (!chars || !colours || !width || !height || !m_glyphs)
        return;

    coord_def adv(std::max(-m_min_offset, 0), 0);
    unsigned int i = 0;

    std::vector<FontVertLayout> verts;
    // TODO enne - make this better
    // This is bad for the CRT.  Maybe we should just reserve some fixed limit?
    // Maybe we should just cache this in FTFont?
    verts.reserve(4 * width * height);

    float texcoord_dy = (float)m_max_advance.y / (float)m_tex.height();
   
    for (unsigned int y = 0; y < height; y++)
    {
        for (unsigned int x = 0; x < width; x++)
        {
            unsigned char c = chars[i];
            unsigned int this_adv = m_glyphs[c].advance + 1;
            adv.x += m_glyphs[c].offset;

            if (m_glyphs[c].renderable)
            {
                unsigned char col = colours[i];
                float tex_x = (float)(c % 16) / 16.0f;
                float tex_y = (float)(c / 16) / 16.0f;
                float tex_x2 = tex_x + (float)this_adv / (float)m_tex.width();
                float tex_y2 = tex_y + texcoord_dy;
                FontVertLayout v;

                v.pos_x = adv.x;
                v.pos_y = adv.y;
                v.tex_x = tex_x;
                v.tex_y = tex_y;
                v.r = term_colours[col][0];
                v.g = term_colours[col][1];
                v.b = term_colours[col][2];
                v.a = 255;
                verts.push_back(v);

                v.pos_x = adv.x;
                v.pos_y = adv.y + m_max_advance.y;
                v.tex_x = tex_x;
                v.tex_y = tex_y2;
                v.r = term_colours[col][0];
                v.g = term_colours[col][1];
                v.b = term_colours[col][2];
                v.a = 255;
                verts.push_back(v);

                v.pos_x = adv.x + this_adv;
                v.pos_y = adv.y + m_max_advance.y;
                v.tex_x = tex_x2;
                v.tex_y = tex_y2;
                v.r = term_colours[col][0];
                v.g = term_colours[col][1];
                v.b = term_colours[col][2];
                v.a = 255;
                verts.push_back(v);

                v.pos_x = adv.x + this_adv;
                v.pos_y = adv.y;
                v.tex_x = tex_x2;
                v.tex_y = tex_y;
                v.r = term_colours[col][0];
                v.g = term_colours[col][1];
                v.b = term_colours[col][2];
                v.a = 255;
                verts.push_back(v);
            }

            i++;
            adv.x += this_adv - m_glyphs[c].offset - 1;
        }

        adv.x = 0;
        adv.y += m_max_advance.y;
    }

    if (!verts.size())
        return;

    GLState state;
    state.array_vertex = true;
    state.array_texcoord = true;
    state.blend = true;
    state.texture = true;
    GLStateManager::set(state);

    m_tex.bind();
    glVertexPointer(2, GL_FLOAT, sizeof(FontVertLayout), &verts[0].pos_x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(FontVertLayout), &verts[0].tex_x);

    if (drop_shadow)
    {
        glColor3f(0.0f, 0.0f, 0.0f);

        glLoadIdentity();
        glTranslatef(x_pos + 1, y_pos + 1, 0.0f);
        glDrawArrays(GL_QUADS, 0, verts.size());

        glColor3f(1.0f, 1.0f, 1.0f);
    }

    glLoadIdentity();
    glTranslatef(x_pos, y_pos, 0.0f);

    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(FontVertLayout), &verts[0].r);
    glDrawArrays(GL_QUADS, 0, verts.size());
    glDisableClientState(GL_COLOR_ARRAY);
}

struct box_vert
{
    float x;
    float y;
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

static void _draw_box(int x_pos, int y_pos, float width, float height,
                      float box_width, unsigned char box_colour,
                      unsigned char box_alpha)
{
    box_vert verts[4];
    for (unsigned int i = 0; i < 4; i++)
    {
        verts[i].r = term_colours[box_colour][0];
        verts[i].g = term_colours[box_colour][1];
        verts[i].b = term_colours[box_colour][2];
        verts[i].a = box_alpha;
    }
    verts[0].x = x_pos - box_width;
    verts[0].y = y_pos - box_width;
    verts[1].x = verts[0].x;
    verts[1].y = y_pos + height + box_width;
    verts[2].x = x_pos + width + box_width;
    verts[2].y = verts[1].y;
    verts[3].x = verts[2].x;
    verts[3].y = verts[0].y;

    glLoadIdentity();

    GLState state;
    state.array_vertex = true;
    state.array_colour = true;
    state.blend = true;
    GLStateManager::set(state);

    glVertexPointer(2, GL_FLOAT, sizeof(box_vert), &verts[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(box_vert), &verts[0].r);
    glDrawArrays(GL_QUADS, 0, sizeof(verts) / sizeof(box_vert)); 

}

void FTFont::render_string(unsigned int px, unsigned int py,
                           const char *text,
                           const coord_def &min_pos, const coord_def &max_pos,
                           unsigned char font_colour, bool drop_shadow,
                           unsigned char box_alpha,
                           unsigned char box_colour,
                           unsigned int outline)
{
    ASSERT(text);

    // Determine extent of this text
    unsigned int max_rows = 1;
    unsigned int cols = 0;
    unsigned int max_cols = 0;
    for (const char *itr = text; *itr; itr++)
    {
        cols++;
        max_cols = std::max(cols, max_cols);

        // NOTE: only newlines should be used for tool tips.  Don't use EOL.
        ASSERT(*itr != '\r');

        if (*itr == '\n')
        {
            cols = 0;
            max_rows++;
        }
    }

    // Create the text block
    unsigned char *chars = (unsigned char *)alloca(max_rows * max_cols);
    unsigned char *colours = (unsigned char *)alloca(max_rows * max_cols);
    memset(chars, ' ', max_rows * max_cols);
    memset(colours, font_colour, max_rows * max_cols);

    // Fill the text block
    cols = 0;
    unsigned int rows = 0;
    for (const char *itr = text; *itr; itr++)
    {
        chars[cols + rows * max_cols] = *itr;
        cols++;

        if (*itr == '\n')
        {
            cols = 0;
            rows++;
        }
    }

    // Find a suitable location on screen
    const int buffer = 5;  // additional buffer size from edges

    int wx = max_cols * char_width();
    int wy = max_rows * char_height();

    // text starting location
    int tx = px - wx / 2;
    int ty = py - wy - outline;

    // box with extra buffer to test against min_pos/max_pos window size
    int sx = tx - buffer;
    int sy = ty - buffer;
    int ex = sx + wx + buffer;
    int ey = sy + wy + buffer;

    if (ex > max_pos.x)
        tx += max_pos.x - ex;
    else if (sx < min_pos.x)
        tx -= sx;

    if (ey > max_pos.y)
        ty += max_pos.y - ey;
    else if (sy < min_pos.y)
        ty -= sy;

    if (box_alpha != 0)
        _draw_box(tx, ty, wx, wy, outline, box_colour, box_alpha);

    render_textblock(tx, ty, chars, colours, max_cols, max_rows, drop_shadow);
}