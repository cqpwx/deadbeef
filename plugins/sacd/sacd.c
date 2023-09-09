#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../../deadbeef.h"

#define START_OF_MASTER_TOC 510
#define SACD_LSN_SIZE 2048
#define SACD_PSN_SIZE 2064
#define SACD_MASTER_TOC_ID "SACDMTOC"
#define SACD_MASTER_TEXT_ID "SACDText"
#define SACD_MASTER_MAN_ID "SACD_Man"
#define MAX_LANGUAGE_COUNT 8
#define SUPPORTED_VERSION_MAJOR 1
#define SUPPORTED_VERSION_MINOR 20
#define SACD_MAX_AERA_COUNT 32

static ddb_decoder2_t plugin;
static DB_functions_t *deadbeef;

typedef struct
{
    uint8_t category;  // category_t
    uint16_t reserved;
    uint8_t genre; // genre_t
}
genre_table_t;

typedef struct
{
  char language_code[2]; // ISO639-2 Language code
  uint8_t character_set; // char_set_t, 1 (ISO 646)
  uint8_t reserved;
} locale_table_t;

typedef struct
{
    char id[8]; // SACDMTOC

    struct
    {
        uint8_t major;
        uint8_t minor;
    } version; // 1.20 / 0x0114

    uint8_t reserved01[6];
    uint16_t album_set_size;
    uint16_t album_sequence_number;
    uint8_t reserved02[4];
    char album_catalog_number[16]; // 0x00 when empty, else padded with spaces for short strings
    genre_table_t album_genre[4];
    uint8_t reserved03[8];
    uint32_t area_1_toc_1_start;
    uint32_t area_1_toc_2_start;
    uint32_t area_2_toc_1_start;
    uint32_t area_2_toc_2_start;
    uint8_t disc_type_reserved   : 7;
    uint8_t disc_type_hybrid     : 1;
    uint8_t reserved04[3];
    uint16_t area_1_toc_size;
    uint16_t area_2_toc_size;
    char disc_catalog_number[16]; // 0x00 when empty, else padded with spaces for short strings
    genre_table_t disc_genre[4];
    uint16_t disc_date_year;
    uint8_t disc_date_month;
    uint8_t disc_date_day;
    uint8_t reserved05[4];
    uint8_t text_area_count;
    uint8_t reserved06[7];
    locale_table_t locales[MAX_LANGUAGE_COUNT];
} master_toc_t;

typedef struct
{
    char id[8]; // SACD_Man, manufacturer information
    uint8_t information[2040];
} master_man_t;

typedef struct
{
    char id[8]; // SACDText
    uint8_t reserved[8];
    uint16_t album_title_position;
    uint16_t album_artist_position;
    uint16_t album_publisher_position;
    uint16_t album_copyright_position;
    uint16_t album_title_phonetic_position;
    uint16_t album_artist_phonetic_position;
    uint16_t album_publisher_phonetic_position;
    uint16_t album_copyright_phonetic_position;
    uint16_t disc_title_position;
    uint16_t disc_artist_position;
    uint16_t disc_publisher_position;
    uint16_t disc_copyright_position;
    uint16_t disc_title_phonetic_position;
    uint16_t disc_artist_phonetic_position;
    uint16_t disc_publisher_phonetic_position;
    uint16_t disc_copyright_phonetic_position;
    uint8_t  data[2000];
} master_sacd_text_t;

static int is_sacd(FILE* fp) {
    char sacdmtoc[8];
    int retval = 0;
    
    while (1) {
        fseek(fp, START_OF_MASTER_TOC * SACD_LSN_SIZE, SEEK_SET);
        if (fread(sacdmtoc, 1, 8, fp) == 8) {
            if (memcmp(sacdmtoc, SACD_MASTER_TOC_ID, 8) == 0) {
                retval = 1;
                break;
            }
        }
        fseek(fp, START_OF_MASTER_TOC * SACD_PSN_SIZE + 12, SEEK_SET);
        if (fread(sacdmtoc, 1, 8, fp) == 8) {
            if (memcmp(sacdmtoc, SACD_MASTER_TOC_ID, 8) == 0) {
                retval = 1;
                break;
            }
        }
        break;
    }
    fseek(fp, 0, SEEK_SET);
    return retval;
}

DB_playItem_t* load_sacd_from_buffer(DB_playItem_t *after, const char *buffer, int64_t sz, int (*cb)(DB_playItem_t *, void *), const char *fname, int *pabort, ddb_playlist_t *plt, void *user_data) {
    // Read master TOC
    master_toc_t* master_toc = buffer + START_OF_MASTER_TOC;
    SWAP16(master_toc->album_set_size);
    SWAP16(master_toc->album_sequence_number);
    SWAP32(master_toc->area_1_toc_1_start);
    SWAP32(master_toc->area_1_toc_2_start);
    SWAP16(master_toc->area_1_toc_size);
    SWAP32(master_toc->area_2_toc_1_start);
    SWAP32(master_toc->area_2_toc_2_start);
    SWAP16(master_toc->area_2_toc_size);
    SWAP16(master_toc->disc_date_year);
    if (master_toc->version.major > SUPPORTED_VERSION_MAJOR || master_toc->version.minor > SUPPORTED_VERSION_MINOR)
        return NULL;

    // Master text
    master_sacd_text_t* master_text = (master_sacd_text_t*)((uint8_t*)master_toc + SACD_LSN_SIZE);
    if (strncmp(SACD_MASTER_TEXT_ID, master_text->id, 8) != 0) {
        return NULL;
    }
    SWAP16(master_text->album_title_position);
    SWAP16(master_text->album_artist_position);
    SWAP16(master_text->album_publisher_position);
    SWAP16(master_text->album_copyright_position);
    SWAP16(master_text->album_title_phonetic_position);
    SWAP16(master_text->album_artist_phonetic_position);
    SWAP16(master_text->album_publisher_phonetic_position);
    SWAP16(master_text->album_copyright_phonetic_position);
    SWAP16(master_text->disc_title_position);
    SWAP16(master_text->disc_artist_position);
    SWAP16(master_text->disc_publisher_position);
    SWAP16(master_text->disc_copyright_position);
    SWAP16(master_text->disc_title_phonetic_position);
    SWAP16(master_text->disc_artist_phonetic_position);
    SWAP16(master_text->disc_publisher_phonetic_position);
    SWAP16(master_text->disc_copyright_phonetic_position);
    
    uint8_t current_charset = master_toc->locales[0].character_set & 0x07;
    char* album_title = NULL;
    if (master_text->album_title_position)
        album_title = charset_convert((char*)master_text + master_text->album_title_position, strlen((char*)master_text + master_text->album_title_position), current_charset);
    char* album_title_phonetic = NULL;
    if (master_text->album_title_phonetic_position)
        album_title_phonetic = charset_convert((char*)master_text + master_text->album_title_phonetic_position, strlen((char*)master_text + master_text->album_title_phonetic_position), current_charset);
    char* album_artist = NULL;
    if (master_text->album_artist_position)
        album_artist = charset_convert((char*)master_text + master_text->album_artist_position, strlen((char*)master_text + master_text->album_artist_position), current_charset);
    char* album_artist_phonetic = NULL;
    if (master_text->album_artist_phonetic_position)
        album_artist_phonetic = charset_convert((char*)master_text + master_text->album_artist_phonetic_position, strlen((char*)master_text + master_text->album_artist_phonetic_position), current_charset);
    char* album_publisher = NULL;
    if (master_text->album_publisher_position)
        album_publisher = charset_convert((char*)master_text + master_text->album_publisher_position, strlen((char*)master_text + master_text->album_publisher_position), current_charset);
    char* album_publisher_phonetic = NULL;
    if (master_text->album_publisher_phonetic_position)
        album_publisher_phonetic = charset_convert((char*)master_text + master_text->album_publisher_phonetic_position, strlen((char*)master_text + master_text->album_publisher_phonetic_position), current_charset);
    char* album_copyright = NULL;
    if (master_text->album_copyright_position)
        album_copyright = charset_convert((char*)master_text + master_text->album_copyright_position, strlen((char*)master_text + master_text->album_copyright_position), current_charset);
    char* album_copyright_phonetic = NULL;
    if (master_text->album_copyright_phonetic_position)
        album_copyright_phonetic = charset_convert((char*)master_text + master_text->album_copyright_phonetic_position, strlen((char*)master_text + master_text->album_copyright_phonetic_position), current_charset);
    char* disc_title = NULL;
    if (master_text->disc_title_position)
        disc_title = charset_convert((char*)master_text + master_text->disc_title_position, strlen((char*)master_text + master_text->disc_title_position), current_charset);
    char* disc_title_phonetic = NULL;
    if (master_text->disc_title_phonetic_position)
        disc_title_phonetic = charset_convert((char*)master_text + master_text->disc_title_phonetic_position, strlen((char*)master_text + master_text->disc_title_phonetic_position), current_charset);
    char* disc_artist = NULL;
    if (master_text->disc_artist_position)
        disc_artist = charset_convert((char*)master_text + master_text->disc_artist_position, strlen((char*)master_text + master_text->disc_artist_position), current_charset);
    char* disc_artist_phonetic = NULL;
    if (master_text->disc_artist_phonetic_position)
        disc_artist_phonetic = charset_convert((char*)master_text + master_text->disc_artist_phonetic_position, strlen((char*)master_text + master_text->disc_artist_phonetic_position), current_charset);
    char* disc_publisher = NULL;
    if (master_text->disc_publisher_position)
        disc_publisher = charset_convert((char*)master_text + master_text->disc_publisher_position, strlen((char*)master_text + master_text->disc_publisher_position), current_charset);
    char* disc_publisher_phonetic = NULL;
    if (master_text->disc_publisher_phonetic_position)
        disc_publisher_phonetic = charset_convert((char*)master_text + master_text->disc_publisher_phonetic_position, strlen((char*)master_text + master_text->disc_publisher_phonetic_position), current_charset);
    char* disc_copyright = NULL;
    if (master_text->disc_copyright_position)
        disc_copyright = charset_convert((char*)master_text + master_text->disc_copyright_position, strlen((char*)master_text + master_text->disc_copyright_position), current_charset);
    char* disc_copyright_phonetic = NULL;
    if (master_text->disc_copyright_phonetic_position)
        disc_copyright_phonetic = charset_convert((char*)master_text + master_text->disc_copyright_phonetic_position, strlen((char*)master_text + master_text->disc_copyright_phonetic_position), current_charset);
    
    // Master man
    master_man_t* master_man = (master_man_t*)((uint8_t*)master_toc + SACD_LSN_SIZE + SACD_LSN_SIZE * MAX_LANGUAGE_COUNT);
    if (strncmp(SACD_MASTER_MAN_ID, master_man->id, 8) != 0) {
        return NULL;
    }
    // Read area
    uint8_t* area[SACD_MAX_AERA_COUNT] = { NULL };
    int area_count = 0;
    if (master_toc->area_1_toc_1_start) {
        area[area_count] = (uint8_t*)malloc(master_toc->area_1_toc_size * SACD_LSN_SIZE);
        if (area[area_count] == NULL) {
            return NULL;
        }
        memcpy(area[area_count], buffer + master_toc->area_1_toc_1_start, master_toc->area_1_toc_size);
        area_count++;
    }
        if (master_toc->area_2_toc_1_start) {
        area[area_count] = (uint8_t*)malloc(master_toc->area_2_toc_size * SACD_LSN_SIZE);
        if (area[area_count] == NULL) {
            return NULL;
        }
        memcpy(area[area_count], buffer + master_toc->area_2_toc_1_start, master_toc->area_2_toc_size);
        area_count++;
    }
    int tracks = 0;
    for (int i = 0; i < area_count; i++) {
        if (area) {
            
        }
    }
}

static DB_playItem_t* load_sacd_playlist(ddb_playlist_t *plt, DB_playItem_t *after, const char *fname, int *pabort, int (*cb)(DB_playItem_t *it, void *data), void *user_data) {
    trace ("enter pl_insert_sacd\n");
    // skip all empty lines and comments
    DB_FILE *fp = deadbeef->fopen (fname);
    if (!fp) {
        trace ("failed to open file %s\n", fname);
        return NULL;
    }
    int64_t sz = deadbeef->fgetlength (fp);
    trace ("loading sacd...\n");
    char *membuffer = malloc (sz);
    if (!membuffer) {
        deadbeef->fclose (fp);
        trace ("failed to allocate %d bytes to read the file %s\n", sz, fname);
        return NULL;
    }
    char *buffer = membuffer;
    deadbeef->fread (buffer, 1, sz, fp);
    deadbeef->fclose (fp);

    after = load_sacd_from_buffer(after, buffer, sz, cb, fname, pabort, plt, user_data);
    trace ("leave pl_insert_sacd\n");
    free (membuffer);
    return after;
}

static DB_playItem_t * sacdplug_load (ddb_playlist_t *plt, DB_playItem_t *after, const char *fname, int *pabort, int (*cb)(DB_playItem_t *it, void *data), void *user_data) {
    char resolved_fname[PATH_MAX];
    char *res = realpath (fname, resolved_fname);
    if (res) {
        fname = resolved_fname;
    }
    DB_playItem_t *ret = NULL;
    // Open file
    FILE* fp = fopen(fname, "r");
    if (fp == NULL) {
        fprintf(stderr, "SACD:Open URI failed.\n");
        return NULL;
    }
    // Detect if it is a SACD ISO
    if (!is_sacd(fp)) {
        fprintf(stderr, "SACD:Not a SACD image.\n");
        return NULL;
    }
    return load_sacd_playlist(plt, after, fname, pabort, cb, user_data);
}


static const char * exts[] = { "iso", NULL };
DB_playlist_t plugin = {
    DDB_PLUGIN_SET_API_VERSION
    .plugin.version_major = 1,
    .plugin.version_minor = 0,
    .plugin.type = DB_PLUGIN_PLAYLIST,
    .plugin.id = "iso",
    .plugin.name = "SACD ISO support",
    .plugin.descr = "Importing SACD iso audio disk file",
    .plugin.copyright = 
        "SACD ISO plugin for DeaDBeeF Player\n"
        "Copyright (C) 2023-2023 Textar Pu\n"
        "\n"
        "This software is provided 'as-is', without any express or implied\n"
        "warranty.  In no event will the authors be held liable for any damages\n"
        "arising from the use of this software.\n"
        "\n"
        "Permission is granted to anyone to use this software for any purpose,\n"
        "including commercial applications, and to alter it and redistribute it\n"
        "freely, subject to the following restrictions:\n"
        "\n"
        "1. The origin of this software must not be misrepresented; you must not\n"
        " claim that you wrote the original software. If you use this software\n"
        " in a product, an acknowledgment in the product documentation would be\n"
        " appreciated but is not required.\n"
        "\n"
        "2. Altered source versions must be plainly marked as such, and must not be\n"
        " misrepresented as being the original software.\n"
        "\n"
        "3. This notice may not be removed or altered from any source distribution.\n"
    ,
    .plugin.website = "https://github.com/cqpwx/deadbeef",
    .load = sacdplug_load,
    .extensions = exts,
};

DB_plugin_t * sacd_load (DB_functions_t *api) {
    deadbeef = api;
    return DB_PLUGIN (&plugin);
}