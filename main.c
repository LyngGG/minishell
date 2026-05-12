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

extern int obtain_order();  /* See parser.y for description */

int main(void)
{
    char ***argvv = NULL;
    int argvc;
    char *filev[3] = { NULL, NULL, NULL };
    int bg;
    int ret;

    setbuf(stdout, NULL);           /* Unbuffered */
    setbuf(stdin, NULL);

    while (1) {
        int status;

        while (waitpid(-1, &status, WNOHANG) > 0) {
            /* barrido de procesos hijos terminados */
        }

        fprintf(stderr, "%s", "msh> ");     /* Prompt */
        ret = obtain_order(&argvv, filev, &bg);
        if (ret == 0) break;                /* EOF */
        if (ret == -1) continue;            /* Syntax error */
        argvc = ret - 1;                    /* Line */
        if (argvc == 0) continue;           /* Empty line */

        
        int i;
        int prev_read = -1;
        int started = 0;
        int in_fd = -1;
        int out_fd = -1;
        int err_fd = -1;
        int redir_ok = 1;
        pid_t *pids = calloc(argvc, sizeof(pid_t));

        if (!pids) {
            perror("[ERROR] calloc");
            continue;
        }

        if (filev[0]) {
            in_fd = open(filev[0], O_RDONLY);
            if (in_fd < 0) {
                perror(filev[0]);
                redir_ok = 0;
            }
        }

        if (redir_ok && filev[1]) {
            out_fd = open(filev[1], O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (out_fd < 0) {
                perror(filev[1]);
                redir_ok = 0;
            }
        }

        if (redir_ok && filev[2]) {
            err_fd = open(filev[2], O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (err_fd < 0) {
                perror(filev[2]);
                redir_ok = 0;
            }
        }

        if (!redir_ok) {
            if (in_fd != -1) close(in_fd);
            if (out_fd != -1) close(out_fd);
            if (err_fd != -1) close(err_fd);
            free(pids);
            continue;
        }

        for (i = 0; i < argvc; i++) {
            int pipefd[2] = { -1, -1 };

            if (i < argvc - 1) {
                if (pipe(pipefd) < 0) {
                    perror("[ERROR] pipe");
                    break;
                }
            }

            if (pid == 0) {
                if (prev_read != -1) {
                    dup2(prev_read, STDIN_FILENO);
                } else if (in_fd != -1) {
                    dup2(in_fd, STDIN_FILENO);
                }

                if (i < argvc - 1) {
                    dup2(pipefd[1], STDOUT_FILENO);
                } else if (out_fd != -1) {
                    dup2(out_fd, STDOUT_FILENO);
                }

                if (err_fd != -1 && i == argvc - 1) {
                    dup2(err_fd, STDERR_FILENO);
                }

                if (prev_read != -1) close(prev_read);
                if (pipefd[0] != -1) close(pipefd[0]);
                if (pipefd[1] != -1) close(pipefd[1]);
                if (in_fd != -1) close(in_fd);
                if (out_fd != -1) close(out_fd);
                if (err_fd != -1) close(err_fd);

                execvp(argvv[i][0], argvv[i]);
                perror(argvv[i][0]);
                exit(1);
            }

            pids[i] = pid;
            started++;
            if (prev_read != -1) close(prev_read);
            if (pipefd[1] != -1) close(pipefd[1]);
            prev_read = pipefd[0];
        }

        if (prev_read != -1) close(prev_read);
        if (in_fd != -1) close(in_fd);
        if (out_fd != -1) close(out_fd);
        if (err_fd != -1) close(err_fd);

        if (!bg && started > 0) {
            waitpid(pids[started - 1], &status, 0);
            while (waitpid(-1, &status, WNOHANG) > 0) {
                /* cleanup */
            }
        }

        free(pids);
        
    }
    exit(0);
    return 0;
}
