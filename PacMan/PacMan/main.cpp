#define SDL_MAIN_HANDLED
#include "graphics.h"

#include <SDL2/SDL.h>

#include <cmath>

#include <iostream>

#include <vector>

#include <fstream>

#include <string>

#include <filesystem>

#include <Windows.h>
//*********************************************************************************************************************
// 										Basic constants
//*********************************************************************************************************************
const int res_X = 240; //resolution x
const int res_Y = 120; //resolution y; right-click on the window title, select properties and set the proper size
int fov = 600; //field of view, in 0.1 degree increments (60 degrees)

//*********************************************************************************************************************
// 										Math, lokup tables
//*********************************************************************************************************************
const double torad = M_PI / 180; //degrees to radians conversion factor
const double todeg = 180 / M_PI; //radians to degrees conversion factor
double sintab[3600]; //lookup table of sine values, every 0.1 degree
double fisheye[res_X]; //lookup table for fisheye correction, 1 value for every screen column
double gausstab[8 * res_X]; //lookup table for gaussian function, centered at 0, for flashlight; 8 for margin
//*********************************************************************************************************************
// 										Input\output & system stuff
//*********************************************************************************************************************
const int mouse_speed = 200; //mouse speed division
int fps = 0;
int tt1, tt2; //for calculating fps

int debug[16]; //various flags/values for testing stuff

//controls
const Uint8* keys = SDL_GetKeyboardState(NULL);
int mousex, mousey, mousex0, mousey0;

//*********************************************************************************************************************
// 										Graphics buffers for drawing
//*********************************************************************************************************************
char char_buff[res_X * res_Y]; //screen character buffer
char nchar_buff[res_X * res_Y]; //screen character number buffer (vor alternative display types)
char color_buff[res_X * res_Y]; //screen color buffer
char char_grad[93] = " `.-':_,~=;><*+!rc/z?sLTv)J7(|Fi{C}fI31tlu[neoZ5Yxjya]2ESwqkP6h9d4VpOGbUAKXHm8RD#$Bg0MNWQ%&@"; //character gradient used to denote brightness
const int grad_length = 90; //must be 2 smaller than above
int hmap[res_X]; //map of heights of wall columns
double lmap[res_X]; //map of light/brightness
int nmap[res_X]; //map of wall normals
int tmap[res_X]; //map of texture coordinates of wall columns
int typemap[res_X]; //wall type (texture number)
double wallxmap[res_X]; //x,y grid coordinates of wall column
double wallymap[res_X];
double walldmap[res_X]; //distance to wall slice
double depth_map[res_X * res_Y]; //screen sized depth map to determine when to draw sprites

//*********************************************************************************************************************
// 										Textures, graphics
//*********************************************************************************************************************
int textures[32 * 32 * 64]; //1-D array containing 16 32x32 textures (1024 pixels); 
//first byte is character, second one is color, 3rd is surface normal, 4th unused yet

int sky[res_X * 2 * res_Y]; //sky texture
int sky_color; //what it says :P
int horizon_pos = 0; //position of the horizon, in characters; 0=middle of the screen

int pal[16][3]; //each of the 16 console colors has 3 brightness variants (like red->light red->yellow)
const int pal_thr1 = 30; //threshold for 1st palette switch
const int pal_thr2 = 85; //threshold for 2nd palette switch

int sprites[32 * 32 * 16];
unsigned short int sprites2[32 * 32 * 256];
//first byte is character, second one is color, 3rd is alpha
//*********************************************************************************************************************
// 										World and player state
//*********************************************************************************************************************

//World map
const int map_size = 24; //square map size
int map[map_size][map_size]; //world map

//pathfinding map
int path_map[map_size][map_size];
int numd = 0; //door number
//global time
int g_time;

double map_decors[256][8]; //enabled,x,y,z,sprite #, sprite #2, breakable, collision size
double maprandoms[16]; //random values for map effects

//Structure holding player data
struct {
    double x, y, z; //coordinates
    double vx, vy, vz; //velocities
    double ang_h, ang_v; //horizontal and vertical angle
    double friction = 0.1; //friction coefficient for movement; set to 0.01 and go ice skating :)
    double accel = 0.01; //acceleration coefficient for movement
    double grav = 0.01; //gravity
    double jump_h = 0.2; //jump height (initial velocity)
    double hp = 100; //hitpoints
    double stamina = 100; //running stamina
    double battery = 1; //flashlight battery
    double score = 0; //score (points)
    int status[4]; //various status effects;
    //status[0]=player hurt
    //status[1]=invulnerability
    //status[2]=walk cycle
}
player;

//Structure holding enemy data
struct {
    int enabled; //flag if enemy is on the map
    int type; //sprite number
    double x, y, z; //coordinates
    double vx, vy, vz; //velocities
    double friction = 0.1; //friction coefficient for movement; 
    double accel = 0.01; //acceleration coefficient for movement
    double grav = 0.01; //gravity	
}
enemies[16];

//Global flags
int F_exit = 0; //turns to 1 when player presses esc.
double key_delay; //for toggle on/off keys, to avoid toggling things 100 times per second

// Projectiles
double projectiles[64][8];//x,y,vx,vy,type,dmg,associated light
int num_projectile;

// Doors
int mapanims[64][2]; //map animation data (like doors): type, frame
double player_anim[16]; //various player animations

//Graphics effects
double bbuff[res_X * res_Y]; //additional brightness buffer that does the final brightness correction, semi-persistent
double visiondata[8]; //carious variables related to player vision: total screen brightness, exposure

//*********************************************************************************************************************
// 										Light
//*********************************************************************************************************************
double light_global; //global brightness
double light_faloff; //distance brightness faloff 
double light_aperture; //eye/camera aperture; dynamically reacts to scene brightness
double light_flashlight; //flashlight type source
double flashlight_coeff[res_X * res_Y]; //pre-computed brightness map (faloff from screen center) 
double sky_light; //amount of light from open sky

double lightmap[map_size * 16][map_size * 16]; //brightness map, every square divided into 16x16 sub-squares
double light_tmp[map_size * 16][map_size * 16]; //secondary helper buffer for lightmap
double static_lights[64][4]; //64 lights; x,y,strength,height; for calculating lightmap

//*********************************************************************************************************************
// 										Game State
//*********************************************************************************************************************

int state = 0; // 0 = menu; 1 = game;

//*********************************************************************************************************************
// 										Various helper functions
//*********************************************************************************************************************

//checks if there is a straight line connection between 2 points; useful for casting light rays
int checkray(double x1, double y1, double x2, double y2, int steps) {
    double dx = (x2 - x1) / (1.0 * steps);
    double dy = (y2 - y1) / (1.0 * steps);
    int k = 1;
    int mcx, mcy;

    for (int i = 0; i < steps; i++) {
        x1 = x1 + dx;
        y1 = y1 + dy;
        mcx = (int)x1;
        mcy = (int)y1;
        if ((mcx > 0) && (mcy > 0) && (mcx < map_size) && (mcy < map_size))
            if ((map[mcx][mcy] % 256) > 0) {
                k = 0;
                break;
            }
    }
    return k;
}

//*********************************************************************************************************************

void clear_buffers() {
    for (int x = 0; x < res_X; x++)
        for (int y = 0; y < res_Y; y++) {
            char_buff[x + y * res_X] = ' ';
            color_buff[x + y * res_X] = 0;
        }
}

//*********************************************************************************************************************

void show_map() //just show the map on the screen, good for debugging map generator
{
    for (int x = 0; x < map_size; x++)
        for (int y = 0; y < map_size; y++) {
            char_buff[x + y * res_X] = '#';
            color_buff[x + y * res_X] = 4 * ((map[x][y] % 256) > 0);
        }
}

//**********************************************************************************************************************
void map_row(const char* str, int row) //reads a string and generates a map row from it; 'a'=wall type 1, 'b' = 2 etc.
{
    int i = 0;
    char c;
    while (str[i]) {
        c = str[i];
        if (c == ' ') map[i][row] = 0 + 1 * 256 + 3 * 65536;
        else if (c == 'a') map[i][row] = 1 + 1 * 256 + 3 * 65536;
        else if (c == 'b') map[i][row] = 2 + 1 * 256 + 3 * 65536;
        else if (c == '1') { map[i][row] = 200 + 256 * 1 + 65536 * 1 + numd * 16777216; mapanims[numd][0] = 1; numd++; }
        else if (c == '2') { map[i][row] = 201 + 256 * 1 + 65536 * 1 + numd * 16777216; mapanims[numd][0] = 1; numd++; }
        else if (c == '8') { map[i][row] = 202 + 256 * 1 + 65536 * 1 + numd * 16777216; mapanims[numd][0] = 1; numd++; }
        else if (c == '_') map[i][row] = 0 + 4 * 256;
        i++;
    };
}

//*********************************************************************************************************************
// 										Procedural texture generation
//*********************************************************************************************************************

void gen_texture(int number, int type, int p1, int p2, int p3, int p4, int p5, int p6) //generate texture at given number; parameters p1-p4 depend on type 
{

    if (type == 0) //brick texture: p1=brick brightness, p2=mortar brightness, p3=brick height, p4=brick row offset, p5=brick color, p6=mortar color
    {
        for (int x = 0; x < 32; x++) //texture generation
            for (int y = 0; y < 32; y++) {
                textures[x + y * 32 + 1024 * number] = (p1 - p2 * ((y % p3 == 0) || ((x + p4 * (y / p3)) % 16 == 0)) + rand() % 2) + (p5 + (p6 - p5) * ((y % p3 == 0) || ((x + p4 * (y / p3)) % 16 == 0))) * 256;
                textures[x + y * 32 + 1024 * number] += 65536 * (128 + 64 * ((y % p3 == 0) || ((x + p4 * (y / p3) - 1) % 16 == 0)) - 64 * ((y % p3 == 0) || ((x + p4 * (y / p3) + 1) % 16 == 0)) + rand() % 32 - 16); //surface normal - bricks+some roughness
            }
    }

    if (type == 1) //large, monocolored bricks/plates; p1=brick brightness, p2=mortar brightness, p3=color
    {
        for (int x = 0; x < 32; x++) //texture generation
            for (int y = 0; y < 32; y++) {
                textures[x + y * 32 + 1024 * number] = p1 - p2 * ((y % 31 == 0) || ((x + 4 * (y / 31)) % 16 == 0)) + rand() % 2 + p3 * 256;
                textures[x + y * 32 + 1024 * number] += 65536 * (128 + rand() % 32 - 16); //surface normal - random roughness
            }
    }
    if (type == 2) //large, monocolored bricks/plates; p1=brick brightness, p2=mortar brightness, p3=color
    {
        for (int x = 0; x < 32; x++) //texture generation
            for (int y = 0; y < 32; y++) {
                int ins = ((x > 4) && (x < 27) && (y > 5) && (y < 26)); //inside square

                textures[x + y * 32 + 1024 * number] = (12 + rand() % 5) + (7 + ins - 2 * ((x > 5) && (x < 8) && (y == 16))) * 256; //Door
                textures[x + y * 32 + 1024 * number] += 65536 * (128 - 96 * ins * (x == 5) + 96 * ins * (x == 26)); //normal map
                //textures[x + y * 32 + 1024 * number] = x ^ y;
                //textures[x + y * 32 + 1024 * number] += 65536 * (128 + rand() % 32 - 16); //surface normal - random roughness
            }
    }

}

//*********************************************************************************************************************
// 										Procedural sprite generation
//*********************************************************************************************************************
void gen_pacman_ghost(int number, int brightness, int color) {
    for (int x = 0; x < 32; x++)
        for (int y = 0; y < 32; y++) {
            double dist = (x - 16) * (x - 16) + (y - 16) * (y - 16);
            if ((dist < 225) || ((y > 16) && (x > 1) && (x < 31) && (y < 32 - x % 4))) sprites[x + y * 32 + 1024 * number] = brightness + 256 * color + 65536;
            dist = (x - 11) * (x - 11) + (y - 12) * (y - 12);
            if (dist < 16) sprites[x + y * 32 + 1024 * number] = 4 * brightness + 256 * 15 + 65536;
            if (dist < 2) sprites[x + y * 32 + 1024 * number] = 0 * brightness + 256 * 0 + 65536;
            dist = (x - 21) * (x - 21) + (y - 12) * (y - 12);
            if (dist < 16) sprites[x + y * 32 + 1024 * number] = 4 * brightness + 256 * 15 + 65536;
            if (dist < 2) sprites[x + y * 32 + 1024 * number] = 0 * brightness + 256 * 0 + 65536;
        }
}



//*********************************************************************************************************************
// 										Procedural sky
//*********************************************************************************************************************
void gen_sky(int brightness) {
    int sky_randoms[res_X * 2 * res_Y]; //helper buffer

    for (int x = 0; x < res_X * 2; x++)
        for (int y = 0; y < res_Y; y++)
            sky_randoms[x + y * 2 * res_X] = rand() % brightness; //generate map of random values

    for (int iter = 1; iter < 8; iter++) //7 iterations of Perlin noise	
        for (int x = 0; x < res_X * 2; x++)
            for (int y = 0; y < res_Y; y++) {
                sky[x + y * 2 * res_X] += sky_randoms[x / iter + y / iter * 2 * res_X];
            }
}

//*********************************************************************************************************************
// 										Pac-man map
//*********************************************************************************************************************

std::vector < std::string > loadPacMap(const std::string& filename) {
    std::vector < std::string > mapData;
    std::ifstream file(filename);

    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (line[0] == '#') continue;
            mapData.push_back(line);
        }

        file.close();
    }
    else {
        std::cerr << "Unable to open file: " << filename << std::endl;
    }

    return mapData;
}

void loadMap(std::string path) {
    std::vector < std::string > map = loadPacMap(path);
    int x = 0;
    for (const std::string s : map) {
        map_row(s.c_str(), x);
        x++;
    }
}

void listFilesWithExtension(const std::string& path,
    const std::string& extension) {
    try {
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            if (std::filesystem::is_regular_file(entry.path()) && entry.path().extension() == extension) {
                std::cout << entry.path().filename() << std::endl;
            }
        }
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error accessing the directory: " << e.what() << std::endl;
    }
}

void gen_map_pacman(std::string path) {
    //some global settings
    //Light settings
    light_global = 0.05;
    light_faloff = 1.0;
    light_flashlight = 1;
    sky_light = 40;
    sky_color = 1;

    //Texture generation - one could use randomized parameters for more variety
    gen_texture(0, 0, 12, 8, 6, 4, 1, 8); //blue brick
    gen_texture(1, 0, 12, 8, 6, 4, 7, 8); //gray brick
    gen_texture(2, 1, 8, 4, 8, 0, 0, 0); //gray plates
    gen_texture(3, 1, 8, 6, 5, 0, 0, 0); //magenta plates
    gen_texture(4, 1, 12, 0, 2, 0, 0, 0); //green grass
    gen_texture(63, 2, 12, 0, 2, 0, 0, 0); //green grass

    for (int x = 0; x < map_size; x++)
        for (int y = 0; y < map_size; y++)
            map[x][y] = 0 + 256 * 1; //clear map
    /*
    map_row("aaaaaaaaaaaaaaaaaaaaaaa",0);
    map_row("aaaaaaaaaaaaaaaaaaaaaaa",1);
    map_row("aa        aaa        aa",2);
    map_row("aa aa aaa aaa aaa aa aa",3);
    map_row("aa                   aa",4);
    map_row("aa aa a aaaaaaa a aa aa",5);
    map_row("aa    a    a    a    aa",6);
    map_row("aaaaa aaa  a  aaa aaaaa",7);
    map_row("aa aa a         a aa aa",8);
    map_row("aa aa a bbb bbb a aa aa",9);
    map_row("aa      b_____b      aa",10);
    map_row("aa aa a bbbbbbb a aa aa",11);
    map_row("aa aa a         a aa aa",12);
    map_row("aaaaa a aaaaaaa a aaaaa",13);
    map_row("aa         a         aa",14);
    map_row("aa aa aaa  a  aaa aa aa",15);
    map_row("aa  a             a  aa",16);
    map_row("aaa a a aaaaaaa a a aaa",17);
    map_row("aa    a    a    a    aa",18);
    map_row("aa aaaaaa  a  aaaaaa aa",19);
    map_row("aa                   aa",20);
    map_row("aaaaaaaaaaaaaaaaaaaaaaa",21);
    map_row("aaaaaaaaaaaaaaaaaaaaaaa",22);
    */
    if (path != "D")
        loadMap("maps/" + path + ".pac");
    else
        loadMap("maps/default.pac");

    player.x = 9.5;
    player.y = 20.5;

    static_lights[0][0] = 2.5; //left-down corner
    static_lights[0][1] = 20.5;
    static_lights[0][2] = 40;

    static_lights[1][0] = 20.5; //right-down corner
    static_lights[1][1] = 20.5;
    static_lights[1][2] = 40;

    static_lights[2][0] = 2.5; //left-up corner
    static_lights[2][1] = 2.5;
    static_lights[2][2] = 40;

    static_lights[3][0] = 20.5; //right-up corner
    static_lights[3][1] = 2.5;
    static_lights[3][2] = 40;

    gen_pacman_ghost(0, 30, 4); //Blinky: sprite #1, brightness 30, color 4 (red)
    enemies[0].x = 8.5;
    enemies[0].y = 16.5;
    enemies[0].enabled = 1;
    enemies[0].type = 0;
    enemies[0].z = 32;

    gen_pacman_ghost(1, 30, 5); //Pinky: sprite #2, brightness 30, color 5 (magenta)
    enemies[1].x = 9.5;
    enemies[1].y = 16.5;
    enemies[1].enabled = 1;
    enemies[1].type = 1;
    enemies[1].z = 32;

    gen_pacman_ghost(2, 30, 2); //Inky: sprite #2, brightness 30, color 2 (cyan)
    enemies[2].x = 10.5;
    enemies[2].y = 16.5;
    enemies[2].enabled = 1;
    enemies[2].type = 2;
    enemies[2].z = 32;

    gen_pacman_ghost(3, 30, 6); //Clyde: sprite #3, brightness 30, color 6 (yellow)
    enemies[3].x = 11.5;
    enemies[3].y = 16.5;
    enemies[3].enabled = 1;
    enemies[3].type = 3;
    enemies[3].z = 32;
}



//*********************************************************************************************************************
// 										Initialization - Loading Files
//*********************************************************************************************************************

void loadsprites()
{
    Uint32 cc;
    int r, g, b;
    int brt, bco, dist, mindist;
    SDL_Surface* sprsheet = SDL_LoadBMP("sprites.bmp");

    //load enemy sprites (32x32)
    for (int frame = 0; frame < 64; frame++)
        for (int x2 = 0; x2 < 32; x2++)
            for (int y2 = 0; y2 < 32; y2++)
            {
                cc = getpixel(sprsheet, x2 + 1 + 33 * (frame % 8), y2 + 1 + 33 * (frame / 8));

                SDL_Color rgb;
                SDL_GetRGB(cc, sprsheet->format, &rgb.r, &rgb.g, &rgb.b);

                r = rgb.r;
                g = rgb.g;
                b = rgb.b;

                brt = (r + g + b) / 16;

                int ccf = 0;
                int cdst = 0;
                int cmindst = 256 * 3;
                for (int i = 0; i < 19; i++)
                {
                    cdst = abs(pal2[i][0] - r) + abs(pal2[i][1] - g) + abs(pal2[i][2] - b);
                    if (cdst < cmindst) { cmindst = cdst; ccf = i; };
                }

                if (brt < 47)
                {
                    sprites2[x2 + y2 * 32 + 1024 * frame] = (brt / 4 + 1) % 256;
                    sprites2[x2 + y2 * 32 + 1024 * frame] += 256 * (ccf % 256);
                }
                else
                {
                    sprites2[x2 + y2 * 32 + 1024 * frame] = 0;
                }

            }
}

//*********************************************************************************************************************
// 										Initialization - pallette
//*********************************************************************************************************************

void set_palette() {
    pal[0][0] = 0;
    pal[0][1] = 8;
    pal[0][2] = 8; //black->dark gray
    pal[1][0] = 1;
    pal[1][1] = 9;
    pal[1][2] = 11; //dark blue -> light blue -> cyan
    pal[2][0] = 2;
    pal[2][1] = 10;
    pal[2][2] = 6; //dark green -> light green -> yellow
    pal[3][0] = 3;
    pal[3][1] = 11;
    pal[3][2] = 15; ///...and so on
    pal[4][0] = 4;
    pal[4][1] = 12;
    pal[4][2] = 14;
    pal[5][0] = 5;
    pal[5][1] = 13;
    pal[5][2] = 12;
    pal[6][0] = 6;
    pal[6][1] = 14;
    pal[6][2] = 15;
    pal[7][0] = 7;
    pal[7][1] = 15;
    pal[7][2] = 15;
    pal[8][0] = 8;
    pal[8][1] = 7;
    pal[8][2] = 15;
    pal[9][0] = 9;
    pal[9][1] = 11;
    pal[9][2] = 15;
    pal[10][0] = 10;
    pal[10][1] = 6;
    pal[10][2] = 15;
    pal[11][0] = 11;
    pal[11][1] = 15;
    pal[11][2] = 15;
    pal[12][0] = 12;
    pal[12][1] = 14;
    pal[12][2] = 15;
    pal[13][0] = 13;
    pal[13][1] = 12;
    pal[13][2] = 15;
    pal[14][0] = 14;
    pal[14][1] = 15;
    pal[14][2] = 15;
    pal[15][0] = 15;
    pal[15][1] = 15;
    pal[15][2] = 15;
}

//*********************************************************************************************************************
// 										Initialization - light precalculation
//*********************************************************************************************************************

void calculate_lights() {
    double cx, cy; //current coordinates
    int k;

    for (int i = 0; i < 64; i++) //go through all lights
        for (int x = 1; x < map_size * 16; x++) //go through whole lightmap, x coord.
            if (fabs(x / 16 - static_lights[i][0]) < 12) //light closer than 12 squares?
                for (int y = 1; y < map_size * 16; y++) //go through whole lightmap, y coord.
                    if (fabs(y / 16 - static_lights[i][1]) < 12) //light closer than 12 squares?
                    {
                        cx = 1.0 / 16.0 * x; //map coordinate x
                        cy = 1.0 / 16.0 * y; //map coordinate y

                        double dst = (cx - static_lights[i][0]) * (cx - static_lights[i][0]) + (cy - static_lights[i][1]) * (cy - static_lights[i][1]); //distance to light
                        if (dst < 144) k = checkray(cx, cy, static_lights[i][0], static_lights[i][1], 256);
                        else k = 0; //check if there is unobstructed line to the light
                        lightmap[x][y] += 1.0 * k * static_lights[i][2] / sqrt(dst); //update lightmap
                    }

    for (int x = 1; x < map_size * 16 - 1; x++) //apply sky
        for (int y = 1; y < map_size * 16 - 1; y++)
            if ((map[x / 16][y / 16] / 65536) == 0) //sky tile?
                lightmap[x][y] += sky_light;

    for (int x = 1; x < map_size * 16 - 1; x++)
        for (int y = 1; y < map_size * 16 - 1; y++)
            light_tmp[x][y] = 0.2 * (lightmap[x][y] + lightmap[x + 1][y] + lightmap[x - 1][y] + lightmap[x][y + 1] + lightmap[x][y - 1]); //simple blur - average of neighbors

    for (int x = 1; x < map_size * 16 - 1; x++)
        for (int y = 1; y < map_size * 16 - 1; y++)
            lightmap[x][y] = light_tmp[x][y]; //save the effect of blur

    //flashlight brightness map
    for (int x = 0; x < res_X; x++)
        for (int y = 0; y < res_Y; y++) {
            double lghtx = 5.0 * (x - res_X / 2) / res_X; //horizontal faloff
            double lghty = 5.0 * (y - res_Y / 2) / res_Y; //vertical faloff
            double lght = exp(-lghtx * lghtx) * exp(-lghty * lghty); //full faloff coefficient
            flashlight_coeff[x + y * res_X] = 512.0 * lght * (1 + 0.2 * ((abs(y) % 2) + (abs(x) % 2))); //final calc + dithering
        }

}

//*********************************************************************************************************************
// 										Initialization - lookup tables
//*********************************************************************************************************************

void init_math() //initialization, precalculation
{
    //precalculate sine values. Important, we add 0.001 to avoid even angles where sin=0 or cos=0
    for (int i = 0; i < 3600; i++) sintab[i] = sin((i + 0.001) * 0.1 * torad);

    //fisheye correction term (cosine of ray angle relative to the screen center, e.g. res_X/2)
    for (int i = 0; i < res_X; i++) fisheye[i] = cos(0.1 * (i - res_X / 2) * torad * fov / res_X);

    for (int i = 0; i < 8 * res_X; i++) gausstab[i] = exp(-1.0 * ((5.0 * i / res_X) * (5.0 * i / res_X))); //gaussian, 5 sigma width
    
}

//*********************************************************************************************************************
// 										Ray Casting
//*********************************************************************************************************************

void cast() //main ray casting function
{
    //some speedup might be possible by declaring all variables beforehand here instead of inside the loops
    long doornum;
    int dr;
    for (int xs = 0; xs < res_X; xs++) //go through all screen columns
    {
        //ray angle = player angle +-half of FoV at screen edges; 
        //add 360 degrees to avoid negative values when using lookup table later
        int r_angle = (int)(3600 + player.ang_h + (xs - res_X / 2) * fov / res_X);

        //ray has a velocity of 1. Now we calculate its horizontal and vertical components; 
        //horizontal uses cosine (e.g. sin(a+90 degrees))
        //use %3600 to wrap the angles to 0-360 degree range
        double r_vx = sintab[(r_angle + 900) % 3600];
        //we will ned an integer step to navigate the map; +1/-1 depending on sign of r_vx
        int r_ivx = (r_vx > 0) ? 1 : -1;

        //now the same for vertical components
        double r_vy = sintab[r_angle % 3600];
        int r_ivy = (r_vy > 0) ? 1 : -1;

        //initial position of the ray; precise and integer values
        //ray starts from player position; tracing is done on doubles (x,y), map checks on integers(ix,iy)
        double r_x = player.x;
        double r_y = player.y;
        int r_ix = (int)r_x;
        int r_iy = (int)r_y;
        double r_dist = 0; //travelled distance
        double t1, t2; //time to intersect next vertical/horizontal grid line;
        double h_clamp; //clamped height - for brightness (so walls do not turn extremely bright when very close)

        //ray tracing; we check only intersections with horizontal/vertical grid lines, so maximum of 2*map_size is possible
        for (int i = 0; i < 2 * (map_size - 1); i++) {
            if ((map[r_ix][r_iy] % 256 > 0) && (map[r_ix][r_iy] % 256 < 200)) break; //map>0 is a wall; hit a wall? end tracing 200=horizontal door

            //calculate time to intersect next vertical grid line; 
            //distance to travel is the difference between double and int coordinate, +1 if moving to the right 
            //example: x=0.3, map x=0, moving to the right, next grid is x=1 and distance is 1-0.3=0.7
            //to get time, divide the distance by speed in that direction 
            t1 = (r_ix - r_x + (r_vx > 0)) / r_vx;
            //the same for horizontal lines
            t2 = (r_iy - r_y + (r_vy > 0)) / r_vy;

            dr = 0;

            if (map[r_ix][r_iy] % 256 == 202) {
                typemap[xs] = 63;
                t2 = t2 / 2.0;
                t1 = t1 / 2.0;
            }

            if ((map[r_ix][r_iy] % 256 == 200)) {
                t2 = t2 / 2.0;
                if (t1 > t2) dr = 1;
                doornum = map[r_ix][r_iy] >> 24;
            } //special case-horizontal door
            if ((map[r_ix][r_iy] % 256 == 201)) {
                t1 = t1 / 2.0;
                if (t1 < t2) dr = 1;
                doornum = map[r_ix][r_iy] >> 24;
            } //special case-vertical door

            //now we select the lower of two times, e.g. the closest intersection
            if (t1 < t2) { //intersection with vertical line
                r_y += r_vy * t1; //update y position
                r_ix += r_ivx; //update x map position by +-1
                r_x = r_ix - (r_vx < 0) * r_ivx; //we are on vertical line -> x coordinate = integer coordinate
                r_dist += t1; //increment distance by velocity (=1) * time
            }
            else { //intersection with horizontal line
                r_x += r_vx * t2;
                r_iy += r_ivy;
                r_y = r_iy - (r_vy < 0) * r_ivy;
                r_dist += t2;
            }

            if (dr == 1) //door visibility check to stop the tracing
            {
                tmap[xs] = (t1 < t2) ? 32 * fabs(r_y - (int)(r_y)) : 32 * fabs(r_x - (int)(r_x)); //calculate texture coordinate - needed for partially open door
                if (tmap[xs] > (mapanims[doornum][1] - 1)) break; //4th map byte = door animation index (map[mx][my]/16777216)
            }
        }
        //end of tracing; the distance is updated during steps, so there is no need to calculate it

        hmap[xs] = (int)(res_Y / 2 / r_dist / fisheye[xs]); //record wall height (~1/distance) apply fisheye correction term
        h_clamp = 1.0 * hmap[xs] / res_Y;
        if (h_clamp > 2) h_clamp = 2;
        typemap[xs] = map[r_ix][r_iy] % 256 - 1; //record the wall type; subtract 1 so map[x][y]=1 means wall type 0
        tmap[xs] = (t1 < t2) ? 32 * fabs(r_y - (int)(r_y)) : 32 * fabs(r_x - (int)(r_x)); //record the texture coordinate (fractional part of x/y coordinate * texture size)
        lmap[xs] = (t1 < t2) ? fabs(r_vx) : fabs(r_vy); //lighting based on ray normal
        lmap[xs] *= 15.0 * light_global * (light_faloff * h_clamp + 1 - light_faloff); //calculate brightness; it is proportional to height, 15.0 is arbitrary constant
        nmap[xs] = (t1 < t2) ? 1 : 0; //record wall normal - good thing we have only 90 degree walls :)
        wallxmap[xs] = r_x; //record final ray position
        wallymap[xs] = r_y;
        walldmap[xs] = r_dist;
        if (dr == 1) {
            typemap[xs] = 63;
            tmap[xs] = (t1 < t2) ? (int)(32 + 32 * fabs(r_y - (int)(r_y)) - mapanims[doornum][1]) % 32 : (int)(32 + 32 * fabs(r_x - (int)(r_x)) - mapanims[doornum][1]) % 32;
        } //door - last texture no 63
    }
}

//*********************************************************************************************************************
// 										Drawing functions
//*********************************************************************************************************************


void draw_projectiles()
{
    int hor_pos, column, kk, cx, cy, fkk, fkc;
    double ang0, ang1;
    double dx, dy, dx2, dy2;
    double dst, scale;

    for (int i = 0; i < 64; i++)
        if (projectiles[i][4])
        {
            ang0 = player.ang_h / 10.0; //in degrees
            hor_pos = (int)player.ang_v;

            dx = projectiles[i][0] - player.x;
            dy = projectiles[i][1] - player.y;
            dx2 = cos(ang0 * torad);
            dy2 = sin(ang0 * torad);

            double dot = dx2 * dx + dy2 * dy;      //dot product between [x1, y1] and [x2, y2]
            double det = dx2 * dy - dy2 * dx;      //determinant
            ang1 = atan2(det, dot) * todeg;

            dst = sqrt(dx * dx + dy * dy) / 2;

            if (dst > 0.1) {
                scale = 32 / dst;
                column = (int)(res_X * (ang1) / (0.1 * fov));

                int ptype = 0;
                if (projectiles[i][4] == 2)ptype = 1024 * 3;
                if (projectiles[i][4] == 1)ptype = 1024 * 4; //sprite off. will be stored in proj. data later

                if (column > -res_X && column < res_X && scale < 128)
                    for (int x = 0; x < scale; x++)
                        for (int y = 0; y < scale; y++)
                        {
                            kk = sprites2[(int)(32.0 * x / scale) + 32 * (int)(32.0 * y / scale) + ptype];
                            cx = (int)(res_X / 2 - scale / 2 + x + column);
                            cy = (int)(res_Y / 2 - scale / 2 + y - hor_pos);
                            if ((kk > 0) && (cy < res_Y) && (cy > 0) && (cx < res_X) && (cx > 0) && (depth_map[cx + cy * res_X] > 2 * dst))
                            {
                                fkk = kk % 256;
                                if (fkk > 12)fkk = 12; if (fkk < 0)fkk = 0;
                                char_buff[cx + cy * res_X] = char_grad[fkk];
                                color_buff[cx + cy * res_X] = pal[kk / 256][0];
                                depth_map[cx + cy * res_X] = 2 * dst;
                            }
                        }
            }//end of distance check
        }
}


void draw() {
    //int off; //offset in the 1-d char/color buffer we are writing to 
    int lm1, lm2, ang; //upper/lower limit of the wall slice; ray angle
    int crdx, crdy, crd, mcx, mcy; //texture x,y coordinate, final coordinate in 1-d texture buffer, max x,y coordinate of floor/ceiling pixel
    double cha, col, norm; //character,color,normal map pixel to draw; use double for smooth distance falloff etc.
    double dx, dy, dz; //ray steps for floor rendering; dz=distance to floor;
    int lcx, lcy, lcrd; //light coordinate for invdist matrix
    int hcap; //wall height with upper cap; for brightness calc.
    double bmpc[2]; //for bump mapping
    double fbump; //for floor/ceiling bump mapping
    double plusrefl;

    double anim_phase = (0.5 + 0.5 * sin(1.0 * g_time / 100.0));
    //go through the screen, column by column
    for (int x = 0; x < res_X; x++) {
        int plusy = (int)(-player.z * (hmap[x] + 1)); //player vertical pos modifier
        //upper limit of the wall, capped at half vertical resolution (middle of the screen=0)
        int lm1 = -((hmap[x] + horizon_pos + plusy) > res_Y / 2 ? res_Y / 2 : (hmap[x] + horizon_pos + plusy));
        //lower limit of the wall, capped at -half vertical resolution (middle of the screen=0)
        int lm2 = ((hmap[x] - horizon_pos - plusy + 1) > res_Y / 2 ? res_Y / 2 : (hmap[x] - horizon_pos - plusy + 1));

        //array offset for putting characters
        int offset = x; //we draw on the column x
        double character; //the number of the character from gradient to draw
        int color; //the color of the character to draw
        double normal; //texture normal
        int iswall; //on/off flag if we are drawing a wall. Needed for depth map
        double bmpc[2]; //for bump mapping
        int r_angle = (int)(3600 + player.ang_h + (x - res_X / 2) * fov / res_X); //ray angle, needed for normal maps
        double r_vx = sintab[(r_angle + 900) % 3600]; //ray step x
        double r_vy = sintab[r_angle % 3600]; //ray step y
        double anim_phase = (0.5 + 0.5 * sin(1.0 * g_time / 100.0));
        for (int y = -res_Y / 2; y < res_Y / 2; y++) //go along the whole screen column, drawing either wall or floor/ceiling
        {
            int ang = (int)(3600 + player.ang_h + (x - res_X / 2) * fov / res_X); //calculate ray angle; needed for floor
            double dx = sintab[(ang + 900) % 3600]; //steps in x and y direction, the same as in tracing, needed for floor
            double dy = sintab[ang % 3600];
            character = 0;
            color = 0; //defaults
            if (y >= lm1 && y <= lm2 && hmap[x] > 0) //are we drawing a wall?
            {
                iswall = 1;
                int crdx = tmap[x]; //we get texture x coordinate from coordinate buffer made in tracing step 
                int crdy = 16 + ((int)(14 * (y + horizon_pos + plusy) / hmap[x])) % 16; //texture y coordinate depends on y, horizon position and height
                int crd = crdx + 32 * crdy + 1024 * typemap[x]; //calculate coordinate to use in 1-d texture buffer
                character = textures[crd] % 256; //get texture pixel (1st byte)
                color = (textures[crd] / 256) % 256; //get texture color (2nd byte)
                normal = 1.0 / 128 * ((textures[crd] / 65536) % 256 - 128); //get texture normal (3rd byte)
                character += ((abs(y) % 2) + (abs(x) % 2)); //add dithering to avoid ugly edges
                character = character * lmap[x]; //multiply by the brightness value of 1-d light map
                character += lightmap[(int)(16 * wallxmap[x])][(int)(16 * wallymap[x])]; //apply 2-D lightmap
                character += player.battery * light_flashlight * flashlight_coeff[x + (y + res_Y / 2) * res_X] * hmap[x] / res_Y * lmap[x]; //flashlight
                character *= (nmap[x] * (fabs(r_vx) + r_vy * normal) + (1 - nmap[x]) * (fabs(r_vy) + r_vx * normal)); //apply texture normals
            }
            else //floor/ceiling?
            {
                iswall = 0;
                double plusy2;
                (y + horizon_pos > 0) ? plusy2 = 32.0 * player.z : plusy2 = -32.0 * player.z; //player height modif.
                //calculate distance to the floor pixel; y and horizon_pos are in pixels, 0.1 is added here to avoid division by 0
                double dz = (res_Y / 2 + plusy2) / (fabs(y + horizon_pos) + 0.0) / fisheye[x];
                if ((dz < 16) && (dz > 0)) //ignore extremely far things
                {
                    int crdx = (int)(1024 + 32.0 * (player.x + dx * dz)) % 32; //floor/ceiling texture coordinates
                    int crdy = (int)(1024 + 32.0 * (player.y + dy * dz)) % 32; //1024 is here just to avoid negative numbers
                    int mcx = (int)(player.x + dx * dz) % map_size; //floor/ceiling map coordinates
                    int mcy = (int)(player.y + dy * dz) % map_size;
                    int crd = crdx + 32 * crdy; //base texture coordinate
                    if (y > (-horizon_pos)) crd += 1024 * ((map[mcx][mcy] / 256) % 256); //2nd byte = floor type
                    else crd += 1024 * ((map[mcx][mcy] / 65536) % 256); //3rd byte = ceiling type

                    /*
                    //special case-water
                    if ((y > -horizon_pos) && (((map[mcx][mcy] / 256) % 256) == 3)) cha = anim_phase * (textures[crd] % 256) + (1 - anim_phase) * (textures[crd + 1024] % 256);
                    bmpc[g_time % 2] = character;
                    color = (textures[crd] / 256) % 256; //get texture color (2nd byte)
                    character = character * 1.0 / (0.1 + 16.0 * dz); //OPTIONAL distance based brightness; change factor 2.5 for faster/slower faloff; factor 0.1 is to avoid division by 0 if dz=0
                    if (light_flashlight > 0) character = character * (1 + 6.0 * gausstab[abs(x - res_X / 2)] * gausstab[abs(2 * y - 2 * horizon_pos)]); //flashlight
                    */
                    //static lights
                    //cha = cha + lightmap[(int)(16 * (player.x + dx * dz))][(int)(16 * (player.y + dy * dz))] * (1.0 + 0.01 * maprandoms[0]);

                    if (((map[mcx][mcy] / 65536) > 0) || (y > (-horizon_pos))) //ground or non-sky?
                    {
                        character = textures[crd] % 256; //get texture pixel (1st byte)
                        color = (textures[crd] / 256) % 256; //get texture color (2nd byte)
                        character += ((abs(y) % 2) + (abs(x) % 2)); //add dithering
                        character *= 0.2 * light_global * (light_faloff * abs(y + horizon_pos) / (dz + 1) + 1 - light_faloff); //distance-based gradient
                        //character+=lightmap[(int)(16*(player.x+dx*dz))][(int)(16*(player.y+dy*dz))]; //apply 2-D lightmap
                        character += 0.2 * (player.battery * light_flashlight * flashlight_coeff[x + (y + res_Y / 2) * res_X] / (dz + 2)); //flashlight
                    }
                    else {
                        character = sky[(res_X * 2 * res_Y + x / 8 + (int)(r_angle / 8) + (y + res_Y / 2 + horizon_pos) * 2 * res_X) % (res_X * res_Y)];
                        character += ((abs(y) % 2) + (abs(x) % 2)); //add dithering 
                        color = sky_color;
                    }
                }
            }
            //limit the value to the limits of character gradient (especially important if there are multiple brightness modifiers)
            if (character > grad_length) character = grad_length;
            if ((character < 0) || std::isnan(character)) character = 0;
            char_buff[offset] = char_grad[(int)character]; //save the character in character buffer
            nchar_buff[offset] = (int)character; //save the character number (basically brightness) in character number buffer
            color_buff[offset] = pal[color][(character > 30) + (character > 85)]; //save the color in color buffer
            iswall ? depth_map[offset] = walldmap[x] : depth_map[offset] = 255; //record depth map
            offset += res_X; //go down by 1 row
        } //end of column
    } //end of drawing
}

//*********************************************************************************************************************
void draw_sprite(int pos, int number, int which) //draw a sprite at specific point in char buffer - will be used for interface, weapon etc.
{
    if (which == 0) {
        for (int x = 0; x < 32; x++)
            for (int y = 0; y < 32; y++)
                if (sprites[x + y * 32 + 1024 * number] / 65536) //alpha>0?
                {
                    char_buff[pos + x + res_X * y] = char_grad[sprites[x + y * 32] % grad_length];
                    nchar_buff[pos + x + res_X * y] = sprites[x + y * 32] % grad_length;
                    color_buff[pos + x + res_X * y] = (sprites[x + y * 32] / 256) % 256;
                }
    }
    else {
        for (int x = 0; x < 32; x++)
            for (int y = 0; y < 32; y++)
                if (sprites2[x + y * 32 + 1024 * number] / 65536) //alpha>0?
                {
                    char_buff[pos + x + res_X * y] = char_grad[sprites2[x + y * 32] % grad_length];
                    nchar_buff[pos + x + res_X * y] = sprites2[x + y * 32] % grad_length;
                    color_buff[pos + x + res_X * y] = (sprites2[x + y * 32] / 256) % 256;
                }
    }
}

//*********************************************************************************************************************
void draw_enemies() {
    int charn, color; //character number and color value to draw
    //helper variables for calculating various distances and angles
    int column, cx, cy; //screen column of sprite center, x,y coordinates to draw to
    double ang0, ang1; //player angle and relative angle to sprite
    double dx, dy, dx2, dy2; //for distance calculations
    double dst, scale; //distance and sprite scale
    double brightness; //brightness modifier for drawing sprite

    for (int i = 0; i < 16; i++)
        if (enemies[i].enabled == 1) {
            ang0 = player.ang_h / 10.0; //player angle, in degrees

            dx = enemies[i].x - player.x; //x,y distance to enemy
            dy = enemies[i].y - player.y;
            dx2 = cos(ang0 * torad); //player heading vector
            dy2 = sin(ang0 * torad);

            double dot = dx2 * dx + dy2 * dy; //dot product between [x1, y1] and [x2, y2]
            double det = dx2 * dy - dy2 * dx; //determinant
            ang1 = atan2(det, dot) * todeg; //player to enemy angle, degrees

            dst = sqrt(dx * dx + dy * dy); //distance to enemy
            scale = 32.0 / dst; //distance-based scaling
            column = (int)(res_X * (ang1) / (0.1 * fov)); //screen column to draw on
            int plusy = (int)(32.0 * player.z / dst); //player vertical pos modifier

            if (column > -res_X && column < res_X && scale < 256) //we are within the screen? isn't sprite too big?
                for (int x = 0; x < scale; x++)
                    for (int y = 0; y < scale; y++) {
                        charn = sprites[((int)(32.0 * x / scale) + 32 * (int)(32.0 * y / scale)) % 1024 + 1024 * enemies[i].type]; //base brightness
                        cx = (int)(res_X / 2 - scale / 2 + x + column); //coordinate x
                        cy = (int)(res_Y / 2 - scale / 2 + y - horizon_pos + plusy); //coordinate y
                        if ((charn / 65536 > 0) && (cy < res_Y) && (cy > 0) && (cx < res_X) && (cx > 0) && (depth_map[cx + cy * res_X] > dst)) //>0 alpha, we are within screen, not obscured (depth map)
                        {
                            color = (charn / 256) % 16; //record color

                            brightness = 32 * light_global; //base global value
                            brightness += 16 * lightmap[(int)(16 * enemies[i].x)][(int)(16 * enemies[i].y)]; //apply 2-D lightmap
                            brightness += player.battery * light_flashlight * flashlight_coeff[cx + cy * res_X]; //apply flashlight
                            brightness = 1E-6 * (brightness + scale * 4); //apply distance scaling coefficient
                            charn = ((int)(charn * brightness)); //final character value
                            if (charn > grad_length) charn = grad_length;
                            if (charn < 0) charn = 0; //value clamping	
                            char_buff[cx + cy * res_X] = char_grad[charn]; //save character to buffer
                            nchar_buff[cx + cy * res_X] = charn; //save character number to buffer
                            color_buff[cx + cy * res_X] = pal[color][(charn > pal_thr1) + (charn > pal_thr2)]; //save color to buffer 
                            depth_map[cx + cy * res_X] = dst; //record depth value - so sprites can obscure each other; 
                            //closer sprites will overdraw farther, farther cannot be drawn on closer due to above depth map update
                        }
                    } //end of enemy drawing	
        } //end of going through enemies
}
//*********************************************************************************************************************
void HUD() {
    int frame = (int)(player_anim[0] / 16) * 1024;
    int off = 56 * 1024 + frame; //weapon fprite offset

    int px = res_X - 56; //weapon
    int py = res_Y - 32;
    for (int x = 0; x < 32; x++)
        for (int y = 0; y < 32; y++)
            if (sprites2[off + x + y * 32] % 16 > 0) {
                int cha = sprites2[off + x + y * 32] % 256;
                int col = sprites2[off + x + y * 32] / 256;
                cha = (int)(cha * 0.02 * (2 + visiondata[0]));
                if (cha > 9) cha = 9;

                char_buff[px + x + (y + py) * res_X] = char_grad[cha % 256];
                color_buff[px + x + (y + py) * res_X] = pal[col][0];
            }

}

//*********************************************************************************************************************
// 										post-processing
//*********************************************************************************************************************
void post_processing() {
    if (player.status[0] > 0) //player is hurt?
        for (int i = 0; i < res_X * res_Y; i++) color_buff[i] = 4; //loop through all pixels and set them to 
}

//*********************************************************************************************************************
// 										Controls (windows-specific)
//*********************************************************************************************************************
void controls() //handles keyboard, mouse controls and player movement; windows-specific
{
    SDL_PumpEvents();
    int interx, intery; //map coordinate player is interacting with
    double accel = player.accel * (1.0 / 150 * player.stamina);
    double dx = accel * sintab[(int)player.ang_h % 3600]; //x step in the direction player is looking; 
    double dy = accel * sintab[((int)player.ang_h + 900) % 3600]; //y step in the direction player is looking

    if (player.hp >= 0.5) {
        if (keys[SDL_SCANCODE_A]) {
            player.vx += dx / 2;
            player.vy -= dy / 2;
        }; //WASD movement
        if (keys[SDL_SCANCODE_D]) {
            player.vx -= dx / 2;
            player.vy += dy / 2;
        };
        if (keys[SDL_SCANCODE_W]) {
            player.vx += dy;
            player.vy += dx;
        };
        if (keys[SDL_SCANCODE_S]) {
            player.vx -= dy / 2;
            player.vy -= dx / 2;
        };

        if (keys[SDL_SCANCODE_F] && (key_delay < 0.1)) {
            light_flashlight = (1 - light_flashlight);
            key_delay = 1;
        }; //F for flashlight

        if (keys[SDL_SCANCODE_G] && (key_delay < 0.1)) {
            player.status[1] = (1 - player.status[1]);
            key_delay = 1;
        }; //g for god mode

        if (keys[SDL_SCANCODE_SPACE] && (player.z < 0.05)) {
            player.vz = player.jump_h;
        }; //space for jump
        if (keys[SDL_SCANCODE_E] && (key_delay < 0.1)) {//use key
            interx = (int)(player.x + 150 * dy);
            intery = (int)(player.y + 150 * dx);

            int dooraction;
            long doornum = map[interx][intery] >> 24;

            if (map[interx][intery] % 256 == 200)//horizontal door
            {
                dooraction = mapanims[doornum][0];
                mapanims[doornum][0] = -dooraction; //1=opening, -1=closing
                //std::cout << "Interact\n";
            }

            if (map[interx][intery] % 256 == 201)//vertical door
            {
                dooraction = mapanims[doornum][0];
                mapanims[doornum][0] = -dooraction; //1=opening, -1=closing
            }
            key_delay = 1;
        }//end use key
    }

    if (keys[SDL_SCANCODE_ESCAPE]) F_exit = 1; //esc for exit

    if (keys[SDL_SCANCODE_H] && (key_delay < 0.1)) {
        debug[0] = (debug[0] + 1) % 3;
        key_delay = 1;
    } //h for toggling display type

    key_delay *= 0.9; //delay so that toggle buttons (like flashlight) do not trigger 100x per second

    mousex0 = P_Res_X / 2;
    mousey0 = P_Res_Y / 2;
    Uint32 mbuttons;
    mbuttons = SDL_GetMouseState(&mousex, &mousey);
    player.ang_h = 500.0 * (mousex - mousex0) / mouse_speed;
    player.ang_v = 20.0 * (mousey - mousey0) / mouse_speed;

    //player movement related stuff
    double spd = 1.0 * sqrt(player.vx * player.vx + player.vy * player.vy);
    player.status[2] += (int)(100.0 * spd); //walk cycle phase
    player.ang_v -= 5 * sin(0.01 * player.status[2]) * sin(0.01 * player.status[2]); //vertical movement when running
    if (player.hp > 0.5) player.ang_v += 0.1 / (50 * spd + 1) * (100 - player.stamina) * sin(0.05 * g_time); //vertical movement when standing tired
    horizon_pos = (int)player.ang_v; //position of the horizon, for looking up/down 0=in the middle

    if (player.ang_h < 3600) player.ang_h += 3600; //if player angle is less than 360 degrees, add 360 degrees so its never negative

    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) && (key_delay < 0.1)) //shot
    {
        projectiles[num_projectile][0] = player.x;
        projectiles[num_projectile][1] = player.y;
        projectiles[num_projectile][2] = 32 * dy;
        projectiles[num_projectile][3] = 32 * dx;
        projectiles[num_projectile][4] = 1;
        projectiles[num_projectile][5] = 1;
        projectiles[num_projectile][6] = num_projectile % 8;
        num_projectile = (num_projectile + 1) % 64;
        key_delay = 1;
        player_anim[0] = 1;
    }
}

//*********************************************************************************************************************
// 										Physics, game logic
//*********************************************************************************************************************

void physics() {
    if (player.x > (map_size - 2)) player.x = map_size - 2;
    if (player.x < 2) player.x = 2; //failsafes from going out of map
    if (player.y > (map_size - 2)) player.y = map_size - 2;
    if (player.y < 2) player.y = 2;

    if (g_time % 8 == 0)
        for (int i = 0; i < 64; i++) //map animations
        {
            if ((mapanims[i][0] == 1) && (mapanims[i][1] > 0)) mapanims[i][1]--; //door opening
            if ((mapanims[i][0] == -1) && (mapanims[i][1] < 32)) mapanims[i][1]++;
        }

    if (player.vx > 0.1) player.vx = 0.1;
    if (player.vx < -0.1) player.vx = -0.1; //failsafes from exceeding certain speed limit
    if (player.vy > 0.1) player.vy = 0.1;
    if (player.vy < -0.1) player.vy = -0.1;

    int block, collision;
    long int doornum;

    block = map[(int)(player.x + 1 * player.vx)][(int)player.y];
    doornum = block >> 24;
    collision = (block % 256 > 0);
    if ((block % 256 == 200) && (mapanims[doornum][0] == -1) && (mapanims[doornum][1] > 30)) collision = 0;
    if ((block % 256 == 201) && (mapanims[doornum][0] == -1) && (mapanims[doornum][1] > 30)) collision = 0;
    if (collision == 1) player.vx = -player.vx / 2; //collisions in x axis - bounce back with half the velocity

    block = map[(int)player.x][(int)(player.y + 1 * player.vy)];
    doornum = block >> 24;
    collision = (block % 256 > 0);
    if ((block % 256 == 200) && (mapanims[doornum][0] == -1) && (mapanims[doornum][1] > 30)) collision = 0;
    if ((block % 256 == 201) && (mapanims[doornum][0] == -1) && (mapanims[doornum][1] > 30)) collision = 0;
    if (collision == 1) player.vy = -player.vy / 2; //collisions in y axis
    player.x += player.vx; //update x,y values with x,y velocities
    player.y += player.vy;
    player.z += player.vz;
    player.vx *= (1 - player.friction); //friction reduces velocity values
    player.vy *= (1 - player.friction);
    player.vz *= (1 - player.friction);

    // Update projectiles
    for (int i = 0; i < 64; i++)if (projectiles[i][4] > 0)
    {
        projectiles[i][0] += projectiles[i][2];
        projectiles[i][1] += projectiles[i][3];

        if (map[(int)(projectiles[i][0])][(int)(projectiles[i][1])] % 256 > 0) { projectiles[i][4] = 0; }
    }

    player.vz -= player.grav; //gravity
    if ((player.z <= 0) && (player.hp >= 0.5)) {
        player.vz = 0;
        player.z = 0;
    } //standing on the ground
    if ((player.z <= -0.5) && (player.hp < 0.5)) {
        player.vz = 0;
        player.z = -0.5;
    } //lying on the ground

    if (player.status[0] > 0) player.status[0]--; //hurt status time countdown
    if (player.hp < 0.5) player.status[0] = 1; //player dead = permanent hurt status

    if (player.stamina < 100) player.stamina += 0.04 * (0.7 + 0.003 * player.hp); //stamina regeneration
    if (player.stamina > 0) player.stamina -= 1.5 * sqrt(player.vx * player.vx + player.vy * player.vy); //reduce stamina by velocity

    if (light_flashlight) player.battery *= 0.9999; //slow decay
    if (player.hp >= 0.5) player.score += 0.1;
    if (player_anim[0] > 0) player_anim[0] = ((int)player_anim[0] + 1) % (16 * 6); //weapon animation
}

//*********************************************************************************************************************

void move_enemies() {
    double nx, ny; //new positions
    double dst; //distance to player
    int rating, maxrating, chosen; //current and max tile rating, chosen direction for pathfinding

    //this way of doing pathfinding is very costly for large maps
    //but it's okay for pac-man
    for (int x = 1; x < map_size - 1; x++)
        for (int y = 1; y < map_size - 1; y++)
            path_map[x][y] = 0; //clear path map

    path_map[(int)player.x][(int)player.y] = map_size; //we set the tile occupied by the player to highest value

    for (int i = map_size; i > 0; i--)
        for (int x = 1; x < map_size - 1; x++)
            for (int y = 1; y < map_size - 1; y++)
                if (path_map[x][y] == i) //the tile has value i?
                {
                    if ((path_map[x + 1][y] == 0) && (map[x + 1][y] % 256 == 0)) path_map[x + 1][y] = i - 1; //set empty neighbouring tiles to (i-1)
                    if ((path_map[x - 1][y] == 0) && (map[x - 1][y] % 256 == 0)) path_map[x - 1][y] = i - 1;
                    if ((path_map[x][y + 1] == 0) && (map[x][y + 1] % 256 == 0)) path_map[x][y + 1] = i - 1;
                    if ((path_map[x][y - 1] == 0) && (map[x][y - 1] % 256 == 0)) path_map[x][y - 1] = i - 1;
                }
    //after this step, enemies just need to go towards highest nearby number to get to the player

    for (int i = 0; i < 16; i++)
        if (enemies[i].enabled == 1) {
            nx = enemies[i].x + 8 * enemies[i].vx;
            ny = enemies[i].y + 8 * enemies[i].vy;
            if (map[(int)nx][(int)enemies[i].y] % 256 > 0) enemies[i].vx = -enemies[i].vx; //map collisions
            if (map[(int)enemies[i].x][(int)ny] % 256 > 0) enemies[i].vy = -enemies[i].vy;

            enemies[i].x += enemies[i].vx; //movement
            enemies[i].y += enemies[i].vy;

            enemies[i].vx *= 0.94; //friction
            enemies[i].vy *= 0.94;

            maxrating = 0;
            chosen = 0;
            for (int dir = 0; dir < 8; dir++) //test 8 cardinal directions
            {
                nx = enemies[i].x + 1.4 * cos(dir * M_PI / 4);
                ny = enemies[i].y + 1.4 * sin(dir * M_PI / 4);
                rating = path_map[((int)nx) % map_size][((int)ny) % map_size];
                //AI quirks of various ghosts
                if (rand() % 8 == 0) rating += rand() % 3; //sometimes add some randomness

                //map tile is floor and has higher rating? record it
                if ((map[((int)nx) % map_size][((int)ny) % map_size] % 256 == 0) && (rating > maxrating)) {
                    maxrating = path_map[((int)nx) % map_size][((int)ny) % map_size];
                    chosen = dir;
                }
            }
            //set the best direction
            nx = cos(chosen * M_PI / 4);
            ny = sin(chosen * M_PI / 4);
            enemies[i].vx += 0.001 * nx;
            enemies[i].vy += 0.001 * ny;

            if (rand() % 16 == 0) {
                enemies[i].vx += 0.01 * (rand() % 3 - 1); //random movements
                enemies[i].vy += 0.01 * (rand() % 3 - 1);
            }

            //player damage
            nx = player.x;
            ny = player.y;
            dst = (enemies[i].x - nx) * (enemies[i].x - nx) + (enemies[i].y - ny) * (enemies[i].y - ny);
            if (dst < 2) //less than 2 squares from player? accelerate directly towards him
            {
                enemies[i].vx += 0.001 * (nx - enemies[i].x);
                enemies[i].vy += 0.001 * (ny - enemies[i].y);
            }

            if ((dst < 0.5) && (player.hp > 0) && (player.status[1] == 0)) { //enemy close? positive hp? no god mode?
                player.hp -= 0.25;
                player.z += 0.05 * (rand() % 3 - 1); //vertical screen shake
                player.ang_h += 2 * (rand() % 3 - 1); //horizontal screen shake
                player.z *= 0.99; //make sure vertical shake is not too big
                player.status[0] = 32; //player hurt status
            }

        } //end of looping through enemies
}

//*********************************************************************************************************************
// 									 Draw minimap
//*********************************************************************************************************************
void minimap(int type) {

    if (type == 0) //standard minimap
    {
        for (int x = 0; x < map_size - 1; x++)
            for (int y = 0; y < map_size - 1; y++) {
                char_buff[x + y * res_X] = '#';
                color_buff[x + y * res_X] = 8 * (map[x][y] % 256 > 0);
            }

        color_buff[(int)player.x + (int)player.y * res_X] = 15; //highlight player
        char_buff[(int)player.x + (int)player.y * res_X] = '@';

        color_buff[(int)enemies[0].x + (int)enemies[0].y * res_X] = 12; //highlight enemies
        char_buff[(int)enemies[0].x + (int)enemies[0].y * res_X] = '*';
        color_buff[(int)enemies[1].x + (int)enemies[1].y * res_X] = 13;
        char_buff[(int)enemies[1].x + (int)enemies[1].y * res_X] = '*';
        color_buff[(int)enemies[2].x + (int)enemies[2].y * res_X] = 10;
        char_buff[(int)enemies[2].x + (int)enemies[2].y * res_X] = '*';
        color_buff[(int)enemies[3].x + (int)enemies[3].y * res_X] = 14;
        char_buff[(int)enemies[3].x + (int)enemies[3].y * res_X] = '*';
    }

    if (type == 1) //shows pathfinding map-for debug purposes
    {
        for (int x = 1; x < map_size - 1; x++)
            for (int y = 1; y < map_size - 1; y++) {
                char_buff[x + y * res_X] = 'a' + path_map[x][y];
                color_buff[x + y * res_X] = 7;
                if (map[x][y] % 256 > 0) color_buff[x + y * res_X] = 0; //wall
            }
        color_buff[(int)player.x + (int)player.y * res_X] = 10; //highlight player
        color_buff[(int)enemies[0].x + (int)enemies[0].y * res_X] = 12; //highlight enemies
        color_buff[(int)enemies[1].x + (int)enemies[1].y * res_X] = 13;
        color_buff[(int)enemies[2].x + (int)enemies[2].y * res_X] = 10;
        color_buff[(int)enemies[3].x + (int)enemies[3].y * res_X] = 14;
    }

}

//*********************************************************************************************************************
// 									 Main game loop
//*********************************************************************************************************************

int main() {
    // Map loading
    std::string mapPath;
    std::cout << "Which map file do you choose (excluding extension)? Type 'D' for default: \n";
    std::cout << "List of maps: \n";
    listFilesWithExtension("maps", ".pac");
    std::cin >> mapPath;

    // Setup
    char str[10]; //for status display
    SDL_SetMainReady();
    init_math();
    set_palette();
    initwindow();
    loadsprites();
    initImGui();

    gen_map_pacman(mapPath);
    gen_sky(10);
    calculate_lights();
    debug[0] = 1;
    bool done = false;

    while (F_exit == 0) //main game loop
    {

        if (state == 0) {

            SDL_SetRelativeMouseMode(SDL_FALSE);
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                ImGui_ImplSDL2_ProcessEvent(&event);
                if (event.type == SDL_QUIT)
                    done = true;
                if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(screen))
                    done = true;
            }

            ImGuiIO& io = ImGui::GetIO(); (void)io;
            ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
            auto displaySize = ImGui::GetIO().DisplaySize;
            ImGui::SetNextWindowSize(ImVec2{ displaySize.x * 0.7f, displaySize.y * 0.5f }, ImGuiCond_Always);
            // Centered
            ImGui::SetNextWindowPos(ImVec2{ displaySize.x * 0.5f, displaySize.y * 0.5f }, ImGuiCond_Always, ImVec2{ 0.5f, 0.5f });
            ImGui::Begin("Menu", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
            if (ImGui::Button("Start")) {
                state = 1;
            }
            ImGui::Checkbox("Lighting", &settings::lighting);
            ImGui::Render();
            SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
            //SDL_SetRenderDrawColor(renderer, (Uint8)(clear_color.x * 255), (Uint8)(clear_color.y * 255), (Uint8)(clear_color.z * 255), (Uint8)(clear_color.w * 255));
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
            SDL_RenderPresent(renderer);
            std::cout << "ImGui\n";
        }
        if (state == 1) {
            SDL_SetRelativeMouseMode(SDL_TRUE);
            controls();
            physics();
            move_enemies();
            cast();
            draw();
            draw_enemies();
            draw_projectiles();
            minimap(0);
            post_processing();
            HUD();

            g_time++;
            if (g_time % 10 == 0) {
                tt2 = tt1;
                tt1 = SDL_GetTicks();
                if (tt1 > tt2) fps = 10000 / (tt1 - tt2); //10k since we measure fps every 10 frames
            }

            //printf(str, "fps: %d hp: %d stamina: %d battery: %d score: %d", fps, (int)player.hp, (int)player.stamina, (int)(100 * player.battery), (int)player.score);
            drawstring(0, 10, str);
            drawstring(res_X * (res_Y - 1), 10, (char*)
                "WASD to move, space to jump, F for flashlight");
            display(debug[0]); //several types of display to choose
            SDL_Delay(5);
        }
    }
    //SDL_FreeCursor(cursor);
    cleanup();
    return 0;
}