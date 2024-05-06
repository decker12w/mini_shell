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
f. $ prog1 | prog 2  Metáde da implementação. Ele so funciona para 2 comandos
encadeados, não para uma cadeia de comandos

Comandos implementados: jobs, fg, bg, cd, exit

Obs: Caso haja algum problema com a exibição do código, pode acessa-lo nesse
repositório do git:

*/

#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// Constantes
#define MAX_CMD_LEN 128 // Tamanho máximo do comando
#define MAX_ARGS 50     // Número máximo de argumentos

// Estrutura de dados para armazenar informações sobre um processo
// Ela é uma lista encadeada de processos
typedef struct process {
  struct process *next;
  char *comando;
  char **argv;
  pid_t pid;
  int completed;
  int stopped;
  int status;
} process;

// Inicialização da lista de processos
process *processes = NULL;

// Protótipos de funções
int comandosCriados(char *[]);
int comandoCd(char *[]);
int comandoExit();
int comandoJobs();
int comandoFg(char *[]);
int comandoBg(char *[]);
void limparProcessos();
void impressãoPrompt();
process *criarProcesso(char *, char **, pid_t);
void executarComandoComPipe(char *cmd);

// Função de tratamento de sinal SIGCHLD para atualizar informações sobre os
// processos filhos, desalocando os recursos utilizados, para que os processos
// não virem um Zombies
void sigchld_handler(int sig) {
  process *p = processes;

  // Percorre a lista de processos para verificar se algum processo foi:
  // terminado, parado, ou finalizado e imprime o estado do processo
  while (p != NULL) {
    // O WNOHANG faz com que a função waitpid não bloqueie o processo pai e o
    // WUNTRACED faz com que a função waitpid retorne se o processo filho foi
    // parado
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

  // Definição do sigaction para tratar o sinal SIGCHLD
  struct sigaction sa;
  sa.sa_handler = &sigchld_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  if (sigaction(SIGCHLD, &sa, 0) == -1) {
    perror(0);
    exit(1);
  }

  // Loop principal do shell
  while (1) {

    // Função para limpar a memoria alocada para os processos finalizados
    limparProcessos();

    // Função para imprimir o prompt do shell: user@hostname:cwd$
    impressãoPrompt();

    // Leitura do comando do usuário
    if (fgets(cmd, MAX_CMD_LEN, stdin) == NULL) {
      break;
    }

    // Verifica se o comando contém um caractere de nova linha decorrente do
    // enter e o remove
    cmd[strcspn(cmd, "\n")] = '\0';

    // Verifica se o comando contém um pipe e executa uma função exclusiva para
    // executar comandos com pipe
    if (strchr(cmd, '|') != NULL) {
      executarComandoComPipe(cmd);
      continue;
    }

    // Separa o comando em argumentos com os tokens do strtok e armazena em um
    // vetor de strings
    int i = 0;
    argv[i] = strtok(cmd, " ");

    while (argv[i] != NULL) {

      argv[++i] = strtok(NULL, " ");
    }

    // Comando exit
    if (strcmp(argv[0], "exit") == 0) {
      comandoExit();
    }

    // Verifica se o comando é um dos comandos internos implementados e os
    // executa
    if (comandosCriados(argv) >= 0) {
      continue;
    }

    // Verifica se o comando contém um simbolo de redirecionamento
    int fd = -1;
    for (int j = 0; j < i; j++) {
      if (strcmp(argv[j], ">") == 0) {

        // Abre o arquivo de log para escrita
        fd = open(argv[j + 1], O_CREAT | O_WRONLY | O_APPEND, 0666);
        if (fd < 1) {
          perror("Erro ao abrir o arquivo de log");
          exit(EXIT_FAILURE);
        }
        argv[j] = NULL; // Remove o redirecionamento do argv
        break;
      }
    }

    // Verifica se o comando deve ser executado em segundo plano
    int background = 0;
    if (i > 0 && strcmp(argv[i - 1], "&") == 0) {
      background = 1;
      argv[i - 1] = NULL;
    }

    // Cria um processo filho para executar o comando
    pid1 = fork();
    if (pid1 < 0) {
      perror("Erro ao criar processo filho\n");
    } else if (pid1 == 0) {

      setpgid(0, 0);

      // Verifica se o comando deve ser redirecionado para um arquivo
      if (fd != -1) {
        dup2(fd, STDOUT_FILENO); // Redireciona a saída padrão para o arquivo
        close(fd);
      }

      // Sobrepoem a imagem do binário especificado pelo comando no processo
      // filho
      if ((execvp(argv[0], argv) == -1)) {
        perror("Erro ao executar o comando");
      }
      exit(EXIT_FAILURE);

    } else {

      // Cria um processo para armazenar informações sobre o processo filho
      process *p = criarProcesso(cmd, argv, pid1);

      // Verifica se está em segundo plano e aguarda o término do processo
      if (!background) {
        int pid_filho = waitpid(pid1, &status, WUNTRACED);

        // Verifica o status do processo filho
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

        // Imprime mensagem informando que o processo está em execução em
        // segundo plano
        printf("Processo %d em execução em segundo plano\n", pid1);
      }
    }
  }

  return EXIT_SUCCESS;
}

// Função para verificar se o comando é um dos comandos internos implementados e
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

// Função para criar um processo e armazenar informações sobre ele
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

// Função para executar o comando cd
int comandoCd(char *argv[]) {
  if (argv[1] == NULL) {
    fprintf(stderr, "Esperado argumento para \"cd\"\n");
    return -1;
  } else {
    // Se o comando cd for executado com sucesso, muda o diretório atual
    if (chdir(argv[1]) != 0) {
      perror("cd falhou");
      return -1;
    }
    return 1;
  }
}

// Função para executar o comando exit
int comandoExit() {
  exit(EXIT_SUCCESS);
  return 0;
}

// Função para executar o comando jobs listando os processos em execução
int comandoJobs() {
  process *p = processes;

  // Percorre a lista de processos e imprime o estado de cada processo se foi
  // concluído, parado ou está em execução
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

// Função para executar o comando fg
int comandoFg(char *argv[]) {
  if (argv[1] == NULL) {
    fprintf(stderr, "Esperado argumento para \"fg\"\n");
    return -1;
  } else {
    pid_t pid = atoi(argv[1]);
    process *p = processes;

    // Percorre a lista de processos para verificar se o processo está parado e
    // envia um sinal para continuar a execução do processo caso o processo
    // esteja parado
    while (p != NULL) {
      if (p->pid == pid) {
        if (p->stopped) {
          kill(pid, SIGCONT);
          p->stopped = 0;
        }

        // Muda o grupo de processos para que o processo filho possa receber a
        // entrada padrão do terminal
        tcsetpgrp(STDIN_FILENO, p->pid);
        waitpid(pid, &(p->status), WUNTRACED);
        if (WIFSTOPPED(p->status)) {
          p->stopped = 1;
        } else {
          p->completed = 1;
        }

        // Restaura o grupo de processos para o shell
        tcsetpgrp(STDIN_FILENO, getpgrp());
        return 1;
      }
      p = p->next;
    }
    fprintf(stderr, "Processo %d não encontrado\n", pid);
    return -1;
  }
}

// Função para executar o comando bg
int comandoBg(char *argv[]) {
  if (argv[1] == NULL) {
    fprintf(stderr, "Esperado argumento para \"bg\"\n");
    return -1;
  } else {
    pid_t pid = atoi(argv[1]);
    process *p = processes;

    // Percorre a lista de processos para verificar se o processo está parado e
    // envia um sinal para continuar a execução caso o processo esteja parado
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

// Desaloca a memória alocada para os processos finalizados
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

// Função para imprimir o prompt do shell
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

// Função para executar comandos com pipe
// Basicamente ele separa o comando em 2 comandos e executa cada um deles
// criando um processo filho. Eles se comunicam atráves de um canal
// unidirecional (pipe) que repassa a saída do primeiro comando para a entrada
// do segundo comando.
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