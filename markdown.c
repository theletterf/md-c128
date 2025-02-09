#include <stdio.h>
#include <string.h>
#include <conio.h>
#include <c128.h>
#include <peekpoke.h>
#include <cbm.h>

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define MAX_LINE_LENGTH 80
#define HEADER_LINES 2    // Number of lines used by header
#define STATUS_LINE 24    // Last line of screen
#define MAX_LINES 21      // 25 - HEADER_LINES - 1 spacing - 1 status line

// VDC Color codes for 80-column mode with white background
#define MD_NORMAL_COLOR   0      // Black for normal text
#define MD_BOLD_COLOR     6      // Dark blue for bold
#define MD_HEADER_COLOR   8      // Dark gray for H1
#define MD_HEADER2_COLOR  4      // Purple for H2
#define MD_ITALIC_COLOR   2      // Red for italic
#define MD_MONO_COLOR     5      // Dark green for code
#define MD_BACKGROUND     1      // White background

// Screen buffer
char screen_buffer[MAX_LINES][MAX_LINE_LENGTH];
unsigned char cursor_x = 0;
unsigned char cursor_y = 0;

// C128 keyboard matrix locations for 80-column mode
#define KBD_MATRIX_ROW    0xD6   // Keyboard row select
#define KBD_MATRIX_COL    0xD4   // Keyboard column read
#define KBD_SHIFT_REG     0xD3   // Shift key register

// Function prototypes
void init_screen(void);
void format_current_line(void);
void handle_input(void);
unsigned char check_shift(void);
unsigned char get_key(void);
unsigned char __fastcall__ kbhit(void);
void draw_header(void);
void draw_status_line(void);
void save_file(void);
void load_file(void);
void draw_dialog(const char *title, unsigned char width, unsigned char height);
void draw_file_list(struct file_entry *files, unsigned char file_count, unsigned char selected,
                    unsigned char start_x, unsigned char start_y, unsigned char height);
void apply_formatting(void);
void format_line_without_cursor(unsigned char line_num);
void new_file(void);

// Function implementations
void draw_header(void) {
    unsigned char center_pos;
    
    // Clear the top two lines
    textcolor(MD_NORMAL_COLOR);
    gotoxy(0, 0);
    cclear(SCREEN_WIDTH);
    gotoxy(0, 1);
    cclear(SCREEN_WIDTH);
    
    // Center and draw the title
    textcolor(MD_BOLD_COLOR);
    center_pos = (SCREEN_WIDTH - 33) / 2;  // 33 is length of title
    gotoxy(center_pos, 0);
    cputs("--=== Markdown Editor for C128 ===--");
    
    // Center and draw the copyright
    textcolor(MD_NORMAL_COLOR);
    center_pos = (SCREEN_WIDTH - 31) / 2;  // 31 is length of copyright
    gotoxy(center_pos, 1);
    cputs("(c) 2025 Fabrizio Ferri Benedetti");
}

void init_screen(void) {
    // Switch to 80-column mode
    videomode(VIDEOMODE_80COL);
    
    // Set up colors
    textcolor(MD_NORMAL_COLOR);
    bgcolor(MD_BACKGROUND);
    
    // Clear screen and buffer
    clrscr();
    memset(screen_buffer, 0, sizeof(screen_buffer));
    
    // Enable cursor and update VDC pointer
    cursor(1);
    *((unsigned int*)0x0012) = 0xbb80 + (HEADER_LINES + 1) * 80;  // Adjust for 80 columns
    
    // Draw the header and status line
    draw_header();
    draw_status_line();
    
    // Move cursor to start of editing area
    cursor_x = 0;
    cursor_y = 0;
    gotoxy(0, HEADER_LINES + 1);
}

void format_current_line(void) {
    char *line = screen_buffer[cursor_y];
    unsigned char i = 0;
    
    gotoxy(0, cursor_y + HEADER_LINES + 1);
    
    while(i < MAX_LINE_LENGTH && line[i] != '\0') {
        if(line[i] == '*' && line[i+1] == '*') {
            textcolor(MD_BOLD_COLOR);
            cputc('*'); cputc('*');
            i += 2;
            while(i < MAX_LINE_LENGTH && line[i] != '\0') {
                if(line[i] == '*' && line[i+1] == '*') {
                    cputc('*'); cputc('*');
                    textcolor(MD_NORMAL_COLOR);
                    i += 2;
                    break;
                }
                cputc(line[i++]);
            }
        }
        else if(line[i] == '*') {
            textcolor(MD_ITALIC_COLOR);
            cputc('*');
            i++;
            while(i < MAX_LINE_LENGTH && line[i] != '\0') {
                if(line[i] == '*') {
                    cputc('*');
                    textcolor(MD_NORMAL_COLOR);
                    i++;
                    break;
                }
                cputc(line[i++]);
            }
        }
        else if(line[i] == '\'') {
            textcolor(MD_MONO_COLOR);
            cputc('\'');
            i++;
            while(i < MAX_LINE_LENGTH && line[i] != '\0') {
                if(line[i] == '\'') {
                    cputc('\'');
                    textcolor(MD_NORMAL_COLOR);
                    i++;
                    break;
                }
                textcolor(MD_MONO_COLOR);
                cputc(line[i++]);
            }
        }
        else if(line[i] == '#' && line[i+1] == '#' && (i == 0 || line[i-1] == ' ')) {
            textcolor(MD_HEADER2_COLOR);
            while(i < MAX_LINE_LENGTH && line[i] != '\0') {
                cputc(line[i++]);
            }
        }
        else if(line[i] == '#' && (i == 0 || line[i-1] == ' ')) {
            textcolor(MD_HEADER_COLOR);
            while(i < MAX_LINE_LENGTH && line[i] != '\0') {
                cputc(line[i++]);
            }
        }
        else {
            textcolor(MD_NORMAL_COLOR);
            cputc(line[i++]);
        }
    }
    
    // Clear to end of line
    cclear(MAX_LINE_LENGTH - strlen(line));
    
    // Position cursor for next input
    gotoxy(cursor_x, cursor_y + HEADER_LINES + 1);
}

void wrap_line(unsigned char line_num) {
    char *current_line = screen_buffer[line_num];
    char *next_line;
    unsigned char i, last_space = 0;
    
    // If we're at the last line or line is short enough, return
    if(line_num >= MAX_LINES - 1 || strlen(current_line) < MAX_LINE_LENGTH - 1) {
        return;
    }
    
    // Find last space before width limit
    for(i = 0; i < MAX_LINE_LENGTH - 1; i++) {
        if(current_line[i] == ' ') {
            last_space = i;
        }
    }
    
    // If no space found, force wrap at width limit
    if(last_space == 0) {
        last_space = MAX_LINE_LENGTH - 1;
    }
    
    // Move remaining lines down
    for(i = MAX_LINES - 1; i > line_num + 1; i--) {
        strcpy(screen_buffer[i], screen_buffer[i-1]);
    }
    
    // Move wrapped content to next line
    next_line = screen_buffer[line_num + 1];
    strcpy(next_line, &current_line[last_space + 1]);
    current_line[last_space] = '\0';
}

unsigned char get_key(void) {
    __asm__ ("jsr $FFE4");
    __asm__ ("sta $02");  // Store result in zero page temporarily
    return *(unsigned char*)0x02;
}

unsigned char __fastcall__ kbhit(void) {
    __asm__("jsr $FFE1");  // STOP key scan
    __asm__("cmp #$00");
    __asm__("beq %g", done);
    __asm__("lda #$ff");
done:
    __asm__("ldx #$00");
    return __A__;
}

void handle_input(void) {
    char key;
    char *current_line;
    unsigned char shift;
    
    key = cgetc();
    current_line = screen_buffer[cursor_y];
    shift = PEEK(211);
    
    // Hide cursor before processing
    cursor(0);
    
    switch(key) {
        case CH_ENTER:
            if(cursor_y < MAX_LINES - 1) {
                cursor_y++;
                cursor_x = 0;
            }
            break;
            
        case CH_DEL:
            if(cursor_x > 0) {
                cursor_x--;
                current_line[cursor_x] = '\0';
            }
            else if(cursor_y > 0) {  // At start of line and not first line
                // Move to end of previous line
                cursor_y--;
                current_line = screen_buffer[cursor_y];
                cursor_x = strlen(current_line);
                
                // Only delete if we won't exceed line length
                if(cursor_x < MAX_LINE_LENGTH - 1) {
                    current_line[cursor_x] = '\0';
                }
            }
            break;
            
        case CH_CURS_LEFT:
            if(cursor_x > 0) cursor_x--;
            break;
            
        case CH_CURS_RIGHT:
            if(cursor_x < MAX_LINE_LENGTH - 1 && current_line[cursor_x] != '\0') cursor_x++;
            break;
            
        case CH_CURS_UP:
            if(cursor_y > 0) cursor_y--;
            break;
            
        case CH_CURS_DOWN:
            if(cursor_y < MAX_LINES - 1) cursor_y++;
            break;
            
        case CH_F1:  // F1 to save
            save_file();
            break;
            
        case CH_F3:  // F3 to load
            load_file();
            break;
            
        case CH_F5:  // F5 for new file
            new_file();
            break;
            
        default:
            if(cursor_x < MAX_LINE_LENGTH - 1) {
                // Just store whatever character we get
                current_line[cursor_x] = key;
                cursor_x++;
                current_line[cursor_x] = '\0';
            }
            break;
    }
    
    // Ensure cursor stays within bounds
    if(cursor_y >= MAX_LINES) {
        cursor_y = MAX_LINES - 1;
    }
    
    format_current_line();
    draw_status_line();
    
    // Position cursor and show it only in editing area
    gotoxy(cursor_x, cursor_y + HEADER_LINES + 1);
    cursor(1);
}

unsigned char check_shift(void) {
    unsigned char status;
    
    // C128 shift key status is in location 211 ($D3)
    status = PEEK(211);
    
    // Bit 1 is set when either shift key is pressed
    return (status & 0x01);
}

void draw_status_line(void) {
    cursor(0);  // Hide cursor while drawing status
    gotoxy(0, STATUS_LINE);
    textcolor(MD_HEADER_COLOR);
    cputs("F1:Save  F3:Load  F5:New  F7:Help         Line:");
    cprintf(" %d/%d", cursor_y + 1, MAX_LINES);
    // Don't re-enable cursor here
}

void draw_dialog(const char *title, unsigned char width, unsigned char height) {
    unsigned char start_x = (SCREEN_WIDTH - width) / 2;
    unsigned char start_y = (25 - height) / 2;
    unsigned char i;
    
    // Save colors
    unsigned char old_color = textcolor(MD_NORMAL_COLOR);
    
    // Draw top border
    gotoxy(start_x, start_y);
    cputc('+');
    for(i = 0; i < width-2; i++) cputc('-');
    cputc('+');
    
    // Draw title
    gotoxy(start_x + (width - strlen(title))/2, start_y);
    cputs(title);
    
    // Draw sides and clear middle
    for(i = 1; i < height-1; i++) {
        gotoxy(start_x, start_y + i);
        cputc('|');
        cclear(width-2);
        gotoxy(start_x + width-1, start_y + i);
        cputc('|');
    }
    
    // Draw bottom border
    gotoxy(start_x, start_y + height-1);
    cputc('+');
    for(i = 0; i < width-2; i++) cputc('-');
    cputc('+');
    
    // Restore color
    textcolor(old_color);
}

void save_file(void) {
    unsigned char i;
    FILE *fp;
    char filename[17] = "md.txt";
    char c;
    unsigned char pos = 0;
    unsigned char dialog_width = 40;
    unsigned char dialog_height = 5;
    unsigned char start_x = (SCREEN_WIDTH - dialog_width) / 2;
    unsigned char start_y = (25 - dialog_height) / 2;
    
    // Draw save dialog
    draw_dialog("Save File", dialog_width, dialog_height);
    
    // Show input prompt
    gotoxy(start_x + 2, start_y + 2);
    textcolor(MD_NORMAL_COLOR);  // Set color for input
    cputs("Filename: ");
    cursor(1);  // Show cursor during input
    
    // Input filename
    while(1) {
        gotoxy(start_x + 12 + pos, start_y + 2);  // Keep cursor position updated
        c = cgetc();
        if(c == CH_ENTER) {
            filename[pos] = '\0';
            break;
        }
        else if(c == CH_DEL && pos > 0) {
            --pos;
            gotoxy(start_x + 12 + pos, start_y + 2);
            cputc(' ');
        }
        else if(c >= 32 && c <= 126 && pos < 15) {
            filename[pos] = c;
            cputc(c);
            ++pos;
        }
    }
    cursor(0);  // Hide cursor after input
    
    // Open file for writing
    fp = fopen(filename, "w");
    if(fp == NULL) {
        draw_dialog("Error", dialog_width, dialog_height);
        gotoxy(start_x + 2, start_y + 2);
        textcolor(2);  // Red
        cputs("Could not create file!");
        cgetc();  // Wait for key
        draw_status_line();
        return;
    }
    
    // Write each line to file
    for(i = 0; i < MAX_LINES; i++) {
        if(screen_buffer[i][0] != '\0') {
            fprintf(fp, "%s\n", screen_buffer[i]);
        } else if(i < cursor_y) {
            // Write empty line if there was a newline here
            fprintf(fp, "\n");
        }
    }
    
    fclose(fp);
    
    // Show success dialog
    draw_dialog("Success", dialog_width, dialog_height);
    gotoxy(start_x + 2, start_y + 2);
    textcolor(5);  // Green
    cputs("File saved: ");
    cputs(filename);
    cputs("\nPress any key...");
    cgetc();
    
    // Reload the file to ensure consistency
    fp = fopen(filename, "r");
    if(fp != NULL) {
        // Clear current buffer
        memset(screen_buffer, 0, sizeof(screen_buffer));
        cursor_x = cursor_y = 0;
        
        // Read file into buffer
        i = 0;
        while(i < MAX_LINES && fgets(screen_buffer[i], MAX_LINE_LENGTH, fp)) {
            // Remove newline if present
            unsigned char len = strlen(screen_buffer[i]);
            if(len > 0 && screen_buffer[i][len-1] == '\n') {
                screen_buffer[i][len-1] = '\0';
            }
            i++;
        }
        fclose(fp);
        
        // Redraw screen
        clrscr();
        draw_header();
        for(i = 0; i < MAX_LINES; i++) {
            if(screen_buffer[i][0] != '\0' || i < cursor_y) {
                cursor_y = i;
                format_current_line();
            }
        }
        cursor_y = 0;
        draw_status_line();
    }
}

#define MAX_FILES 50
#define MAX_FILENAME 17

struct file_entry {
    char name[MAX_FILENAME];
    unsigned int size;
};

void draw_file_list(struct file_entry *files, unsigned char file_count, unsigned char selected,
                    unsigned char start_x, unsigned char start_y, unsigned char height) {
    unsigned char i;
    unsigned char display_count = height - 4;  // Account for dialog borders and header
    unsigned char start_idx = (selected / display_count) * display_count;
    
    // Clear file list area
    for(i = 0; i < display_count; i++) {
        gotoxy(start_x + 2, start_y + 2 + i);
        cclear(36);  // Clear line within dialog
    }
    
    // Draw visible files
    for(i = 0; i < display_count && (i + start_idx) < file_count; i++) {
        gotoxy(start_x + 2, start_y + 2 + i);
        if(i + start_idx == selected) {
            revers(1);
            textcolor(MD_BOLD_COLOR);
        } else {
            revers(0);
            textcolor(MD_NORMAL_COLOR);
        }
        cprintf("%-32s", files[i + start_idx].name);
    }
    revers(0);
}

void apply_formatting(void) {
    unsigned char i;
    cursor(0);  // Hide cursor during formatting
    
    // First pass: load plain text
    for(i = 0; i < MAX_LINES; i++) {
        if(screen_buffer[i][0] != '\0') {
            revers(0);
            textcolor(MD_NORMAL_COLOR);
            gotoxy(0, i + HEADER_LINES + 1);
            cputs(screen_buffer[i]);
            cclear(MAX_LINE_LENGTH - strlen(screen_buffer[i]));
        }
    }
    
    // Second pass: apply formatting
    for(i = 0; i < MAX_LINES; i++) {
        if(screen_buffer[i][0] != '\0') {
            cursor_y = i;
            cursor_x = strlen(screen_buffer[i]);  // Move to end of line
            format_current_line();
        }
    }
    
    // Position cursor at end of text
    cursor_y = 0;
    for(i = 0; i < MAX_LINES; i++) {
        if(screen_buffer[i][0] != '\0') {
            cursor_y = i;
        }
    }
    cursor_x = strlen(screen_buffer[cursor_y]);
    gotoxy(cursor_x, cursor_y + HEADER_LINES + 1);
    cursor(1);  // Show cursor again
}

void format_line_without_cursor(unsigned char line_num) {
    char *line = screen_buffer[line_num];
    unsigned char i = 0;
    
    gotoxy(0, line_num + HEADER_LINES + 1);
    revers(0);  // Ensure reverse is off
    
    while(i < MAX_LINE_LENGTH && line[i] != '\0') {
        if(line[i] == '*' && line[i+1] == '*') {
            textcolor(MD_BOLD_COLOR);
            cputc('*'); cputc('*');
            i += 2;
            while(i < MAX_LINE_LENGTH && line[i] != '\0') {
                if(line[i] == '*' && line[i+1] == '*') {
                    cputc('*'); cputc('*');
                    textcolor(MD_NORMAL_COLOR);
                    i += 2;
                    break;
                }
                cputc(line[i++]);
            }
        }
        else if(line[i] == '*') {
            textcolor(MD_ITALIC_COLOR);
            cputc('*');
            i++;
            while(i < MAX_LINE_LENGTH && line[i] != '\0') {
                if(line[i] == '*') {
                    cputc('*');
                    textcolor(MD_NORMAL_COLOR);
                    i++;
                    break;
                }
                cputc(line[i++]);
            }
        }
        else if(line[i] == '\'') {
            textcolor(MD_MONO_COLOR);
            cputc('\'');
            i++;
            while(i < MAX_LINE_LENGTH && line[i] != '\0') {
                if(line[i] == '\'') {
                    cputc('\'');
                    textcolor(MD_NORMAL_COLOR);
                    i++;
                    break;
                }
                textcolor(MD_MONO_COLOR);
                cputc(line[i++]);
            }
        }
        else if(line[i] == '#' && line[i+1] == '#' && (i == 0 || line[i-1] == ' ')) {
            textcolor(MD_HEADER2_COLOR);
            while(i < MAX_LINE_LENGTH && line[i] != '\0') {
                cputc(line[i++]);
            }
        }
        else if(line[i] == '#' && (i == 0 || line[i-1] == ' ')) {
            textcolor(MD_HEADER_COLOR);
            while(i < MAX_LINE_LENGTH && line[i] != '\0') {
                cputc(line[i++]);
            }
        }
        else {
            textcolor(MD_NORMAL_COLOR);
            cputc(line[i++]);
        }
    }
    
    // Clear to end of line
    cclear(MAX_LINE_LENGTH - strlen(line));
}

void load_file(void) {
    struct file_entry files[MAX_FILES];
    unsigned char file_count = 0;
    unsigned char selected = 0;
    char c;
    FILE *fp;
    unsigned char i;
    unsigned char dialog_width = 40;
    unsigned char dialog_height = 15;
    unsigned char start_x = (SCREEN_WIDTH - dialog_width) / 2;
    unsigned char start_y = (25 - dialog_height) / 2;
    
    // Read directory
    cbm_opendir(8, 8);
    while(file_count < MAX_FILES) {
        struct cbm_dirent entry;
        if(cbm_readdir(8, &entry) != 0) break;
        if(entry.type == CBM_T_PRG || entry.type == CBM_T_SEQ) {
            // Check if file ends with .md
            unsigned char len = strlen(entry.name);
            if(len > 3 && strcmp(entry.name + len - 3, ".md") == 0) {
                strcpy(files[file_count].name, entry.name);
                files[file_count].size = entry.size;
                file_count++;
            }
        }
    }
    cbm_closedir(8);
    
    if(file_count == 0) {
        draw_dialog("Error", dialog_width, 5);
        gotoxy(start_x + 2, start_y + 2);
        textcolor(2);  // Red
        cputs("No .md files found!");
        cgetc();
        draw_status_line();
        return;
    }
    
    // Draw file browser dialog
    draw_dialog("Load File", dialog_width, dialog_height);
    draw_file_list(files, file_count, selected, start_x, start_y, dialog_height);
    
    // Handle navigation
    while(1) {
        c = cgetc();
        switch(c) {
            case CH_CURS_UP:
                if(selected > 0) {
                    selected--;
                    draw_file_list(files, file_count, selected, start_x, start_y, dialog_height);
                }
                break;
                
            case CH_CURS_DOWN:
                if(selected < file_count - 1) {
                    selected++;
                    draw_file_list(files, file_count, selected, start_x, start_y, dialog_height);
                }
                break;
                
            case CH_ENTER:
                // Load selected file
                fp = fopen(files[selected].name, "r");
                if(fp == NULL) {
                    draw_dialog("Error", dialog_width, 5);
                    gotoxy(start_x + 2, start_y + 2);
                    textcolor(2);
                    cputs("Could not open file!");
                    cgetc();
                    return;
                }
                
                // Read file into buffer
                i = 0;
                while(i < MAX_LINES && fgets(screen_buffer[i], MAX_LINE_LENGTH, fp)) {
                    // Remove newline if present
                    unsigned char len = strlen(screen_buffer[i]);
                    if(len > 0 && screen_buffer[i][len-1] == '\n') {
                        screen_buffer[i][len-1] = '\0';
                    }
                    i++;
                }
                fclose(fp);
                
                // Clear screen and redraw without cursor
                cursor(0);  // Hide cursor during redraw
                clrscr();
                draw_header();
                
                // Format all lines without cursor
                for(i = 0; i < MAX_LINES; i++) {
                    if(screen_buffer[i][0] != '\0') {
                        format_line_without_cursor(i);
                    }
                }
                
                // Now position cursor and show it
                cursor_x = 0;
                cursor_y = 0;
                gotoxy(0, HEADER_LINES + 1);
                cursor(1);
                
                draw_status_line();
                return;
                
            case CH_ESC:
                draw_status_line();
                return;
        }
    }
}

void new_file(void) {
    unsigned char i;
    unsigned char dialog_width = 40;
    unsigned char dialog_height = 5;
    unsigned char start_x = (SCREEN_WIDTH - dialog_width) / 2;
    unsigned char start_y = (25 - dialog_height) / 2;
    char c;
    
    // Show confirmation dialog
    draw_dialog("New File", dialog_width, dialog_height);
    gotoxy(start_x + 2, start_y + 2);
    textcolor(2);  // Red for warning
    cputs("Clear all text? (Y/N)");
    
    // Get confirmation
    c = cgetc();
    if(c != 'y' && c != 'Y') {
        // Redraw screen and return
        clrscr();
        draw_header();
        for(i = 0; i < MAX_LINES; i++) {
            if(screen_buffer[i][0] != '\0') {
                cursor_y = i;
                format_current_line();
            }
        }
        draw_status_line();
        return;
    }
    
    // Clear buffer
    memset(screen_buffer, 0, sizeof(screen_buffer));
    
    // Reset screen
    clrscr();
    draw_header();
    draw_status_line();
    
    // Reset cursor position
    cursor_x = 0;
    cursor_y = 0;
    gotoxy(0, HEADER_LINES + 1);
}

int main(void) {
    init_screen();
    
    while(1) {
        handle_input();
    }
    
    return 0;
}