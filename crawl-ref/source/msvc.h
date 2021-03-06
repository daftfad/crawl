/*
 *  File:       msvc.h
 *  Summary:    Header file for MSVC compiles
 *  Written by: Paul Du Bois
 *
 *  $Author$ on $Date$
 */

#if defined(_MSC_VER)


#define fileno _fileno
#define itoa _itoa
#define snprintf _snprintf
#define strcasecmp _stricmp
#define stricmp _stricmp
#define strlwr _strlwr
#define strncasecmp _strnicmp
#define strnicmp _strnicmp
#define unlink _unlink

// No va_copy in MSVC
#if !defined(va_copy)
#define va_copy(dst, src) \
   ((void) memcpy(&(dst), &(src), sizeof(va_list)))
#endif

#pragma warning( disable : 4290 )
#pragma warning( disable : 4351 )
// bool -> int
#pragma warning( disable : 4800 )

// struct vs class XXX: fix these some day!
#pragma warning( disable : 4099 )

// truncating conversions XXX: fix these too!
#pragma warning( disable : 4244 )


// ----------------------------------------------------------------------
// dirent.h replacement
// ----------------------------------------------------------------------

#define DT_DIR 4
#define DT_REG 8

struct DIR;
struct dirent
{
    // ino_t d_ino;
    unsigned short d_reclen;
    unsigned char d_type;
    unsigned short d_namlen;
    char d_name[255];
};

DIR* opendir(const char* path);
dirent* readdir(DIR*);
int closedir(DIR*);
int ftruncate(int fp, int size);


#endif /* defined(_MSC_VER) */
