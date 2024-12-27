/*****************************************************************************/
/*                           CSC209-24s A3 CSCSHELL                          */
/*       Copyright 2024 -- Demetres Kostas PhD (aka Darlene Heliokinde)      */
/*****************************************************************************/

#include "cscshell.h"

#define CONTINUE_SEARCH NULL


// COMPLETE
char *resolve_executable(const char *command_name, Variable *path){

    if (command_name == NULL || path == NULL){
        return NULL;
    }

    if (strcmp(command_name, CD) == 0){
        return strdup(CD);
    }

    if (strcmp(path->name, PATH_VAR_NAME) != 0){
        ERR_PRINT(ERR_NOT_PATH);
        return NULL;
    }

    char *exec_path = NULL;

    if (strchr(command_name, '/')){
        exec_path = strdup(command_name);
        if (exec_path == NULL){
            perror("resolve_executable");
            return NULL;
        }
        return exec_path;
    }

    // we create a duplicate so that we can mess it up with strtok
    char *path_to_toke = strdup(path->value);
    if (path_to_toke == NULL){
        perror("resolve_executable");
        return NULL;
    }
    char *current_path = strtok(path_to_toke, ":");

    do {
        DIR *dir = opendir(current_path);
        if (dir == NULL){
            ERR_PRINT(ERR_BAD_PATH, current_path);
            closedir(dir);
            continue;
        }

        struct dirent *possible_file;

        while (exec_path == NULL) {
            // rare case where we should do this -- see: man readdir
            errno = 0;
            possible_file = readdir(dir);
            if (possible_file == NULL) {
                if (errno > 0){
                    perror("resolve_executable");
                    closedir(dir);
                    goto res_ex_cleanup;
                }
                // end of files, break
                break;
            }

            if (strcmp(possible_file->d_name, command_name) == 0){
                // +1 null term, +1 possible missing '/'
                size_t buflen = strlen(current_path) +
                    strlen(command_name) + 1 + 1;
                exec_path = (char *) malloc(buflen);
                // also sets remaining buf to 0
                strncpy(exec_path, current_path, buflen);
                if (current_path[strlen(current_path)-1] != '/'){
                    strncat(exec_path, "/", 2);
                }
                strncat(exec_path, command_name, strlen(command_name)+1);
            }
        }
        closedir(dir);

        // if this isn't null, stop checking paths
        if (possible_file) break;

    } while ((current_path = strtok(CONTINUE_SEARCH, ":")));

res_ex_cleanup:
    free(path_to_toke);
    return exec_path;
}

// a helper method for trimming whitespace
char *trim_whitespace(char *str) {
    char *end;

    // trim leading space
    while (isspace((unsigned char)*str)){str++;}

    // if all spaces
    if (*str == 0) { 
        return str;
    }

    // trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)){
        end--;
    } 

    // add null terminator
    *(end + 1) = '\0';

    return str;
}

// helper method, no longer useful
void process_special_commands(char *line, Variable **variables) {
    
    // first check if command is 'cd'
    if (strncmp(line, "cd ", 3) == 0) {
        
        // extract the directory path
        char *path = line + 3;

        // if the path is empty or "~", change to the home directory
        if (strlen(path) == 0 || strcmp(path, "~") == 0) {
            
            path = getenv("HOME");
            if (path == NULL) {
                fprintf(stderr, "cd: HOME environment variable not set.\n");
                return;
            }
        }

        // change the current directory
        if (chdir(path) != 0) {
            perror("cd");
        }

        return;
    }

    // assume command is a variable assignment (like VAR=value), split the line at '='
    char *delimiter = strchr(line, '=');
    *delimiter = '\0';
    char *varName = line;
    char *varValue = delimiter + 1;

    // iterate to check for variable's existence
    Variable *current = *variables;
    while (current != NULL) {
        // check if variable found, and if so, update its value
        if (strcmp(current->name, varName) == 0) {
            free(current->value);
            current->value = strdup(varValue);
            return;
        }

        // iterate to next variable
        current = current->next;
    }

    // if variable not found, add it
    Variable *newVar = malloc(sizeof(Variable));
    
    // error checking
    if (newVar == NULL) {
        fprintf(stderr, "Failed to allocate memory for new variable.\n");
        return;
    }

    newVar->name = strdup(varName);
    newVar->value = strdup(varValue);
    newVar->next = *variables;
    *variables = newVar;
}

void process_command_parameters(Command *command) {
    char **parameters = command->args;

    // assign first parameter as executable path
    command->exec_path = parameters[0]; 

    int index = 0;

    // iterate over parameters
    while (parameters[index]) {
        
        // check for output redirection
        if (!strcmp(parameters[index], ">") && parameters[index + 1]) {
            command->redir_out_path = parameters[index + 1];
            command->redir_append = 0;
            command->args[index] = NULL;
        }

        // check for append redirection
        else if (!strcmp(parameters[index], ">>") && parameters[index + 1]) {
            command->redir_out_path = parameters[index + 1];
            command->redir_append = 1;
            command->args[index] = NULL;
        }

        // check for input redirection
        else if (!strcmp(parameters[index], "<") && parameters[index + 1]) {
            command->redir_in_path = parameters[index + 1];
            command->redir_append = 0;
            command->args[index] = NULL;
        }

        index++;
    }
}

// helper method to validate variable name
int is_valid_variable_name(const char *name) {
    
    // check first character
    if (!isalpha((unsigned char)*name) || *name == '_') {
        return 0;
    }
    
    // then, check all following characters
    for (const char *p = name + 1; *p; p++) {
        if (!isalpha((unsigned char)*p) && *p != '_'){
            return 0;
        }
    }

    // return if valid
    return 1;
}

// helper method for splitting command arguments
char **parse_args(char *command) {

    int alloc_size = 64;
    int index = 0;
    char **parsed_args = malloc(alloc_size * sizeof(char*));

    // error checking
    if (!parsed_args) {
        fprintf(stderr, "memory allocation error!\n");
        exit(EXIT_FAILURE);
    }

    // delimiters are spaces, tabs, carriage returns, newlines, and alarms
    char *delimiter = " \t\r\n\a";

    parsed_args[0] = strtok(command, delimiter);

    // iterate on arguments
    while (parsed_args[index] != NULL) {

        // if index to large
        if (++index >= alloc_size) {
            
            // update allocation size by a sweet spot value
            alloc_size += 64;
            parsed_args = realloc(parsed_args, alloc_size * sizeof(char*));
            
            // error checking on memory failure
            if (!parsed_args) {
                fprintf(stderr, "memory allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        parsed_args[index] = strtok(NULL, delimiter);
    }

    return parsed_args;

}

// helper method for splitting command arguments by pipes
char **parse_args_by_pipe(char *commandLine) {
    
    int alloc_size = 64;
    int index = 0;
    char **parsed_cmds = malloc(alloc_size * sizeof(char*));  

    // error checking
    if (!parsed_cmds) {
        fprintf(stderr, "memory allocation error\n");
        exit(EXIT_FAILURE);
    }

    // avoid modifying the original string
    char *temp_command_line = strdup(commandLine);
    
    // more error checking
    if (!temp_command_line) {
        fprintf(stderr, "error duplicating command line\n");
        exit(EXIT_FAILURE);
    }

    // parse command line by pipe characters
    parsed_cmds[index] = strtok(temp_command_line, "|");
    
    // iterate over commands
    while (parsed_cmds[index]) {
        index++;
        
        // if index to large
        if (index >= alloc_size) 
        {  
            // update allocation size by a sweet spot value
            alloc_size += 64;
            parsed_cmds = realloc(parsed_cmds, alloc_size * sizeof(char*));  // Reallocate with the new size
            
            // error checking on memory failure
            if (!parsed_cmds) {
                fprintf(stderr, "memory allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        // split command by pipe
        parsed_cmds[index] = strtok(NULL, "|");  
    }

    return parsed_cmds;
}

Command *parse_line(char *line, Variable **variables){
    // first, check if the line is empty or a comment
    if (line == NULL || line[0] == '\0' || line[0] == '#') {
        return NULL;
    }

    // next, trim whitespace
    line = trim_whitespace(line);

    // check if line now empty
    if (strlen(line) == 0 || line[0] == '\0' || line[0] == '#') {
        return NULL;
    }

    // variable assignment
    if (strchr(line, '=')) {
        char *equals_ptr = strchr(line, '=');
        
        if (line == equals_ptr) {
            ERR_PRINT(ERR_VAR_START);
            return (Command *)-1;
        }

        // split line at the equals sign
        *equals_ptr = '\0'; 
        char *name = line;
        char *value = equals_ptr + 1;

        // validate and add/update variable
        if (is_valid_variable_name(name)) {

            // iterate on all variables
            Variable *variable;
            for (variable = *variables; variable != NULL; variable = variable->next) {
                if (strcmp(variable->name, name) == 0) {
                    
                    // free old value
                    free(variable->value);  

                    // update with new value
                    variable->value = strdup(value); 
                }
            }

            // if not found, create new variable
            variable = malloc(sizeof(Variable));
            variable->name = strdup(name);
            variable->value = strdup(value);

            // add to front of linked list
            variable->next = *variables;
            *variables = variable;
        } 

        // else throw error
        else {
            ERR_PRINT(ERR_VAR_NAME, name);
            return (Command *)-1;
        }

        return NULL;
    }

    // replace variables in the line
    char *replaced_line = replace_variables_mk_line(line, *variables);

    // split the command line into tokens and accounting for pipes
    char **commands_split = parse_args_by_pipe(replaced_line);

    Command *head = NULL;
    Command *curr = NULL;
    int i = 0;

    // iterate over commands split by pipe
    while(commands_split[i] != NULL)
    {
        if (head == NULL) {
            head = curr = malloc(sizeof(Command));
        } 
        
        else {
            curr->next = malloc(sizeof(Command));
            curr = curr->next;
        }

        // initialize command structure
        memset(curr, 0, sizeof(Command)); 

        // split into args
        char **parsed_args = parse_args(replace_variables_mk_line(commands_split[i], *variables));
        curr->args = parsed_args;

        // process for redirection
        process_command_parameters(curr);
        
        i++;
    }

    free(commands_split);
    free(replaced_line);

    return head;

}


/*
** This function is partially implemented for you, but you may
** scrap the implementation as long as it produces the same result.
**
** Creates a new line on the heap with all named variable *usages*
** replaced with their associated values.
**
** Returns NULL if replacement parsing had an error, or (char *) -1 if
** system calls fail and the shell needs to exit.
*/
char *replace_variables_mk_line(const char *line, Variable *variables){

    // if null return null
    if (line == NULL || variables == NULL) {
        return NULL;
    }

    // store new line, error check
    size_t new_line_length = strlen(line) + 1;
    char *new_line = calloc(new_line_length, sizeof(char));
    if (new_line == NULL) {
        perror("replace_variables_mk_line: malloc failed");
        exit(EXIT_FAILURE);
    }

    // iterating through original line
    const char *cursor = line;
    while (*cursor) {

        // if start of variable found, find end
        if (*cursor == '$') {
            cursor++;
            const char *start = cursor;
            int var_length = 0;

            if (*cursor == '{') {
                // skip the opening bracket
                cursor++; 
                
                start = cursor; 
                
                while (*cursor && *cursor != '}') {
                    cursor++;
                    var_length++;
                }
                
                // skip the closing bracket
                if (*cursor == '}') {cursor++;}
            } 
            
            // skip any valid variable name characters
            else {
                while ((isalpha((unsigned char)*cursor) || *cursor == '_') && *cursor) {
                    cursor++;
                    var_length++;
                }
            }

            // setting a max length of 100 for a variable name
            char var_buffer[100] = {0};
            strncpy(var_buffer, start, var_length);
            
            // find the variable
            Variable *found = NULL;
            Variable *current = variables;
            while (current != NULL) {

                // if variable found
                if (strcmp(current->name, var_buffer) == 0) {
                    // set var to current
                    found = current; 
                }

                // move to the next variable in the list
                current = current->next; 
            }

            // if variable is found
            if (found) {

                // calculate new line length
                size_t value_length = strlen(found->value);
                new_line_length += value_length - var_length;

                // reallocate memory to accommodate the new value, and error check
                char *temp = realloc(new_line, new_line_length);
                if (temp == NULL) {free(new_line), exit(EXIT_FAILURE);}
                new_line = temp;

                // append found value to new_line
                strcat(new_line, found->value);
            }

        }

        // else it's a regular character, and we append it
        else {

            // here we add 2 for char and null terminator
            char curr_char_and_nt[2] = {*cursor, '\0'};

            // append the character and null terminator to new_line
            strcat(new_line, curr_char_and_nt);

            // update cursor
            cursor++;
        }
    }

    return new_line;
}


void free_variable(Variable *var, uint8_t recursive){

    // define current variable and iterate through all variables
    Variable *current_var = var;
    while (current_var != NULL) {

        // define the next variable in chain
        Variable *next_var = current_var->next;

        // if it's recursive then break from current iteration and move to next
        if (!recursive) {
            current_var = next_var;
        }

        // first, free memory allocated for name and value strings, if they exist
        free(current_var->name);
        free(current_var->value);

        // lastly completely free the current variable struct
        free(current_var);
        
        // move to next variable if there is one
        current_var = next_var;
    }

}
