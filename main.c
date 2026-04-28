// main.c
// Matthew OS DOS-like shell build
// Original skeleton by maniek86, shell customization by Matthew Ahn

#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

u32 time_ms;

#include "memaccess.c"
#include "misc.c"
#include "ivt.c"
#include "math.c"
#include "disk.c"

#define COLS 80
#define ROWS 25
#define ATTR 0x0F
#define ATTR_TITLE 0x1F
#define ATTR_OK 0x0A
#define ATTR_ERR 0x0C
#define ATTR_DIM 0x08

static int cur_x = 0;
static int cur_y = 0;
static char current_drive = 'C';
static int echo_on = 1;
static u32 boot_ticks = 0;

// ---------------- low level screen ----------------
static void put_cell(int x, int y, char c, u8 attr) {
    if (x < 0 || x >= COLS || y < 0 || y >= ROWS) return;
    dispchar(chr((u8)c, attr), (u16)((y * COLS + x) * 2));
}

static void clear_screen(void) {
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) put_cell(x, y, ' ', ATTR);
    }
    cur_x = 0;
    cur_y = 0;
}

static void scroll(void) {
    // Simple clear-bottom scrolling approximation: redraw by clearing screen when full.
    // Avoids relying on libc/memmove in this tiny real-mode demo.
    clear_screen();
}

static void newline(void) {
    cur_x = 0;
    cur_y++;
    if (cur_y >= ROWS) scroll();
}

static void putc_attr(char c, u8 attr) {
    if (c == '\n') { newline(); return; }
    if (c == '\r') { cur_x = 0; return; }
    if (c == '\b') {
        if (cur_x > 0) {
            cur_x--;
            put_cell(cur_x, cur_y, ' ', attr);
        }
        return;
    }
    put_cell(cur_x, cur_y, c, attr);
    cur_x++;
    if (cur_x >= COLS) newline();
}

static void puts_attr(const char *s, u8 attr) {
    while (*s) putc_attr(*s++, attr);
}

static void puts(const char *s) { puts_attr(s, ATTR); }

static void draw_text_at(int x, int y, const char *s, u8 attr) {
    int i = 0;
    while (s[i]) {
        put_cell(x + i, y, s[i], attr);
        i++;
    }
}

static int strlen_s(const char *s) { int n = 0; while (s[n]) n++; return n; }

static int streq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int starts_with(const char *s, const char *prefix) {
    int i = 0;
    while (prefix[i]) {
        char ca = s[i];
        char cb = prefix[i];
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
        i++;
    }
    return 1;
}

static void trim_left(char **p) {
    while (**p == ' ') (*p)++;
}

static void print_dec(u32 v) {
    char buf[16];
    int i = 0;
    if (v == 0) { putc_attr('0', ATTR); return; }
    while (v > 0 && i < 15) {
        buf[i++] = '0' + (v % 10);
        v /= 10;
    }
    while (i--) putc_attr(buf[i], ATTR);
}

static void prompt(void) {
    putc_attr('\n', ATTR);
    putc_attr(current_drive, ATTR);
    puts(":\\>");
}

// ---------------- keyboard polling ----------------
static const char scancode_normal[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'', '`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',
};

static const char scancode_shift[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ',
};

static int shift_down = 0;
static int caps_on = 0;

static char get_key(void) {
    while (1) {
        if ((inb(0x64) & 1) == 0) continue;
        u8 sc = inb(0x60);

        if (sc == 0x2A || sc == 0x36) { shift_down = 1; continue; }
        if (sc == 0xAA || sc == 0xB6) { shift_down = 0; continue; }
        if (sc == 0x3A) { caps_on = !caps_on; continue; }
        if (sc & 0x80) continue;

        char c = shift_down ? scancode_shift[sc] : scancode_normal[sc];
        if (c >= 'a' && c <= 'z' && caps_on) c -= 32;
        if (c >= 'A' && c <= 'Z' && caps_on && shift_down) c += 32;
        if (c) return c;
    }
}

static void read_line(char *buf, int max) {
    int len = 0;
    while (1) {
        char c = get_key();
        if (c == '\n' || c == '\r') {
            buf[len] = 0;
            newline();
            return;
        }
        if (c == '\b') {
            if (len > 0) {
                len--;
                putc_attr('\b', ATTR);
            }
            continue;
        }
        if (c >= 32 && c <= 126 && len < max - 1) {
            buf[len++] = c;
            if (echo_on) putc_attr(c, ATTR);
        }
    }
}


// ---------------- Allison heart easter egg ----------------
// VGA text attribute: 0x0C = bright red foreground on black background.
// This keeps the old shell behavior intact and only changes the ALLISON output.
#define ATTR_HEART 0x0C

static void delay_line(void) {
    for (volatile u32 i = 0; i < 260000UL; i++) {
        // busy-wait delay for tiny real-mode demo; no libc/timer dependency
    }
}

static void puts_line_slow(const char *s, u8 attr) {
    puts_attr(s, attr);
    putc_attr('\n', attr);
    delay_line();
}

static void print_allison_heart(void) {
    puts_line_slow("       *****       *****       ", ATTR_HEART);
    puts_line_slow("     *********   *********     ", ATTR_HEART);
    puts_line_slow("   ************* *************   ", ATTR_HEART);
    puts_line_slow("  *****************************  ", ATTR_HEART);
    puts_line_slow(" ******************************* ", ATTR_HEART);
    puts_line_slow(" ******************************* ", ATTR_HEART);
    puts_line_slow("  *****************************  ", ATTR_HEART);
    puts_line_slow("   ***************************   ", ATTR_HEART);
    puts_line_slow("    *************************    ", ATTR_HEART);
    puts_line_slow("      *********************      ", ATTR_HEART);
    puts_line_slow("        *****************        ", ATTR_HEART);
    puts_line_slow("          *************          ", ATTR_HEART);
    puts_line_slow("            *********            ", ATTR_HEART);
    puts_line_slow("              *****              ", ATTR_HEART);
    puts_line_slow("                *                ", ATTR_HEART);
    puts_line_slow("", ATTR);
    puts_line_slow("        A   L      L      I   SSSSS  OOOOO  N   N", ATTR);
    puts_line_slow("       A A  L      L      I   S      O   O  NN  N", ATTR);
    puts_line_slow("      A   A L      L      I   SSSSS  O   O  N N N", ATTR);
    puts_line_slow("      AAAAA L      L      I       S  O   O  N  NN", ATTR);
    puts_line_slow("      A   A LLLLL  LLLLL  I   SSSSS  OOOOO  N   N", ATTR);
}

// ---------------- fake DOS commands ----------------
static void print_banner(void) {
    clear_screen();
    draw_text_at(40, 22, "4C 6F 76 65 20 79 6F 75 2C 20", ATTR_DIM);
    draw_text_at(50, 23, "41 6C 6C 69 73 6F 6E 2E", ATTR_DIM);
    puts_attr("============================= MATTHEW OS =============================", ATTR_TITLE);
    puts("\n");
    puts_attr("      ##     ##    ###    ######## ######## ##     ## ######## ##      ##\n", ATTR_OK);
    puts_attr("      ###   ###   ## ##      ##       ##    ##     ## ##       ##  ##  ##\n", ATTR_OK);
    puts_attr("      #### ####  ##   ##     ##       ##    ##     ## ##       ##  ##  ##\n", ATTR_OK);
    puts_attr("      ## ### ## #########    ##       ##    ######### ######   ##  ##  ##\n", ATTR_OK);
    puts_attr("      ##     ## ##     ##    ##       ##    ##     ## ##       ##  ##  ##\n", ATTR_OK);
    puts_attr("      ##     ## ##     ##    ##       ##    ##     ## ########  ###  ### \n", ATTR_OK);
    puts("\n");
    puts_attr("                  Matthew OS v0.3 - 486 Real Mode Shell\n", ATTR_TITLE);
    puts("                  Copyright (C) 2026 Matthew Ahn\n");
    puts("                  Type HELP for command list.\n");
}

static void cmd_help(void) {
    puts("Supported commands:\n");
    puts("  HELP        CLS         VER         DATE        TIME\n");
    puts("  DIR         TYPE        MEM         ECHO        ECHO ON/OFF\n");
    puts("  CD          CHDIR       MD          RD          COPY\n");
    puts("  DEL         REN         FORMAT      CHKDSK      SYS\n");
    puts("  REBOOT      COLOR       ABOUT       TREE        EXIT\n");
    puts("\nMost file commands are simulated because no FAT driver is implemented yet.\n");
}

static void cmd_dir(void) {
    puts(" Volume in drive "); putc_attr(current_drive, ATTR); puts(" is MATTHEW\n");
    puts(" Directory of "); putc_attr(current_drive, ATTR); puts(":\\\n\n");
    puts("COMMAND  COM       4096  04-27-26  12:00p\n");
    puts("KERNEL   BIN       8192  04-27-26  12:00p\n");
    puts("README   TXT       1337  04-27-26  12:00p\n");
    puts("ALLISON  EXE        512  04-27-26  12:00p\n");
    puts("        4 file(s)        14137 bytes\n");
    puts("        0 dir(s)       1457664 bytes free\n");
}

static void cmd_type(char *arg) {
    trim_left(&arg);
    if (streq(arg, "README.TXT") || streq(arg, "README")) {
        puts("Matthew OS is currently a bootable real-mode shell demo.\n");
        puts("Keyboard input and DOS-like commands are working.\n");
        puts("Disk files shown by DIR are simulated for now.\n");
    } else if (streq(arg, "ALLISON.EXE") || streq(arg, "ALLISON")) {
        print_allison_heart();
    } else {
        puts_attr("File not found - ", ATTR_ERR); puts(arg); puts("\n");
    }
}

static void cmd_mem(void) {
    puts("Memory Type        Total       Used       Free\n");
    puts("Conventional      640K        64K        576K\n");
    puts("Upper memory        0K         0K          0K\n");
    puts("Extended memory     N/A in this tiny demo\n");
}

static void cmd_chkdsk(void) {
    puts("The type of the file system is FAT12-like.\n");
    puts("Volume MATTHEW created by Makefile floppy image builder.\n");
    puts("1457664 bytes total disk space.\n");
    puts("No real FAT parser yet: report is simulated.\n");
}

static void cmd_tree(void) {
    puts("C:.\n");
    puts("+---DOS\n");
    puts("+---GAMES\n");
    puts("+---SYSTEM\n");
}

static void not_impl(const char *name) {
    puts(name);
    puts(": command accepted, but filesystem write support is not implemented yet.\n");
}

static void execute(char *cmd) {
    char *p = cmd;
    trim_left(&p);
    if (p[0] == 0) return;

    // Easter egg: exact command Allison / allison
    if (streq(p, "ALLISON")) { print_allison_heart(); return; }

    if (streq(p, "HELP") || streq(p, "?")) { cmd_help(); return; }
    if (streq(p, "CLS")) { clear_screen(); return; }
    if (streq(p, "VER")) { puts("Matthew OS Version 0.3\n"); return; }
    if (streq(p, "DATE")) { puts("Current date is Mon 04-27-2026\n"); return; }
    if (streq(p, "TIME")) { puts("Timer ticks since boot: "); print_dec(time_ms); puts(" ms\n"); return; }
    if (streq(p, "DIR")) { cmd_dir(); return; }
    if (starts_with(p, "TYPE ")) { cmd_type(p + 5); return; }
    if (streq(p, "MEM")) { cmd_mem(); return; }
    if (streq(p, "TREE")) { cmd_tree(); return; }
    if (streq(p, "CHKDSK")) { cmd_chkdsk(); return; }
    if (streq(p, "ABOUT")) { puts("Made for a 486 homebrew computer project.\n"); return; }
    if (streq(p, "EXIT")) { puts("Cannot exit: no parent OS exists.\n"); return; }
    if (streq(p, "REBOOT")) { puts("Rebooting...\n"); outb(0x64, 0xFE); while(1){} }

    if (starts_with(p, "ECHO OFF")) { echo_on = 0; return; }
    if (starts_with(p, "ECHO ON")) { echo_on = 1; return; }
    if (starts_with(p, "ECHO ")) { puts(p + 5); puts("\n"); return; }

    if (streq(p, "A:")) { current_drive = 'A'; return; }
    if (streq(p, "B:")) { current_drive = 'B'; return; }
    if (streq(p, "C:")) { current_drive = 'C'; return; }

    if (starts_with(p, "CD") || starts_with(p, "CHDIR")) { puts("Current directory is "); putc_attr(current_drive, ATTR); puts(":\\\n"); return; }
    if (starts_with(p, "MD ")) { not_impl("MD"); return; }
    if (starts_with(p, "RD ")) { not_impl("RD"); return; }
    if (starts_with(p, "COPY ")) { not_impl("COPY"); return; }
    if (starts_with(p, "DEL ")) { not_impl("DEL"); return; }
    if (starts_with(p, "REN ")) { not_impl("REN"); return; }
    if (starts_with(p, "FORMAT")) { puts_attr("FORMAT blocked. This demo will not erase disks.\n", ATTR_ERR); return; }
    if (starts_with(p, "SYS")) { not_impl("SYS"); return; }
    if (starts_with(p, "COLOR")) { puts("COLOR command recognized, but VGA attribute switching is minimal here.\n"); return; }

    puts_attr("Bad command or file name\n", ATTR_ERR);
}

// ---------------- interrupts from original skeleton ----------------
void nmi_handler() {
    __asm("pusha");
    outb(0x20, 0x20);
    __asm("popa;leave;iret");
}

void keyboard_hanlder() {
    __asm("pusha");
    outb(0x20, 0x20);
    __asm("popa;leave;iret");
}

void pic_handler() {
    __asm("pusha");
    time_ms = time_ms + 1;
    outb(0x20, 0x20);
    __asm("popa;leave;iret");
}

void interrupt_setup() {
    set_timer_hz(1000);
    ivt_set_callback(&nmi_handler, 2);
    ivt_set_callback(&pic_handler, 8);
    ivt_set_callback(&keyboard_hanlder, 9);
}

void main(void) {
    __asm("cli");
    interrupt_setup();
    __asm("sti");
    outb(0xe9, 'S');
    resetDisk(0);

    print_banner();
    char line[96];
    while (1) {
        prompt();
        read_line(line, sizeof(line));
        execute(line);
    }
}
