#include <stdlib.h>
#include <string.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

static int line(char *buffer, unsigned capacity)
{
    unsigned length = 0;
    for (;;) {
        char c; if (read(0, &c, 1) != 1) continue;
        if (c == '\n' || c == '\r') { buffer[length] = 0; (void)write(1, "\n", 1); return 0; }
        if ((c == 8 || c == 127) && length) { length--; (void)write(1, "\b \b", 3); continue; }
        if (c >= 32 && c < 127 && length + 1 < capacity) { buffer[length++] = c; (void)write(1, &c, 1); }
    }
}

/* Keep the supported command language deliberately small, but make its
 * token boundary match the native FacetShell: whitespace, quotes and escaped
 * characters work; shell syntax with semantics we do not provide is rejected. */
static int parse(char *line, char *words[], size_t capacity, size_t *count)
{
    char *source = line, *destination = line;
    *count = 0;
    while (*source != '\0') {
        while (*source == ' ' || *source == '\t') source++;
        if (*source == '\0') break;
        if (*count == capacity) return -1;
        words[(*count)++] = destination;
        char quote = 0;
        for (;;) {
            char c = *source;
            if (c == '\0') { if (quote != 0) return -1; break; }
            if (c == '\\') { if (*++source == '\0') return -1; *destination++ = *source++; continue; }
            if (quote != 0) { source++; if (c == quote) quote = 0; else *destination++ = c; continue; }
            if (c == '\'' || c == '"') { quote = c; source++; continue; }
            if (c == '|' || c == '>' || c == '<' || c == '&' || c == '*') return -1;
            if (c == ' ' || c == '\t') break;
            *destination++ = *source++;
        }
        while (*source == ' ' || *source == '\t') source++;
        *destination++ = '\0';
    }
    return 0;
}
int main(void)
{
    char command[256], cwd[256];
    for (;;) {
        const char *user = getenv("USER");
        if (getcwd(cwd, sizeof(cwd)) != NULL) { write(1,cwd,strlen(cwd)); write(1," ",1); }
        (void)write(1, user != NULL && !strcmp(user,"root") ? "# " : "$ ", 2);
        (void)line(command, sizeof(command));
        char *words[16]; size_t count=0;
        if (parse(command, words, 16, &count) != 0) {
            static const char message[] = "sh: unsupported or invalid syntax\n";
            (void)write(1, message, sizeof(message) - 1);
            continue;
        }
        if (count==0) continue;
        if (!strcmp(words[0], "exit")) return 0;
        if (!strcmp(words[0], "help")) {
            static const char text[]="help echo pwd cd exit ls cat\n";
            (void)write(1,text,sizeof(text)-1);
            continue;
        }
        if (!strcmp(words[0], "pwd")) { write(1,cwd,strlen(cwd)); write(1,"\n",1); continue; }
        if (!strcmp(words[0], "cd")) { if(count!=2 || chdir(words[1])!=0) write(1,"sh: cd failed\n",14); continue; }
        if (!strcmp(words[0], "echo")) { for(size_t i=1;i<count;i++){if(i>1)write(1," ",1);write(1,words[i],strlen(words[i]));}write(1,"\n",1);continue; }
        const char *name = !strcmp(words[0],"ls") ? "/bin/ls" : !strcmp(words[0],"cat") ? "/bin/cat" : words[0];
        char *argv[17];
        argv[0] = (char *)name;
        for(size_t i=1;i<count;i++) argv[i]=words[i];
        argv[count] = NULL;
        pid_t pid;
        int status;
        if (posix_spawn(&pid,name,NULL,NULL,argv,NULL) != 0) {
            static const char message[] = "sh: command failed\n";
            (void)write(1, message, sizeof(message) - 1);
            continue;
        }
        if (waitpid(pid,&status,0) != pid)
            (void)write(1, "sh: command wait failed\n", 24);
    }
}
