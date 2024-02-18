#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>

#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

int previous_exit_status = 0;
int previous_bgid = 0;

char *words[MAX_WORDS];
size_t wordsplit(char const *line);
char * expand(char const *word);

int main(int argc, char *argv[])
{
  FILE *input = stdin;
  char *input_fn = "(stdin)"; 
  if (argc == 2) {
    input_fn = argv[1];
    input = fopen(input_fn, "re");
    if (!input) err(1, "%s", input_fn);
  } else if (argc > 2) {
    errx(1, "too many arguments");
  }

  char *line = NULL;
  size_t n = 0;

  if (input == stdin) {
    
  }


  for (;;) {
prompt:;
    /* TODO: Manage background processes */
    int background_child_status = 0;
    pid_t background_pid = waitpid(0, &background_child_status, WUNTRACED | WNOHANG);

   if (background_pid > 0) {
    if (WIFEXITED(background_child_status)) {
      fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t) background_pid, WEXITSTATUS(background_child_status));
    }
    if (WIFSIGNALED(background_child_status)) {
      fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t) background_pid, WTERMSIG(background_child_status));
    }
    if (WIFSTOPPED(background_child_status)) {
      fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) background_pid);
      kill(background_pid, SIGCONT);
    }
   } 

    clearerr(input);
    errno = 0;

    /* TODO: prompt */
    if (input == stdin) {
        fprintf(stderr, "%s", expand("${PS1}"));
    }

    /* get input from stdinput */
    ssize_t line_len = getline(&line, &n, input);
    if (line_len < 0) {
      if (feof(input)) {
        exit(atoi(expand("$?"))); /* exit if end of file is set */
      }
      else if (ferror(input)) {
        goto prompt; /* re-enter prompt if error occured from reading */
      }
    }
    
    /* split the input into array or words then expand */
    size_t nwords = wordsplit(line); /* number of words */
    for (size_t i = 0; i < nwords; ++i) {
      /* fprintf(stderr, "Word %zu: %s\n", i, words[i]); */
      char *exp_word = expand(words[i]);
      free(words[i]);
      words[i] = exp_word;
      /* fprintf(stderr, "Expanded Word %zu: %s\n", i, words[i]); */
    }

    /* parsing the words separating words from operators*/
    bool background = false;
    char *read_from = NULL;
    char *write_to = NULL;
    char *append_to = NULL;
    
    int ntokens = 0;
    char *tokens[MAX_WORDS];
    
    /* iterate over wordlist */
    for (int i = 0; i < nwords; i++) {
      if (i == nwords - 1 && strcmp(words[i], "&") == 0) { /* check if the last word is & */
        background = true;
        continue;
      }
      else if (strcmp(words[i], "<") == 0) { /* obtain file name to read from, skip over operator and filename, keep track of operators */
        read_from = words[++i]; 
        continue;
      }
      else if (strcmp(words[i], ">") == 0) { /* obtain file name to write to */
        write_to = words[++i];
        FILE *output;
        output = fopen(write_to, "w");
        if (output == NULL) {
          perror("Error: Failed to open output stream");
          exit(EXIT_FAILURE);
        }
        fclose(output);
        continue;
      }
      else if (strcmp(words[i], ">>") == 0) { /* obtain file name to write append to */
        append_to = words[++i];
        continue;
      }
      /* copy non-operator words into tokens array */
      tokens[ntokens++] = words[i];
    }
    /* terminate tokens array with NULL */
    tokens[ntokens] = NULL;
    ntokens++;

    /* begin execution using tokens */
 
    /* no input given */
    if (tokens[0] == NULL) { 
        goto prompt;
    }
    
    /* exit command */
    else if (strcmp(tokens[0], "exit") == 0) {
      if (ntokens > 3) {
        fprintf(stderr, "Error: Too many arguments for exit command\n");
        goto prompt;
      }
      else if (ntokens == 3) {
        if (isdigit(*tokens[1])) {
            exit(atoi(tokens[1]));
        }
        else {
          fprintf(stderr, "Error: Argument is not a number.\n");
          goto prompt;
        }
      }
      else {
        exit(atoi(expand("$?")));
      }
    }

    /* cd command */
    else if(strcmp(tokens[0], "cd") == 0) {
      if (ntokens > 3) {
        fprintf(stderr, "Error: Too many arguments for cd command\n");
        goto prompt;
      }
      else if (ntokens == 3) {
        if (chdir(tokens[1]) != 0) {
          fprintf(stderr, "Error: Changing directories failed\n");
        }
      }
      else {
        if (chdir(expand("${HOME}")) != 0) {
          fprintf(stderr, "Error: Default action for changing directories failed\n");
        }
        goto prompt;
      }
    }

    /* execute non-built in commands */
    else {
      pid_t spawn_id = -5;
      pid_t child_id = -5;
      int child_status = -5;
      spawn_id = fork();
      

      switch(spawn_id) {
        case -1:
          fprintf(stderr, "Fork failed\n");
          goto prompt;
        
        case 0:
          /* child processes */

          /* redirection operators*/
           if (append_to != NULL) {
            FILE *output;
            output = fopen(append_to, "a");
            if (output == NULL) {
              perror("Error: Failed to open append stream");
              exit(EXIT_FAILURE);
            }
            int result = dup2(fileno(output), 1);
            if (result == -1) {
              perror("Error: Updating append file descriptor failed");
              exit(EXIT_FAILURE);
            }
          } 

           if (write_to != NULL) {
            FILE *output;           
            output = fopen(write_to, "w");
            if (output == NULL) {
              perror("Error: Failed to open output stream");
              exit(EXIT_FAILURE);
            }
            int result = dup2(fileno(output), 1);
            if (result == -1) {
              perror("Error: Updating write file descriptor failed");
              exit(EXIT_FAILURE);
            }
           }

          if (read_from != NULL) {
            input = fopen(read_from, "re"); /* input is of FILE type */
            if (!input) {
              fprintf(stderr, "Error: Could not redirect read");
              exit(EXIT_FAILURE);
            }
            int result = dup2(fileno(input), 0); /* use fileno to return integer file descriptor */
            if (result == -1) {
               fprintf(stderr, "Error: Updating read file descriptor failed");
               exit(EXIT_FAILURE);
            }
          }

          if (execvp(tokens[0], tokens) < 0) {
            fprintf(stderr, "Error: execvp has failed.");
            exit(EXIT_FAILURE);
          }
          else {
            exit(EXIT_SUCCESS);
          }
        
        default:
          /* parent processes */
          if (background == true) {
            previous_bgid = spawn_id;
            goto prompt;
          }
          else {
            child_id = waitpid(spawn_id, &child_status, WUNTRACED);

            if (WIFSIGNALED(child_status)) {
              previous_exit_status = 128 + WTERMSIG(child_status);
            }
            else if (WIFSTOPPED(child_status)) {
              kill(spawn_id, SIGCONT);
              fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) spawn_id);
              previous_bgid = spawn_id;
              previous_exit_status = WEXITSTATUS(child_status);
              goto prompt;
            }
            else {
              previous_exit_status = WEXITSTATUS(child_status);
            }

            goto prompt;
          }
      }
    }
  }
}

char *words[MAX_WORDS] = {0};


/* Splits a string into words delimited by whitespace. Recognizes
 * comments as '#' at the beginning of a word, and backslash escapes.
 *
 * Returns number of words parsed, and updates the words[] array
 * with pointers to the words, each as an allocated string.
 */
size_t wordsplit(char const *line) {
  size_t wlen = 0;
  size_t wind = 0;

  char const *c = line;
  for (;*c && isspace(*c); ++c); /* discard leading space */

  for (; *c;) {
    if (wind == MAX_WORDS) break;
    /* read a word */
    if (*c == '#') break;
    for (;*c && !isspace(*c); ++c) {
      if (*c == '\\') ++c;
      void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));
      if (!tmp) err(1, "realloc");
      words[wind] = tmp;
      words[wind][wlen++] = *c; 
      words[wind][wlen] = '\0';
    }
    ++wind;
    wlen = 0;
    for (;*c && isspace(*c); ++c);
  }
  return wind;
}


/* Find next instance of a parameter within a word. Sets
 * start and end pointers to the start and end of the parameter
 * token.
 */
char
param_scan(char const *word, char const **start, char const **end)
{
  static char const *prev;
  if (!word) word = prev;
  
  char ret = 0;
  *start = 0;
  *end = 0;
  for (char const *s = word; *s && !ret; ++s) {
    s = strchr(s, '$');
    if (!s) break;
    switch (s[1]) {
    case '$':
    case '!':
    case '?':
      ret = s[1];
      *start = s;
      *end = s + 2;
      break;
    case '{':;
      char *e = strchr(s + 2, '}');
      if (e) {
        ret = s[1];
        *start = s;
        *end = e + 1;
      }
      break;
    }
  }
  prev = *end;
  return ret;
}

/* Simple string-builder function. Builds up a base
 * string by appending supplied strings/character ranges
 * to it.
 */
char *
build_str(char const *start, char const *end)
{
  static size_t base_len = 0;
  static char *base = 0;

  if (!start) {
    /* Reset; new base string, return old one */
    char *ret = base;
    base = NULL;
    base_len = 0;
    return ret;
  }
  /* Append [start, end) to base string 
   * If end is NULL, append whole start string to base string.
   * Returns a newly allocated string that the caller must free.
   */
  size_t n = end ? end - start : strlen(start);
  size_t newsize = sizeof *base *(base_len + n + 1);
  void *tmp = realloc(base, newsize);
  if (!tmp) err(1, "realloc");
  base = tmp;
  memcpy(base + base_len, start, n);
  base_len += n;
  base[base_len] = '\0';

  return base;
}

/* Expands all instances of $! $$ $? and ${param} in a string 
 * Returns a newly allocated string that the caller must free
 */
char *
expand(char const *word)
{
  char const *pos = word;
  char const *start, *end;
  char c = param_scan(pos, &start, &end);
  build_str(NULL, NULL);
  build_str(pos, start);
  while (c) {
    if (c == '!') {
        char *str_bgid = NULL;
        char str_default[] = "";
        if (previous_bgid == 0){
            build_str(str_default, NULL);
        }
        else if (asprintf(&str_bgid, "%jd", (intmax_t) previous_bgid) != -1) {
            build_str(str_bgid, NULL);
        }
        free(str_bgid);
    }
    else if (c == '$') {
        pid_t pid = getpid();
        char *str_pid = NULL;
        if (asprintf(&str_pid, "%jd", (intmax_t) pid) != -1) {
            build_str(str_pid, NULL);
        }
        free(str_pid);
    }
    else if (c == '?') {
        char *str_exit_status = NULL;
        if (previous_exit_status == 0) {
            build_str("0", NULL);
        }
        else {
            if (asprintf(&str_exit_status, "%jd", (intmax_t) previous_exit_status) != -1) {
                build_str(str_exit_status, NULL);
                free (str_exit_status);
            }
        }
    }
    else if (c == '{') {
      char *parameter = build_str(start + 2, end - 1);
      char str_default[] = "";
      char *envValue = getenv(parameter);
      build_str(NULL, NULL);
      build_str(pos, start);
      if (envValue == NULL) {
          build_str(str_default, NULL);
      }else {
        build_str(envValue, NULL);
      }
    }
    pos = end;
    c = param_scan(pos, &start, &end);
    build_str(pos, start);
  }
  return build_str(start, NULL);
} 
