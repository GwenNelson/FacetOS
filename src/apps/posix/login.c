#include <crypt.h>
#include <pwd.h>
#include <shadow.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void read_line(char *buffer, unsigned capacity, int echo)
{
    unsigned length = 0;
    for (;;) { char c; if (read(0,&c,1)!=1) continue;
        if (c=='\r'||c=='\n') { buffer[length]=0; write(1,"\n",1); return; }
        if ((c==8||c==127)&&length) { length--; if(echo) write(1,"\b \b",3); continue; }
        if (c>=32&&c<127&&length+1<capacity) { buffer[length++]=c; if(echo) write(1,&c,1); }
    }
}
static void banner(void)
{
    char host[64] = "unknown-domain";
    char *terminal = ttyname(0);
    (void)gethostname(host, sizeof(host));
    (void)write(1, "FacetOS POSIX login (", 21);
    (void)write(1, host, strlen(host));
    (void)write(1, " on ", 4);
    (void)write(1, terminal == NULL ? "unknown terminal" : terminal,
                strlen(terminal == NULL ? "unknown terminal" : terminal));
    (void)write(1, ")\n", 2);
}

int main(void)
{
    char user[64], password[128];
    banner();
    for (;;) {
        (void)write(1, "login: ", 7);
        read_line(user, sizeof(user), 1);
        (void)write(1, "password: ", 10);
        read_line(password, sizeof(password), 0);
        struct passwd *account = getpwnam(user);
        struct spwd *shadow = account == NULL ? NULL : getspnam(user);
        char *hash = shadow == NULL ? NULL : crypt(password, shadow->sp_pwdp);
        if (account == NULL || shadow == NULL || hash == NULL ||
            strcmp(hash, shadow->sp_pwdp) != 0 ||
            setgid(account->pw_gid) != 0 || setuid(account->pw_uid) != 0 ||
            chdir(account->pw_dir) != 0) {
            (void)write(1, "Login incorrect\n", 16);
            continue;
        }
        char *argv[] = {account->pw_shell, NULL};
        pid_t pid;
        int status;
        if (posix_spawn(&pid, account->pw_shell, NULL, NULL, argv, NULL) != 0 ||
            waitpid(pid, &status, 0) != pid) {
            (void)write(1, "Unable to start shell\n", 22);
            return 1;
        }
        /* Login has permanently installed the account credentials.  End this
         * process after logout so its privileged supervisor can start a fresh
         * login instead of reusing a demoted authentication process. */
        return 0;
    }
}
