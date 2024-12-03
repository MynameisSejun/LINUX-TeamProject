#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <dirent.h>
#ifndef MAX_INPUT
#define MAX_INPUT 1024
#endif
#define MAX_ARGS 64

// 리디렉션 처리
void handle_redirection(char **args);

// SIGCHLD, SIGINT, SIGQUIT 핸들러
void sigchld_handler(int signo);
void sigint_handler(int signo);
void sigquit_handler(int signo);

// 입력 처리 및 명령어 파싱
int parse_input(char *input, char **args);
void print_prompt();
void show_jobs();
void handle_pipe(char *input);

// 빌트인 명령어 실행
void execute_ls();
void execute_pwd();
void execute_cd(char *path);
void execute_mkdir(char *path);
void execute_rmdir(char *path);
void execute_ln(char *source, char *dest);
void execute_cp(char *source, char *dest);
void execute_rm(char *path);
void execute_mv(char *source, char *dest);
void execute_cat(char *path);
int handle_builtin_commands(char **args);
int execute_builtin(char **args);
// 백그라운드 프로세스 정보를 저장하는 구조체
struct bg_process {
    pid_t pid;
    char command[MAX_INPUT];
};

// 백그라운드 프로세스 목록
struct bg_process bg_processes[100];
int bg_count = 0;

// SIGCHLD 핸들러: 종료된 백그라운드 프로세스 처리
void sigchld_handler(int signo) {
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < bg_count; i++) {
            if (bg_processes[i].pid == pid) {
                printf("\n[%d] 완료    %s\n", pid, bg_processes[i].command);

                // 완료된 프로세스 제거
                for (int j = i; j < bg_count - 1; j++) {
                    bg_processes[j] = bg_processes[j + 1];
                }
                bg_count--;
                break;
            }
        }
    }
}

// SIGINT 핸들러: Ctrl-C 처리
void sigint_handler(int signo) {
    printf("\nSIGINT 수신: 프로세스 종료\n");
}

// SIGQUIT 핸들러: Ctrl-Z 처리
void sigquit_handler(int signo) {
    printf("\nSIGQUIT 수신: 프로세스 일시 중지\n");
}

// 문자열을 공백 기준으로 분리
int parse_input(char *input, char **args) {
    int i = 0;
    int background = 0;

    char *token = strtok(input, " \n");
    while (token != NULL && i < MAX_ARGS - 1) {
        if (strcmp(token, "&") == 0) {
            background = 1;  // 백그라운드 실행 플래그 설정
            break;
        }
        args[i++] = token;
        token = strtok(NULL, " \n");
    }
    args[i] = NULL;

    if (i > 0 && args[i - 1][strlen(args[i - 1]) - 1] == '&') {
        args[i - 1][strlen(args[i - 1]) - 1] = '\0';
        background = 1;
    }

    return background;
}

void print_prompt() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("\n%s $ ", cwd);
    } else {
        printf("\n$ ");
    }
    fflush(stdout);
}

void show_jobs() {
    if (bg_count == 0) {
        printf("실행 중인 백그라운드 프로세스가 없습니다.\n");
        return;
    }

    printf("\n실행 중인 백그라운드 프로세스 목록:\n");
    for (int i = 0; i < bg_count; i++) {
        printf("[%d] 실행 중  %s\n", bg_processes[i].pid, bg_processes[i].command);
    }
}

void handle_pipe(char *input) {
    // 파이프 처리 함수 구현 (기존 코드에서 복사)
    char *commands[MAX_ARGS];
    int num_commands = 0;
    char *token = strtok(input, "|");

    while (token != NULL && num_commands < MAX_ARGS) {
        commands[num_commands++] = token;
        token = strtok(NULL, "|");
    }

    int pipefds[2 * (num_commands - 1)];
    for (int i = 0; i < num_commands - 1; i++) {
        if (pipe(pipefds + i * 2) == -1) {
            perror("파이프 생성 실패");
            exit(1);
        }
    }

    for (int i = 0; i < num_commands; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("프로세스 생성 실패");
            exit(1);
        } else if (pid == 0) {
            if (i > 0) {
                dup2(pipefds[(i - 1) * 2], STDIN_FILENO);
            }
            if (i < num_commands - 1) {
                dup2(pipefds[i * 2 + 1], STDOUT_FILENO);
            }

            for (int j = 0; j < 2 * (num_commands - 1); j++) {
                close(pipefds[j]);
            }

            char *args[MAX_ARGS];
            parse_input(commands[i], args);
            handle_redirection(args);

            if (execvp(args[0], args) == -1) {
                perror("명령어 실행 실패");
                exit(1);
            }
        }
    }

    for (int i = 0; i < 2 * (num_commands - 1); i++) {
        close(pipefds[i]);
    }
    for (int i = 0; i < num_commands; i++) {
        wait(NULL);
    }
}

void handle_redirection(char **args) {
    int fd;
    int j = 0;

    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], ">") == 0) {
            if (args[i + 1] == NULL) {
                fprintf(stderr, "리디렉션 대상 파일을 지정해야 합니다.\n");
                exit(1);
            }
            fd = open(args[i + 1], O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (fd == -1) {
                perror("출력 파일 열기 실패");
                exit(1);
            }
            if (dup2(fd, STDOUT_FILENO) == -1) {
                perror("dup2 실패");
                close(fd);
                exit(1);
            }
            close(fd);
            printf("출력을 파일 '%s'로 리디렉션합니다.\n", args[i + 1]);
            args[i] = NULL;
            args[i + 1] = NULL;
            for (int j = i; args[j + 2] != NULL; j++) {
                args[j] = args[j + 2];
            }
            args[j] = NULL;
            break;
        } else if (strcmp(args[i], "<") == 0) {
            if (args[i + 1] == NULL) {
                fprintf(stderr, "입력 파일을 지정해야 합니다.\n");
                exit(1);
            }
            fd = open(args[i + 1], O_RDONLY);
            if (fd == -1) {
                perror("입력 파일 열기 실패");
                exit(1);
            }
            if (dup2(fd, STDIN_FILENO) == -1) {
                perror("dup2 실패");
                close(fd);
                exit(1);
            }
            close(fd);
            printf("입력을 파일 '%s'로 리디렉션합니다.\n", args[i + 1]);
            args[i] = NULL;
            args[i + 1] = NULL;
            for (int j = i; args[j + 2] != NULL; j++) {
                args[j] = args[j + 2];
            }
            args[j] = NULL;
            break;
        }
    }
}





// 명령어 처리 함수들
void execute_ls() {
    DIR *dir = opendir(".");
    if (dir == NULL) {
        perror("디렉토리 열기 실패");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') { // 숨김 파일 제외
            printf("%s  ", entry->d_name);
        }
    }
    printf("\n");
    closedir(dir);
}

void execute_pwd() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("현재 디렉토리 가져오기 실패");
    }
}

void execute_cd(char *path) {
    if (path == NULL) {
        fprintf(stderr, "cd: 디렉토리를 지정해야 합니다.\n");
        return;
    }

    if (chdir(path) == -1) {
        perror("cd 실패");
    }
}

void execute_mkdir(char *path) {
    if (mkdir(path, 0755) == -1) {
        perror("mkdir 실패");
    }
}

void execute_rmdir(char *path) {
    if (rmdir(path) == -1) {
        perror("rmdir 실패");
    }
}

void execute_ln(char *source, char *dest) {
    if (link(source, dest) == -1) {
        perror("ln 실패");
    }
}

void execute_cp(char *source, char *dest) {
    int src_fd = open(source, O_RDONLY);
    if (src_fd == -1) {
        perror("cp 실패");
        return;
    }

    int dest_fd = open(dest, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (dest_fd == -1) {
        perror("cp 실패");
        close(src_fd);
        return;
    }

    char buffer[1024];
    ssize_t n_read;
    while ((n_read = read(src_fd, buffer, sizeof(buffer))) > 0) {
        write(dest_fd, buffer, n_read);
    }

    close(src_fd);
    close(dest_fd);
}

void execute_rm(char *path) {
    if (unlink(path) == -1) {
        perror("rm 실패");
    }
}

void execute_mv(char *source, char *dest) {
    if (rename(source, dest) == -1) {
        perror("mv 실패");
    }
}

void execute_cat(char *path) {
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        perror("cat 실패");
        return;
    }

    char buffer[1024];
    ssize_t n_read;
    while ((n_read = read(fd, buffer, sizeof(buffer))) > 0) {
        write(STDOUT_FILENO, buffer, n_read);
    }

    close(fd);
}

int handle_builtin_commands(char **args) {
    if (strcmp(args[0], "ls") == 0 || strcmp(args[0], "pwd") == 0 ||
        strcmp(args[0], "cd") == 0 || strcmp(args[0], "mkdir") == 0 ||
        strcmp(args[0], "rmdir") == 0 || strcmp(args[0], "ln") == 0 ||
        strcmp(args[0], "cp") == 0 || strcmp(args[0], "rm") == 0 ||
        strcmp(args[0], "mv") == 0 || strcmp(args[0], "cat") == 0) {
        return 1; // 빌트인 명령어임
    }
    return 0; // 빌트인 명령어가 아님
}

int execute_builtin(char **args) {
    if (strcmp(args[0], "ls") == 0) {
        execute_ls();
        return 0;
    } else if (strcmp(args[0], "pwd") == 0) {
        execute_pwd();
        return 0;
    } else if (strcmp(args[0], "cd") == 0) {
        execute_cd(args[1]);
        return 0;
    } else if (strcmp(args[0], "mkdir") == 0) {
        execute_mkdir(args[1]);
        return 0;
    } else if (strcmp(args[0], "rmdir") == 0) {
        execute_rmdir(args[1]);
        return 0;
    } else if (strcmp(args[0], "ln") == 0) {
        execute_ln(args[1], args[2]);
        return 0;
    } else if (strcmp(args[0], "cp") == 0) {
        execute_cp(args[1], args[2]);
        return 0;
    } else if (strcmp(args[0], "rm") == 0) {
        execute_rm(args[1]);
        return 0;
    } else if (strcmp(args[0], "mv") == 0) {
        execute_mv(args[1], args[2]);
        return 0;
    } else if (strcmp(args[0], "cat") == 0) {
        execute_cat(args[1]);
        return 0;
    }
    return -1; // 빌트인 명령어가 아님
}

int main() {
    char input[MAX_INPUT];
    char *args[MAX_ARGS];
    pid_t pid;

    // 시그널 핸들러 등록
    signal(SIGCHLD, sigchld_handler);
    signal(SIGINT, sigint_handler);
    signal(SIGQUIT, sigquit_handler);

    printf("4조 김가영(팀장), 배재진, 박세준 쉘 프로그램\n");

    int original_stdout = dup(STDOUT_FILENO); // 원래 표준 출력 저장

    while (1) {
        // 프롬프트 출력 전 표준 출력 복구
        dup2(original_stdout, STDOUT_FILENO);

        // 쉘 프롬프트 출력
        print_prompt();

        // 사용자 입력 읽기
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\n");
            break;
        }

        // 입력이 공백 또는 빈 줄이면 다시 프롬프트 출력
        if (strlen(input) == 1) {
            continue;
        }

        // 개행 문자 제거
        input[strcspn(input, "\n")] = 0;

        // "exit" 명령어 처리
        if (strcmp(input, "exit") == 0) {
            printf("쉘을 종료합니다.\n");
            break;
        }

        // "jobs" 명령어 처리
        if (strcmp(input, "jobs") == 0) {
            show_jobs();
            continue;
        }

        // 파이프 처리
        if (strchr(input, '|') != NULL) {
            handle_pipe(input);
            continue;
        }

        // 명령어 파싱 및 백그라운드 여부 확인
        int background = parse_input(input, args);

        // 부모 프로세스에서 빌트인 명령어 확인
        if (handle_builtin_commands(args)) {
            pid = fork();
            if (pid < 0) {
                perror("fork 실패");
                continue;
            } else if (pid == 0) {
                // 자식 프로세스에서 리디렉션 처리
                handle_redirection(args);

                // 빌트인 명령어 실행
                if (execute_builtin(args) == -1) {
                    exit(1); // 빌트인 실행 실패 시 종료
                }
            } else {
                if (!background) {
                    int status;
                    waitpid(pid, &status, 0); // 포그라운드 명령어 대기
                }
            }
            continue;
        }

        // 외부 명령어 실행
        pid = fork();
        if (pid < 0) {
            perror("fork 실패");
            continue;
        } else if (pid == 0) {
            handle_redirection(args); // 자식 프로세스에서 리디렉션 처리
            // 디버깅: args 배열 상태 출력
        for (int i = 0; args[i] != NULL; i++) {
            printf("args[%d]: %s\n", i, args[i]);
        }

        if (execvp(args[0], args) == -1) {
            perror("명령어 실행 실패");
            exit(1);
        }

            // 외부 명령어 실행
            if (execvp(args[0], args) == -1) {
                perror("명령어 실행 실패");
                exit(1);
            }
        } else {
            if (background) {
                // 백그라운드 프로세스 관리
                bg_processes[bg_count].pid = pid;
                strcpy(bg_processes[bg_count].command, input);
                printf("[%d] %d\n", bg_count + 1, pid);
                bg_count++;
            } else {
                // 포그라운드 명령어 대기
                int status;
                waitpid(pid, &status, 0);
            }
        }
    }
    return 0;
}

