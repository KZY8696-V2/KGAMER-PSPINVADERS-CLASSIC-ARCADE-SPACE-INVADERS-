#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <stdlib.h>

PSP_MODULE_INFO("SpaceInvadersPSP", 0, 1, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272

typedef enum {
    STATE_MENU,
    STATE_HELP,
    STATE_PLAYING,
    STATE_PAUSE,
    STATE_GAMEOVER,
    STATE_WIN
} GameState;

typedef enum {
    FIRE_MODE_SEMI_AUTO,
    FIRE_MODE_AUTO,
    FIRE_MODE_MACROFIRE
} FireMode;

typedef struct {
    float x, y;
    int width, height;
    int active;
} Player;

typedef struct {
    float x, y;
    int width, height;
    int active;
    int dir;
} Alien;

typedef struct {
    float x, y;
    int active;
} Bullet;

typedef struct {
    int x, y;
    int health;
    int active;
} BunkerBlock;

// Exit callback for PSP Home button
int exit_callback(int arg1, int arg2, void *common) {
    sceKernelExitGame();
    return 0;
}

int callback_thread(SceSize args, void *argp) {
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

int setup_callbacks(void) {
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
    return thid;
}

int main(void) {
    setup_callbacks();
    pspDebugScreenInit();

    GameState state = STATE_MENU;
    int menu_selection = 0;
    int pause_selection = 0; 

    Player player = { 220.0f, 240.0f, 20, 10, 1 };
    Bullet bullet = { 0, 0, 0 };
    Bullet alien_bullet = { 0, 0, 0 };
    
    int shoot_cooldown = 0; 
    FireMode fire_mode = FIRE_MODE_SEMI_AUTO;
    int macrofire_delay = 0;

    // Kompakt ve yakın düzen: 3 Satır, 7 Sütun (Toplam 21 Uzaylı)
    #define ROWS 3
    #define COLS 7
    Alien aliens[ROWS][COLS];
    int score = 0;
    int lives = 3;

    #define BUNKER_COUNT 3
    #define BLOCKS_PER_BUNKER 6
    BunkerBlock bunkers[BUNKER_COUNT * BLOCKS_PER_BUNKER];

    SceCtrlData pad;
    unsigned int last_buttons = 0;

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    while (1) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~last_buttons;

        if (state == STATE_MENU) {
            if (pressed & PSP_CTRL_UP) {
                menu_selection--;
                if (menu_selection < 0) menu_selection = 2;
            }
            if (pressed & PSP_CTRL_DOWN) {
                menu_selection++;
                if (menu_selection > 2) menu_selection = 2;
            }
            if (pressed & PSP_CTRL_CROSS) {
                if (menu_selection == 0) {
                    player.x = 220.0f;
                    score = 0;
                    lives = 3;
                    bullet.active = 0;
                    alien_bullet.active = 0;
                    shoot_cooldown = 0;
                    fire_mode = FIRE_MODE_SEMI_AUTO;
                    macrofire_delay = 0;
                    
                    // Uzaylıları oyuncuya daha yakın yerleştir
                    for (int r = 0; r < ROWS; r++) {
                        for (int c = 0; c < COLS; c++) {
                            aliens[r][c].x = 100 + c * 40;
                            aliens[r][c].y = 50 + r * 20;
                            aliens[r][c].width = 14;
                            aliens[r][c].height = 10;
                            aliens[r][c].active = 1;
                            aliens[r][c].dir = 1;
                        }
                    }

                    int b_idx = 0;
                    int bunker_positions[3] = { 12, 27, 42 };
                    for (int i = 0; i < BUNKER_COUNT; i++) {
                        int bx = bunker_positions[i];
                        for (int b = 0; b < BLOCKS_PER_BUNKER; b++) {
                            bunkers[b_idx].x = bx + (b % 3);
                            bunkers[b_idx].y = 20 + (b / 3);
                            bunkers[b_idx].health = 3;
                            bunkers[b_idx].active = 1;
                            b_idx++;
                        }
                    }

                    state = STATE_PLAYING;
                } else if (menu_selection == 1) {
                    state = STATE_HELP;
                } else if (menu_selection == 2) {
                    sceKernelExitGame();
                }
            }
        }
        else if (state == STATE_HELP) {
            if ((pressed & PSP_CTRL_CIRCLE) || (pressed & PSP_CTRL_CROSS)) {
                state = STATE_MENU;
            }
        }
        else if (state == STATE_PLAYING) {
            if (pressed & PSP_CTRL_START) {
                pause_selection = 0;
                state = STATE_PAUSE;
            }

            if (pad.Buttons & PSP_CTRL_LEFT)  player.x -= 4.0f;
            if (pad.Buttons & PSP_CTRL_RIGHT) player.x += 4.0f;

            if (player.x < 10) player.x = 10;
            if (player.x > SCREEN_WIDTH - 30) player.x = SCREEN_WIDTH - 30;

            if (pressed & PSP_CTRL_TRIANGLE) {
                if (fire_mode == FIRE_MODE_SEMI_AUTO) {
                    fire_mode = FIRE_MODE_AUTO;
                } else if (fire_mode == FIRE_MODE_AUTO) {
                    fire_mode = FIRE_MODE_MACROFIRE;
                    macrofire_delay = 180; 
                } else {
                    fire_mode = FIRE_MODE_SEMI_AUTO;
                }
            }

            if (shoot_cooldown > 0) shoot_cooldown--;
            if (macrofire_delay > 0) macrofire_delay--;

            int want_to_shoot = 0;

            if (fire_mode == FIRE_MODE_SEMI_AUTO) {
                if ((pressed & PSP_CTRL_CROSS) && shoot_cooldown == 0) {
                    want_to_shoot = 1;
                    shoot_cooldown = 90; 
                }
            } 
            else if (fire_mode == FIRE_MODE_AUTO) {
                if (pad.Buttons & PSP_CTRL_CROSS) {
                    want_to_shoot = 1; 
                }
            } 
            else if (fire_mode == FIRE_MODE_MACROFIRE) {
                if (macrofire_delay == 0) {
                    want_to_shoot = 1; 
                }
            }

            if (want_to_shoot && !bullet.active) {
                bullet.x = player.x + player.width / 2;
                bullet.y = player.y;
                bullet.active = 1;
            }

            if (bullet.active) {
                bullet.y -= 8.0f;
                if (bullet.y < 0) bullet.active = 0;

                int bx = (int)(bullet.x / 8);
                int by = (int)(bullet.y / 10);

                for (int i = 0; i < BUNKER_COUNT * BLOCKS_PER_BUNKER; i++) {
                    if (bunkers[i].active && bunkers[i].x == bx && bunkers[i].y == by) {
                        bunkers[i].health--;
                        bullet.active = 0;
                        if (bunkers[i].health <= 0) {
                            bunkers[i].active = 0;
                        }
                    }
                }
            }

            if (alien_bullet.active) {
                alien_bullet.y += 4.0f;
                if (alien_bullet.y > SCREEN_HEIGHT) alien_bullet.active = 0;

                int abx = (int)(alien_bullet.x / 8);
                int aby = (int)(alien_bullet.y / 10);

                for (int i = 0; i < BUNKER_COUNT * BLOCKS_PER_BUNKER; i++) {
                    if (bunkers[i].active && bunkers[i].x == abx && bunkers[i].y == aby) {
                        bunkers[i].health--;
                        alien_bullet.active = 0;
                        if (bunkers[i].health <= 0) {
                            bunkers[i].active = 0;
                        }
                    }
                }

                if (alien_bullet.active && 
                    alien_bullet.x >= player.x && alien_bullet.x <= player.x + player.width &&
                    alien_bullet.y >= player.y && alien_bullet.y <= player.y + player.height) {
                    alien_bullet.active = 0;
                    lives--;
                    if (lives <= 0) {
                        state = STATE_GAMEOVER;
                    }
                }
            } else {
                int ar = rand() % ROWS;
                int ac = rand() % COLS;
                if (aliens[ar][ac].active) {
                    alien_bullet.x = aliens[ar][ac].x + aliens[ar][ac].width / 2;
                    alien_bullet.y = aliens[ar][ac].y;
                    alien_bullet.active = 1;
                }
            }

            int edge_hit = 0;
            for (int r = 0; r < ROWS; r++) {
                for (int c = 0; c < COLS; c++) {
                    if (!aliens[r][c].active) continue;
                    if (aliens[r][c].x > SCREEN_WIDTH - 40 || aliens[r][c].x < 20) {
                        edge_hit = 1;
                    }
                }
            }

            int remaining_aliens = 0;
            for (int r = 0; r < ROWS; r++) {
                for (int c = 0; c < COLS; c++) {
                    if (!aliens[r][c].active) continue;
                    remaining_aliens++;

                    if (edge_hit) aliens[r][c].dir *= -1;
                    aliens[r][c].x += 0.6f * aliens[r][c].dir;
                    if (edge_hit) aliens[r][c].y += 4.0f;

                    if (aliens[r][c].y >= player.y - 5) {
                        state = STATE_GAMEOVER;
                    }

                    if (bullet.active && 
                        bullet.x >= aliens[r][c].x && bullet.x <= aliens[r][c].x + aliens[r][c].width &&
                        bullet.y >= aliens[r][c].y && bullet.y <= aliens[r][c].y + aliens[r][c].height) {
                        aliens[r][c].active = 0;
                        bullet.active = 0;
                        score += 10;
                    }
                }
            }

            if (remaining_aliens == 0) {
                state = STATE_WIN;
            }
        }
        else if (state == STATE_PAUSE) {
            if (pressed & PSP_CTRL_UP) {
                pause_selection--;
                if (pause_selection < 0) pause_selection = 1;
            }
            if (pressed & PSP_CTRL_DOWN) {
                pause_selection++;
                if (pause_selection > 1) pause_selection = 1;
            }
            if (pressed & PSP_CTRL_CROSS) {
                if (pause_selection == 0) {
                    state = STATE_PLAYING; 
                } else if (pause_selection == 1) {
                    state = STATE_MENU; 
                }
            }
        }
        else if (state == STATE_GAMEOVER || state == STATE_WIN) {
            if (pressed & PSP_CTRL_START) {
                state = STATE_MENU;
            }
        }

        last_buttons = pad.Buttons;

        pspDebugScreenClear();
        pspDebugScreenSetXY(0, 0);

        if (state == STATE_MENU) {
            pspDebugScreenPrintf("\n\n");
            pspDebugScreenPrintf("       ====================================\n");
            pspDebugScreenPrintf("       *        PSP SPACE INVADERS        *\n");
            pspDebugScreenPrintf("       ====================================\n\n\n");
            
            pspDebugScreenPrintf("         %s START GAME\n\n", menu_selection == 0 ? ">" : " ");
            pspDebugScreenPrintf("         %s HOW TO PLAY\n\n", menu_selection == 1 ? ">" : " ");
            pspDebugScreenPrintf("         %s EXIT\n\n\n", menu_selection == 2 ? ">" : " ");
            
            pspDebugScreenPrintf("       [Up/Down]: Select   [X]: Confirm\n\n\n\n");
            pspDebugScreenPrintf(" 2026 KGAMER                        Space Invaders PSP 1.0 \n");
        } 
        else if (state == STATE_HELP) {
            pspDebugScreenPrintf("\n       === HOW TO PLAY ===\n\n");
            pspDebugScreenPrintf("       - Compact Area: 21 Aliens (3x7 Grid, Closer)\n");
            pspDebugScreenPrintf("       - Left/Right: Move  |  X: Shoot\n");
            pspDebugScreenPrintf("       - [Triangle]: Change Fire Mode (Semi / Auto / Macro)\n");
            pspDebugScreenPrintf("       - [START]: Pause / Return to Menu\n");
            pspDebugScreenPrintf("       - Destructible bunkers and 3 lives system.\n\n");
            pspDebugScreenPrintf("       [O / X] Go Back\n");
        }
        else if (state == STATE_PLAYING) {
            const char* mode_name = "Semi-Auto";
            if (fire_mode == FIRE_MODE_AUTO) mode_name = "Auto";
            else if (fire_mode == FIRE_MODE_MACROFIRE) {
                if (macrofire_delay > 0) mode_name = "Macro(Warming)";
                else mode_name = "Macrofire(AUTO)";
            }

            pspDebugScreenPrintf("Score: %-3d | Lives: %d | Mode: %s\n\n", score, lives, mode_name);

            // Daha küçük / kompakt bir oynanış alanı (örneğin ortaya odaklı dar alan)
            for (int y = 0; y < 24; y++) {
                pspDebugScreenPrintf("    "); // Sol boşluk (daraltılmış alan)
                for (int x = 10; x < 50; x++) {
                    char ch = ' ';
                    
                    int px = (int)(player.x / 8);
                    int py = (int)(player.y / 10);
                    if (x >= px && x < px + 3 && y == py) ch = 'A';

                    if (bullet.active) {
                        int bx = (int)(bullet.x / 8);
                        int by = (int)(bullet.y / 10);
                        if (x == bx && y == by) ch = '|';
                    }

                    if (alien_bullet.active) {
                        int abx = (int)(alien_bullet.x / 8);
                        int aby = (int)(alien_bullet.y / 10);
                        if (x == abx && y == aby) ch = 'v';
                    }

                    for (int r = 0; r < ROWS; r++) {
                        for (int c = 0; c < COLS; c++) {
                            if (aliens[r][c].active) {
                                int ax = (int)(aliens[r][c].x / 8);
                                int ay = (int)(aliens[r][c].y / 10);
                                if (x >= ax && x < ax + 2 && y == ay) ch = 'W';
                            }
                        }
                    }

                    for (int i = 0; i < BUNKER_COUNT * BLOCKS_PER_BUNKER; i++) {
                        if (bunkers[i].active && bunkers[i].x == x && bunkers[i].y == y) {
                            if (bunkers[i].health == 3) ch = '#';
                            else if (bunkers[i].health == 2) ch = 'H';
                            else ch = '.';
                        }
                    }

                    pspDebugScreenPrintf("%c", ch);
                }
                pspDebugScreenPrintf("\n");
            }
        }
        else if (state == STATE_PAUSE) {
            pspDebugScreenPrintf("\n\n\n");
            pspDebugScreenPrintf("           #############################\n");
            pspDebugScreenPrintf("           #        GAME PAUSED        #\n");
            pspDebugScreenPrintf("           #############################\n\n\n");
            pspDebugScreenPrintf("         %s RESUME GAME\n\n", pause_selection == 0 ? ">" : " ");
            pspDebugScreenPrintf("         %s MAIN MENU\n\n\n", pause_selection == 1 ? ">" : " ");
            pspDebugScreenPrintf("       [Up/Down]: Select   [X]: Confirm\n");
        }
        else if (state == STATE_GAMEOVER) {
            pspDebugScreenPrintf("\n\n\n");
            pspDebugScreenPrintf("           #############################\n");
            pspDebugScreenPrintf("           #        GAME OVER!         #\n");
            pspDebugScreenPrintf("           #       Your Score: %-5d     #\n", score);
            pspDebugScreenPrintf("           #############################\n\n");
            pspDebugScreenPrintf("       Press [START] to return to Main Menu.\n");
        }
        else if (state == STATE_WIN) {
            pspDebugScreenPrintf("\n\n\n");
            pspDebugScreenPrintf("           #############################\n");
            pspDebugScreenPrintf("           #      YOU WIN, CONGRATS!   #\n");
            pspDebugScreenPrintf("           #       Your Score: %-5d     #\n", score);
            pspDebugScreenPrintf("           #############################\n\n");
            pspDebugScreenPrintf("       Press [START] to return to Main Menu.\n");
        }

        sceDisplayWaitVblankStart();
    }

    return 0;
}
