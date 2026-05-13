/*-
 * main.c
 * Minishell C source
 * Shows how to use "obtain_order" input interface function.
 *
 * Copyright (c) 1993-2002-2019, Francisco Rosales <frosal@fi.upm.es>
 * Todos los derechos reservados.
 *
 * Publicado bajo Licencia de Proyecto Educativo Práctico
 * <http://laurel.datsi.fi.upm.es/~ssoo/LICENCIA/LPEP>
 *
 * Queda prohibida la difusión total o parcial por cualquier
 * medio del material entregado al alumno para la realización
 * de este proyecto o de cualquier material derivado de este,
 * incluyendo la solución particular que desarrolle el alumno.
 *
 * DO NOT MODIFY ANYTHING OVER THIS LINE
 * THIS FILE IS TO BE MODIFIED
 */

#include <stddef.h>			/* NULL */
#include <stdio.h>			/* setbuf, printf */
#include <stdlib.h>
#include <unistd.h>     	/* fork, execvp, dup2 */
#include <sys/types.h>  	/* pid_t */
#include <sys/wait.h>   	/* waitpid, WIFEXITED, WEXITSTATUS */
#include <fcntl.h>      	/* open, O_RDONLY, O_WRONLY, ... */
#include <signal.h>     	/* sigaction, SIGINT, SIGQUIT, SIGIOT */
#include <string.h>        /* strcmp */
#include <limits.h>        /* PATH_MAX */

extern int obtain_order(char ****argvvp, char *filep[3], int *bgp);  /* See parser.y for description */

static void set_shell_signals(void)
{
    struct sigaction sa;

    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGIOT, &sa, NULL);
}

static void set_child_signals(int background)
{
    struct sigaction sa;

    sa.sa_handler = background ? SIG_IGN : SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGIOT, &sa, NULL);
}

int main(void)
{
    /* argvv: pipeline de comandos, filev: redirecciones, bg: bandera de fondo */
    char ***argvv = NULL;
    int argvc;
    char *filev[3] = { NULL, NULL, NULL };
    int bg;
    int ret;

    /* Desactiva el buffer de stdio para prompt y entrada */
    setbuf(stdout, NULL);           /* Unbuffered */
    setbuf(stdin, NULL);

    /* El shell ignora senales de teclado */
    set_shell_signals();

    while (1) {
        int status;

        while (waitpid(-1, &status, WNOHANG) > 0) {
            /* Recolecta hijos en segundo plano terminados para evitar zombies */    
        }

        /* Muestra prompt y parsea una linea de comandos */
        fprintf(stderr, "%s", "msh> ");     /* Prompt */
        ret = obtain_order(&argvv, filev, &bg);
        if (ret == 0) break;                /* EOF */
        if (ret == -1) continue;            /* Syntax error */
        argvc = ret - 1;                    /* Line */
        if (argvc == 0) continue;           /* Empty line */

        /* Mandato interno: cd (solo si no hay pipeline) */
        if (argvc == 1 && argvv[0] && argvv[0][0] && strcmp(argvv[0][0], "cd") == 0) {
            char *target = argvv[0][1];

            if (!target || target[0] == '\0') {
                target = getenv("HOME");
            }

            if (!target || target[0] == '\0') {
                fprintf(stderr, "cd: HOME not set\n");
            } else if (chdir(target) < 0) {
                perror(target);
            } else {
                char cwd[PATH_MAX];
                if (getcwd(cwd, sizeof(cwd)) != NULL) {
                    printf("%s\n", cwd);
                } else {
                    perror("getcwd");
                }
            }

            continue;
        }

        /* Estado de ejecucion de la pipeline: se comparte en el bucle */
        int i;
        int prev_read = -1;
        int started = 0;
        int in_fd = -1;
        int out_fd = -1;
        int err_fd = -1;
        int redir_ok = 1;
        pid_t *pids = calloc(argvc, sizeof(pid_t));

        /* Reserva arreglo de pid para la pipeline */
        if (!pids) {
            perror("[ERROR] calloc");
            continue;
        }

        /* Las redirecciones se preparan antes de fork para fallar temprano */
        /* Abre redireccion de entrada si existe */
        if (filev[0]) {
            in_fd = open(filev[0], O_RDONLY);
            if (in_fd < 0) {
                perror(filev[0]);
                redir_ok = 0;
            }
        }

        /* Abre redireccion de salida si existe */
        if (redir_ok && filev[1]) {
            out_fd = creat(filev[1], 0666);
            if (out_fd < 0) {
                perror(filev[1]);
                redir_ok = 0;
            }
        }

        /* Abre redireccion de error si existe */
        if (redir_ok && filev[2]) {
            err_fd = creat(filev[2], 0666);
            if (err_fd < 0) {
                perror(filev[2]);
                redir_ok = 0;
            }
        }

        /* Aborta temprano si falla una redireccion */
        if (!redir_ok) {
            if (in_fd != -1) close(in_fd);
            if (out_fd != -1) close(out_fd);
            if (err_fd != -1) close(err_fd);
            free(pids);
            continue;
        }

        /* Se crea un proceso por comando; los pipes conectan salida-entrada */
        for (i = 0; i < argvc; i++) {
            int pipefd[2] = { -1, -1 };

            /* Crea pipe para todos menos el ultimo comando */
            if (i < argvc - 1) {
                if (pipe(pipefd) < 0) {
                    perror("[ERROR] pipe");
                    break;
                }
            }

            /* Proceso hijo: hereda fds y solo ajusta lo necesario */
            pid_t pid = fork();
            if (pid == 0) {
                /* En foreground se deja accion por defecto */
                /* En background se ignoran senales de teclado */
                set_child_signals(bg);

                if (prev_read != -1) {
                    if (dup2(prev_read, STDIN_FILENO) < 0) {
                        perror("dup2 stdin");
                        if (prev_read != -1) close(prev_read);
                        if (pipefd[0] != -1) close(pipefd[0]);
                        if (pipefd[1] != -1) close(pipefd[1]);
                        if (in_fd != -1) close(in_fd);
                        if (out_fd != -1) close(out_fd);
                        if (err_fd != -1) close(err_fd);
                        exit(1);
                    }
                } else if (in_fd != -1) {
                    if (dup2(in_fd, STDIN_FILENO) < 0) {
                        perror("dup2 stdin");
                        if (prev_read != -1) close(prev_read);
                        if (pipefd[0] != -1) close(pipefd[0]);
                        if (pipefd[1] != -1) close(pipefd[1]);
                        if (in_fd != -1) close(in_fd);
                        if (out_fd != -1) close(out_fd);
                        if (err_fd != -1) close(err_fd);
                        exit(1);
                    }
                }

                if (i < argvc - 1) {
                    if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
                        perror("dup2 stdout");
                        if (prev_read != -1) close(prev_read);
                        if (pipefd[0] != -1) close(pipefd[0]);
                        if (pipefd[1] != -1) close(pipefd[1]);
                        if (in_fd != -1) close(in_fd);
                        if (out_fd != -1) close(out_fd);
                        if (err_fd != -1) close(err_fd);
                        exit(1);
                    }
                } else if (out_fd != -1) {
                    if (dup2(out_fd, STDOUT_FILENO) < 0) {
                        perror("dup2 stdout");
                        if (prev_read != -1) close(prev_read);
                        if (pipefd[0] != -1) close(pipefd[0]);
                        if (pipefd[1] != -1) close(pipefd[1]);
                        if (in_fd != -1) close(in_fd);
                        if (out_fd != -1) close(out_fd);
                        if (err_fd != -1) close(err_fd);
                        exit(1);
                    }
                }

                /* Solo redirige stderr en el ultimo comando */
                if (err_fd != -1 && i == argvc - 1) {
                    if (dup2(err_fd, STDERR_FILENO) < 0) {
                        perror("dup2 stderr");
                        if (prev_read != -1) close(prev_read);
                        if (pipefd[0] != -1) close(pipefd[0]);
                        if (pipefd[1] != -1) close(pipefd[1]);
                        if (in_fd != -1) close(in_fd);
                        if (out_fd != -1) close(out_fd);
                        if (err_fd != -1) close(err_fd);
                        exit(1);
                    }
                }

                /* Cierra fds no usados en el hijo */
                if (prev_read != -1) close(prev_read);
                if (pipefd[0] != -1) close(pipefd[0]);

                if (bg && started > 0) {
                    int bgpid = pids[started - 1];
                    fprintf(stderr, "[%d]\n", started, bgpid);
                } else if (started > 0) {
                    waitpid(pids[started - 1], &status, 0);
                    while (waitpid(-1, &status, WNOHANG) > 0) {
                        /* cleanup */
                    }
                    
                }

                if (pipefd[1] != -1) close(pipefd[1]);
                if (in_fd != -1) close(in_fd);
                if (out_fd != -1) close(out_fd);
                if (err_fd != -1) close(err_fd);

                /* Ejecuta comando; si falla, termina el hijo */
                execvp(argvv[i][0], argvv[i]);
                perror(argvv[i][0]);
                exit(1);
            }

            /* Proceso padre: registra hijo y cierra fds para no filtrar */
            pids[i] = pid;
            started++;
            if (prev_read != -1) close(prev_read);
            if (pipefd[1] != -1) close(pipefd[1]);
            prev_read = pipefd[0];
        }

        /* Padre: cierra fds restantes */
        if (prev_read != -1) close(prev_read);
        if (in_fd != -1) close(in_fd);
        if (out_fd != -1) close(out_fd);
        if (err_fd != -1) close(err_fd);

        /* Solo se espera al ultimo para respetar pipeline y evitar bloqueos */
        /* Si es fondo, el shell vuelve al prompt sin esperar */
        if (!bg && started > 0) {
            waitpid(pids[started - 1], &status, 0);
            while (waitpid(-1, &status, WNOHANG) > 0) {
                /* cleanup */
            }
        }

        /* Libera el arreglo de pid de esta linea */
        free(pids);
        
    }
    /* Sale del shell */
    exit(0);
    return 0;
}
