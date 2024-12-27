/*****************************************************************************/
/*                           CSC209-24s A3 CSCSHELL                          */
/*       Copyright 2024 -- Demetres Kostas PhD (aka Darlene Heliokinde)      */
/*****************************************************************************/

#include "cscshell.h"


// COMPLETE
int cd_cscshell(const char *target_dir){
    if (target_dir == NULL) {
        char user_buff[MAX_USER_BUF];
        if (getlogin_r(user_buff, MAX_USER_BUF) != 0) {
           perror("run_command");
           return -1;
        }
        struct passwd *pw_data = getpwnam((char *)user_buff);
        if (pw_data == NULL) {
           perror("run_command");
           return -1;
        }
        target_dir = pw_data->pw_dir;
    }

    if(chdir(target_dir) < 0){
        perror("cd_cscshell");
        return -1;
    }
    return 0;
}

int *execute_line(Command *head){
    
    // handle empty command, nothing to execute
    if (!head) {
        return NULL;
    }

    // initialize and allocate return status
    int *return_status = malloc(sizeof(int));
    
    // error checking
    if (!return_status) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    *return_status = 0;

    // create pipe, track the output end of the last pipe
    int pipe_fds[2];
    // int last_output_fd = -1;

    Command *current_cmd = head;

    while (current_cmd != NULL) {
        // set up piping if next command exists
        if (current_cmd->next) {
            
            // error checking
            if (pipe(pipe_fds) == -1) {
                perror("pipe");
                exit(EXIT_FAILURE);
            }

            current_cmd->stdout_fd = pipe_fds[1]; 
            current_cmd->next->stdin_fd = pipe_fds[0];
        }

        // use cd_cscshell if current_cmd is cd
        if(strcmp(current_cmd->exec_path, "cd") == 0) {
            *return_status = cd_cscshell(current_cmd->args[1]);
        }

        // else run current_cmd
        else {
            int pid = run_command(current_cmd);
            waitpid(pid, return_status, 0);
        }

        // close pipe's write end
        if (current_cmd->next) {
            close(pipe_fds[1]); 
        }

        current_cmd = current_cmd->next;  
    }

    return return_status;
    
    // debugging
    #ifdef DEBUG
    printf("\n***********************\n");
    printf("BEGIN: Executing line...\n");
    #endif

    #ifdef DEBUG
    printf("All children created\n");
    #endif

    // Wait for all the children to finish

    #ifdef DEBUG
    printf("All children finished\n");
    #endif

    #ifdef DEBUG
    printf("END: Executing line...\n");
    printf("***********************\n\n");
    #endif
}


/*
** Forks a new process and execs the command
** making sure all file descriptors are set up correctly.
**
** Parent process returns -1 on error.
** Any child processes should not return.
*/
int run_command(Command *command){
    
    // if the command, args[0] is cd, use cd_cscshell to handle it, otherwise, go to home directory if none specified
    if(strcmp(command->exec_path, "cd") == 0) {
        
        if(command->args[1] != NULL) {
            cd_cscshell(command->args[1]);
        }

        else {
            cd_cscshell(NULL);
        }

        return 0;
    }
    
    // create fork and error check
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed");
        return -1;
    }

    // child process
    if (pid == 0) {

        // input redirection
        if (command->redir_in_path != NULL) {
            int in_fd = open(command->redir_in_path, O_RDONLY);
            
            if (in_fd == -1) {
                perror("open");
                exit(EXIT_FAILURE);
            }

            dup2(in_fd, STDIN_FILENO);
            if (dup2(in_fd, STDIN_FILENO) == -1) {
                perror("dup2");
                close(in_fd);
            }

            close(in_fd);
        }

        // output redirection
        if (command->redir_out_path != NULL) {
            int flags = O_WRONLY | O_CREAT | (command->redir_append ? O_APPEND : O_TRUNC);
            int out_fd = open(command->redir_out_path, flags, 0644);

            if (out_fd == -1) {
                perror("open");
                exit(EXIT_FAILURE);
            }

            if (dup2(out_fd, STDOUT_FILENO) == -1) {
                perror("dup2");
                close(out_fd);
            }

            close(out_fd);
        }

        // execute the command annd handle failure
        execvp(command->exec_path, command->args);
        perror("execvp ");
        exit(EXIT_FAILURE);
    }

    return pid;
}


int run_script(char *file_path, Variable **root){
    
    // Attempt to open the specified script file, exit with error if unsuccessful
    FILE *script_file = fopen(file_path, "r");
    if (!script_file) {
        return -1;  // Return an error code if file opening fails
    }

    char buffer[MAX_SINGLE_LINE];

    // Read and process each line in the script file

    while (fgets(buffer, sizeof(buffer), script_file)) {

        // eliminate the newline character at the end of the line
        buffer[strcspn(buffer, "\n")] = '\0';

        // convert line into executable commands
        Command *cmd = parse_line(buffer, root);
        
        if (cmd == (Command *) -1) {
            fclose(script_file);
            return -1;
        }

        // execute the commands, if valid
        if (cmd) {
            int *result = execute_line(cmd);
            if (result == (int *) -1) {
                free(result);
                fclose(script_file);
                return -1;
            }
            free(result);
        }
        // free any allocated resources for the command
        free_command(cmd);
    }

    // close the script file
    fclose(script_file);
    return 1;
}

void free_command(Command *command){

    // iterate through all commands
    while (command != NULL) {

        // move to next command if there is one
        Command *next_command = command->next;
        
        // first, free executable path
        free(command->exec_path);

        // next, free array of arguments
        if (command->args != NULL) {
            free(command->args);
            command->args = NULL;
        }

        // then free input/output redirection path
        free(command->redir_in_path);
        free(command->redir_out_path);
 
        // completely free current command struct
        free(command);

        command = next_command;
    }
}
