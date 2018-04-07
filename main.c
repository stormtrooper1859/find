#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <string.h>
#include <stdbool.h>
#include <sys/wait.h>

#define BUF_SIZE 1024

struct flags {
    long long inode_num;
    long long size;
    const char *name;
    char *path_to_executable;
    int links_num;
    int greater_than;
    bool should_inode;
    bool should_name;
    bool should_equals;
    bool should_links;
    bool should_execute;
};

struct linux_dirent64 {
    __ino64_t d_ino;    /* 64-bit inode number */
    __off64_t d_off;    /* 64-bit offset to next structure */
    unsigned short d_reclen; /* Size of this dirent */
    unsigned char d_type;   /* File type */
    char d_name[]; /* Filename (null-terminated) */
};

int parse(int argc, const char *argv[], struct flags *flag) {
    flag->should_inode = false;
    flag->should_name = false;
    flag->should_equals = false;
    flag->should_links = false;
    flag->should_execute = false;
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "-inum") == 0) {
            flag->should_inode = true;
            if (++i < argc) {
                flag->inode_num = atoi(argv[i]);
            } else {
                //error with parsing
                printf("expected number of inode after -inum\n");
                return -1;
            }
        } else if (strcmp(argv[i], "-name") == 0) {
            flag->should_name = true;
            if (++i < argc) {
                flag->name = argv[i];
            } else {
                //error with parsing
                printf("expected name of file after -name\n");
                return -1;
            }
        } else if (strcmp(argv[i], "-size") == 0) {
            flag->should_equals = true;
            if (++i < argc) {
                if (argv[i][0] == '+') {
                    flag->greater_than = 1;
                } else if (argv[i][0] == '-') {
                    flag->greater_than = -1;
                } else if (argv[i][0] == '=') {
                    flag->greater_than = 0;
                } else {
                    printf("expected + - or = first character after -size\n");
                    return -1;
                }

                flag->size = atoll(&argv[i][1]);
            } else {
                //error with parsing
                printf("expected [+|-|=][size_in_bytes] after -size\n");
                return -1;
            }
        } else if (strcmp(argv[i], "-nlinks") == 0) {
            flag->should_links = true;
            if (++i < argc) {
                flag->links_num = atoi(argv[i]);
            } else {
                //error with parsing
                printf("expected number of links after -nlinks\n");
                return -1;
            }
        } else if (strcmp(argv[i], "-exec") == 0) {
            flag->should_execute = true;
            if (++i < argc) {
                flag->path_to_executable = argv[i];
            } else {
                //error with parsing
                printf("expected path to executable after -exec\n");
                return -1;
            }
        } else {
            //error
            printf("undefined token: %s\n", argv[i]);
            return -1;
        }
    }
    return 0;
}

void dfs(const char *path, const struct flags *flag) {
    const int file = open(path, O_RDONLY | O_DIRECTORY);
    if (file == -1) {
        char *fp = malloc(sizeof(char) * (strlen(path) + 6));
        strcpy(fp, "open ");
        strcat(fp, path);
        perror(fp);
        free(fp);
        return;
    }

    char *buf = malloc(sizeof(char) * BUF_SIZE);
    struct linux_dirent64 *dirp = NULL;

    while (true) {
        long rez = syscall(SYS_getdents64, file, buf, BUF_SIZE);

        if (rez == -1) {
            perror("getdents");
            close(file);
            free(buf);
            return;
        }

        if (rez == 0) {
            break;
        }

        for (int i = 0; i < rez;) {
            dirp = (struct linux_dirent64 *) (buf + i);

            if (strcmp(dirp->d_name, ".") == 0 || strcmp(dirp->d_name, "..") == 0) {
                i += dirp->d_reclen;
                continue;
            }

            char *fp = malloc(sizeof(char) * (strlen(path) + strlen(dirp->d_name) + 2));
            strcpy(fp, path);
            strcat(fp, "/");
            strcat(fp, dirp->d_name);

            if (dirp->d_type == DT_REG) {
                struct stat sb;
                int rstat = stat(fp, &sb);

                if (rstat == -1) {
                    perror("stat");
                    i += dirp->d_reclen;
                    free(fp);
                    continue;
                }

                bool isOk = true;

                if (flag->should_name) {
                    if (strcmp(dirp->d_name, flag->name) != 0) {
                        isOk = false;
                    }
                }
                if (flag->should_inode) {
                    if (flag->inode_num != dirp->d_ino) {
                        isOk = false;
                    }
                }
                if (flag->should_links) {
                    if (flag->links_num != sb.st_nlink) {
                        isOk = false;
                    }
                }
                if (flag->should_equals) {
                    if (flag->greater_than == 1 && sb.st_size < flag->size) {
                        isOk = false;
                    } else if (flag->greater_than == 0 && flag->size != sb.st_size) {
                        isOk = false;
                    } else if (flag->greater_than == -1 && sb.st_size > flag->size) {
                        isOk = false;
                    }
                }
                if (isOk) {
                    printf("%s\n", fp);
                    if (flag->should_execute) {
                        char *argv2[] = {flag->path_to_executable, fp, NULL};
                        char *envp[] = {NULL, NULL};
                        const pid_t pid = fork();
                        if (pid == -1) {
                            perror("fork");
                        } else {
                            if (!pid) {
                                execve(argv2[0], argv2, envp);
                                perror("execve");
                                exit(EXIT_FAILURE);
                            } else {
                                if (waitpid(pid, 0, 0) == -1) {
                                    perror("waitpid");
                                };
                            }
                        }
                    }
                }
            } else if (dirp->d_type == DT_DIR) {
                dfs(fp, flag);
            }
            free(fp);
            i += dirp->d_reclen;
        }
    }

    free(buf);
    close(file);
}


int main(int argc, const char *argv[]) {
    if (argc < 2) {
        printf("Usage: find [search directory] [parameters]\n");
        exit(EXIT_FAILURE);
    }

    if (strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0) {
        printf("Usage: find [search directory] [parameters]\n"
               " Parameters are:\n"
               "  -h, --help - display this information\n"
               "  -inum <number> - find file by number of inode\n"
               "  -name <filename> - find file by name\n"
               "  -nlinks <number> - find file by number of hardlinks\n"
               "  -size [+-=]<number> - find file which size more, less or equal of this size\n"
               "  -exec <path to executable> - pass found file name in first argument to the executable\n"
               );
        return 0;
    }

    const char *path = argv[1];
    struct flags flag;
    if (parse(argc, argv, &flag) != 0) {
        exit(EXIT_FAILURE);
    }

    dfs(path, &flag);

    return 0;
}
