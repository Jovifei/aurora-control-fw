/* clangd 裸机解析用最小 stdio stub；目标固件不链接标准库。 */
#ifndef AURORA_CLANGD_STDIO_H
#define AURORA_CLANGD_STDIO_H

typedef struct
{
    int unused;
} FILE;

#ifndef EOF
#define EOF (-1)
#endif

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int sprintf(char *buffer, const char *format, ...);
int snprintf(char *buffer, unsigned int size, const char *format, ...);
int puts(const char *string);
int fputc(int character, FILE *stream);
int fputs(const char *string, FILE *stream);

#endif
