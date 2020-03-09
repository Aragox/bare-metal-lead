#include "config.h"

typedef unsigned char      u8;
typedef signed   char      s8;
typedef unsigned short     u16;
typedef signed   short     s16;
typedef unsigned int       u32;
typedef signed   int       s32;
typedef unsigned long long u64;
typedef signed   long long s64;

#define noreturn __attribute__((noreturn)) void

typedef enum bool {
    false,
    true
} bool;

/* Simple math */

/* A very simple and stupid exponentiation algorithm */
static inline double pow(double a, double b)
{
    double result = 1;
    while (b-- > 0)
        result *= a;
    return result;
}

/* Port I/O */

static inline u8 inb(u16 p)
{
    u8 r;
    asm("inb %1, %0" : "=a" (r) : "dN" (p));
    return r;
}

static inline void outb(u16 p, u8 d)
{
    asm("outb %1, %0" : : "dN" (p), "a" (d));
}

/* Divide by zero (in a loop to satisfy the noreturn attribute) in order to
 * trigger a division by zero ISR, which is unhandled and causes a hard reset.
 */
noreturn reset(void)
{
    volatile u8 one = 1, zero = 0;
    while (true)
        one /= zero;
}

/* Timing */

/* Return the number of CPU ticks since boot. */
static inline u64 rdtsc(void)
{
    u32 hi, lo;
    asm("rdtsc" : "=a" (lo), "=d" (hi));
    return ((u64) lo) | (((u64) hi) << 32);
}

/* Return the current second field of the real-time-clock (RTC). Note that the
 * value may or may not be represented in such a way that it should be
 * formatted in hex to display the current second (i.e. 0x30 for the 30th
 * second). */
u8 rtcs(void)
{
    u8 last = 0, sec;
    do { /* until value is the same twice in a row */
        /* wait for update not in progress */
        do { outb(0x70, 0x0A); } while (inb(0x71) & 0x80);
        outb(0x70, 0x00);
        sec = inb(0x71);
    } while (sec != last && (last = sec));
    return sec;
}

/* The number of CPU ticks per millisecond */
u64 tpms;

/* Set tpms to the number of CPU ticks per millisecond based on the number of
 * ticks in the last second, if the RTC second has changed since the last call.
 * This gets called on every iteration of the main loop in order to provide
 * accurate timing. */
void tps(void)
{
    static u64 ti = 0;
    static u8 last_sec = 0xFF;
    u8 sec = rtcs();
    if (sec != last_sec) {
        last_sec = sec;
        u64 tf = rdtsc();
        tpms = (u32) ((tf - ti) >> 3) / 125; /* Less chance of truncation */
        ti = tf;
    }
}

/* IDs used to keep separate timing operations separate */
enum timer {
    TIMER_UPDATE,
    TIMER_CLEAR,
    TIMER__LENGTH
};

u64 timers[TIMER__LENGTH] = {0};

/* Return true if at least ms milliseconds have elapsed since the last call
 * that returned true for this timer. When called on each iteration of the main
 * loop, has the effect of returning true once every ms milliseconds. */
bool interval(enum timer timer, u32 ms)
{
    u64 tf = rdtsc();
    if (tf - timers[timer] >= tpms * ms) {
        timers[timer] = tf;
        return true;
    } else return false;
}

/* Return true if at least ms milliseconds have elapsed since the first call
 * for this timer and reset the timer. */
bool wait(enum timer timer, u32 ms)
{
    if (timers[timer]) {
        if (rdtsc() - timers[timer] >= tpms * ms) {
            timers[timer] = 0;
            return true;
        } else return false;
    } else {
        timers[timer] = rdtsc();
        return false;
    }
}

/* Video Output */

/* Seven possible display colors. Bright variations can be used by bitwise OR
 * with BRIGHT (i.e. BRIGHT | BLUE). */
enum color {
    BLACK,
    BLUE,
    GREEN,
    CYAN,
    RED,
    MAGENTA,
    YELLOW,
    GRAY,
    BRIGHT
};

#define COLS (80)
#define ROWS (25)
u16 *const video = (u16*) 0xB8000;

/* Display a character at x, y in fg foreground color and bg background color.
 */
void putc(u8 x, u8 y, enum color fg, enum color bg, char c)
{
    u16 z = (bg << 12) | (fg << 8) | c;
    video[y * COLS + x] = z;
}

/* Display a string starting at x, y in fg foreground color and bg background
 * color. Characters in the string are not interpreted (e.g \n, \b, \t, etc.).
 * */
void puts(u8 x, u8 y, enum color fg, enum color bg, const char *s)
{
    for (; *s; s++, x++)
        putc(x, y, fg, bg, *s);
}

/* Clear the screen to bg backround color. */
void clear(enum color bg)
{
    u8 x, y;
    for (y = 0; y < ROWS; y++)
        for (x = 0; x < COLS; x++)
            putc(x, y, bg, bg, ' ');
}

/* Keyboard Input */

#define KEY_1     (0x2)
#define KEY_2     (0x3)
#define KEY_3     (0x4)
#define KEY_4     (0x5)
#define KEY_D     (0x20)
#define KEY_H     (0x23)
#define KEY_P     (0x19)
#define KEY_R     (0x13)
#define KEY_S     (0x1F)
#define KEY_UP    (0x48)
#define KEY_DOWN  (0x50)
#define KEY_LEFT  (0x4B)
#define KEY_RIGHT (0x4D)
#define KEY_ENTER (0x1C)
#define KEY_SPACE (0x39)

/* Return the scancode of the current up or down key if it has changed since
 * the last call, otherwise returns 0. When called on every iteration of the
 * main loop, returns non-zero on a key event. */
u8 scan(void)
{
    static u8 key = 0;
    u8 scan = inb(0x60);
    if (scan != key)
        return key = scan;
    else return 0;
}

/* PC Speaker */

/* Set the frequency of the PC speaker through timer 2 of the programmable
 * interrupt timer (PIT). */
void pcspk_freq(u32 hz)
{
    u32 div = 1193180 / hz;
    outb(0x43, 0xB6);
    outb(0x42, (u8) div);
    outb(0x42, (u8) (div >> 8));
}

/* Enable timer 2 of the PIT to drive the PC speaker. */
void pcspk_on(void)
{
    outb(0x61, inb(0x61) | 0x3);
}

/* Disable timer 2 of the PIT to drive the PC speaker. */
void pcspk_off(void)
{
    outb(0x61, inb(0x61) & 0xFC);
}

/* Formatting */

/* Format n in radix r (2-16) as a w length string. */
char *itoa(u32 n, u8 r, u8 w)
{
    static const char d[16] = "0123456789ABCDEF";
    static char s[34];
    s[33] = 0;
    u8 i = 33;
    do {
        i--;
        s[i] = d[n % r];
        n /= r;
    } while (i > 33 - w);
    return (char *) (s + i);
}

/* Random */

/* Generate a random number from 0 inclusive to range exclusive from the number
 * of CPU ticks since boot. */
u32 rand(u32 range)
{
    return (u32) rdtsc() % range;
}

/* Shuffle an array of bytes arr of length len in-place using Fisher-Yates. */
void shuffle(u8 arr[], u32 len)
{
    u32 i, j;
    u8 t;
    for (i = len - 1; i > 0; i--) {
        j = rand(i + 1);
        t = arr[i];
        arr[i] = arr[j];
        arr[j] = t;
    }
}

//##################################################################################################################################################################################
//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//LOGICA DEL JUEGO
//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//##################################################################################################################################################################################

/* Two-dimensional array of color values */
u8 well[WELL_HEIGHT][WELL_WIDTH];

struct Piece {
    u32 i; /* Index*/
    u32 hp; /*HP*/
    u32 dmg; /*Damage*/
    bool alive; /*State*/
    s8 x, y; /* Coordinates */
};

#define N_ENEMYS (25)
#define N_LASERS (25)
#define N_WALLS (80)

    struct Piece enemy[N_ENEMYS];
    struct Piece laser[N_LASERS];
    struct Piece wall[N_WALLS];
    struct Piece player;

u32 score = 0, level = 1, speed = INITIAL_SPEED;

bool paused = false, game_over = false;

#define WALLSPAWN (75000)
#define WALLMOVE (37500)
#define ENEMYSPAWN (200000)
#define ENEMYMOVE (100000)
#define STARTWALLCHANGE (1000000)

#define DIRECTIONSIZE (24)
#define REPEATMOVE (9)

u32 wallspawn = WALLSPAWN, wallmove = WALLMOVE, enemyspawn = ENEMYSPAWN, enemymove = ENEMYMOVE, startwallchange = STARTWALLCHANGE;

u32 cont_enemyspawn = 0, cont_wallspawn = 0, cont_enemymove = 0, cont_wallmove = 0;

u8 direction[DIRECTIONSIZE] = { 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 1, 2, 1, 0, 0, 1, 1, 0, 1, 2, 0, 0, 2, 1 };
u8 dx = 0;
u32 cont_start_dx = 0, cont_repeat = REPEATMOVE, cont_change = 0;

// Initialize next level 
void next_level(u32 l) {
    
    level = l;

    // Initialize level settings 
        switch(l) {
        case 1:
            enemyspawn = ENEMYSPAWN; 
            wallspawn = WALLSPAWN; 
            enemymove = ENEMYMOVE;  
            wallmove = WALLMOVE;   
            break;
        case 2:
            enemyspawn = ENEMYSPAWN * 2; 
            wallspawn = WALLSPAWN; 
            enemymove = ENEMYMOVE;  
            wallmove = WALLMOVE; 
            startwallchange = STARTWALLCHANGE * 1.5;  
            break;
        case 3:
            enemyspawn = ENEMYSPAWN * 1.5; 
            wallspawn = WALLSPAWN; 
            enemymove = ENEMYMOVE;  
            wallmove = WALLMOVE; 
            startwallchange = STARTWALLCHANGE * 1.5;              
            break;
        case 4:
            enemyspawn = ENEMYSPAWN * 1.5; 
            wallspawn = enemyspawn / 2; 
            enemymove = ENEMYMOVE * 2;  
            wallmove = enemymove; 
            startwallchange = STARTWALLCHANGE * 10;  
            break;
        }
        cont_change = 0;
        cont_enemyspawn = enemyspawn; 
        cont_wallspawn = wallspawn; 
        cont_enemymove = enemymove;  
        cont_wallmove = wallmove; 
        cont_start_dx = startwallchange; 
    
    // Initialize pieces
    //Enemies
    u8 i;
        switch(l) {
        case 1:
            for (i = 0; i < N_ENEMYS; i++) {
                enemy[i].i = 1;
                enemy[i].hp = 3;
                enemy[i].dmg = 1;     
                enemy[i].alive = false; 
                enemy[i].x = 0;   
                enemy[i].y = 0; 
            }
            break;
        case 2:
            for (i = 0; i < N_ENEMYS; i++) {
                enemy[i].i = 2;
                enemy[i].hp = 999;
                enemy[i].dmg = 1;     
                enemy[i].alive = false; 
                enemy[i].x = 0;   
                enemy[i].y = 0; 
            }
            break;
        case 3:
            for (i = 0; i < N_ENEMYS; i++) {
                enemy[i].i = 3;
                enemy[i].hp = 3;
                enemy[i].dmg = 1;     
                enemy[i].alive = false; 
                enemy[i].x = 0;   
                enemy[i].y = 0; 
            }
            break;
        case 4:
            for (i = 0; i < N_ENEMYS; i++) {
                enemy[i].i = 4;
                enemy[i].hp = 999;
                enemy[i].dmg = 1;     
                enemy[i].alive = false; 
                enemy[i].x = 0;   
                enemy[i].y = 0; 
            }
            break;  
        }

    //Wall
            for (i = 0; i < N_WALLS; i++) {
                wall[i].i = 1;
                wall[i].hp = 999;
                wall[i].dmg = 1;     
                wall[i].alive = false; 
                wall[i].x = 0;   
                wall[i].y = 0; 
            }
    //Player Laser
            for (i = 0; i < N_LASERS; i++) {
                laser[i].i = 1;
                laser[i].hp = 999;
                laser[i].dmg = 1;     
                laser[i].alive = false; 
                laser[i].x = 0;   
                laser[i].y = 0; 
            }

    //Player
             player.i = 1;
             player.hp = 1;
             player.dmg = 0;     
             player.alive = false; 
             player.x = WELL_WIDTH + 1;   
             player.y = WELL_HEIGHT - 1; 
}

/* Increase the score by value, and change to next level.
 */
void increase_score(u32 value)
{
  score += value;
  if (score >= 60 && score < 120 && level < 2) {
     next_level(2);
  }
  if (score >= 120 && score < 180 && level < 3) {
     next_level(3);
  }
  if (score >= 180 && level < 4) {
     next_level(4);
  }
} 

/* Try to move the player by dx and return true if successful.
 */
bool move(s8 dx)
{
    if (game_over)
        return false;

    if(!paused){
        if(dx < 0 && 2 < player.x){
    	    player.x += dx;
        }
        if(dx > 0 && player.x < WELL_WIDTH*2){
    	    player.x += dx;
        }
    }
    return true;
}

/* Try to move the player's lasers by 1 in y and return true if successful.
 */
bool move_playerlasers()
{
    u8 i, j;

    if (game_over)
       return false;

    if(!paused){
       for (i = 0; i < N_LASERS; i++) {
         if (laser[i].alive == true && laser[i].y > 1) { // Move lasers if they'are alive
           laser[i].y += -1;
           if (laser[i].y <= 1) { // Laser is not alive anymore
             laser[i].alive = false;
           }

           for (j = 0; j < N_ENEMYS; j++) { // If enemys collide with lasers
             if (enemy[j].alive == true) { // If enemy is alive
               if (laser[i].y == enemy[j].y && ((laser[i].x == enemy[j].x) || (laser[i].x == enemy[j].x + 1) || (laser[i].x + 1 == enemy[j].x))) {
                 enemy[j].hp += -laser[i].dmg;
                 laser[i].alive = false; // Laser is not alive anymore 
                 if (enemy[j].hp <= 0) {
                   increase_score(3);
                   enemy[j].alive = false; // Enemy is not alive anymore                   
                 }      
               }
             }
           } 
         }        
       }
    }
    return true;    
}

/* Try to move the enemys by 1 in y and return true if successful.
 */
bool move_enemys()
{
    u8 i;

    if (game_over)
       return false;

    if(!paused){
       for (i = 0; i < N_ENEMYS; i++) {
         if (enemy[i].alive == true && enemy[i].y < WELL_HEIGHT) { // Move enemys if they'are alive
           enemy[i].y += 1;
           if (enemy[i].y >= WELL_HEIGHT) { // Enemy is not alive anymore
             enemy[i].alive = false;
             switch(level) { // Increase score if level 2 or 4
             case 2:
             increase_score(3);
             break;
             case 4:
             increase_score(3);
             break;  
             }
           }
           // If player collides with enemy
           if (enemy[i].y == WELL_HEIGHT - 1) {
             if ((enemy[i].x == player.x) || (enemy[i].x == player.x + 1) || (enemy[i].x + 1 == player.x)) {
               game_over = true; // GAME OVER    
             }
           }   
         }        
       }
    }
    return true;    
}

/* Try to move the walls by 1 in y and return true if successful.
 */
bool move_walls()
{
    u8 i, j;

    if (game_over)
       return false;

    if(!paused){

       for (i = 0; i < N_WALLS; i++) {
         if (wall[i].alive == true && wall[i].y < WELL_HEIGHT) { // Move walls if they'are alive
           wall[i].y += 1;

           for (j = 0; j < N_ENEMYS; j++) { // If enemys collide with walls 
             if (enemy[j].alive == true) { // If enemy is alive
               switch(wall[i].i) { // If wall orientation is left or right
                 case 1: // Left
                     if (wall[i].x == enemy[j].x && wall[i].y == enemy[j].y) {
                       enemy[j].x += 3;       
                     }
                     if (wall[i].x + 1 == enemy[j].x && wall[i].y == enemy[j].y) {
                       enemy[j].x += 2;         
                     }
                     break;
                 case 2: // Right
                     if (wall[i].x == enemy[j].x + 1 && wall[i].y == enemy[j].y) {
                       enemy[j].x += -2;       
                     }
                     if (wall[i].x + 1 == enemy[j].x + 1 && wall[i].y == enemy[j].y) {
                       enemy[j].x += -3;         
                     }
                     break;
               } 
             }       
           } 
           // If player collides with walls
           if (wall[i].y == WELL_HEIGHT - 1) {
             if ((wall[i].x == player.x) || (wall[i].x == player.x + 1) || (wall[i].x + 1 == player.x)) {
               game_over = true; // GAME OVER    
             }
           }         
       
           if (wall[i].y >= WELL_HEIGHT) { // Wall is not alive anymore
             wall[i].alive = false;
             wall[i].x = 0;
             wall[i].y = 0;
           }
         }        
       }
    }
    return true;    
}

/* Spawns a player's laser.
 */
void spawn_playerlaser() 
{
   u8 i;
   if (!game_over && !paused) {

   for (i = 0; i < N_LASERS; i++) {
     if (laser[i].alive == false) { // Search for lasers that aren't alive
       laser[i].alive = true;
       laser[i].x = player.x;
       laser[i].y = WELL_HEIGHT - 2;
       break;
     }        
   }
   
   }
}

/* Spawns an enemy, with a differential dx.
 */
void spawn_enemy(s8 dx) 
{
   u8 i;
   u32 r = 0; // Random range

   if (!game_over && !paused) {

   switch(level) { // Select the range between walls for level
   case 1:
       r = rand(WELL_WIDTH - 3) + WELL_WIDTH - WELL_WIDTH/2 + 3 + dx;
       break;
   case 2:
       r = rand(WELL_WIDTH - WELL_WIDTH/4 - 2) + WELL_WIDTH - (WELL_WIDTH/2 - WELL_WIDTH/8) + 3 + dx;
       break;
   case 3:
       r = rand(WELL_WIDTH - WELL_WIDTH/4 - 2) + WELL_WIDTH - (WELL_WIDTH/2 - WELL_WIDTH/8) + 3 + dx;
       break;
   case 4:
       r = rand(WELL_WIDTH/2 - 3) + WELL_WIDTH - WELL_WIDTH/4 + 3 + dx;
       break;  
   }
   
   for (i = 0; i < N_ENEMYS; i++) {
     if (enemy[i].alive == false) { // Search for enemys that aren't alive
       enemy[i].alive = true;
       enemy[i].x = r;
       enemy[i].y = 2;
       switch(level) { // Select the range between walls for level
       case 1:
           enemy[i].hp = 2;
           enemy[i].dmg = 1;  
           break;
       case 2:
           enemy[i].hp = 999;
           enemy[i].dmg = 1;    
           break;
       case 3:
           enemy[i].hp = 2;
           enemy[i].dmg = 1;  
           break;
       case 4:
           enemy[i].hp = 999;
           enemy[i].dmg = 1; 
           break;  
       }
       break;
     }        
   }

   }
}

/* Spawns a wall in orientation left or rigth, and with a differential dx.
 */
void spawn_wall(u8 orientation, s8 dx) 
{
   u8 i;
   u32 dif = WELL_WIDTH/2;

   if (!game_over && !paused) {   

   for (i = 0; i < N_WALLS; i++) {
     if (wall[i].alive == false) { // Search for walls that aren't alive
       wall[i].alive = true;
        switch(level) { // Select the range between walls for level
        case 1:
            dif = WELL_WIDTH/2;
            break;
        case 2:
            dif = WELL_WIDTH/2 - WELL_WIDTH/8;
            break;
        case 3:
            dif = WELL_WIDTH/2 - WELL_WIDTH/8;
            break;
        case 4:
            dif = WELL_WIDTH/4;
            break;  
        }
       if (orientation == 0) { // If orientation equals left
         wall[i].i = 1; // Reset id
         wall[i].x += WELL_WIDTH - dif + 1 + dx;
       } else { // If orientation equals right
         wall[i].i = 2; // Reset id
         wall[i].x += WELL_WIDTH + dif + 1 + dx;
       }
       wall[i].y = 2;
       break;
     }        
   }

   }
}

/* Update the game state. Called at an interval relative to the current level.
 */
void update(void)
{
    move_playerlasers();
}

#define TITLE_X (COLS / 2 - 9)
#define TITLE_Y (ROWS / 2 - 1)

/* Draw about information in the centre. Shown on boot and pause. */
void draw_about(void) {
    puts(TITLE_X,      TITLE_Y,     BLACK,            RED,     "   ");
    puts(TITLE_X + 3,  TITLE_Y,     BLACK,            MAGENTA, "   ");
    puts(TITLE_X + 6,  TITLE_Y,     BLACK,            BLUE,    "   ");
    puts(TITLE_X + 9,  TITLE_Y,     BLACK,            GREEN,   "   ");
/*    puts(TITLE_X + 12, TITLE_Y,     BLACK,            YELLOW,  "   ");
    puts(TITLE_X + 15, TITLE_Y,     BLACK,            CYAN,    "   ");*/
    puts(TITLE_X,      TITLE_Y + 1, BRIGHT | RED,     RED,     " L ");
    puts(TITLE_X + 3,  TITLE_Y + 1, BRIGHT | MAGENTA, MAGENTA, " E ");
    puts(TITLE_X + 6,  TITLE_Y + 1, BRIGHT | BLUE,    BLUE,    " A ");
    puts(TITLE_X + 9,  TITLE_Y + 1, BRIGHT | GREEN,   GREEN,   " D ");
/*    puts(TITLE_X + 12, TITLE_Y + 1, BRIGHT | YELLOW,  YELLOW,  "   ");
    puts(TITLE_X + 15, TITLE_Y + 1, BRIGHT | CYAN,    CYAN,    "   ");*/
    puts(TITLE_X,      TITLE_Y + 2, BLACK,            RED,     "   ");
    puts(TITLE_X + 3,  TITLE_Y + 2, BLACK,            MAGENTA, "   ");
    puts(TITLE_X + 6,  TITLE_Y + 2, BLACK,            BLUE,    "   ");
    puts(TITLE_X + 9,  TITLE_Y + 2, BLACK,            GREEN,   "   ");
/*    puts(TITLE_X + 12, TITLE_Y + 2, BLACK,            YELLOW,  "   ");
    puts(TITLE_X + 15, TITLE_Y + 2, BLACK,            CYAN,    "   ");*/

    puts(0, ROWS - 1, BRIGHT | BLACK, BLACK,
         LEAD_NAME " " LEAD_VERSION " " LEAD_URL);
}

#define WELL_X (2)

#define STATUS_X (COLS * 3/4)
#define STATUS_Y (ROWS / 2 - 4)

#define SCORE_X STATUS_X
#define SCORE_Y (ROWS / 2 - 1)

#define LEVEL_X SCORE_X
#define LEVEL_Y (SCORE_Y + 4)

/* Draw the well, current tetrimino, its ghost, the preview tetrimino, the
 * status, score and level indicators. Each well/tetrimino cell is drawn one
 * screen-row high and two screen-columns wide. The top two rows of the well
 * are hidden. Rows in the cleared_rows array are drawn as white rather than
 * their actual colors. */
void draw(void)
{
    u8 x, y, i;

    if (paused) {
        draw_about();
        goto status;
    }

    // Border
    for (y = 2; y < WELL_HEIGHT; y++) {
        putc(WELL_X - 1,            y, BLACK, GRAY, ' ');
       // putc(COLS / 2 + 2,          y, BLACK, GRAY, ' ');
        putc(WELL_WIDTH*2 + 2,          y, BLACK, GRAY, ' ');
    }
    for (x = 0; x < WELL_WIDTH * 2 + 2; x++)
        putc(WELL_X + x - 1, WELL_HEIGHT, BLACK, GRAY, ' ');

    // Well 
    for (y = 0; y < 2; y++)
        for (x = 0; x < WELL_WIDTH; x++)
            puts(WELL_X + x * 2, y, BLACK, BLACK, "  ");
    for (y = 2; y < WELL_HEIGHT; y++)
        for (x = 0; x < WELL_WIDTH; x++)
            if (well[y][x])
                puts(WELL_X + x * 2, y, BLACK, well[y][x], "  ");
            else
                puts(WELL_X + x * 2, y, BRIGHT, BLACK, "::");

    // Player
     puts(player.x, WELL_HEIGHT - 1, BRIGHT, YELLOW, "^^");

    // Enemys
    for (i = 0; i < N_ENEMYS; i++) {
      if (enemy[i].alive == true) { // Draws enemys if they'are alive
        switch(level) { // Show enemys for level
        case 1:
            puts(enemy[i].x, enemy[i].y, RED, GRAY, "VV");
            break;
        case 2:
            puts(enemy[i].x, enemy[i].y, YELLOW, BLACK, "OO");
            break;
        case 3:
            puts(enemy[i].x, enemy[i].y, GRAY, BLUE, "XX");
            break;
        case 4:
            puts(enemy[i].x, enemy[i].y, YELLOW, GRAY, "S)");
            break;  
        }
      }        
    }

    // Player Lasers
    for (i = 0; i < N_LASERS; i++) {
      if (laser[i].alive == true) { // Draws lasers if they'are alive
        puts(laser[i].x, laser[i].y, RED, BLACK, "||");
      }        
    }

    // Walls
    for (i = 0; i < N_WALLS; i++) {
      if (wall[i].alive == true) { // Draws walls if they'are alive
        switch(level) { // Show walls for level
        case 1:
            puts(wall[i].x, wall[i].y, MAGENTA, RED, "[]");
            break;
        case 2:
            puts(wall[i].x, wall[i].y, BLACK, YELLOW, "[]");
            break;
        case 3:
            puts(wall[i].x, wall[i].y, RED, BLUE, "[]");
            break;
        case 4:
            puts(wall[i].x, wall[i].y, CYAN, MAGENTA, "[]");
            break;  
        }
      }        
    }

status:
    if (paused)
        puts(STATUS_X + 2, STATUS_Y, BRIGHT | YELLOW, BLACK, "PAUSED");
    if (game_over)
        puts(STATUS_X, STATUS_Y, BRIGHT | RED, BLACK, "GAME OVER");

    // Score 
    puts(SCORE_X + 7, SCORE_Y, BLUE, BLACK, "SCORE");
    puts(SCORE_X + 5, SCORE_Y + 2, BRIGHT | BLUE, BLACK, itoa(score, 10, 10));

    // Level 
    puts(LEVEL_X + 7, LEVEL_Y, BLUE, BLACK, "LEVEL");
    puts(LEVEL_X + 5, LEVEL_Y + 2, BRIGHT | BLUE, BLACK, itoa(level, 10, 10));
}


noreturn main()
{
    clear(BLACK);
    draw_about();
    puts(TITLE_X - 8,  TITLE_Y + 10, BLACK,            GREEN,   " Press any key to continue... ");

    /* Wait a full second to calibrate timing. */
    u32 itpms;
    u8 start_key = scan();
    tps();
    itpms = tpms; while (tpms == itpms) tps();
    itpms = tpms; while (tpms == itpms) tps();

    // Wait for a "press key to continue"
    while (1) {
      if ((start_key = scan())) {
       break;
      }
      tps();
    }

    // Inicialize game speed
    double speed_s = pow(0.8 - (10) * 0.007, (10));
    speed = speed_s * 1000;

        switch(start_key) {
        case KEY_1:
            next_level(1);
            break;
        case KEY_2:
            next_level(2);
            break;
        case KEY_3:
            next_level(3);
            break;
        case KEY_4:
            next_level(4);
            break;
        default:
            next_level(1);   
        }

    clear(BLACK);
    draw();

    bool debug = false, help = false;
    u8 last_key;
loop:
    tps();

    if (debug) {
        u32 i;
        puts(0,  0, BRIGHT | GREEN, BLACK, "RTC sec:");
        puts(10, 0, GREEN,          BLACK, itoa(rtcs(), 16, 2));
        puts(0,  1, BRIGHT | GREEN, BLACK, "ticks/ms:");
        puts(10, 1, GREEN,          BLACK, itoa(tpms, 10, 10));
        puts(0,  2, BRIGHT | GREEN, BLACK, "key:");
        puts(10, 2, GREEN,          BLACK, itoa(last_key, 16, 2));
        for (i = 0; i < TIMER__LENGTH; i++) {
            puts(0,  7 + i, BRIGHT | GREEN, BLACK, "timer:");
            puts(10, 7 + i, GREEN,          BLACK, itoa(timers[i], 10, 10));
        }
    }

    if (help) {
        puts(1, 12, BRIGHT | BLUE, BLACK, "LEFT");
        puts(7, 12, BLUE,          BLACK, "- Move left");
        puts(1, 13, BRIGHT | BLUE, BLACK, "RIGHT");
        puts(7, 13, BLUE,          BLACK, "- Move right");
        puts(1, 14, BRIGHT | BLUE, BLACK, "SPACE BAR");
        puts(7, 14, BLUE,          BLACK, "- Shoot");
        puts(1, 17, BRIGHT | BLUE, BLACK, "P");
        puts(7, 17, BLUE,          BLACK, "- Pause");
        puts(1, 18, BRIGHT | BLUE, BLACK, "D");
        puts(7, 18, BLUE,          BLACK, "- Toggle debug info");
        puts(1, 19, BRIGHT | BLUE, BLACK, "H");
        puts(7, 19, BLUE,          BLACK, "- Toggle help");
    }

  bool updated = false;
  if (level == 2 || level == 3 || level == 4) {
    if (cont_start_dx > 0) { // Update dx for spawn of walls and enemys once cont_start_dx reaches zero
      cont_start_dx += -1;
    } else {

      if (cont_wallspawn <= 0) { // If walls are ready to spawn
        cont_repeat += -1;

          switch(direction[cont_change]) { // Select direction
          case 0:
              dx += - 1;
              break;
          case 1:
              dx += 1;
              break;
          }

        if (cont_repeat <= 0) { // Change direction
          cont_repeat = REPEATMOVE;
          cont_change += 1;
          if (cont_change >= DIRECTIONSIZE) { // Finishes update for dx of walls and enemys 
            cont_change = 0;
            cont_start_dx = startwallchange;
          }
        }
      }
    }
  }
    if (cont_wallspawn > 0) { // Spawns a wall once counter reaches zero
      cont_wallspawn += -1;
    } else {
      cont_wallspawn = wallspawn;
      spawn_wall(0, dx);
      spawn_wall(1, dx);
      updated = true;
    }
    if (cont_enemyspawn > 0) { // Spawns an enemy once counter reaches zero
      cont_enemyspawn += -1;
    } else {
      cont_enemyspawn = enemyspawn;
      spawn_enemy(dx);
      updated = true;
    }
    if (cont_wallmove > 0) { // Moves walls once counter reaches zero
      cont_wallmove += -1;
    } else {
      cont_wallmove = wallmove;
      move_walls();
      updated = true;
    }
    if (cont_enemymove > 0) { // Moves enemys once counter reaches zero
      cont_enemymove += -1;
    } else {
      cont_enemymove = enemymove;
      move_enemys();
      updated = true;
    }

    u8 key;
    if ((key = scan())) {
        last_key = key;
        switch(key) {
        case KEY_D:
            debug = !debug;
            if (debug)
                help = false;
            clear(BLACK);
            break;
        case KEY_H:
            help = !help;
            if (help)
                debug = false;
            clear(BLACK);
            break;
        case KEY_LEFT:
            move(-1);
            break;
        case KEY_RIGHT:
            move(1);
            break;
        case KEY_SPACE:
            spawn_playerlaser();
            break;
        case KEY_P:
            if (game_over)
                break;
            clear(BLACK);
            paused = !paused;
            break;
        case KEY_1:
            if (game_over)
                break;
            clear(BLACK);
            paused = !paused;
            break;
        case KEY_2:
            if (game_over)
                break;
            clear(BLACK);
            paused = !paused;
            break;
        case KEY_3:
            if (game_over)
                break;
            clear(BLACK);
            paused = !paused;
            break;
        case KEY_4:
            if (game_over)
                break;
            clear(BLACK);
            paused = !paused;
            break;
        }
        updated = true;
    }

    if (!paused && !game_over && interval(TIMER_UPDATE, speed)) {
        update();
        updated = true;
    }

    if (updated) {
        draw();
    }

    goto loop;
}
