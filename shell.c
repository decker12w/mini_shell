/*
Autor: José Maia de Oliveira
Data: 05/05/2024
RA: 823395

Estruturas implementadas:

a. $ prog    X
b. $ prog &   X             .
c. $ prog parâmetros X
d. $ prog parâmetros & X
e. $ prog [parâmetros] > arquivo X
f. $ prog1 | prog 2  Metáde da implementação. Ele so funciona para 2 comandos encadeados, 
não para uma cadeia de comandos

Comandos implementados: jobs, fg, bg, cd, exit

*/

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_CMD_LEN 128
#define MAX_ARGS 50

typedef struct process {
  struct process *next;
  char *comando;
  char **argv;
  pid_t pid;
  int completed;
  int stopped;
  int status;
} process;

process *processes = NULL;

int comandosCriados(char *[]);
int comandoCd(char *[]);
int comandoExit();
int comandoJobs();
int comandoFg(char *[]);
int comandoBg(char *[]);
void limparProcessos();
void impressãoPrompt();
process *criarProcesso(char *comando, char **argv, pid_t pid);
void executarComandoComPipe(char *cmd);


void sigchld_handler(int sig) {
  process *p = processes;
  while (p != NULL) {
    if (waitpid(p->pid, &(p->status), WNOHANG | WUNTRACED) > 0) {
      if (WIFEXITED(p->status)) {
        printf("Processo %d terminou com status %d\n", p->pid,
               WEXITSTATUS(p->status));
        p->completed = 1;
      } else if (WIFSIGNALED(p->status)) {
        printf("Processo %d terminou devido ao sinal %d\n", p->pid,
               WTERMSIG(p->status));
        p->completed = 1;
      } else if (WIFSTOPPED(p->status)) {
        p->stopped = 1;
        printf("Processo %d parou devido ao sinal %d\n", p->pid,
               WSTOPSIG(p->status));
      }
    }
    p = p->next;
  }
}

int main(int argc, char *argv[]) {

  char cmd[MAX_CMD_LEN];
  int pid1, pid2;
  int status;

  struct sigaction sa;
  sa.sa_handler = &sigchld_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  if (sigaction(SIGCHLD, &sa, 0) == -1) {
    perror(0);
    exit(1);
  }

  while (1) {
    limparProcessos();
    impressãoPrompt();
    if (fgets(cmd, MAX_CMD_LEN, stdin) == NULL) {
      break;
    }
    cmd[strcspn(cmd, "\n")] = '\0';

if (strchr(cmd, '|') != NULL) {
      executarComandoComPipe(cmd);
      continue;

    }
    int i = 0;
    argv[i] = strtok(cmd, " ");

    while (argv[i] != NULL) {

      argv[++i] = strtok(NULL, " ");
    }

    if (strcmp(argv[0], "exit") == 0) {
      comandoExit();
    }

    if (comandosCriados(argv) >= 0) {
      continue;
    }

    int fd = -1;
    for (int j = 0; j < i; j++) {
      if (strcmp(argv[j], ">") == 0) {
        fd = open(argv[j + 1], O_CREAT | O_WRONLY | O_APPEND, 0666);
        if (fd < 1) {
          perror("Erro ao abrir o arquivo de log");
          exit(EXIT_FAILURE);
        }
        argv[j] = NULL; // Remove o redirecionamento do argv
        break;
      }
    }

    int background = 0;
    if (i > 0 && strcmp(argv[i - 1], "&") == 0) {
      background = 1;
      argv[i - 1] = NULL;
    }

    pid1 = fork();
    if (pid1 < 0) {
      perror("Erro ao criar processo filho\n");
    } else if (pid1 == 0) {

      setpgid(0, 0);

      if (fd != -1) {
        dup2(fd, STDOUT_FILENO); // Redireciona a saída padrão para o arquivo
        close(fd);
      }
      if ((execvp(argv[0], argv) == -1)) {
        perror("Erro ao executar o comando");
      }
      exit(EXIT_FAILURE);

    } else {
      process *p = criarProcesso(cmd, argv, pid1);

      if (!background) {
        int pid_filho = waitpid(pid1, &status, WUNTRACED);

        if (WIFEXITED(status)) {
          p->completed = 1;
          p->status = WEXITSTATUS(status);
        }
        if (WIFSIGNALED(status)) {
          p->completed = 1;
          p->status = WTERMSIG(status);
        }
        if (WIFSTOPPED(status)) {
          p->stopped = 1;
          p->status = WSTOPSIG(status);
        }
      } else {
        printf("Processo %d em execução em segundo plano\n", pid1);
      }
    }
  }

  return EXIT_SUCCESS;
}

int comandosCriados(char *argv[]) {
  if (strcmp(argv[0], "cd") == 0) {
    return comandoCd(argv);
  } else if (strcmp(argv[0], "jobs") == 0) {
    return comandoJobs();
  } else if (strcmp(argv[0], "fg") == 0) {
    return comandoFg(argv);
  } else if (strcmp(argv[0], "bg") == 0) {
    return comandoBg(argv);
  }
  return -1;
}

process *criarProcesso(char *comando, char **argv, pid_t pid) {
  process *p = malloc(sizeof(process));
  p->comando = strdup(argv[0]);
  p->argv = argv;
  p->pid = pid;
  p->completed = 0;
  p->stopped = 0;
  p->status = 0;
  p->next = processes;
  processes = p;
  return p;
}

int comandoCd(char *argv[]) {
  if (argv[1] == NULL) {
    fprintf(stderr, "Esperado argumento para \"cd\"\n");
    return -1;
  } else {
    if (chdir(argv[1]) != 0) {
      perror("cd falhou");
      return -1;
    }
    return 1;
  }
}

int comandoExit() {
  exit(EXIT_SUCCESS);
  return 0;
}

int comandoJobs() {
  process *p = processes;
  while (p != NULL) {
    printf("%d: %s", p->pid, p->comando);
    if (p->completed) {
      printf(" - concluído");
    } else if (p->stopped) {
      printf(" - parado");
    } else {
      printf(" - em execução");
    }
    printf("\n");
    p = p->next;
  }
  return 0;
}
int comandoFg(char *argv[]) {
  if (argv[1] == NULL) {
    fprintf(stderr, "Esperado argumento para \"fg\"\n");
    return -1;
  } else {
    pid_t pid = atoi(argv[1]);
    process *p = processes;
    while (p != NULL) {
      if (p->pid == pid) {
        if (p->stopped) {
          kill(pid, SIGCONT);
          p->stopped = 0;
        }
        tcsetpgrp(STDIN_FILENO, p->pid);
        waitpid(pid, &(p->status), WUNTRACED);
        if (WIFSTOPPED(p->status)) {
          p->stopped = 1;
        } else {
          p->completed = 1;
        }
        tcsetpgrp(STDIN_FILENO, getpgrp());
        return 1;
      }
      p = p->next;
    }
    fprintf(stderr, "Processo %d não encontrado\n", pid);
    return -1;
  }
}

int comandoBg(char *argv[]) {
  if (argv[1] == NULL) {
    fprintf(stderr, "Esperado argumento para \"bg\"\n");
    return -1;
  } else {
    pid_t pid = atoi(argv[1]);
    process *p = processes;
    while (p != NULL) {
      if (p->pid == pid) {
        if (p->stopped) {
          kill(pid, SIGCONT);
          p->stopped = 0;
        }
        return 1;
      }
      p = p->next;
    }
    fprintf(stderr, "Processo %d não encontrado\n", pid);
    return -1;
  }
}

void limparProcessos() {
  process *p = processes;
  process *anterior = NULL;
  while (p != NULL) {
    if (p->completed) {
      if (anterior == NULL) {
        processes = p->next;
        free(p);
        p = processes;
      } else {
        anterior->next = p->next;
        free(p);
        p = anterior->next;
      }
    } else {
      anterior = p;
      p = p->next;
    }
  }
}

void impressãoPrompt() {

  struct passwd *pw;
  pw = getpwuid(getuid());

  char *user = pw->pw_name;
  char hostname[1024];
  char cwd[MAX_CMD_LEN];
  char *argv[MAX_ARGS];

  gethostname(hostname, 1024);
  getcwd(cwd, 1024);

  printf("\033[36m%s@%s:%s$ \033[0m", user, hostname, cwd);
}
void executarComandoComPipe(char *cmd) {
  char *cmd1 = strtok(cmd, "|");
  char *cmd2 = strtok(NULL, "|");

  char *argv1[MAX_ARGS];
  char *argv2[MAX_ARGS];

  int i = 0;
  argv1[i] = strtok(cmd1, " ");
  while (argv1[i] != NULL) {
    argv1[++i] = strtok(NULL, " ");
  }

  i = 0;
  argv2[i] = strtok(cmd2, " ");
  while (argv2[i] != NULL) {
    argv2[++i] = strtok(NULL, " ");
  }

  int pipefd[2];
  pipe(pipefd);

  pid_t pid1 = fork();
  if (pid1 == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    execvp(argv1[0], argv1);
    exit(EXIT_FAILURE);
  }

  pid_t pid2 = fork();
  if (pid2 == 0) {
    close(pipefd[1]);
    dup2(pipefd[0], STDIN_FILENO);
    execvp(argv2[0], argv2);
    exit(EXIT_FAILURE);
  }

  close(pipefd[0]);
  close(pipefd[1]);
  waitpid(pid1, NULL, 0);
  waitpid(pid2, NULL, 0);
}