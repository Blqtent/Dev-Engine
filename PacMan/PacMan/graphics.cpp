#pragma once
#include <SDL2/SDL.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_sdlrenderer2.h>
#include <imgui.h>
#include <math.h>
#include "common.h"
 //*********************************************************************************************************************
// 									 general constants/external variables
//*********************************************************************************************************************
extern
const int res_X;
extern
const int res_Y;
extern
const int grad_length;
extern char char_buff[]; //screen character buffer
extern char nchar_buff[]; //screen character number buffer
extern double depth_map[]; //depth map
extern char color_buff[]; //screen color buffer

int pal2[32][3] = //rgb definitions of console colors (extended to 32 entries)
    {
        {
            0,
            0,
            0
        },
        {
            0,
            55,
            218
        },
        {
            19,
            161,
            14
        },
        {
            58,
            150,
            221
        },
        {
            197,
            15,
            31
        },
        {
            136,
            23,
            152
        },
        {
            193,
            156,
            0
        },
        {
            150,
            150,
            150
        }, //original: 204,204,204
        {
            118,
            118,
            118
        },
        {
            59,
            120,
            255
        },
        {
            22,
            198,
            12
        },
        {
            97,
            214,
            214
        },
        {
            231,
            72,
            86
        },
        {
            180,
            0,
            158
        },
        {
            249,
            241,
            165
        },
        {
            242,
            242,
            242
        }, //standard palette ends here
        {
            94,
            43,
            15
        }, //brown
        {
            255,
            111,
            0
        }, //orange
        {
            64,
            64,
            85
        }, //dark blue-gray
        {
            0,
            0,
            0
        },
        {
            0,
            0,
            0
        },
        {
            0,
            0,
            0
        },
        {
            0,
            0,
            0
        },
        {
            0,
            0,
            0
        },
        {
            0,
            0,
            0
        },
        {
            0,
            0,
            0
        },
        {
            0,
            0,
            0
        },
        {
            0,
            0,
            0
        },
        {
            0,
            0,
            0
        },
        {
            0,
            0,
            0
        },
        {
            0,
            0,
            0
        },
        {
            0,
            0,
            0
        }
    };

//*********************************************************************************************************************
// 									 SDL graphics constants
//*********************************************************************************************************************
const int upscale = 1; //font upscaling factor
const int font_inX = 8;
const int font_inY = 8; //input font size
const int font_marginX = 0; //horizontal margins for more vert. characters
const int font_outX = 8;
const int font_outY = 8; //output size

const int P_Res_X = res_X * font_outX * upscale;
const int P_Res_Y = res_Y * font_outY * upscale; //pixel resolution

SDL_Window * screen; //screen buffer
SDL_Renderer * renderer; //renderer context
SDL_Texture * texture1; //font texture

//----------------------------------------------------------------------Initialization functions

void initImGui() {
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForSDLRenderer(screen, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
}

void initwindow() {
    screen = SDL_CreateWindow("My Game Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, P_Res_X, P_Res_Y, SDL_WINDOW_OPENGL);
    renderer = SDL_CreateRenderer(screen, -1, 0);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear"); // make the scaled rendering look smoother.
    SDL_RenderSetLogicalSize(renderer, P_Res_X, P_Res_Y);
    SDL_Surface * fontimg = SDL_LoadBMP("cga8.bmp");

    Uint32 colorkey = SDL_MapRGB(fontimg -> format, 0, 0, 0);
    SDL_SetColorKey(fontimg, SDL_TRUE, colorkey);

    texture1 = SDL_CreateTextureFromSurface(renderer, fontimg);
    SDL_SetTextureBlendMode(texture1, SDL_BLENDMODE_ADD); //SDL_BLENDMODE_ADD, _BLEND, _NONE

    SDL_free(fontimg);
}

//----------------------------------------------------------------General-purpose graphics functions
Uint32 getpixel(SDL_Surface * surface, int x, int y) {
    int bpp = surface -> format -> BytesPerPixel;
    /* Here p is the address to the pixel we want to retrieve */
    Uint8 * p = (Uint8 * ) surface -> pixels + y * surface -> pitch + x * bpp;

    switch (bpp) {
    case 1:
        return * p;
        break;

    case 2:
        return * (Uint16 * ) p;
        break;

    case 3:
        if (SDL_BYTEORDER == SDL_BIG_ENDIAN)
            return p[0] << 16 | p[1] << 8 | p[2];
        else
            return p[0] | p[1] << 8 | p[2] << 16;
        break;

    case 4:
        return * (Uint32 * ) p;
        break;

    default:
        return 0; /* shouldn't happen, but avoids warnings */
    }
}

//-----------------------------------------------------------------------------Text graphics functions
void settcolor(int r, int g, int b) //set text color
{
    SDL_SetTextureColorMod(texture1, r, g, b);
}

void drawchar(int pos, unsigned char character) //draw character at given position
{
    int offx = (character + 256) % 32; //+256=bold
    int offy = (character + 256) / 32;

    SDL_Rect srcrect;
    SDL_Rect dstrect;

    srcrect.x = offx * font_inX;
    srcrect.y = offy * font_inX;
    srcrect.w = font_inX - font_marginX;
    srcrect.h = font_inY;
    dstrect.x = (pos % res_X) * P_Res_X / res_X;
    dstrect.y = (pos / res_X) * P_Res_Y / res_Y;
    dstrect.w = font_outX * upscale;
    dstrect.h = font_outY * upscale;
    SDL_RenderCopy(renderer, texture1, & srcrect, & dstrect);
}

void drawstring(int pos, int col, char * str) //draw a string at given position
{
    int i = 0;
    while (str[i]) {
        char_buff[pos + i] = str[i];
        color_buff[pos + i] = col;
        i++;
    }
}
//---------------------------------------------------------Main display function
void display(int disp_mode) {
    int r, g, b;
    double brightness;

    SDL_RenderClear(renderer);
    if (disp_mode == 0) //standard ASCII display
    {
        int lastcol = 0; //last used color
        for (int i = 0; i < res_X * res_Y; i++) {
            if (color_buff[i] != lastcol) //new color is needed?
            {
                lastcol = color_buff[i]; //read the new color
                r = pal2[lastcol][0]; //get r,g,b from palette
                g = pal2[lastcol][1];
                b = pal2[lastcol][2];
                settcolor(r, g, b); //set color
            }
            drawchar(i, char_buff[i]); //draw character
        }
    }

    if (disp_mode == 1) //full blocks
    {
        int lastcol;
        for (int i = 0; i < res_X * res_Y; i++) {
            lastcol = color_buff[i];
            r = pal2[lastcol][0];
            g = pal2[lastcol][1];
            b = pal2[lastcol][2];
            brightness = 1.0 * sqrt(1.0 * nchar_buff[i] / grad_length);
            if (settings::lighting) {
                r = (int)(1.0 * brightness * r);
                g = (int)(1.0 * brightness * g);
                b = (int)(1.0 * brightness * b);
            }
            settcolor(r, g, b);
            drawchar(i, (char) 219); //char 219=full block
        }
    }

    if (disp_mode == 2) //depth buffer
    {
        int lastcol;
        for (int i = 0; i < res_X * res_Y; i++) {
            lastcol = color_buff[i];
            r = 255;
            g = 255;
            b = 255;
            brightness = 1.0 / (1 + 0.5 * depth_map[i]);

            r = (int)(1.0 * brightness * r);
            g = (int)(1.0 * brightness * g);
            b = (int)(1.0 * brightness * b);
            settcolor(r, g, b);
            drawchar(i, (char) 219);
        }
    }

    SDL_RenderPresent(renderer);
}

void cleanup() {
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyTexture(texture1);
    SDL_Quit();
}