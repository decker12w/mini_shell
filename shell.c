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
int comandoPs();


void sigchld_handler(int sig) {
  process *p = processes;
  while (p != NULL) {
    if (waitpid(p->pid, &(p->status), WNOHANG | WUNTRACED) > 0) {
      if (WIFEXITED(p->status)) {
        printf("Processo %d terminou com status %d\n", p->pid, WEXITSTATUS(p->status));
        p->completed = 1;
      } else if (WIFSIGNALED(p->status)) {
        printf("Processo %d terminou devido ao sinal %d\n", p->pid, WTERMSIG(p->status));
        p->completed = 1;
      } else if (WIFSTOPPED(p->status)) {
        p->stopped = 1;
        printf("Processo %d parou devido ao sinal %d\n", p->pid, WSTOPSIG(p->status));
      }
    }
    p = p->next;
  }
}

int main(int argc, char *argv[]) {

  char cmd[MAX_CMD_LEN];
  int pid;
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

    pid = fork();
    if (pid < 0) {
      perror("Erro ao criar processo filho\n");
    } else if (pid == 0) {
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
      process *p = malloc(sizeof(process));
      p->comando = strdup(argv[0]);
      p->argv = argv;
      p->pid = pid;
      p->completed = 0;
      p->stopped = 0;
      p->status = 0;
      p->next = processes;
      processes = p;

      if (!background) {
        int pid_filho = wait(&status);
        if (WIFEXITED(status)) {
          p->completed = 1;
          p->status = WEXITSTATUS(status);
        }
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
