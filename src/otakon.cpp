// compilation command: "g++ ./src/otakon.cpp -o ./bin/otakon -lpng"

#include <linux/limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h> // necessary for atoi()
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <png++/png.hpp>  //https://www.nongnu.org/pngpp/doc/
#include <memory>
#include <string>
#include <stdexcept>
#include <time.h> // needed for initializing srand()
//#include <dirent.h> // necessary for directory control.
#include <errno.h>
#include <sys/types.h> // necessary for directory control.
#include <sys/stat.h>

using namespace png;

//#define SPEECH_FILE_PATH "images/otakon_speech.txt"
#define SPRITE_FILE_PATH "../images"
#define DEFAULT_SPRITE_FILE_PATH "../images/default.png"
#define CONFIG_FILE_PATH "../config/config.txt"
#define CONFIG_SWAP_FILE_PATH "../config/cfg_swp"
#define DOCS_DIR_PATH "../docs/"
#define COUNTER_FILE_PATH "../bin/counter.dat"

#define ALPHA_DEFAULT_RED   0
#define ALPHA_DEFAULT_GREEN 0
#define ALPHA_DEFAULT_BLUE  0
#define DEFAULT_EDGE_WIDTH  0

#define RETV_SUCC 0
#define RETV_FAIL 1

#define ARR_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define IMG_PTR_GET(img, x, y) ( *((img) + ((img)->get_width() * (y) + x)) )//get pixel from image pointer

// Global variables.
color bgColor;
int edgeWidth;
char abs_exe_path[PATH_MAX];


// Taken from https://stackoverflow.com/questions/2342162/stdstring-formatting-like-sprintf
template<typename ... Args>
std::string string_format( const std::string& format, Args ... args )
{
    int size_s = std::snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'
    if( size_s <= 0 ){ throw std::runtime_error( "Error during formatting." ); }
    auto size = static_cast<size_t>( size_s );
    std::unique_ptr<char[]> buf( new char[ size ] );
    std::snprintf( buf.get(), size, format.c_str(), args ... );
    return std::string( buf.get(), buf.get() + size - 1 ); // We don't want the '\0' inside
}
// (The code snippet above is licensed under CC0 1.0).


long map(long x, long in_min, long in_max, long out_min, long out_max) {
	return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}


void BlendAlphaWithColor(rgba_pixel *pix_in, color alphaColor) {
	pix_in->red   = map(pix_in->alpha, 255, 0, pix_in->red,   alphaColor.red);
	pix_in->green = map(pix_in->alpha, 255, 0, pix_in->green, alphaColor.green);
	pix_in->blue  = map(pix_in->alpha, 255, 0, pix_in->blue,  alphaColor.blue);
}


int PrintRasterTerm_single(image<rgb_pixel> * sprite) {
	if (!sprite->get_width() || !sprite->get_height()) return RETV_FAIL;

	for (int y = 0; y < sprite->get_height(); y++) {
		for (int x = 0; x < sprite->get_width(); x++) {
			basic_rgb_pixel<unsigned char> pix = sprite->get_pixel(x, y);
			printf("\033[0;38;2;%d;%d;%d;48;2;0;0;0m█\033[0m", pix.red, pix.green, pix.blue);
		}
		printf("\n");
	}
	return RETV_SUCC;
}


basic_rgba_pixel<unsigned char> get_pixel_w_edges(int x, int y, image<rgba_pixel> *sprite) {
    if (x < 0 || y < 0 || x >= sprite->get_width() || y >= sprite->get_height()) return (basic_rgba_pixel<unsigned char>){bgColor.red,bgColor.green,bgColor.blue,0};
    return sprite->get_pixel(x, y);
}

int PrintRasterTerm_double(image<rgba_pixel> *sprite) {
	if (!sprite->get_width() || !sprite->get_height()) return RETV_FAIL;
    // fprintf(stderr, "\033[0;38;2;255;255;255;48;2;0;0;0mEDGEW:%d\033[0m\n\r", edgeWidth); //DEBUG
    //edgeWidth = 0; //DEBUG

	for (int y = -edgeWidth; y < (int)sprite->get_height()+edgeWidth; y+=2) {
		for (int x = -edgeWidth; x < (int)sprite->get_width()+edgeWidth; x++) {
			basic_rgba_pixel<unsigned char> top = get_pixel_w_edges(x, y, sprite);
			basic_rgba_pixel<unsigned char> btm;

			if (y+1 < sprite->get_height()) {
				btm = get_pixel_w_edges(x, y+1, sprite);
			} else {
				btm = (basic_rgba_pixel<unsigned char>){bgColor.red, bgColor.green, bgColor.blue, 255};
			}
			BlendAlphaWithColor(&top, bgColor);
			BlendAlphaWithColor(&btm, bgColor);
			printf("\033[0;38;2;%d;%d;%d;48;2;%d;%d;%dm▀\033[0m", top.red, top.green, top.blue, btm.red, btm.green, btm.blue);
		}
		printf("\n");
	}
	return RETV_SUCC;
}

enum order_t {
    MODE_UNKNOWN,
	MODE_RANDOM,
	MODE_FIRST,
	MODE_ORDER,
	MODE_REVERSE
};

enum command_t {
	COMM_UNKNOWN, // for unrecognized commands.
	COMM_NONE,    // for when there's no next command.
	COMM_MODE,    // set the selection mode.
	COMM_LIST,    // set list of filenames of the images.
	COMM_KILL,    // kill the program before printing.
	COMM_ALPHA,   // set alpha color.
	COMM_EDGE     // set the width of the edge around the image.
};

#define MAX_COMMAND_LENGHT 5 // excluding terminator
const char *commands[] {
	"MODE",
	"LIST",
	"KILL",
	"ALPHA",
    "EDGE"
};

#define MAX_COMMAND_MODE_LENGHT 7 // excluding terminator
const char *modes[] {
	"RANDOM",
	"FIRST",
	"ORDER",
	"REVERSE"
};

#define MAX_LIST_LEN 256
#define MAX_FILENAME_LENGHT 256
char filenames[MAX_LIST_LEN][MAX_FILENAME_LENGHT+1];
int tot_filenames = 0;

const uint8_t default_sprite_file_hex[] = {
	0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
	0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0xF3, 0xFF,
	0x61, 0x00, 0x00, 0x00, 0x7E, 0x49, 0x44, 0x41, 0x54, 0x38, 0x4F, 0x63, 0x64, 0x60, 0x60, 0xF8,
	0xFF, 0x56, 0x46, 0x05, 0x48, 0x91, 0x0E, 0x84, 0x9F, 0xDC, 0x61, 0x60, 0x04, 0x6A, 0xFE, 0x0F,
	0x62, 0x90, 0x03, 0x40, 0x16, 0xC3, 0x0D, 0x40, 0x76, 0x05, 0xC8, 0x40, 0x7C, 0x7C, 0x90, 0x65,
	0x30, 0x35, 0x28, 0x06, 0xC0, 0x04, 0x61, 0x2E, 0x02, 0x19, 0x82, 0xCE, 0x86, 0x19, 0x4C, 0x96,
	0x01, 0xC8, 0xDE, 0x24, 0xCB, 0x00, 0x64, 0xAF, 0x61, 0x35, 0x00, 0x66, 0x03, 0x59, 0x5E, 0xA0,
	0x38, 0x16, 0x28, 0x36, 0x00, 0x16, 0xE2, 0xC8, 0x34, 0xB2, 0x97, 0x70, 0xC5, 0x08, 0xCE, 0x68,
	0xC4, 0x65, 0x20, 0x72, 0x54, 0x63, 0x4D, 0x48, 0xD8, 0xE2, 0x1D, 0x5D, 0x13, 0x51, 0x09, 0x09,
	0x9B, 0x0B, 0x08, 0x46, 0x23, 0x36, 0x9B, 0x90, 0x6D, 0xC3, 0xE6, 0x3A, 0xCA, 0x33, 0x13, 0xA5,
	0xD9, 0x19, 0x00, 0x51, 0xBF, 0xE0, 0x77, 0xB8, 0x16, 0x4D, 0xA4, 0x00, 0x00, 0x00, 0x00, 0x49,
	0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
};



bool CharIsSpacing(char c) {
	return (c == ' ' || c == '\n' || c == '\r' || c == ',');
}

void fProceedUntil(FILE* f, char target) {
	while (!feof(f) && fgetc(f) != target) continue;
}

void fProceedUntilName(FILE* f) {
	//printf("Proceeding until name.\n\r'"); //DEBUG
	char c = ' ';
	while (!feof(f)) {
		c = fgetc(f);
		//printf("%c", c); fflush(stdout); //DEBUG
		if (!CharIsSpacing(c)) break;
		//continue;
	}
	if (!CharIsSpacing(c)) ungetc(c, f);
	//printf("'\n\r...Name found.\n\r"); //DEBUG
}

int fParseNextWord(char* buff, int maxlen, FILE* f) {
	// EXAMPLE EXAMPLE EXA...
	// ^enter function with fpointer here or on a leading space.
	// EXAMPLE EXAMPLE EXA...
	//        ^function exits with fpointer here.
    // NOTE: returns -1 in case of error.
	fProceedUntilName(f);
	char c;
	int len = 0;
	while (!feof(f)) {
		c = fgetc(f);
		if (CharIsSpacing(c)) {
			break;
		} else {
			if (len >= maxlen) {
				fprintf(stderr, "[OTAKON][ERROR]:Max word size exceeded at \"%s\"+\"%c\" (expected len is %d).\n\r", &(buff[0]), c, maxlen);
				len = -1;
				break;
			} else {
				buff[len++] = c;
			}
		}
	}
	if (len > 0) {
		buff[len] = '\0';
	}
	//printf("Got name: '%s' (len: %d)\n\r", &(buff[0]), len); fflush(stdout); //DEBUG
	return len;
};

/*void fParseNextString(FILE* f, char* buff, int maxlen) {
	// EXAMPLE "EXAMPLE" EXA...
	//          ^enter function with fpointer here or at any point before next double-quotes.
	// EXAMPLE "EXAMPLE" EXA...
	//                 ^function exits with fpointer here.
	int len = 0;
	while (!feof(f) && c = fgetc(f) != "'" && len < maxlen) buff[len++] = c;
};*/

command_t fParseNextCommand(FILE* f, char* buff) {
	//char buff[MAX_COMMAND_LENGHT];
	int len = fParseNextWord(&(buff[0]), MAX_COMMAND_LENGHT, f);
	if (len < 1) return COMM_NONE;
	for (int i = 0; i < ARR_SIZE(commands); i++) {
		//fprintf(stderr, "strcmp('%s', '%s')...\n\r", commands[i], &(buff[0])); //DEBUG
		if ( strcmp(commands[i], &(buff[0])) == 0 ) {
			switch (i) {
				case 0: return COMM_MODE;
				case 1: return COMM_LIST;
				case 2: return COMM_KILL;
                case 3: return COMM_ALPHA;
                case 4: return COMM_EDGE;
			}
		}
	}
	return COMM_UNKNOWN;
}

order_t fParseCommandMode(FILE* f, char* buff) {
	//char buff[MAX_COMMAND_MODE_LENGHT];
	int len = fParseNextWord(&(buff[0]), MAX_COMMAND_MODE_LENGHT, f);
    if (len < 1) return MODE_UNKNOWN;
	for (int i = 0; i < ARR_SIZE(modes); i++) {
		if ( strcmp(modes[i], &(buff[0])) == 0 ) {
			switch (i) {
				case 0: return MODE_RANDOM;
				case 1: return MODE_FIRST;
				case 2: return MODE_ORDER;
				case 3: return MODE_REVERSE;
			}
		}
	}
	return MODE_UNKNOWN;
}

void ConfigRm(const char* str) {
    // remove all occurrences of the given string from the given config file.
    // NOTE: f must be opened in rwb mode.
    char old_file_path[PATH_MAX];
    char new_file_path[PATH_MAX];
    int ret = snprintf(old_file_path, sizeof(old_file_path), "%s/%s", abs_exe_path, CONFIG_FILE_PATH);
    assert(ret < PATH_MAX);
    ret = snprintf(new_file_path, sizeof(new_file_path), "%s/%s", abs_exe_path, CONFIG_SWAP_FILE_PATH);
    assert(ret < PATH_MAX);
    FILE* of = fopen(old_file_path, "rb"); //old file.
    FILE* nf = fopen(new_file_path, "wb"); //new file.

    int maxlen = strlen(str);
    int len = 0;
    char buff[maxlen+1];
    bool readingList = false;

    while (true) {
        int c = fgetc(of);
        if (c == EOF) break;
        if (c == '{') readingList = true;
        if (c == '}') readingList = false;
        buff[len++] = c;
        if (c != str[len-1] || len >= maxlen || readingList) {
            buff[len] = '\0';
            if (strcmp(buff, str) != 0) fwrite(&(buff[0]), sizeof(char), len, nf);
            len = 0;
        }
    }

    // swap old file with temporary new file.
    fclose(of); fclose(nf);
    remove(old_file_path);
    rename(new_file_path, old_file_path);
}

void ConfigAppend(const char* str) {
    char config_file_path[PATH_MAX];
    int ret = snprintf(config_file_path, PATH_MAX, "%s/%s", abs_exe_path, CONFIG_FILE_PATH);
    assert(ret < PATH_MAX);
    FILE* f = fopen(config_file_path, "a");
    fprintf(f, "%s", str);
    fclose(f);
}

void LoadCounterValue(int* counter_ptr) {
    char counter_file_path_buff[PATH_MAX];
    int ret = snprintf(counter_file_path_buff, sizeof(counter_file_path_buff), "%s/%s", abs_exe_path, COUNTER_FILE_PATH);
	assert(ret < PATH_MAX);
    FILE* counterFile = fopen(counter_file_path_buff, "rb");
	if (!counterFile) {
		counterFile = fopen(counter_file_path_buff, "wb");
		int temp = 0; fwrite(&temp, sizeof(temp), 1, counterFile); // new count start from zero.
		fclose(counterFile);
		counterFile = fopen(counter_file_path_buff, "rb");
	}
	fread(counter_ptr, sizeof(*counter_ptr), 1, counterFile);
	fclose(counterFile);
}

void SaveCounterValue(int counter) {
    char counter_file_path_buff[PATH_MAX];
    int ret = snprintf(counter_file_path_buff, sizeof(counter_file_path_buff), "%s/%s", abs_exe_path, COUNTER_FILE_PATH);
	assert(ret < PATH_MAX);
    FILE* counterFile = fopen(counter_file_path_buff, "wb");
	fwrite(&counter, sizeof(counter), 1, counterFile);
	fclose(counterFile);
}


void GetExecutableAbsPath(char* abspath_out, size_t abspath_max_len, char *relpath_in) {
    ssize_t len = readlink("/proc/self/exe", abspath_out, abspath_max_len);
    int i;
    for (i = len-1; i > 2 && (abspath_out[i] != '/') ; i--) ;
    //if (i < 1) i = 1;
    abspath_out[i] = '\0';
    //printf("Absolute path to executable is: %s\n\r", abspath_out); //DEBUG
}

void AssureDir(const char* rel_dir_path) {
    // make sure the given directory exists.
    char temp_path_buff[PATH_MAX];
    int ret = snprintf(temp_path_buff, sizeof(temp_path_buff), "%s/%s", abs_exe_path, rel_dir_path);
    assert(ret < PATH_MAX);
    /*DIR* d = opendir(temp_path_buff);
    if (d) {
        closedir(d); // directory exists, close it.
    } else if (errno == ENOENT) {
        // directory does not exist, create it.
counter_file_path_buff
    } else {
        // opendir() failed for other reasons.
        fprintf(stderr, "[OTAKON][ERROR]:Failed to open \"%s\"\n\r, likely due to permission issues.", rel_dir_path);
    }*/
    struct stat st = {0};
    if (stat(temp_path_buff, &st) == -1) {
        mkdir(temp_path_buff, 0700);
    }
}

bool fExists(const char* abs_file_path) {
    // make sure the given file exists.
    FILE* f = fopen(abs_file_path, "rb");
    if (f) {
        fclose(f); return 1;
    } else {
        return 0;
    }
}


// ##################### OTAKON RUN

int OtakonRun() {
    char cfg_file_path_buff[PATH_MAX];
    int ret = snprintf(cfg_file_path_buff, PATH_MAX, "%s/%s", abs_exe_path, CONFIG_FILE_PATH);
	assert(ret < PATH_MAX);
    FILE* cfgFile = fopen(cfg_file_path_buff, "rb");

	order_t orderMode = MODE_RANDOM; // default selection mode

	while (!feof(cfgFile)) {
		//printf("[Parsing next command]\n\r"); //DEBUG
		char* comm_name_buff = (char*)malloc(MAX_COMMAND_LENGHT);
		command_t comm = fParseNextCommand(cfgFile, &(comm_name_buff[0]));
		switch (comm) {
			case COMM_MODE:
				{
					//printf("--> Recognized command (MODE)\n\r"); fflush(stdout); //DEBUG
					char* comm_mode_buff = (char*)malloc(MAX_COMMAND_MODE_LENGHT);
					orderMode = fParseCommandMode(cfgFile, &(comm_mode_buff[0]));
					if (orderMode == MODE_UNKNOWN) {
						fprintf(stderr, "[OTAKON][ERROR]:Unrecognized ordering mode (\"%s\").\n\r", &(comm_mode_buff[0]));
						return 1;
					}
					//printf("--> Order mode set to %d\n\r", orderMode); //DEBUG
				}
				break;
			default:
			case COMM_LIST:
				//printf("--> Recognized command (LIST)\n\r"); fflush(stdout); //DEBUG
				tot_filenames = 0;
				fProceedUntil(cfgFile, '{');
				fProceedUntilName(cfgFile);
				// parse names that make up the list of filenames.
				while (!feof(cfgFile) && tot_filenames < MAX_LIST_LEN) {
					char c = ' ';
					int temp_len = 0;
					while (!feof(cfgFile) && temp_len < MAX_FILENAME_LENGHT) {
						c = fgetc(cfgFile);
						if (CharIsSpacing(c) || c == '}') break;
						filenames[tot_filenames][temp_len++] = c;
					}
					if (c == '}') break;
					if (temp_len > 0) {
						if (temp_len >= MAX_FILENAME_LENGHT) temp_len = MAX_FILENAME_LENGHT - 1;
						filenames[tot_filenames][temp_len] = '\0';
						tot_filenames++;
					}
				}
				break;
			case COMM_ALPHA:
				{
					//printf("--> Recognized command (ALPHA)\n\r"); fflush(stdout); //DEBUG
					char temp_buff[3];
                    uint8_t* temp_rgb_ptrs[3] = {
                        &(bgColor.red),
                        &(bgColor.green),
                        &(bgColor.blue)
                    };
                    for (int i = 0; i < 3; i++) {
                        int len = fParseNextWord(&(temp_buff[0]), 3, cfgFile);
                        if (len == 0) {
                            fprintf(stderr, "[OTAKON][ERROR]:Missing ALPHA color information.\n\r"); return 1;
                        } else if (len == -1) return 1;
                        *(temp_rgb_ptrs[i]) = atoi(&(temp_buff[0]));
                    }
				}
				break;
            case COMM_EDGE:
                {
					//printf("--> Recognized command (ALPHA)\n\r"); fflush(stdout); //DEBUG
					char temp_buff[3];
                    int len = fParseNextWord(&(temp_buff[0]), 2, cfgFile);
                    if (len == 0) {
                        fprintf(stderr, "[OTAKON][ERROR]:Missing EDGE width information.\n\r"); return 1;
                    } else if (len == -1) return 1;
                    edgeWidth = atoi(&(temp_buff[0]));
                    if (edgeWidth < 0) edgeWidth = 0;
				}
			case COMM_NONE:
				// no more commands, do nothing.
				break;
			case COMM_KILL:
				fclose(cfgFile);
				return 0;
			case COMM_UNKNOWN:
				fprintf(stderr, "[ERROR][OTAKON]:Unrecognized command in config file (\"%s\").\n\r", &(comm_name_buff[0]));
				return 1;
		}
	}
	fclose(cfgFile);

	// choose an image from the list of filenames, following the chosen criteria.
	int chosenIdx = 0;
	int counter = 0;

	if (tot_filenames > 0) {
		// open counter file and get counter value.
		if (orderMode == MODE_ORDER || orderMode == MODE_REVERSE) LoadCounterValue(&counter);
		switch (orderMode) {
			default:
			case MODE_RANDOM:
				chosenIdx = rand() % tot_filenames;
				break;
			case MODE_FIRST:
				chosenIdx = 0;
				break;
			case MODE_ORDER:
				if (counter >= tot_filenames) counter = tot_filenames-1;
				chosenIdx = counter;
				if (++counter >= tot_filenames) counter = 0;
				//save counter to file.
				break;
			case MODE_REVERSE:
				if (counter >= tot_filenames) counter = tot_filenames-1;
				chosenIdx = counter;
				if (--counter < 0) counter = tot_filenames-1;
				break;
		}
	}

	// open counter file and write new counter value.
	if (orderMode == MODE_ORDER || orderMode == MODE_REVERSE) SaveCounterValue(counter);

    char sprite_path_buff[PATH_MAX];
    ret = snprintf(sprite_path_buff, sizeof(sprite_path_buff), "%s/%s/%s.png", abs_exe_path, SPRITE_FILE_PATH, filenames[chosenIdx]);
    assert(ret < PATH_MAX);

    image<rgba_pixel> *sprite;
    bool default_img_fallback = false;
	if (tot_filenames > 0) {
        if (fExists(sprite_path_buff)) {
            // load chosen image.
            sprite = new image<rgba_pixel>(sprite_path_buff/*string_format("%s/%s/%s.png", abs_exe_path, SPRITE_FILE_PATH, filenames[chosenIdx])*/);
        } else {
            fprintf(stderr, "[OTAKON][ERROR]:Could not open image \"%s\".\n\r", sprite_path_buff);
            default_img_fallback = true;
        }
	} else {
        fprintf(stderr, "[OTAKON][WARN]:LIST of png filenames was not provided or is void.\n\r");
        default_img_fallback = true;
    }
	if (default_img_fallback) {
		// make sure that the default image exists.
        ret = snprintf(sprite_path_buff, PATH_MAX, "%s/%s", abs_exe_path, DEFAULT_SPRITE_FILE_PATH);
		assert(ret < PATH_MAX);
        FILE* temp = fopen(sprite_path_buff, "rb");
		if (!temp) {
			temp = fopen(sprite_path_buff, "wb");
			fwrite(&default_sprite_file_hex, sizeof(default_sprite_file_hex), 1, temp);
		}
		fclose(temp);
		sprite = new image<rgba_pixel>(sprite_path_buff);
	}
    #define MSGGRAY 30
    printf("\033[0;38;2;%d;%d;%d;48;2;0;0;0m[OTAKON]:printing image to terminal.\033[0m\n\r", MSGGRAY, MSGGRAY, MSGGRAY);
	PrintRasterTerm_double(sprite);
	delete sprite;

	return (tot_filenames == 0);
}

#define HELP_SCREEN_STR "\
[OTAKON]: Available commands are:\n\r\
    help   (h)\n\r\
       show available commands.\n\r\
    docs   (d) <mode>\n\r\
       show docs file. [mode: 'term'(default), 'pdf', 'html']\n\r\
    run    (r)\n\r\
       run Otakon\n\r\
    config (c,cfg,conf) <arg>\n\r\
       show configuration file. [arg: 'show'(default),\n\r\
       'edit', 'kill'/'disable', 'enable']\n\r\
"


// ##################### MAIN

int main(int argc, char* argv[]) {
    GetExecutableAbsPath(abs_exe_path, PATH_MAX, argv[0]);
    // initialize random seed.
    srand(time(NULL) + getpid());
	// initialize color to defaults.
	bgColor.red = ALPHA_DEFAULT_RED;
	bgColor.green = ALPHA_DEFAULT_GREEN;
	bgColor.blue = ALPHA_DEFAULT_BLUE;
    // initialize edge width.
    edgeWidth = DEFAULT_EDGE_WIDTH;
    // make sure config directory exists.
    AssureDir("../config");
    AssureDir("../images");
    AssureDir("../docs");
	// make sure config file exists.
    char cfg_file_path_buff[PATH_MAX];
    int ret = snprintf(cfg_file_path_buff, sizeof(cfg_file_path_buff), "%s/%s", abs_exe_path, CONFIG_FILE_PATH);
	assert(ret < PATH_MAX);

    FILE* cfgFile = fopen(cfg_file_path_buff, "rb");
	if (!cfgFile) {
		cfgFile = fopen(cfg_file_path_buff, "wb");
		fprintf(cfgFile, "MODE RANDOM\nALPHA 0,0,0\nEDGE 0\nLIST = {\ndefault\n}");
        fprintf(stderr, "[OTAKON][WARN]:Configuration file not found, falling back to default config.");
	}
	fclose(cfgFile);

    char prompt_buff[PATH_MAX + 32];
	if (argc == 1) {
        printf("[OTAKON]:Write \"otakon help\" to show available commands.\n\r");
    } else {
        if (!strcmp(argv[1], "r") || !strcmp(argv[1], "run")) {
            // run Otakon.
            return OtakonRun();
        }
        else if (!strcmp(argv[1], "h") || !strcmp(argv[1], "help")) {
            // show help screen.
            printf(HELP_SCREEN_STR);
        }
        else if (!strcmp(argv[1], "d") || !strcmp(argv[1], "docs")) {
            if (argc > 2 && (!strcmp(argv[2], "pdf"))) {
                sprintf(prompt_buff, "open %s/%sotakon_docs.pdf", abs_exe_path, DOCS_DIR_PATH);
            } else if (argc > 2 && !strcmp(argv[2], "html")) {
                sprintf(prompt_buff, "open %s/%sotakon_docs.html", abs_exe_path, DOCS_DIR_PATH);
            } else {
                sprintf(prompt_buff, "less %s/%sotakon_docs.md", abs_exe_path, DOCS_DIR_PATH);
            }
            system(prompt_buff);
        }
        else if (!strcmp(argv[1], "c") || !strcmp(argv[1], "cfg") || !strcmp(argv[1], "conf") || !strcmp(argv[1], "config")) {
            // otakon conf <conf_arg>
            if (argc > 2 && strcmp(argv[2], "show")) {
                if (!strcmp(argv[2], "e") || !strcmp(argv[2], "edit")) {
                    snprintf(prompt_buff, sizeof(prompt_buff), "$EDITOR %s", cfg_file_path_buff);
                    system(prompt_buff);
                }
                else if (!strcmp(argv[2], "kill") || !strcmp(argv[2], "disable")) {
                    ConfigRm("KILL");
                    ConfigAppend("KILL");
                    printf("[OTAKON][CONFIG]:Otakon is now disabled.\n\rRun \"otakon config enable\" or remove\n\rthe KILL instruction manually from the\n\rconfig file to enable it again.\n\r");
                }
                else if (!strcmp(argv[2], "enable")) {
                    ConfigRm("KILL");
                    printf("[OTAKON][CONFIG]:Otakon is now enabled.\n\rRun \"otakon config disable\" or \n\r\"otakon config kill\" or write the KILL\n\rcommand in the config file to disable it.\n\r");
                } else {
                    fprintf(stderr, "[OTAKON][ERROR]:Unrecognized argument for config.\n\rRun \"otakon help\" for details.\n\r");
                }
            } else {
                sprintf(prompt_buff, "less %s/%s", abs_exe_path, CONFIG_FILE_PATH);
                system(prompt_buff);
            }
        }
        else {
            fprintf(stderr, "[OTAKON][ERROR]:Unrecognized command \"%s\", please write \"otakon help\" to show help.\n\r", argv[1]);
        }
    }
    return 0;
}
